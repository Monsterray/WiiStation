#ifdef PERF_PROF
/* perf_prof.c - Phase 1 profiling counters + report.
 *
 * Report sinks (best effort, never fatal):
 *  - sd:/wiisxrx/perf.log (append) -- the durable record for A/B runs;
 *  - on-screen DEBUG overlay rows 22..35 when SHOW_DEBUG is on;
 *  - SysPrintf (USB gecko) when PRINTGECKO is on.
 */
#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include "perf_prof.h"
#include "gc_input/controller.h"   /* the ports report below */
#include "../mem2_manager.h"

/* Diagnostic-only; declared here rather than in mem2_manager.h so the
 * profiler stays the single consumer. */
extern uint32_t gx_mem2_check(void);

#ifdef SHOW_DEBUG
#include "DEBUG.h"
#endif

perf_counters_t g_perf;

unsigned long long perf_now_us(void)
{
	// gettime() (u64), NOT gettick() (u32): the Wii time base advances at
	// TB_TIMER_CLOCK*1000 == 60,750,000 ticks/sec, so a 32-bit tick counter
	// wraps every 2^32/60.75e6 ~= 70.7 seconds. Reports are emitted every
	// 1800 frames (~30 s), so wraps landed inside measurement windows
	// routinely, and every "perf_now_us() - t0" that straddled one underflowed
	// into a huge u64 (cpu_us/present_us showing ~1.8e19 in perf.log).
	return ticks_to_microsecs(gettime());
}

unsigned long long perf_now_ticks(void)
{
	return gettime();
}

#if PERF_PROF_BIOS
/* The busiest HLE BIOS calls, by name. A game that uses none prints nothing. */
extern char *biosA0n[256], *biosB0n[256], *biosC0n[256];

static void perf_report_bios(FILE *f)
{
	struct { const char *tab; unsigned n; unsigned long c; } top[10], t;
	const uint32_t *counts[3] = { g_perf.bios_a, g_perf.bios_b, g_perf.bios_c };
	char *const *names[3] = { biosA0n, biosB0n, biosC0n };
	static const char tabs[3] = { 'A', 'B', 'C' };
	unsigned long total = 0;
	int ntop = 0, i, j, k;

	memset(top, 0, sizeof(top));
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 256; j++) {
			if (!counts[i][j])
				continue;
			total += counts[i][j];
			/* keep the ten biggest, insertion sorted: the list is tiny */
			t.tab = &tabs[i]; t.n = j; t.c = counts[i][j];
			for (k = 0; k < ntop && top[k].c >= t.c; k++)
				;
			if (k >= 10)
				continue;
			if (ntop < 10)
				ntop++;
			for (int m = ntop - 1; m > k; m--)
				top[m] = top[m - 1];
			top[k] = t;
		}
	}
	if (!total)
		return;

	fprintf(f, "bios: calls=%lu custom=%lu us=%llu exc=%lu exc_us=%llu |", total,
		(unsigned long)g_perf.bios_custom, g_perf.bios_us,
		(unsigned long)g_perf.bios_exc_calls, g_perf.bios_exc_us);
	for (i = 0; i < ntop; i++) {
		int tab = top[i].tab - tabs;
		const char *nm = names[tab][top[i].n];
		fprintf(f, " %c%02x:%s=%lu", tabs[tab], top[i].n,
			nm ? nm : "?", top[i].c);
	}
	fprintf(f, "\n");
}
#endif

unsigned long long g_netwait_old_us, g_netwait_new_us;
unsigned long g_netwait_old_wakes, g_netwait_new_wakes;

void perf_reset(void)
{
	{
		/* one log per boot: the SD image keeps files across runs, so appending
		 * across boots made the log grow and mixed runs */
		static int fresh = 0;
		if (!fresh) { FILE *f = fopen("sd:/wiisxrx/perf.log", "w"); if (f) fclose(f); fresh = 1; }
	}
	memset(&g_perf, 0, sizeof(g_perf));
	g_perf.wall_start_ticks = gettime();
}

/* Every N presented frames, append one block. 1800 ~= 30 s at 60 fps. */
#define PERF_REPORT_EVERY_FRAMES 1800

static void perf_sample_mem(void)
{
	uint32_t used_kb = gx_mem2_used() >> 10;
	if (used_kb > g_perf.mem2_peak_kb)
		g_perf.mem2_peak_kb = used_kb;
}

/* One-shot snapshot of PSX VRAM at a fixed emulated time. Keyed on the
 * core's vblank count rather than on presents so that two plugins' runs of
 * the same autoboot stop at the same game frame whatever their speed; the
 * two dumps are then diffed offline. All GPU plugins publish their VRAM
 * through the one psxVuw global. */
#define PERF_VRAM_DUMP_VBLANK 6000
extern unsigned short *psxVuw;
extern unsigned autoinput_dump_vbl;
static int autoinput_dump_vbl_set(void) { return autoinput_dump_vbl != 0; }
static void perf_vram_dump(void)
{
	size_t n = 0;
	FILE *f = fopen("sd:/wiisxrx/vram.bin", "wb");
	if (f) {
		if (psxVuw) n = fwrite(psxVuw, 1, 1024u * 512u * 2u, f);
		fclose(f);
	}
	f = fopen("sd:/wiisxrx/perf.log", "a");
	if (f) {
		fprintf(f, "vramdump: frames=%lu vblanks=%lu bytes=%lu\n",
			(unsigned long)g_perf.present_frames, (unsigned long)g_perf.vblanks, (unsigned long)n);
		fclose(f);
	}
}

/* Primitive-stream trace. flags: bit0 semi-transparent, bit1 textured,
 * bit2 quad, bit3 gouraud. cmd 0x02 = fill, 0xF5 = display address change
 * (x0,y0 = new position). Arms on the first untextured semi-transparent
 * polygon at least 200x150 PSX pixels; records the next 96 events and
 * per-present counts for 8 presents. */
#ifndef PERF_PT_ARM_PRESENT
#define PERF_PT_ARM_PRESENT 0      /* 0 = arm only on a large untextured semi-transparent polygon */
#endif
extern unsigned int frame_counter;            /* psxcounters.c: +1 per VBlank */
extern unsigned autoinput_atrace_vbl;         /* PadWiiSX.c: 'atrace <vblank>' line */

/* Audio timeline (aev[] in perf_prof.h). Record fields per kind:
 *   F/R  XA sector queued (R = stream start): a = ring fill before the feed (samples),
 *        b = cycle - cycles_played (mixer lag), c = bit0 output driver busy,
 *        bits1-3 XARepeat, bit4 a gap was open (this sector ends it)
 *   H    MixCD started repeating the last sample (ring empty): a = ns_to, b = XARepeat
 *   G    MixCD started contributing silence: a = ns_to, b = cdClearSamples
 *   S    heartbeat every 16th SPU_async: a = ring fill, b = cycle - cycles_played
 *        (after the tempo pull-back, if any), c = driver busy
 *   D    rate control, every 16th update (dfsound/ratectl.c): a = queued frames at the
 *        driver, b = nudge in ppm, c = bit0 driver (0 sdl, 1 cube), bit1 demand saturated,
 *        bits2+ the integral term + 4096
 * Own file, rewritten whole: like ptrace.log, appending thousands of lines to perf.log
 * risks a broken FAT chain when Dolphin is killed mid-write. */
static void perf_audio_flush(void)
{
	unsigned k;
	FILE *f = fopen("sd:/wiisxrx/atrace.log", "w");
	if (!f) return;
	fprintf(f, "--- audio trace events=%lu flushed_at_present=%lu vblank=%lu ---\n",
		(unsigned long)g_perf.aev_n, (unsigned long)g_perf.present_frames, (unsigned long)g_perf.vblanks);
	for (k = 0; k < g_perf.aev_n; k++)
		fprintf(f, "ae: %c w=%lu c=%lu a=%ld b=%ld f=%u\n", g_perf.aev[k].kind,
			(unsigned long)g_perf.aev[k].wall_us, (unsigned long)g_perf.aev[k].cycle,
			(long)g_perf.aev[k].a, (long)g_perf.aev[k].b, g_perf.aev[k].c);
	fclose(f);
	g_perf.aev_printed = 1;
}
void perf_audio_event(unsigned kind, unsigned cycle, int a, int b, unsigned c)
{
	unsigned k;
	if (!g_perf.aev_on) {
		if (autoinput_atrace_vbl && frame_counter < autoinput_atrace_vbl) return;
		g_perf.aev_on = 1;
	}
	if (g_perf.aev_n >= PERF_AEV_N) return;
	k = g_perf.aev_n++;
	g_perf.aev[k].kind = (uint8_t)kind; g_perf.aev[k].wall_us = (uint32_t)perf_now_us();
	g_perf.aev[k].cycle = cycle; g_perf.aev[k].a = a; g_perf.aev[k].b = b; g_perf.aev[k].c = (uint16_t)c;
}
extern unsigned autoinput_trace_vbl[8];       /* PadWiiSX.c: 'trace <vblank>' lines */
extern int autoinput_trace_n;
static int trace_sched_next = 0;
#if PERF_PROF_TRACE
static void perf_trace_flush(void);
void perf_prim_trace(unsigned cmd, unsigned flags, unsigned abr, unsigned color, int x0, int y0, int x1, int y1)
{
	unsigned idx;
	int sched = 0;
	if (trace_sched_next < autoinput_trace_n && frame_counter >= autoinput_trace_vbl[trace_sched_next]) {
		trace_sched_next++; sched = 1;
	}
	int trigger = ((flags & 3) == 1 && cmd >= 0x20 && cmd < 0x80 && (x1 - x0) >= 200 && (y1 - y0) >= 150) ||
	              (PERF_PT_ARM_PRESENT && !g_perf.pt_armed && g_perf.present_frames >= PERF_PT_ARM_PRESENT);
	if (sched) {
		/* scheduled capture: start a fresh episode now, whatever the primitive */
		if (g_perf.pt_armed && !g_perf.pt_printed) perf_trace_flush();
		g_perf.pt_n = 0;
		memset(g_perf.pt_prims, 0, sizeof(g_perf.pt_prims));
		memset(g_perf.pt_semi, 0, sizeof(g_perf.pt_semi));
		memset(g_perf.pt_fills, 0, sizeof(g_perf.pt_fills));
		g_perf.pt_armed = 0; g_perf.pt_printed = 0; trigger = 1;
	}
	if (!g_perf.pt_armed) {
		if (!trigger) return;
	} else if (g_perf.present_frames - g_perf.pt_start_present >= 16) {
		/* episode complete: keep it until the next overlay starts a new one, so
		 * the report always shows the most recent episode (e.g. the pause
		 * screen, not the boot fade) */
		if (!trigger) return;
		if (!g_perf.pt_printed) perf_trace_flush();
		g_perf.pt_printed = 0;
		g_perf.pt_n = 0;
		memset(g_perf.pt_prims, 0, sizeof(g_perf.pt_prims));
		memset(g_perf.pt_semi, 0, sizeof(g_perf.pt_semi));
		memset(g_perf.pt_fills, 0, sizeof(g_perf.pt_fills));
		g_perf.pt_armed = 0;
	}
	if (!g_perf.pt_armed) { g_perf.pt_armed = 1; g_perf.pt_start_present = g_perf.present_frames; g_perf.pt_start_vblank = g_perf.vblanks; }
	idx = g_perf.present_frames - g_perf.pt_start_present;
	if (idx < 16) {
		if (cmd == 0x02) g_perf.pt_fills[idx]++;
		else if (cmd != 0xF5 && cmd < 0xE0) { g_perf.pt_prims[idx]++; if (flags & 1) g_perf.pt_semi[idx]++; }
	}
	/* keep the ring for what matters: once a quarter full, small opaque
	 * polygons are counted but no longer stored */
	if (g_perf.pt_n >= 650 && cmd >= 0x20 && cmd < 0x80 && !(flags & 1) && (x1 - x0) * (y1 - y0) < 60000) return;
	/* a scene that draws hundreds of SMALL SEMI-TRANSPARENT primitives per present
	 * (Crash 3's title glow: 479 of them) fills the ring in five presents, so the
	 * whole-screen primitive at the end of each present never reaches the log.
	 * Past half full, keep only the big ones, whatever their blend. */
	if (g_perf.pt_n >= 1300 && cmd >= 0x20 && cmd < 0x80 && (x1 - x0) * (y1 - y0) < 60000) return;
	if (g_perf.pt_n < 2600) {
		unsigned k = g_perf.pt_n++;
		g_perf.pt[k].present = (uint16_t)idx; g_perf.pt[k].cmd = (uint8_t)cmd; g_perf.pt[k].flags = (uint8_t)flags;
		g_perf.pt[k].abr = (uint8_t)abr; g_perf.pt[k].color = color & 0xffffff;
		g_perf.pt[k].x0 = (int16_t)x0; g_perf.pt[k].y0 = (int16_t)y0; g_perf.pt[k].x1 = (int16_t)x1; g_perf.pt[k].y1 = (int16_t)y1;
	}
}

#endif /* PERF_PROF_TRACE */

/* Not part of the trace group: this records scripted input events for the report, and costs
 * one bounded array write per press. */
void perf_autoinput_event(unsigned vblank, unsigned mask)
{
	if (g_perf.ai_n < 16) {
		g_perf.ai_ev[g_perf.ai_n].vbl = vblank; g_perf.ai_ev[g_perf.ai_n].mask = (uint16_t)mask;
		g_perf.ai_ev[g_perf.ai_n].present = g_perf.present_frames; g_perf.ai_n++;
	}
}

#if PERF_PROF_TRACE

/* Write the current trace episode to perf.log (also printed by perf_report). */
static void perf_trace_lines(FILE *f)
{
	unsigned k;
	fprintf(f, "ptrace: armed_at_present=%lu vblank=%lu prims/semi/fills per present:", (unsigned long)g_perf.pt_start_present, (unsigned long)g_perf.pt_start_vblank);
	for (k = 0; k < 16; k++) fprintf(f, " %u/%u/%u", g_perf.pt_prims[k], g_perf.pt_semi[k], g_perf.pt_fills[k]);
	fprintf(f, "\n");
	for (k = 0; k < g_perf.pt_n; k++)
		fprintf(f, "pt: +%u cmd=%02x %s%s%s%s abr=%u col=%06lx (%d,%d)-(%d,%d)\n",
			g_perf.pt[k].present, g_perf.pt[k].cmd,
			(g_perf.pt[k].flags & 1) ? "S" : "-", (g_perf.pt[k].flags & 2) ? "T" : "-",
			(g_perf.pt[k].flags & 4) ? "Q" : "-", (g_perf.pt[k].flags & 8) ? "G" : "-",
			g_perf.pt[k].abr, (unsigned long)g_perf.pt[k].color,
			g_perf.pt[k].x0, g_perf.pt[k].y0, g_perf.pt[k].x1, g_perf.pt[k].y1);
}
static void perf_trace_flush(void)
{
	/* own file, rewritten per episode: a 2600-line flush appended to perf.log
	 * left a broken FAT chain when Dolphin was killed mid-write */
	FILE *f = fopen("sd:/wiisxrx/ptrace.log", "w");
	if (!f) return;
	fprintf(f, "--- trace episode (flushed at present %lu) ---\n", (unsigned long)g_perf.present_frames);
	perf_trace_lines(f);
	fclose(f);
	g_perf.pt_printed = 1;
}
#endif /* PERF_PROF_TRACE */

/* The pad timeline. One line per change, with the driver's output beside what the game
 * reads, so a sweep sent in through Dolphin can be checked end to end: the stick should
 * cover 0..255, sit at 128 when Dolphin sends its centre, and never step backwards.
 * Own file, rewritten whole, like ptrace.log and atrace.log. */
static void perf_pad_flush(void)
{
	unsigned k;
	FILE *f;
	if (!g_perf.pev_n) return;
	f = fopen("sd:/wiisxrx/padtrace.csv", "w");
	if (!f) return;
	fprintf(f, "vbl,pad,type,drv_btns,drv_lx,drv_ly,drv_rx,drv_ry,out_btns,out_lx,out_ly,out_rx,out_ry\n");
	for (k = 0; k < g_perf.pev_n; k++)
		fprintf(f, "%lu,%u,%c,%04x,%u,%u,%u,%u,%04x,%u,%u,%u,%u\n",
			(unsigned long)g_perf.pev[k].vbl, g_perf.pev[k].pad,
			g_perf.pev[k].type ? g_perf.pev[k].type : '-',
			g_perf.pev[k].drv_btns, g_perf.pev[k].drv_lx, g_perf.pev[k].drv_ly,
			g_perf.pev[k].drv_rx, g_perf.pev[k].drv_ry,
			g_perf.pev[k].out_btns, g_perf.pev[k].out_lx, g_perf.pev[k].out_ly,
			g_perf.pev[k].out_rx, g_perf.pev[k].out_ry);
	fclose(f);
}

void perf_pad_event(unsigned pad, unsigned type, unsigned drv_btns, unsigned drv_l,
                    unsigned drv_r, unsigned out_btns, unsigned out_l, unsigned out_r)
{
	/* Last record per port, so a held stick does not fill the ring. */
	static uint32_t last[2][3];
	uint32_t a = (drv_btns << 16) | (out_btns & 0xFFFF);
	uint32_t b = (drv_l << 16) | (drv_r & 0xFFFF);
	uint32_t c = (out_l << 16) | (out_r & 0xFFFF);
	unsigned slot = pad < 2 ? pad : 1;
	unsigned k;

	if (last[slot][0] == a && last[slot][1] == b && last[slot][2] == c) return;
	last[slot][0] = a; last[slot][1] = b; last[slot][2] = c;

	if (g_perf.pev_n >= PERF_PEV_N) return;
	k = g_perf.pev_n++;
	g_perf.pev[k].vbl = (uint32_t)g_perf.vblanks;
	g_perf.pev[k].pad = (uint8_t)pad;
	g_perf.pev[k].type = (uint8_t)type;
	g_perf.pev[k].drv_btns = (uint16_t)drv_btns;
	g_perf.pev[k].out_btns = (uint16_t)out_btns;
	g_perf.pev[k].drv_lx = (uint8_t)(drv_l >> 8); g_perf.pev[k].drv_ly = (uint8_t)drv_l;
	g_perf.pev[k].drv_rx = (uint8_t)(drv_r >> 8); g_perf.pev[k].drv_ry = (uint8_t)drv_r;
	g_perf.pev[k].out_lx = (uint8_t)(out_l >> 8); g_perf.pev[k].out_ly = (uint8_t)out_l;
	g_perf.pev[k].out_rx = (uint8_t)(out_r >> 8); g_perf.pev[k].out_ry = (uint8_t)out_r;
}

void perf_present_tick(unsigned long long present_us)
{
	g_perf.present_frames++;
#if PERF_PROF_TRACE
	if (g_perf.pt_armed && !g_perf.pt_printed && g_perf.present_frames - g_perf.pt_start_present >= 16)
		perf_trace_flush();
#endif
	g_perf.present_us += present_us;
	/* the audio ring is written from the present path, not from inside the mixer */
	if (g_perf.aev_n == PERF_AEV_N && !g_perf.aev_printed)
		perf_audio_flush();
	{
		extern unsigned autoinput_dump_vbl;   /* PadWiiSX.c: 'dump <vblank>' in autoinput.txt */
		if (!g_perf.vram_dumped && autoinput_dump_vbl && frame_counter >= autoinput_dump_vbl) {
			g_perf.vram_dumped = 1;
			perf_vram_dump();
			perf_report();     /* a scheduled dump marks the moment of interest: report the counters now, not 1800 presents later */
		}
	}
	if (!g_perf.vram_dumped && g_perf.vblanks >= PERF_VRAM_DUMP_VBLANK && !autoinput_dump_vbl_set()) {
		g_perf.vram_dumped = 1;
		perf_vram_dump();
	}
	if ((g_perf.present_frames % PERF_REPORT_EVERY_FRAMES) == 0)
		perf_report();
}

/* Labels for the irq_fires[] histogram, in PSXINT_* order (r3000a.h). Kept as
 * a local table rather than including r3000a.h here, which would drag the
 * emulator core into every file that profiles. lightrec.c static-asserts that
 * the array is large enough; if an event is ever inserted mid-enum, this list
 * needs the same edit. */
static const char *perf_irq_name(unsigned i)
{
	static const char * const names[] = {
		"sio", "cdr", "cdread", "gpudma", "mdecoutdma", "spudma",
		"gpubusy", "mdecindma", "gpuotcdma", "cdrdma", "newdrc",
		"rcnt", "cdrlid", "cdrplay", "spu_update", "spu_irq",
		"lightgun"
	};
	return (i < sizeof(names) / sizeof(names[0])) ? names[i] : "?";
}

void perf_report(void)
{
	char line[160];
	FILE *f;

	perf_sample_mem();

	f = fopen("sd:/wiisxrx/perf.log", "a");
	if (f) {
		uint32_t mem1_kb = SYS_GetArena1Size() >> 10;
		uint32_t m2used = gx_mem2_used() >> 10;
		uint32_t m2tot = gx_mem2_total() >> 10;

		fprintf(f, "--- perf frames=%lu ---\n", (unsigned long)g_perf.present_frames);
		{
			extern uint32_t dwActFixes; extern char CdromId[10];
			fprintf(f, "fixes: dwActFixes=%08lx cdrom=%s\n", (unsigned long)dwActFixes, CdromId);
		}
		fprintf(f, "cpu: slices=%lu int=%lu cpu_us=%llu jit_full=%lu jit_part=%lu interp_fb=%lu hle=%lu exc=%lu\n",
			(unsigned long)g_perf.jit_slices, (unsigned long)g_perf.int_slices,
			(unsigned long long)ticks_to_microsecs(g_perf.cpu_ticks),
			(unsigned long)g_perf.jit_resets_full,
			(unsigned long)g_perf.jit_resets_partial,
			(unsigned long)g_perf.jit_interp_fallbacks,
			(unsigned long)g_perf.jit_hle, (unsigned long)g_perf.jit_exceptions);
		fprintf(f, "wall: wall_us=%llu vblanks=%lu pal=%lu nested=%lu\n",
			(unsigned long long)ticks_to_microsecs(gettime() - g_perf.wall_start_ticks),
			(unsigned long)g_perf.vblanks,
			(unsigned long)g_perf.psx_pal,
			(unsigned long)g_perf.jit_nested);
		fprintf(f, "inside: limit_us=%llu limit=%lu target=%lu spu_us=%llu spu=%lu out_us=%llu out=%lu hw_us=%llu hw=%lu hw_gpu_us=%llu hw_gpu=%lu\n",
			(unsigned long long)ticks_to_microsecs(g_perf.limit_ticks),
			(unsigned long)g_perf.limit_calls,
			(unsigned long)g_perf.limit_target,
			(unsigned long long)ticks_to_microsecs(g_perf.spu_ticks),
			(unsigned long)g_perf.spu_calls,
			(unsigned long long)ticks_to_microsecs(g_perf.out_ticks),
			(unsigned long)g_perf.out_calls,
			(unsigned long long)ticks_to_microsecs(g_perf.hw_ticks),
			(unsigned long)g_perf.hw_calls,
			(unsigned long long)ticks_to_microsecs(g_perf.hw_gpu_ticks),
			(unsigned long)g_perf.hw_gpu_calls);
		fprintf(f, "slicecost: sched_us=%llu jit_us=%llu post_us=%llu exit_norm=%lu exit_irq=%lu\n",
			(unsigned long long)ticks_to_microsecs(g_perf.slice_sched_ticks),
			(unsigned long long)ticks_to_microsecs(g_perf.slice_jit_ticks),
			(unsigned long long)ticks_to_microsecs(g_perf.slice_post_ticks),
			(unsigned long)g_perf.exit_normal,
			(unsigned long)g_perf.exit_check_irq);
		fprintf(f, "slice: cycles=%llu avg=%lu tiny=%lu\n",
			g_perf.slice_cycles,
			(unsigned long)(g_perf.jit_slices
				? g_perf.slice_cycles / g_perf.jit_slices : 0),
			(unsigned long)g_perf.slice_tiny);
		fprintf(f, "irq:");
		{
			unsigned i;
			for (i = 0; i < PERF_IRQ_SLOTS; i++)
				if (g_perf.irq_fires[i])
					fprintf(f, " %s=%lu", perf_irq_name(i),
						(unsigned long)g_perf.irq_fires[i]);
		}
		fprintf(f, "\n");
		fprintf(f, "ram: mem1_free_kb=%lu mem2=%lu/%luKB peak=%luKB fails=%lu null_read=%lu heap_ok=%u\n",
			(unsigned long)mem1_kb, (unsigned long)m2used, (unsigned long)m2tot,
			(unsigned long)g_perf.mem2_peak_kb, (unsigned long)g_perf.mem2_alloc_fails,
			(unsigned long)g_perf.mem_null_read, (unsigned)gx_mem2_check());
		fprintf(f, "gpu: tex_hit=%lu miss=%lu resets=%lu loads=%lu bytes=%llu batches=%lu\n",
			(unsigned long)g_perf.gx_tex_hits, (unsigned long)g_perf.gx_tex_misses,
			(unsigned long)g_perf.gx_tex_resets, (unsigned long)g_perf.gx_tex_loads,
			g_perf.gx_tex_bytes, (unsigned long)g_perf.gx_batches);
		fprintf(f, "gpu: drawdone=%lu drawdone_us=%llu convert_us=%llu present_us=%llu\n",
			(unsigned long)g_perf.gx_drawdone, g_perf.gx_drawdone_us,
			g_perf.gx_convert_us, g_perf.present_us);
		fprintf(f, "texk: conv_us=%llu conv=%lu texels=%llu tile_us=%llu tile=%lu mdec_us=%llu mdec=%lu\n",
			(unsigned long long)ticks_to_microsecs(g_perf.ogx_conv_ticks), (unsigned long)g_perf.ogx_conv_calls,
			g_perf.ogx_conv_texels,
			(unsigned long long)ticks_to_microsecs(g_perf.ogx_tile_ticks), (unsigned long)g_perf.ogx_tile_calls,
			(unsigned long long)ticks_to_microsecs(g_perf.mdec_ticks), (unsigned long)g_perf.mdec_calls);
		fprintf(f, "ogx: gc=%lu sub_new=%lu sub_hit=%lu skip=%lu unaligned=%lu oob=%lu\n",
			(unsigned long)g_perf.ogx_gc, (unsigned long)g_perf.ogx_sub_new,
			(unsigned long)g_perf.ogx_sub_hit, (unsigned long)g_perf.ogx_skip,
			(unsigned long)g_perf.ogx_unaligned, (unsigned long)g_perf.ogx_oob_upload);
		fprintf(f, "ogxskip: b1=%lu b2=%lu op_empty=%lu op_semi=%lu\n",
			(unsigned long)g_perf.ogx_skip_b1, (unsigned long)g_perf.ogx_skip_b2,
			(unsigned long)g_perf.ogx_skip_op_empty, (unsigned long)g_perf.ogx_skip_op_semi);
		fprintf(f, "ogxupl: semi=%lu opaque=%lu mismatch=%lu\n",
			(unsigned long)g_perf.ogx_upl_semi, (unsigned long)g_perf.ogx_upl_opaque,
			(unsigned long)g_perf.ogx_upl_mismatch);
		fprintf(f, "ogxgeom: coord_rej=%lu\n", (unsigned long)g_perf.ogx_coord_rej);
		fprintf(f, "efbloss: upl_calls=%lu upl_done=%lu | pres: total=%lu clear=%lu inflight_skip=%lu\n",
			(unsigned long)g_perf.upl_calls, (unsigned long)g_perf.upl_done,
			(unsigned long)g_perf.pres_total, (unsigned long)g_perf.pres_clear,
			(unsigned long)g_perf.pres_skipped);
		fprintf(f, "offsoft: prims=%lu rejected=%lu | pad: startpoll=%lu update=%lu ai_calls=%lu\n",
			(unsigned long)g_perf.off_soft_prims, (unsigned long)g_perf.off_soft_rejected,
			(unsigned long)g_perf.pad_startpoll, (unsigned long)g_perf.pad_update, (unsigned long)g_perf.ai_calls);
		{
			extern char padType[10];
			fprintf(f, "sio: write8=%lu start=%lu ctrl16=%lu read8=%lu irq=%lu padtype0=%d\n",
				(unsigned long)g_perf.sio_write8, (unsigned long)g_perf.sio_start, (unsigned long)g_perf.sio_ctrl16,
				(unsigned long)g_perf.sio_read8, (unsigned long)g_perf.sio_irq, (int)padType[0]);
		}
		fprintf(f, "ogxoff: prims=%lu tex=%lu off_roi=%lu fills=%lu fills_off=%lu va=%lu va_roi=%lu\n",
			(unsigned long)g_perf.ogx_off_prim, (unsigned long)g_perf.ogx_off_prim_tex,
			(unsigned long)g_perf.ogx_off_roi,
			(unsigned long)g_perf.ogx_fill_all, (unsigned long)g_perf.ogx_fill_off,
			(unsigned long)g_perf.ogx_va_all, (unsigned long)g_perf.ogx_va_roi);
		{
			unsigned k;
			for (k = 0; k < g_perf.ogx_va_n && k < 8; k++)
				fprintf(f, "ogxva: rect=(%d,%d)-(%d,%d)\n",
					g_perf.ogx_va[k].x0, g_perf.ogx_va[k].y0, g_perf.ogx_va[k].x1, g_perf.ogx_va[k].y1);
		}
		{
			unsigned k;
			for (k = 0; k < g_perf.ogx_op_n && k < 8; k++)
				fprintf(f, "ogxop: kind=%u tex=%u rect=(%d,%d)-(%d,%d) color=%04x\n",
					g_perf.ogx_op[k].kind, g_perf.ogx_op[k].tex,
					g_perf.ogx_op[k].x0, g_perf.ogx_op[k].y0, g_perf.ogx_op[k].x1, g_perf.ogx_op[k].y1,
					g_perf.ogx_op[k].color);
		}
		{
			unsigned k;
			for (k = 0; k < g_perf.ai_n; k++)
				fprintf(f, "autoinput: vblank=%lu mask=%04x at_present=%lu\n", (unsigned long)g_perf.ai_ev[k].vbl, g_perf.ai_ev[k].mask, (unsigned long)g_perf.ai_ev[k].present);
		}
		if (g_perf.pt_armed) {
			unsigned k;
			fprintf(f, "ptrace: armed_at_present=%lu vblank=%lu prims/semi/fills per present:", (unsigned long)g_perf.pt_start_present, (unsigned long)g_perf.pt_start_vblank);
			/* cmd legend: 02 fill, E3/E4 draw area start/end (x,y), E5 draw offset, F5 display address (x,y),
			 * F6 horizontal range (x1,x2), F7 vertical range (y1,y2), F8 display mode (raw in col) */
			for (k = 0; k < 16; k++) fprintf(f, " %u/%u/%u", g_perf.pt_prims[k], g_perf.pt_semi[k], g_perf.pt_fills[k]);
			fprintf(f, "\n");
			for (k = 0; k < g_perf.pt_n; k++)
				fprintf(f, "pt: +%u cmd=%02x %s%s%s%s abr=%u col=%06lx (%d,%d)-(%d,%d)\n",
					g_perf.pt[k].present, g_perf.pt[k].cmd,
					(g_perf.pt[k].flags & 1) ? "S" : "-", (g_perf.pt[k].flags & 2) ? "T" : "-",
					(g_perf.pt[k].flags & 4) ? "Q" : "-", (g_perf.pt[k].flags & 8) ? "G" : "-",
					g_perf.pt[k].abr, (unsigned long)g_perf.pt[k].color,
					g_perf.pt[k].x0, g_perf.pt[k].y0, g_perf.pt[k].x1, g_perf.pt[k].y1);
		}
		fprintf(f, "ogxeq: n=%lu mism=%lu hole=%lu\n",
			(unsigned long)g_perf.ogx_eq_n, (unsigned long)g_perf.ogx_eq_mism, (unsigned long)g_perf.ogx_eq_hole);
		{
			unsigned k;
			for (k = 0; k < g_perf.ogx_eq_r && k < 8; k++)
				fprintf(f, "ogxeqr: tex=%u mode=%u page=%u clut=%04x nv=%u psx_uv=(%u,%u) gx_xy=(%u,%u) exp=%04x got=%04x\n",
					g_perf.ogx_eq[k].texid, g_perf.ogx_eq[k].mode, g_perf.ogx_eq[k].page, g_perf.ogx_eq[k].clut,
					g_perf.ogx_eq[k].nv, g_perf.ogx_eq[k].pu, g_perf.ogx_eq[k].pv, g_perf.ogx_eq[k].x, g_perf.ogx_eq[k].y,
					g_perf.ogx_eq[k].exp, g_perf.ogx_eq[k].got);
		}
		fprintf(f, "ogxdraw: ft=%lu gt=%lu uni_ft=%lu uni_gt=%lu vram_wr=%lu inval=%lu\n",
			(unsigned long)g_perf.ogx_draw_ft, (unsigned long)g_perf.ogx_draw_gt,
			(unsigned long)g_perf.ogx_uni_ft, (unsigned long)g_perf.ogx_uni_gt,
			(unsigned long)g_perf.ogx_vram_wr, (unsigned long)g_perf.ogx_inval);
		{
			unsigned k;
			for (k = 0; k < g_perf.ogx_ud_n && k < 8; k++)
				fprintf(f, "ogxud: gt=%u nv=%u mode=%u page=%u clut=%04x tex=%u %ux%u semi=%u texel=%04x uv1024=(%u,%u)(%u,%u)(%u,%u)(%u,%u)\n",
					g_perf.ogx_ud[k].gt, g_perf.ogx_ud[k].nv, g_perf.ogx_ud[k].mode, g_perf.ogx_ud[k].page, g_perf.ogx_ud[k].clut, g_perf.ogx_ud[k].texid,
					g_perf.ogx_ud[k].w, g_perf.ogx_ud[k].h, g_perf.ogx_ud[k].semi, g_perf.ogx_ud[k].texel,
					g_perf.ogx_ud[k].u[0], g_perf.ogx_ud[k].v[0], g_perf.ogx_ud[k].u[1], g_perf.ogx_ud[k].v[1],
					g_perf.ogx_ud[k].u[2], g_perf.ogx_ud[k].v[2], g_perf.ogx_ud[k].u[3], g_perf.ogx_ud[k].v[3]);
		}
		{
			unsigned k;
			for (k = 0; k < g_perf.ogx_mm_n && k < 8; k++)
				fprintf(f, "ogxmm: tex=%ux%u rect=%u,%u %ux%u glblend=%u semitrans=%u\n",
					g_perf.ogx_mm[k].w, g_perf.ogx_mm[k].h, g_perf.ogx_mm[k].x, g_perf.ogx_mm[k].y,
					g_perf.ogx_mm[k].dw, g_perf.ogx_mm[k].dh, g_perf.ogx_mm[k].blend, g_perf.ogx_mm[k].semi);
		}
		fprintf(f, "cd: reads=%lu bytes=%llu seq=%lu rand=%lu worst_us=%lu total_us=%llu\n",
			(unsigned long)g_perf.cd_reads, g_perf.cd_bytes,
			(unsigned long)g_perf.cd_seq, (unsigned long)g_perf.cd_rand,
			(unsigned long)g_perf.io_worst_us, g_perf.io_total_us);
		fprintf(f, "cdpf: hit=%lu miss=%lu reads=%lu\n",
			(unsigned long)g_perf.cd_pf_hit, (unsigned long)g_perf.cd_pf_miss,
			(unsigned long)g_perf.cd_pf_reads);
		fprintf(f, "chd: hit=%lu miss=%lu err=%lu chd_us=%llu\n",
			(unsigned long)g_perf.chd_hit, (unsigned long)g_perf.chd_miss,
			(unsigned long)g_perf.chd_err, g_perf.chd_us);
		fprintf(f, "xa: sectors=%lu starts=%lu fed=%lu trunc=%lu fill_min=%lu fill_max=%lu freq=%lu stereo=%lu dt_min=%lu dt_max=%lu filtered=%lu\n",
			(unsigned long)g_perf.xa_sectors, (unsigned long)g_perf.xa_starts, (unsigned long)g_perf.xa_fed,
			(unsigned long)g_perf.xa_trunc, (unsigned long)g_perf.xa_fill_min, (unsigned long)g_perf.xa_fill_max,
			(unsigned long)g_perf.xa_freq, (unsigned long)g_perf.xa_stereo,
			(unsigned long)g_perf.xa_dt_min, (unsigned long)g_perf.xa_dt_max, (unsigned long)g_perf.xa_filtered);
		fprintf(f, "xamix: mix=%lu hold=%lu gaps=%lu gap_calls=%lu gap_samples=%lu | spu: ns=%lu pulls=%lu busy=%lu desync=%lu aev=%lu | out: dry=%lu drop=%lu\n",
			(unsigned long)g_perf.xa_mix, (unsigned long)g_perf.xa_hold, (unsigned long)g_perf.xa_gaps,
			(unsigned long)g_perf.xa_gap_calls, (unsigned long)g_perf.xa_gap_samples,
			(unsigned long)g_perf.spu_ns, (unsigned long)g_perf.spu_pulls, (unsigned long)g_perf.spu_busy,
			(unsigned long)g_perf.spu_desync, (unsigned long)g_perf.aev_n,
			(unsigned long)g_perf.out_dry, (unsigned long)g_perf.out_drop);
		fprintf(f, "rate: ppm=%ld i=%ld min=%ld max=%ld occ_min=%lu occ_max=%lu occ_avg=%lu updates=%lu sat=%lu changes=%lu prefill=%lu | limit: debt_max=%lu debt_drops=%lu\n",
			(long)g_perf.rate_ppm_now, (long)g_perf.rate_i_now, (long)g_perf.rate_ppm_min, (long)g_perf.rate_ppm_max,
			(unsigned long)g_perf.rate_occ_min, (unsigned long)g_perf.rate_occ_max,
			(unsigned long)(g_perf.rate_updates ? g_perf.rate_occ_sum / g_perf.rate_updates : 0),
			(unsigned long)g_perf.rate_updates, (unsigned long)g_perf.rate_sat, (unsigned long)g_perf.rate_changes,
			(unsigned long)g_perf.rate_prefill, (unsigned long)g_perf.limit_debt_max, (unsigned long)g_perf.limit_debt_drops);
		fprintf(f, "save: states=%lu save_us=%llu loads=%lu load_us=%llu bytes=%lu fails=%lu mcd=%lu mcd_us=%llu mcd_fails=%lu\n",
			(unsigned long)g_perf.state_saves, g_perf.state_save_us,
			(unsigned long)g_perf.state_loads, g_perf.state_load_us,
			(unsigned long)g_perf.state_bytes, (unsigned long)g_perf.state_fails,
			(unsigned long)g_perf.mcd_saves, g_perf.mcd_save_us,
			(unsigned long)g_perf.mcd_fails);
		if(g_netwait_old_us)
			fprintf(f, "netwait: old_us=%llu old_wakes=%lu new_us=%llu new_wakes=%lu\n",
				g_netwait_old_us, g_netwait_old_wakes,
				g_netwait_new_us, g_netwait_new_wakes);
		#if PERF_PROF_BIOS
		perf_report_bios(f);
		#endif
		if (g_perf.mdec_blocks)
			fprintf(f, "mdec: blocks=%lu dconly=%lu rl_us=%llu yuv_us=%llu\n",
				(unsigned long)g_perf.mdec_blocks, (unsigned long)g_perf.mdec_dconly,
				g_perf.mdec_rl_us, g_perf.mdec_yuv_us);
		{
			/* What each PlayStation port ended up as, and which physical controllers the
			 * drivers found. Without this an assignment can only be judged from whether a
			 * game happens to poll a port. */
			extern char padType[10], padAssign[10];
			int p;
			fprintf(f, "ports:");
			for (p = 0; p < 10; p++)
				fprintf(f, " %d:%d/%d", p, padType[p], padAssign[p]);
			fprintf(f, " | avail");
			for (p = 0; p < num_controller_t; p++) {
				const controller_t *c = controller_ts[p];
				fprintf(f, " %c=%d%d%d%d", c->identifier,
					c->available[0], c->available[1],
					c->available[2], c->available[3]);
			}
			fprintf(f, "\n");
		}
		fprintf(f, "menu: frames=%lu menu_us=%llu strings=%lu glyphs=%lu texloads=%lu\n",
			(unsigned long)g_perf.menu_frames, g_perf.menu_us,
			(unsigned long)g_perf.menu_strings, (unsigned long)g_perf.menu_glyphs,
			(unsigned long)g_perf.menu_texloads);
		fclose(f);
	}
	if (g_perf.aev_n)
		perf_audio_flush();
	perf_pad_flush();

#ifdef SHOW_DEBUG
	/* Mirror compact lines to overlay rows 22..29 (rows 0..21 are taken by
	 * the existing DBG_* slots; DEBUG_update ages them after 5 s). */
	snprintf(line, sizeof(line), "perf f=%lu cpu=%llums jitR=%lu/%lu fb=%lu",
		(unsigned long)g_perf.present_frames,
		(unsigned long long)ticks_to_microsecs(g_perf.cpu_ticks) / 1000,
		(unsigned long)g_perf.jit_resets_full,
		(unsigned long)g_perf.jit_resets_partial,
		(unsigned long)g_perf.jit_interp_fallbacks);
	DEBUG_print(line, 22);
	snprintf(line, sizeof(line), "gpu batch=%lu hit=%lu miss=%lu rst=%lu dd=%lu(%llums)",
		(unsigned long)g_perf.gx_batches,
		(unsigned long)g_perf.gx_tex_hits, (unsigned long)g_perf.gx_tex_misses,
		(unsigned long)g_perf.gx_tex_resets,
		(unsigned long)g_perf.gx_drawdone, g_perf.gx_drawdone_us / 1000);
	DEBUG_print(line, 23);
	snprintf(line, sizeof(line), "cd r=%lu seq=%lu chd h/m/e=%lu/%lu/%lu worst=%luus",
		(unsigned long)g_perf.cd_reads,
		(unsigned long)g_perf.cd_seq,
		(unsigned long)g_perf.chd_hit, (unsigned long)g_perf.chd_miss,
		(unsigned long)g_perf.chd_err, (unsigned long)g_perf.io_worst_us);
	DEBUG_print(line, 24);
	snprintf(line, sizeof(line), "mem2 %luKB peak %luKB fail %lu menu %lu glyphs",
		(unsigned long)(gx_mem2_used() >> 10),
		(unsigned long)g_perf.mem2_peak_kb,
		(unsigned long)g_perf.mem2_alloc_fails,
		(unsigned long)g_perf.menu_glyphs);
	DEBUG_print(line, 25);
#endif
}
#else
typedef int perf_prof_disabled_in_this_build;
#endif /* PERF_PROF */
