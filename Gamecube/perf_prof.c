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
#include <ogc/lwp_objmgr.h>   /* the threads: line (_lwp_thr_objects) */
#include <ogc/lwp_threads.h>

#include "perf_prof.h"
#include "lc.h"
#include "gc_input/controller.h"   /* the ports report below */
#include "../mem2_manager.h"
#include "../psxcommon.h"
#include "../r3000a.h"      /* psxRegs, for perf_state_log */
#include "MEM2.h"   /* NEW_MEM2_LO, LIGHTREC_CODE_SIZE: the mem2map: line */

/* Diagnostic-only; declared here rather than in mem2_manager.h so the
 * profiler stays the single consumer. */
extern uint32_t gx_mem2_check(void);

#ifdef SHOW_DEBUG
#include "DEBUG.h"
#endif

perf_counters_t g_perf;
perf_carry_t g_carry;

unsigned long long perf_now_us(void)
{
	// gettime() (u64), NOT gettick() (u32): the Wii time base advances at
	// TB_TIMER_CLOCK*1000 == 60,750,000 ticks/sec, so a 32-bit tick counter
	// wraps every 2^32/60.75e6 ~= 70.7 seconds. Reports are emitted every
	// 1800 frames (~30 s), so wraps landed inside measurement windows
	// routinely, and every "perf_now_us() - t0" that straddled one underflowed
	// into a huge u64 (cpu_us/present_us showing ~1.8e19 in perf.log).
	//
	// ticks_to_microsecs() divides a u64: a libgcc call (__udivdi3) on this 32-bit CPU,
	// and 11% of a debug run's busy time with the probes that call this per HLE call, per
	// SPU mix and per CD sector (hprof, 2026-10-01). Multiply by 2^24/TB_TIMER_CLOCK (in
	// MHz) instead: 0.6 ppm low, no overflow before 12 days of uptime.
	return (gettime() * (((1ULL << 24) * 1000 + TB_TIMER_CLOCK / 2) / TB_TIMER_CLOCK)) >> 24;
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

#if PERF_PROF_PMC
#include <ogc/machine/processor.h>

/* Point the counters at the chosen events and zero them. Called from perf_reset(), so the
 * numbers cover one run of the emulated machine. */
static void perf_pmc_start(void)
{
	mtmmcr0(0);                 /* stop counting while the selects change */
	mtpmc1(0); mtpmc2(0); mtpmc3(0); mtpmc4(0);
	mtmmcr1(PMC_MMCR1);
	mtmmcr0(PMC_MMCR0);
}

/* Add what each counter moved since the last read. Called at every present (well inside the
 * 5.9 s it takes a 32-bit cycle count to wrap) and once more at report time. The unsigned
 * subtraction is right across a wrap. */
static void perf_pmc_read(void)
{
	uint32_t now[4];
	int i;
	now[0] = mfpmc1(); now[1] = mfpmc2(); now[2] = mfpmc3(); now[3] = mfpmc4();
	for (i = 0; i < 4; i++) {
		g_perf.pmc[i] += (uint32_t)(now[i] - g_perf.pmc_last[i]);
		g_perf.pmc_last[i] = now[i];
	}
}
#endif

unsigned long long g_netwait_old_us, g_netwait_new_us;
unsigned long g_netwait_old_wakes, g_netwait_new_wakes;

void perf_reset(void)
{
	/* vramio.log: a line where each game of a chain starts (kind FF) */
	perf_vram_event(0xFF, 0, 0, 1024, 512, 0, 0);
	{
		/* one log per boot: the SD image keeps files across runs, so appending
		 * across boots made the log grow and mixed runs */
		static int fresh = 0;
		if (!fresh) {
			FILE *f = fopen("sd:/wiisxrx/perf.log", "w");
			if (f) {
				char when[32];
				perf_datetime(when, sizeof when);
				fprintf(f, "# run started %s, build %s %s\n", when, __DATE__, __TIME__);
				fclose(f);
			}
			fresh = 1;
		}
	}
	memset(&g_perf, 0, sizeof(g_perf));
	g_perf.wall_start_ticks = gettime();
#if PERF_PROF_PMC
	perf_pmc_start();
#endif
#if PERF_PROF_HPROF
	hprof_start();
#endif
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
	/* The XFB the TV shows: an 8-byte header (width, height, big-endian u32) and YUYV.
	 * Under Dolphin it holds the copies only with XFB_RAM=1 (XFBToTextureEnable=False). */
	{
		extern u32 *xfb[3];
		extern unsigned g_xfb_w, g_xfb_h;
		unsigned hdr[2] = { g_xfb_w, g_xfb_h };
		f = fopen("sd:/wiisxrx/xfb.bin", "wb");
		if (f) {
			fwrite(hdr, 4, 2, f);
			if (xfb[2])   /* FB_FRONT in SoftGPU/drawGX.c */
				fwrite(MEM_K1_TO_K0(xfb[2]), 2, g_xfb_w * g_xfb_h, f);
			fclose(f);
		}
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

/* VRAM transfers, whole run (sd:/wiisxrx/vramio.log). One line per distinct event: a CPU
 * read of VRAM (C0), an image load (A0), a VRAM-to-VRAM move (80) or a readback outcome
 * (C1), with the rect, two values the caller chooses and, when the same event repeats,
 * a count and the last vblank. Only transfers of 32x32 or more are kept: that is where a
 * game moves pictures (a pause screen's frozen frame, a fade's last frame), and it keeps
 * the file to a few dozen lines instead of the primitive trace's thousands.
 *
 * A repeat folds into the same event among the last VIO_WINDOW lines, not only the last
 * one: a game cycles several transfers a frame (two buffers, a few texture strips), and
 * folding only neighbours filled 2048 lines in four minutes of Ape Escape. The search stops
 * at a game start (FF), so each game of a chain keeps its own lines. Past VIO_MAX lines new
 * events are counted, not kept ("lines dropped" in the header). */
#define VIO_MAX    4096
#define VIO_WINDOW 64
static struct { uint32_t vbl, last, n; uint16_t kind, x, y, w, h; int32_t a, b; } vio[VIO_MAX];
static unsigned vio_n, vio_count[4], vio_dropped;

/* the 16 most frequent PCs at vblank time (space-saving: a new PC replaces the rarest) */
static struct { uint32_t pc, n; } pcs[16];
static unsigned crumb_pc;   /* the last vblank's guest PC, for the breadcrumb */

void perf_pc_sample(unsigned pc)
{
	int i, low = 0;
	crumb_pc = pc;
	for (i = 0; i < 16; i++) {
		if (pcs[i].n && pcs[i].pc == pc) { pcs[i].n++; return; }
		if (pcs[i].n < pcs[low].n) low = i;
	}
	pcs[low].pc = pc;
	pcs[low].n++;
}

void perf_vram_event(unsigned kind, int x, int y, int w, int h, int a, int b)
{
	unsigned k = kind == 0xC0 ? 0 : kind == 0xA0 ? 1 : kind == 0x80 ? 2 : 3, i;
	if (kind != 0xFF) vio_count[k]++;
	if (w * h < 32 * 32 && kind < 0xF0) return;   /* F1, F5: w,h is the draw offset */
	for (i = kind == 0xFF ? 0 : vio_n; i > 0 && vio_n - i < VIO_WINDOW; i--) {   /* a game start never folds */
		if (vio[i - 1].kind == 0xFF) break;
		if (vio[i - 1].kind == kind && vio[i - 1].x == x && vio[i - 1].y == y &&
		    vio[i - 1].w == w && vio[i - 1].h == h && vio[i - 1].a == a && vio[i - 1].b == b) {
			vio[i - 1].n++; vio[i - 1].last = frame_counter;
			return;
		}
	}
	if (vio_n >= VIO_MAX) { vio_dropped++; return; }
	vio[vio_n].vbl = vio[vio_n].last = frame_counter; vio[vio_n].n = 1;
	vio[vio_n].kind = (uint16_t)kind;
	vio[vio_n].x = (uint16_t)x; vio[vio_n].y = (uint16_t)y; vio[vio_n].w = (uint16_t)w; vio[vio_n].h = (uint16_t)h;
	vio[vio_n].a = a; vio[vio_n].b = b;
	vio_n++;
}

/* The console text of the run (SysPrintf: the PS1 program's BIOS printf/puts/putchar under
 * the HLE BIOS, and WiiStation's own messages), for sd:/wiisxrx/tty.log. SysPrintf printed
 * only in USB Gecko builds, so a test program's results (AmiDog's psxtest_gte prints every
 * error it finds) were lost. */
static char tty_buf[64 * 1024];
static unsigned tty_n;

void perf_tty(const char *s)
{
	while (*s && tty_n < sizeof(tty_buf) - 1)
		tty_buf[tty_n++] = *s++;
}

static void perf_tty_flush(void)
{
	FILE *f;
	if (!tty_n) return;
	f = fopen("sd:/wiisxrx/tty.log", "w");
	if (!f) return;
	fwrite(tty_buf, 1, tty_n, f);
	fclose(f);
}

static void perf_vram_flush(void)
{
	unsigned k;
	FILE *f;
	if (!vio_n) return;
	f = fopen("sd:/wiisxrx/vramio.log", "w");
	if (!f) return;
	fprintf(f, "# vblank[-last] xN kind x,y wxh a b   (C0 CPU read; A0 image load; 80 move: a,b = source;"
		" C1 EFB sync of a read (GlesGpu/efbSync.inc): a = tiles GX drew, b = tiles no snapshot could fill;"
		" C2: a = %% of the rect written, b = 1 if the live EFB was copied for it;"
		" C4 present: a = frames kept (snapshots on), b = a read has missed;"
		" F1 vblank (OpenGX, non-interlaced): x,y display, wxh = draw offset, a = iDrawnSomething,"
		" b = 1 full-screen upload + 2 present pending + 4 presented + 8 drawing the displayed buffer;"
		" F5 GP1 05 display move: x,y new display, wxh = draw offset, a = iDrawnSomething, b = 1 presented;"
		" FF: a game starts)\n");
	{
		char when[32];
		perf_datetime(when, sizeof when);
		fprintf(f, "# written %s\n", when);
	}
	fprintf(f, "# counts all sizes: c0=%u a0=%u 80=%u c1=%u, lines dropped=%u\n",
		vio_count[0], vio_count[1], vio_count[2], vio_count[3], vio_dropped);
	for (k = 0; k < vio_n; k++) {
		if (vio[k].n > 1)
			fprintf(f, "%lu-%lu x%lu ", (unsigned long)vio[k].vbl, (unsigned long)vio[k].last, (unsigned long)vio[k].n);
		else
			fprintf(f, "%lu ", (unsigned long)vio[k].vbl);
		fprintf(f, "%02X %u,%u %ux%u %ld %ld\n", vio[k].kind, vio[k].x, vio[k].y, vio[k].w, vio[k].h,
			(long)vio[k].a, (long)vio[k].b);
	}
	fclose(f);
}

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

/* Every emulated vblank (psxcounters.c): the scheduled dump. Not at a present: a game that
 * stops presenting while it loads (FF7 after vblank 688) never reached a dump set there. */
/* Per-vblank timeline: for every emulated vblank, the wall time since the last one and how
 * much of it the core (cpu_ticks), the GPU registers (hw_gpu_ticks), the frame limiter's wait
 * (limit_ticks) and the SPU took, plus the frames presented. A slowdown a person sees shows
 * as a run of vblanks with a long wall and no limiter wait; the totals once a minute hide it.
 * Units of 512 time-base ticks (8.4 us on the Wii), saturating at 0xffff: a shift, no divide.
 * The ring comes from the MEM2 heap (12 bytes a vblank, 65536 vblanks = 18 emulated minutes)
 * and each chained game writes it to sd:/wiisxrx/vtl_NN.bin at its end (perf_vtl_flush);
 * scripts/vtl_view.py reads it. */
#define VTL_MAX 65536
#define VTL_SHIFT 9
typedef struct { unsigned short wall, cpu, gpu, limit, spu, pres, jit; } vtl_t;   /* jit: Lightrec compiling */
static vtl_t *vtl;
static unsigned vtl_n;
static uint64_t vtl_t0, vtl_cpu0, vtl_gpu0, vtl_lim0, vtl_spu0, vtl_jit0;
extern unsigned long long lightrec_jit_ticks;   /* deps/lightrec/lightrec.c */
static uint32_t vtl_pres0;

static inline unsigned short vtl_u16(uint64_t ticks)
{
	ticks >>= VTL_SHIFT;
	return ticks > 0xffff ? 0xffff : (unsigned short)ticks;
}

/* The guest's signature per vblank, beside the timeline: a hash of every 8th word of RAM
 * (0.45 ms a vblank on a Wii), the cycle count and the PC. Two runs that should be the same
 * show where the guest first differs: scripts/vsig_cmp.py, vsig_NN.bin per chained game. */
typedef struct { u32 ram, cycle, pc; } vsig_t;
static vsig_t *vsig;

static void vsig_take(vsig_t *g)
{
	extern s8 *psxM;
	const u32 *w = (const u32 *)psxM;
	u32 h = 2166136261u, k;
	for (k = 0; k < 0x200000 / 4; k += 8)
		h = (h ^ w[k]) * 16777619u;
	g->ram = h;
	g->cycle = psxRegs.cycle;
	g->pc = psxRegs.pc;
}

static void vtl_tick(void)
{
	uint64_t now = gettime();
	if (!vtl) {
		vtl = (vtl_t *)_mem2_malloc(VTL_MAX * sizeof(vtl_t));
		vsig = (vsig_t *)_mem2_malloc(VTL_MAX * sizeof(vsig_t));
		if (!vtl)
			return;
		vtl_n = 0;
	} else if (vtl_n < VTL_MAX && vtl_t0) {
		vtl_t *v = &vtl[vtl_n++];
		if (vsig)
			vsig_take(&vsig[vtl_n - 1]);
		v->wall = vtl_u16(now - vtl_t0);
		v->cpu = vtl_u16(g_perf.cpu_ticks - vtl_cpu0);
		v->gpu = vtl_u16(g_perf.hw_gpu_ticks - vtl_gpu0);
		v->limit = vtl_u16(g_perf.limit_ticks - vtl_lim0);
		v->spu = vtl_u16(g_perf.spu_ticks - vtl_spu0);
		v->pres = (unsigned short)(g_perf.present_frames - vtl_pres0);
		v->jit = vtl_u16(lightrec_jit_ticks - vtl_jit0);
	}
	vtl_t0 = now;
	vtl_cpu0 = g_perf.cpu_ticks; vtl_gpu0 = g_perf.hw_gpu_ticks;
	vtl_lim0 = g_perf.limit_ticks; vtl_spu0 = g_perf.spu_ticks;
	vtl_pres0 = g_perf.present_frames;
	vtl_jit0 = lightrec_jit_ticks;
}

/* A chained game's end (GamecubeMain.cpp chainNext): its timeline to vtl_NN.bin, then a fresh
 * one for the next game. Header: "VTL1", the shift, the vblank count. */
void perf_vtl_flush(int game)
{
	char path[40];
	FILE *f;
	unsigned hdr[3] = { 0x56544c32, VTL_SHIFT, vtl_n };   /* "VTL2": 7 fields */
	if (!vtl)
		return;
	snprintf(path, sizeof path, "sd:/wiisxrx/vtl_%02d.bin", game);
	if ((f = fopen(path, "wb"))) {
		fwrite(hdr, sizeof hdr, 1, f);
		fwrite(vtl, sizeof(vtl_t), vtl_n, f);
		fclose(f);
	}
	snprintf(path, sizeof path, "sd:/wiisxrx/vsig_%02d.bin", game);
	if (vsig && (f = fopen(path, "wb"))) {
		unsigned vh[2] = { 0x56534731, vtl_n };   /* "VSG1", then {ram, cycle, pc} per vblank */
		fwrite(vh, sizeof vh, 1, f);
		fwrite(vsig, sizeof(vsig_t), vtl_n, f);
		fclose(f);
	}
	vtl_n = 0;
	vtl_t0 = 0;
#if PERF_PROF_HPROF
	hprof_flush(game);
#endif
}

/* A chained game's end: hashes of the guest's RAM and of VRAM, and its PC and cycle count.
 * Two runs with equal "state:" lines and different vram_NN.bin had the same guest and a
 * host-side difference in VRAM; a different ram= is a guest divergence. FNV-1a, 32-bit. */
void perf_state_log(int game)
{
	extern s8 *psxM;
	extern unsigned short *psxVuw;
	u32 hr = 2166136261u, hv = 2166136261u, k;
	const u32 *w = (const u32 *)psxM;
	FILE *f;

	for (k = 0; k < 0x200000 / 4; k++)
		hr = (hr ^ w[k]) * 16777619u;
	if (psxVuw)
		for (k = 0, w = (const u32 *)psxVuw; k < 1024 * 512 / 2; k++)
			hv = (hv ^ w[k]) * 16777619u;
	if ((f = fopen("sd:/wiisxrx/perf.log", "a"))) {
		fprintf(f, "state: game=%d ram=%08x vram=%08x pc=%08x cycle=%u vblank=%u\n",
			game, (unsigned)hr, (unsigned)hv, (unsigned)psxRegs.pc, (unsigned)psxRegs.cycle,
			(unsigned)frame_counter);
		fclose(f);
	}
}

void perf_fatal_log(unsigned code, const char *reason)
{
	FILE *f;

	perf_report();   /* the counters so far, then the reason: the run stops here */
	if ((f = fopen("sd:/wiisxrx/perf.log", "a"))) {
		fprintf(f, "fatal: code=0x%02x pc=%08x cycle=%u vblank=%u %s\n", code,
			(unsigned)psxRegs.pc, (unsigned)psxRegs.cycle, (unsigned)frame_counter, reason);
		fclose(f);
	}
}

void perf_vblank_tick(void)
{
	extern unsigned autoinput_crash_vbl;
	vtl_tick();
	/* "crashtest <vblank>": a DSI on purpose, the way the 2026-09-29 Lightrec crash stored
	 * through r31 = 0, to prove the crash path (vm dsihandler -> HBC agent -> report) on the Wii. */
	/* Crash breadcrumb: one line a second, written and closed at once, so the last line
	 * survives a freeze (a buffered perf.log block dies with it). Costs one small SD write
	 * a second (a dropped frame each time on the Wii), so only when the input script says
	 * "crumbs"; the line says which chained game, vblank, guest PC and presents. */
	extern int autoinput_crumbs;
	if (autoinput_crumbs && (frame_counter % 60) == 0) {
		extern int perf_chain_index(void);
		FILE *cf = fopen("sd:/wiisxrx/crumb.log", "a");
		if (cf) {
			fprintf(cf, "g%d v%u pc%08x p%lu cd%lu\n", perf_chain_index(), frame_counter, crumb_pc,
				(unsigned long)g_perf.present_frames, (unsigned long)g_perf.cd_reads);
			fclose(cf);
		}
	}
	if (autoinput_crash_vbl && frame_counter >= autoinput_crash_vbl) {
		perf_report();
		*(volatile unsigned *)0xFFFFFFF4 = 0x0badc0de;
	}
	/* "hangtest <vblank>": spin here for good, interrupts on, to prove the hang watchdog
	 * (ws_crash.c): a crash block with code 0x81 about 60 s later, then HBC. */
	{
		extern unsigned autoinput_hang_vbl;
		if (autoinput_hang_vbl && frame_counter >= autoinput_hang_vbl) {
			perf_report();
			for (;;)
				__asm__ volatile ("nop");
		}
	}
	/* "padtype <vblank> <port> <type>": the port's type changes as the Configure Input menu
	 * changes it (set_port_type), with the game running. A report first, so the counters
	 * before and after the change are apart. */
	{
		extern int autoinput_padtype_due(unsigned now, unsigned *port, unsigned *type);   /* PadWiiSX.c */
		extern void set_port_type(int port, int type);
		unsigned pt_port, pt_type;
		while (autoinput_padtype_due(frame_counter, &pt_port, &pt_type)) {
			FILE *pf;
			perf_report();
			set_port_type((int)pt_port, (int)pt_type);
			if ((pf = fopen("sd:/wiisxrx/perf.log", "a"))) {
				fprintf(pf, "padtype: vblank=%u port=%u type=%u\n", frame_counter, pt_port + 1, pt_type);
				fclose(pf);
			}
		}
	}
	if (!g_perf.vram_dumped && autoinput_dump_vbl && frame_counter >= autoinput_dump_vbl) {
		g_perf.vram_dumped = 1;
		perf_vram_dump();
		perf_report();     /* a scheduled dump marks the moment of interest: report the counters now, not 1800 presents later */
	}
}

void perf_present_tick(unsigned long long present_us)
{
	g_perf.present_frames++;
#if PERF_PROF_PMC
	perf_pmc_read();
#endif
#if PERF_PROF_TRACE
	if (g_perf.pt_armed && !g_perf.pt_printed && g_perf.present_frames - g_perf.pt_start_present >= 16)
		perf_trace_flush();
#endif
	g_perf.present_us += present_us;
	/* the audio ring is written from the present path, not from inside the mixer */
	if (g_perf.aev_n == PERF_AEV_N && !g_perf.aev_printed)
		perf_audio_flush();
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
			char when[32];
			perf_datetime(when, sizeof when);
			fprintf(f, "time: %s\n", when);
		}
		{
			extern uint32_t dwActFixes; extern char CdromId[10];
			fprintf(f, "fixes: dwActFixes=%08lx cdrom=%s", (unsigned long)dwActFixes, CdromId);
			if (g_perf.hack_dc2)   /* where the hack still fires, and on what (GAME_HACKS.md) */
				fprintf(f, " dc2_drop=%lu at=%lu,%lu", (unsigned long)g_perf.hack_dc2,
					(unsigned long)g_perf.hack_dc2_x, (unsigned long)g_perf.hack_dc2_y);
			fprintf(f, "\n");
		}
		fprintf(f, "cpu: slices=%lu int=%lu cpu_us=%llu jit_full=%lu jit_part=%lu interp_fb=%lu hle=%lu exc=%lu\n",
			(unsigned long)g_perf.jit_slices, (unsigned long)g_perf.int_slices,
			(unsigned long long)ticks_to_microsecs(g_perf.cpu_ticks),
			(unsigned long)g_perf.jit_resets_full,
			(unsigned long)g_perf.jit_resets_partial,
			(unsigned long)g_perf.jit_interp_fallbacks,
			(unsigned long)g_perf.jit_hle, (unsigned long)g_perf.jit_exceptions);
		/* The live LWP threads (libogc2 allows LWP_MAX_THREADS, 16): their count and, for
		 * each, its entry function, priority and state -- a count that grows from one chained
		 * game to the next is a leak (the CD read-ahead failed to start late in a 16-game
		 * chain, 2026-09-30); powerpc-eabi-addr2line -f -e the ELF names the entries. */
		{
			extern lwp_objinfo _lwp_thr_objects;
			unsigned i, live = 0;
			char tl[16 * 24] = "";
			for (i = 0; i < _lwp_thr_objects.max_nodes; i++) {
				lwp_cntrl *t = (lwp_cntrl *)_lwp_thr_objects.local_table[i];
				if (!t)
					continue;
				live++;
				snprintf(tl + strlen(tl), sizeof tl - strlen(tl), " %08x@%u/%x",
					(unsigned)t->entry, (unsigned)t->cur_prio, (unsigned)t->cur_state);
			}
			fprintf(f, "threads: live=%u of %u free=%u |%s\n", live, (unsigned)_lwp_thr_objects.max_nodes,
				(unsigned)_lwp_thr_objects.inactives_cnt, tl);
		}
		/* Lightrec compiling (deps/lightrec/lightrec.c): new blocks decoded and optimized
		 * (pre) and blocks turned into PowerPC code; a game that loads new code stalls on it */
		{
			extern unsigned long long lightrec_jit_ticks, lightrec_jit_pre_ticks;
			extern unsigned int lightrec_jit_blocks, lightrec_jit_pre_blocks;
			fprintf(f, "jit: pre_blocks=%u pre_us=%llu blocks=%u gen_us=%llu\n",
				lightrec_jit_pre_blocks, (unsigned long long)ticks_to_microsecs(lightrec_jit_pre_ticks),
				lightrec_jit_blocks,
				(unsigned long long)ticks_to_microsecs(lightrec_jit_ticks - lightrec_jit_pre_ticks));
		}
		{   /* lightrec_compile_block's parts (deps/lightrec/lightrec.c lightrec_cprof) */
			extern unsigned long long lightrec_cprof[4];
			fprintf(f, "jitparts: new_state_us=%llu nodes_us=%llu emit_code_us=%llu tail_us=%llu\n",
				(unsigned long long)ticks_to_microsecs(lightrec_cprof[0]),
				(unsigned long long)ticks_to_microsecs(lightrec_cprof[1]),
				(unsigned long long)ticks_to_microsecs(lightrec_cprof[2]),
				(unsigned long long)ticks_to_microsecs(lightrec_cprof[3]));
		}
		{   /* the emitted code's checksum: equal in two runs = the same code was generated */
			extern unsigned int lightrec_code_sum, lightrec_code_bytes, lightrec_code_shape;
			fprintf(f, "jitcode: bytes=%u sum=%08x shape=%08x\n", lightrec_code_bytes,
				lightrec_code_sum, lightrec_code_shape);
		}
#if PERF_PROF_HPROF
		hprof_report(f);
#endif
		{   /* _jit_optimize's passes (deps/lightning/lib/lightning.c lightning_opt_ticks) */
			extern unsigned long long lightning_opt_ticks[10];
			static const char *nm[10] = { "thread", "labels", "split", "setup", "follow",
				"patch", "simplify", "store", "refollow", "final" };
			extern unsigned int lightning_saves, lightning_patch_nodes;
			int k;
			fprintf(f, "lopt:");
			for (k = 0; k < 10; k++)
				fprintf(f, " %s=%llu", nm[k], (unsigned long long)ticks_to_microsecs(lightning_opt_ticks[k]));
			fprintf(f, " | saves=%u nodes=%u\n", lightning_saves, lightning_patch_nodes);
		}
		{   /* Lightning's node-pool allocation (deps/lightning/lib/lightning.c new_pool) */
			extern unsigned long long lightning_pool_ticks;
			extern unsigned int lightning_pools;
			fprintf(f, "lpool: pool_us=%llu pools=%u\n",
				(unsigned long long)ticks_to_microsecs(lightning_pool_ticks), lightning_pools);
		}
		{   /* GNU Lightning's own stages inside gen_us (deps/lightning/lib/lightning.c) */
			extern unsigned long long lightning_prof_ticks[3];
			extern unsigned int lightning_prof_emits, lightning_prof_blocks;
			fprintf(f, "lightning: blocks=%u optimize_us=%llu size_us=%llu emit_us=%llu emits=%u\n",
				lightning_prof_blocks,
				(unsigned long long)ticks_to_microsecs(lightning_prof_ticks[0]),
				(unsigned long long)ticks_to_microsecs(lightning_prof_ticks[1]),
				(unsigned long long)ticks_to_microsecs(lightning_prof_ticks[2]),
				lightning_prof_emits);
		}
		/* LimiterWait=1 (SoftGPU/oldGpuFps.c): how the sleeping wait behaved */
		fprintf(f, "limitwait: sleeps=%lu sleep_us=%llu over_max_us=%llu overs=%lu\n",
			(unsigned long)g_perf.limit_sleeps,
			(unsigned long long)ticks_to_microsecs(g_perf.limit_sleep_ticks),
			(unsigned long long)ticks_to_microsecs(g_perf.limit_over_max),
			(unsigned long)g_perf.limit_overs);
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
		if (g_perf.softcall_runs || g_perf.softcall_steps)
			fprintf(f, "softcall: runs=%lu steps=%lu escapes=%lu run_escapes=%lu hle_exits=%lu\n",
				(unsigned long)g_perf.softcall_runs, (unsigned long)g_perf.softcall_steps,
				(unsigned long)g_perf.softcall_escapes,
				(unsigned long)g_perf.softcall_run_escapes,
				(unsigned long)g_perf.softcall_hle_exits);
		if (g_perf.jit_nested)
			fprintf(f, "nested: n=%lu sched_us=%llu jit_us=%llu limit_us=%llu\n",
				(unsigned long)g_perf.jit_nested,
				(unsigned long long)ticks_to_microsecs(g_perf.slice_nested_sched_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.slice_nested_jit_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.slice_nested_limit_ticks));
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
		/* the time each event's callback took (lightrec.c irq_test), exceptions it raised
		 * included: what the scheduler's share of wall is made of */
		fprintf(f, "irqus:");
		{
			unsigned i;
			for (i = 0; i < PERF_IRQ_SLOTS; i++)
				if (g_perf.irq_ticks[i])
					fprintf(f, " %s=%llu", perf_irq_name(i),
						(unsigned long long)ticks_to_microsecs(g_perf.irq_ticks[i]));
			fprintf(f, " line_exc=%llu\n", (unsigned long long)ticks_to_microsecs(g_perf.irq_line_ticks));
		}
		fprintf(f, "ram: mem1_free_kb=%lu mem2=%lu/%luKB peak=%luKB fails=%lu null_read=%lu heap_ok=%u\n",
			(unsigned long)mem1_kb, (unsigned long)m2used, (unsigned long)m2tot,
			(unsigned long)g_perf.mem2_peak_kb, (unsigned long)g_perf.mem2_alloc_fails,
			(unsigned long)g_perf.mem_null_read, (unsigned)gx_mem2_check());
		{
			/* MEM2 below and above the heap, and the JIT's code: what the fixed reserves and
			 * the Lightrec buffer are really used for (Docs/MEMORY_MAP.md). arena2 = libogc's
			 * own allocations from the 2 MB kept for it below the heap (SYS_AllocArena2MemLo
			 * moves Arena2Lo up); top_gap = from the heap's end to IOS's top. */
			extern unsigned int lightrec_get_mem_usage(int type);   /* deps/lightrec memmanager.c */
			static unsigned code_peak_kb, mem1_min_kb = ~0u;
			unsigned code_kb = lightrec_get_mem_usage(0) >> 10;   /* MEM_FOR_CODE */
			unsigned a2lo = (unsigned)SYS_GetArena2Lo(), a2hi = (unsigned)SYS_GetArena2Hi();
			unsigned base = (unsigned)NEW_MEM2_LO;
			if (code_kb > code_peak_kb) code_peak_kb = code_kb;
			if (mem1_kb < mem1_min_kb) mem1_min_kb = mem1_kb;
			fprintf(f, "mem2map: arena2_used_kb=%u of %u ios_hi=%08x heap_end_gap_kb=%u ipc=%08x-%08x | lightrec_code_kb=%u peak=%u of %u | mem1_min_kb=%u\n",
				(a2lo - base) >> 10, (a2hi - base) >> 10, (unsigned)gx_mem2_ios_hi,
				(unsigned)((gx_mem2_ios_hi - (a2hi + gx_mem2_total())) >> 10),
				*(unsigned *)0x80003130, *(unsigned *)0x80003134,
				code_kb, code_peak_kb, (unsigned)(LIGHTREC_CODE_SIZE >> 10), mem1_min_kb);
		}
		{
			extern void ogx_tex_mem(unsigned *n, unsigned *data_kb, unsigned *semi_kb);
			unsigned tn, tkb, skb;
			ogx_tex_mem(&tn, &tkb, &skb);
			extern unsigned ogx_fail_n, ogx_fail_data_kb, ogx_fail_semi_kb, ogx_fail_mem2_kb, ogx_fail_want;
			fprintf(f, "texmem: live=%u data_kb=%u semi_kb=%u flushes=%lu | atfail: live=%u data_kb=%u semi_kb=%u mem2_kb=%u want=%u | subcache max=%lu wraps=%lu\n",
				tn, tkb, skb, (unsigned long)g_perf.ogx_tex_flush,
				ogx_fail_n, ogx_fail_data_kb, ogx_fail_semi_kb, ogx_fail_mem2_kb, ogx_fail_want,
				(unsigned long)g_perf.subcache_max, (unsigned long)g_perf.subcache_wraps);
		}
		fprintf(f, "gpu: tex_hit=%lu miss=%lu resets=%lu loads=%lu bytes=%llu batches=%lu\n",
			(unsigned long)g_perf.gx_tex_hits, (unsigned long)g_perf.gx_tex_misses,
			(unsigned long)g_perf.gx_tex_resets, (unsigned long)g_perf.gx_tex_loads,
			g_perf.gx_tex_bytes, (unsigned long)g_perf.gx_batches);
		fprintf(f, "gpu: drawdone=%lu dd_skip=%lu drawdone_us=%llu convert_us=%llu present_us=%llu\n",
			(unsigned long)g_perf.gx_drawdone, (unsigned long)g_perf.gx_drawdone_skip, g_perf.gx_drawdone_us,
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
		fprintf(f, "efbloss: upl_calls=%lu upl_done=%lu | pres: total=%lu clear=%lu inflight_skip=%lu | mirror: loads=%lu used=%lu\n",
			(unsigned long)g_perf.upl_calls, (unsigned long)g_perf.upl_done,
			(unsigned long)g_perf.pres_total, (unsigned long)g_perf.pres_clear,
			(unsigned long)g_perf.pres_skipped,
			(unsigned long)g_perf.upl_partial_calls, (unsigned long)g_perf.upl_partial_used);
		fprintf(f, "mirror: set=%lu geom_restarts=%lu first_diff_word=%lu off=%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
			(unsigned long)g_perf.mirror_set, (unsigned long)g_perf.mirror_geom_restarts,
			(unsigned long)g_perf.mirror_diff_word,
			(unsigned long)g_perf.mirror_off[1], (unsigned long)g_perf.mirror_off[2],
			(unsigned long)g_perf.mirror_off[3], (unsigned long)g_perf.mirror_off[4],
			(unsigned long)g_perf.mirror_off[5], (unsigned long)g_perf.mirror_off[6],
			(unsigned long)g_perf.mirror_off[7]);
		fprintf(f, "uplret: dis=%lu skip=%lu rgb24=%lu px1=%lu\n",
			(unsigned long)g_perf.upl_r_dis, (unsigned long)g_perf.upl_r_skip,
			(unsigned long)g_perf.upl_r_rgb24, (unsigned long)g_perf.upl_r_1px);
		{
			int k, j;
			for (k = 0; k < 2 && g_perf.upl_st_n; k++) {
				fprintf(f, "uplst%d:", k);
				for (j = 0; j < 22; j++) fprintf(f, " %ld", (long)g_perf.upl_st[k][j]);
				fprintf(f, "\n");
			}
		}
		{   /* where the PSX CPU was at each vblank, most frequent first: pc=count */
			int k, j, used[16] = { 0 };
			fprintf(f, "pcs:");
			for (k = 0; k < 8; k++) {
				int best = -1;
				for (j = 0; j < 16; j++)
					if (!used[j] && pcs[j].n && (best < 0 || pcs[j].n > pcs[best].n)) best = j;
				if (best < 0) break;
				used[best] = 1;
				fprintf(f, " %08lx=%lu", (unsigned long)pcs[best].pc, (unsigned long)pcs[best].n);
			}
			fprintf(f, "\n");
		}
		fprintf(f, "offsoft: prims=%lu rejected=%lu inside=%lu | pad: startpoll=%lu update=%lu ai_calls=%lu rumble=%lu/%lu ambiguous=%lu\n",
			(unsigned long)g_perf.off_soft_prims, (unsigned long)g_perf.off_soft_rejected,
			(unsigned long)g_perf.off_soft_inside,
			(unsigned long)g_perf.pad_startpoll, (unsigned long)g_perf.pad_update, (unsigned long)g_perf.ai_calls,
			(unsigned long)g_perf.rumble_on, (unsigned long)g_perf.rumble_off,
			(unsigned long)g_perf.ai_ambiguous);
		{
			unsigned k;
			if (g_perf.mtap_full[0] | g_perf.mtap_single[0] | g_perf.mtap_full[1] | g_perf.mtap_single[1])
				for (k = 0; k < 2; k++)
					fprintf(f, "mtap: port=%u full=%lu single=%lu tap0=%lu tap1=%lu addr=%lu/%lu/%lu/%lu empty=%lu short=%lu hle=%lu\n", k + 1,
						(unsigned long)g_perf.mtap_full[k], (unsigned long)g_perf.mtap_single[k],
						(unsigned long)g_perf.mtap_tap[k][0], (unsigned long)g_perf.mtap_tap[k][1],
						(unsigned long)g_perf.mtap_addr[k][0], (unsigned long)g_perf.mtap_addr[k][1],
						(unsigned long)g_perf.mtap_addr[k][2], (unsigned long)g_perf.mtap_addr[k][3],
						(unsigned long)g_perf.mtap_empty[k], (unsigned long)g_perf.mtap_short[k],
						(unsigned long)g_perf.hle_padpoll[k]);
			fprintf(f, "padproto:");
			for (k = 0; k < 16; k++)
				if (g_perf.pad_cmd[k])
					fprintf(f, " %02x=%lu", 0x40 + k, (unsigned long)g_perf.pad_cmd[k]);
			fprintf(f, " | press id=%02x len=%u bytes=", g_perf.pad_press_id, g_perf.pad_press_len);
			for (k = 0; k < 8; k++)
				fprintf(f, "%02x", g_perf.pad_press[k]);
			fprintf(f, "\n");
		}
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
			/* what the script scheduled, so a missing dump can be told from a missing script */
			fprintf(f, "autosched: dump=%u dumped=%u trace=%d atrace=%u\n", autoinput_dump_vbl,
				(unsigned)g_perf.vram_dumped, autoinput_trace_n, autoinput_atrace_vbl);
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
		fprintf(f, "cd: reads=%lu bytes=%llu seq=%lu rand=%lu worst_us=%lu total_us=%llu idle_skips=%lu idle_cycles=%llu gpu_idle_skips=%lu gpu_idle_cycles=%llu\n",
			(unsigned long)g_perf.cd_reads, g_perf.cd_bytes,
			(unsigned long)g_perf.cd_seq, (unsigned long)g_perf.cd_rand,
			(unsigned long)g_perf.io_worst_us, g_perf.io_total_us,
			(unsigned long)g_perf.cd_idle_skips, g_perf.cd_idle_cycles,
			(unsigned long)g_perf.gpu_idle_skips, g_perf.gpu_idle_cycles);
		fprintf(f, "sd: rd=%lu sec=%llu us=%llu worst_us=%lu bg=%lu bg_us=%llu"
			" h=%lu/%lu/%lu/%lu/%lu wr=%lu wsec=%llu wus=%llu wworst_us=%lu\n",
			(unsigned long)g_perf.sd_rd, g_perf.sd_rd_sec, g_perf.sd_rd_us,
			(unsigned long)g_perf.sd_rd_worst_us, (unsigned long)g_perf.sd_rd_bg, g_perf.sd_rd_bg_us,
			(unsigned long)g_perf.sd_rd_hist[0], (unsigned long)g_perf.sd_rd_hist[1],
			(unsigned long)g_perf.sd_rd_hist[2], (unsigned long)g_perf.sd_rd_hist[3],
			(unsigned long)g_perf.sd_rd_hist[4], (unsigned long)g_perf.sd_wr, g_perf.sd_wr_sec,
			g_perf.sd_wr_us, (unsigned long)g_perf.sd_wr_worst_us);
		fprintf(f, "tvmode: calls=%lu w=%lu h=%lu range=%lu-%lu height=%lu double=%lu\n",
			(unsigned long)g_perf.tv_calls, (unsigned long)g_perf.tv_w, (unsigned long)g_perf.tv_h,
			(unsigned long)g_perf.tv_y0, (unsigned long)g_perf.tv_y1,
			(unsigned long)g_perf.tv_height, (unsigned long)g_perf.tv_double);
		{
			unsigned k;
			for (k = 0; k < g_perf.tv_calls && k < 8; k++)
				fprintf(f, "tvlog: %ux%u at vblank %u\n", g_perf.tv_log[k][0],
					g_perf.tv_log[k][1], g_perf.tv_log[k][2]);
		}
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
		{
			const perf_carry_t *c = &g_carry;
			int k;
			fprintf(f, "carry: fc=%lu", (unsigned long)c->frame_counter);
			for (k = 0; k < 3; k++)
				fprintf(f, " rc%d=%lx/%lx/%lu/%lu", k, (unsigned long)c->rcnt[k][0],
					(unsigned long)c->rcnt[k][1], (unsigned long)c->rcnt[k][2], (unsigned long)c->rcnt[k][3]);
			fprintf(f, " spu=%lu/%lu/%04lx/%06lx/%08lx sio=%04lx/%04lx/%lu/%lu pad1=%lu/%c gc=%lx av=%lu"
				" | rcntfire=%lu,%lu,%lu,%lu\n",
				(unsigned long)c->spu[0], (unsigned long)c->spu[1], (unsigned long)c->spu[2],
				(unsigned long)c->spu[3], (unsigned long)c->spu[4],
				(unsigned long)c->sio[0], (unsigned long)c->sio[1], (unsigned long)c->sio[2],
				(unsigned long)c->sio[3],
				(unsigned long)c->pad[0], c->pad[1] ? (char)c->pad[1] : '-',
				(unsigned long)c->pad[2], (unsigned long)c->pad[3],
				(unsigned long)g_perf.rcnt_fire[0], (unsigned long)g_perf.rcnt_fire[1],
				(unsigned long)g_perf.rcnt_fire[2], (unsigned long)g_perf.rcnt_fire[4]);
		}
		#if PERF_PROF_GPUSPLIT
		if (g_perf.gpu_prim_calls || g_perf.gpu_vram_words)
			fprintf(f, "gpusplit: parse_us=%llu vram_us=%llu vram_words=%lu "
				"off_us=%llu off=%lu prim_us=%llu prims=%lu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_parse_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_vram_ticks),
				(unsigned long)g_perf.gpu_vram_words,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_off_ticks),
				(unsigned long)g_perf.gpu_off_calls,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_prim_ticks),
				(unsigned long)g_perf.gpu_prim_calls);
		if (g_perf.gpu_gp1_calls || g_perf.gpu_read_calls)
			fprintf(f, "gpuregs: gp1_us=%llu gp1=%lu read_us=%llu reads=%lu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_gp1_ticks),
				(unsigned long)g_perf.gpu_gp1_calls,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_read_ticks),
				(unsigned long)g_perf.gpu_read_calls);
		if (g_perf.gpu_gp1_calls)
			fprintf(f, "gpuflip: geom_us=%llu present_us=%llu presents=%lu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.flip_geom_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.flip_present_ticks),
				(unsigned long)g_perf.flip_presents);
		if (g_perf.pres_uploads || g_perf.pres_vout_ticks)
			fprintf(f, "gpupres: upload_us=%llu uploads=%lu prep_us=%llu capture_us=%llu vout_us=%llu sync_us=%llu efbcap_us=%llu efbcap=%lu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.pres_upload_ticks),
				(unsigned long)g_perf.pres_uploads,
				(unsigned long long)ticks_to_microsecs(g_perf.pres_prep_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.pres_capture_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.pres_vout_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.pres_sync_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.efb_cap_ticks),
				(unsigned long)g_perf.efb_cap_calls);
		{   /* the ten GP0 commands that took longest, any GPU plugin: cmd=us/calls */
			int c, k, best, used[128] = { 0 };
			fprintf(f, "gpucmd:");
			for (k = 0; k < 10; k++) {
				best = -1;
				for (c = 0; c < 128; c++)
					if (!used[c] && g_perf.gpu_cmd_calls[c] &&
					    (best < 0 || g_perf.gpu_cmd_ticks[c] > g_perf.gpu_cmd_ticks[best]))
						best = c;
				if (best < 0) break;
				used[best] = 1;
				fprintf(f, " %02x=%llu/%lu", best,
					(unsigned long long)ticks_to_microsecs(g_perf.gpu_cmd_ticks[best]),
					(unsigned long)g_perf.gpu_cmd_calls[best]);
			}
			fprintf(f, "\n");
		}
		if (g_perf.gpu_prim_calls) {
			static const char *const cls[8] = {
				"misc", "poly", "line", "rect", "vv", "cv", "vc", "state"
			};
			int c;
			fprintf(f, "gpuprim:");
			for (c = 0; c < 8; c++)
				if (g_perf.gpu_cls_calls[c])
					fprintf(f, " %s=%llu/%lu", cls[c],
						(unsigned long long)ticks_to_microsecs(g_perf.gpu_cls_ticks[c]),
						(unsigned long)g_perf.gpu_cls_calls[c]);
			fprintf(f, "\n");
			fprintf(f, "gpudeep: fill_gx_us=%llu fill_sw_us=%llu fill_mark_us=%llu "
				"vramfin_us=%llu vramfin=%lu inv_us=%llu inv=%lu inv_scan=%lu cwu_us=%llu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_fill_gx_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_fill_sw_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_fill_mark_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_vramfin_ticks),
				(unsigned long)g_perf.gpu_vramfin_calls,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_inv_ticks),
				(unsigned long)g_perf.gpu_inv_calls, (unsigned long)g_perf.gpu_inv_scan,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_cwu_ticks));
#if PERF_PROF_TEXCHECK
			{   /* the staleness oracle: cached texels that no longer match VRAM */
				unsigned k;
				fprintf(f, "texcheck: hits=%lu texels=%llu nonzero=%llu stale=%llu stale_hits=%lu\n",
					(unsigned long)g_perf.tc_hits, (unsigned long long)g_perf.tc_texels,
					(unsigned long long)g_perf.tc_nonzero,
					(unsigned long long)g_perf.tc_bad, (unsigned long)g_perf.tc_bad_hits);
				fprintf(f, "uplcheck: uploads=%lu same_content=%lu redundant=%lu area_px=%llu dirty_px=%llu\n",
					(unsigned long)g_perf.upl_checked, (unsigned long)g_perf.upl_same_content,
					(unsigned long)g_perf.upl_redundant, (unsigned long long)g_perf.upl_area_px,
					(unsigned long long)g_perf.upl_dirty_px);
				for (k = 0; k < g_perf.tc_n; k++)
					fprintf(f, "texcheckr: vbl=%lu mode=%u page=%u texel=%u,%u entry=(%u,%u)-(%u,%u) want=%04x got=%04x\n",
						(unsigned long)g_perf.tc_s[k].vbl, g_perf.tc_s[k].mode, g_perf.tc_s[k].page,
						g_perf.tc_s[k].u, g_perf.tc_s[k].v, g_perf.tc_s[k].x1, g_perf.tc_s[k].y1,
						g_perf.tc_s[k].x2, g_perf.tc_s[k].y2, g_perf.tc_s[k].exp, g_perf.tc_s[k].got);
			}
#endif
			{   /* G5: which source drops cached textures, and how much it drops */
				static const char *nm[8] = { "other", "load", "move", "fill", "prim", "efb", "soft", "7" };
				int k;
				fprintf(f, "texinv: same_loads=%lu changed_loads=%lu changed_px=%llu",
					(unsigned long)g_perf.vload_same, (unsigned long)g_perf.vload_changed,
					(unsigned long long)g_perf.vload_px_changed);
				for (k = 0; k < 8; k++)
					if (g_perf.inv_src_calls[k])
						fprintf(f, " %s=%lu/%lu/%llu", nm[k], (unsigned long)g_perf.inv_src_calls[k],
							(unsigned long)g_perf.inv_src_drop[k], (unsigned long long)g_perf.inv_src_texels[k]);
				fprintf(f, "\n");
				for (k = 0; k < (int)g_perf.inv_rect_n; k++)
					fprintf(f, "texinvr: src=%s rect=%d,%d %dx%d drops=%lu e.g. mode=%u page=%u entry=(%u,%u)-(%u,%u)\n",
						nm[g_perf.inv_rect[k].src & 7], g_perf.inv_rect[k].x, g_perf.inv_rect[k].y,
						g_perf.inv_rect[k].w, g_perf.inv_rect[k].h, (unsigned long)g_perf.inv_rect[k].n,
						g_perf.inv_rect[k].mode, g_perf.inv_rect[k].page,
						g_perf.inv_rect[k].ex1, g_perf.inv_rect[k].ey1, g_perf.inv_rect[k].ex2, g_perf.inv_rect[k].ey2);
			}
			fprintf(f, "gpudraw: ogx_us=%llu draws=%lu common_us=%llu state_us=%llu "
				"states=%lu tex_us=%llu mode_us=%llu\n",
				(unsigned long long)ticks_to_microsecs(g_perf.ogx_draw_ticks),
				(unsigned long)g_perf.ogx_draw_calls,
				(unsigned long long)ticks_to_microsecs(g_perf.ogx_common_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.ogx_state_ticks),
				(unsigned long)g_perf.ogx_state_calls,
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_tex_ticks),
				(unsigned long long)ticks_to_microsecs(g_perf.gpu_mode_ticks));
			fprintf(f, "gxcache: sets=%lu skips=%lu\n",
				(unsigned long)g_perf.ogx_state_sets, (unsigned long)g_perf.ogx_state_skips);
		}
		#endif
		#if PERF_PROF_GTE
		{
			/* Only the 22 defined functions can be reached; anything else would be an
			 * invalid CP2 instruction, which lightrec.c drops. */
			static const char *const gte_names[64] = {
				[0x01] = "RTPS",  [0x06] = "NCLIP", [0x0c] = "OP",   [0x10] = "DPCS",
				[0x11] = "INTPL", [0x12] = "MVMVA", [0x13] = "NCDS", [0x14] = "CDP",
				[0x16] = "NCDT",  [0x1b] = "NCCS",  [0x1c] = "CC",   [0x1e] = "NCS",
				[0x20] = "NCT",   [0x28] = "SQR",   [0x29] = "DCPL", [0x2a] = "DPCT",
				[0x2d] = "AVSZ3", [0x2e] = "AVSZ4", [0x30] = "RTPT", [0x3d] = "GPF",
				[0x3e] = "GPL",   [0x3f] = "NCCT",
			};
			unsigned long total = 0;
			int i, n;
			for (i = 0; i < 64; i++)
				total += g_perf.gte_calls[i];
			if (total) {
				fprintf(f, "gte: calls=%lu us=%llu |", total,
					(unsigned long long)ticks_to_microsecs(g_perf.gte_ticks));
				/* Biggest first, by selection: 64 entries, printed once. */
				for (n = 0; n < 8; n++) {
					int best = -1;
					for (i = 0; i < 64; i++)
						if (g_perf.gte_calls[i] &&
						    (best < 0 || g_perf.gte_calls[i] > g_perf.gte_calls[best]))
							best = i;
					if (best < 0)
						break;
					fprintf(f, " %s=%lu", gte_names[best] ? gte_names[best] : "?",
						(unsigned long)g_perf.gte_calls[best]);
					g_perf.gte_calls[best] = 0;   /* the report is the last reader */
				}
				fprintf(f, "\n");
			}
		}
		#endif
		#if PERF_PROF_SPU
		if (g_perf.spu_chans)
			fprintf(f, "spustage: chans=%lu adpcm_us=%llu adsr_us=%llu mix_us=%llu rvb_us=%llu\n",
				(unsigned long)g_perf.spu_chans, g_perf.spu_adpcm_us,
				g_perf.spu_adsr_us, g_perf.spu_mix_us, g_perf.spu_rvb_us);
		#endif
		#if PERF_PROF_PMC
		perf_pmc_read();
		fprintf(f, "pmc: mmcr0=%08x mmcr1=%08x pmc1=%llu pmc2=%llu pmc3=%llu pmc4=%llu\n",
			(unsigned)PMC_MMCR0, (unsigned)PMC_MMCR1,
			(unsigned long long)g_perf.pmc[0], (unsigned long long)g_perf.pmc[1],
			(unsigned long long)g_perf.pmc[2], (unsigned long long)g_perf.pmc[3]);
		#endif
		lc_report(f);
		fprintf(f, "menu: frames=%lu menu_us=%llu strings=%lu glyphs=%lu texloads=%lu\n",
			(unsigned long)g_perf.menu_frames, g_perf.menu_us,
			(unsigned long)g_perf.menu_strings, (unsigned long)g_perf.menu_glyphs,
			(unsigned long)g_perf.menu_texloads);
		fclose(f);
	}
	if (g_perf.aev_n)
		perf_audio_flush();
	perf_pad_flush();
	perf_vram_flush();
	perf_tty_flush();

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

/* outside PERF_PROF: the chain marker and lab.log use it in every build */
#include <stdio.h>
#include <time.h>

/* the wall-clock date and time, "2026-09-28 19:50:01": the Wii's clock (RTC) on a Wii, the
 * PC's in Dolphin. In every log, so a log says when its run was. */
void perf_datetime(char *buf, int size)
{
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	if (t)
		strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
	else
		snprintf(buf, size, "unknown");
}
