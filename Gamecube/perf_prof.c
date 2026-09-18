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

void perf_reset(void)
{
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
void perf_prim_trace(unsigned cmd, unsigned flags, unsigned abr, unsigned color, int x0, int y0, int x1, int y1)
{
	unsigned idx;
	int trigger = ((flags & 3) == 1 && cmd >= 0x20 && cmd < 0x80 && (x1 - x0) >= 200 && (y1 - y0) >= 150) ||
	              (PERF_PT_ARM_PRESENT && !g_perf.pt_armed && g_perf.present_frames >= PERF_PT_ARM_PRESENT);
	if (!g_perf.pt_armed) {
		if (!trigger) return;
	} else if (g_perf.present_frames - g_perf.pt_start_present >= 8) {
		/* episode complete: keep it until the next overlay starts a new one, so
		 * the report always shows the most recent episode (e.g. the pause
		 * screen, not the boot fade) */
		if (!trigger) return;
		g_perf.pt_n = 0;
		memset(g_perf.pt_prims, 0, sizeof(g_perf.pt_prims));
		memset(g_perf.pt_semi, 0, sizeof(g_perf.pt_semi));
		memset(g_perf.pt_fills, 0, sizeof(g_perf.pt_fills));
		g_perf.pt_armed = 0;
	}
	if (!g_perf.pt_armed) { g_perf.pt_armed = 1; g_perf.pt_start_present = g_perf.present_frames; }
	idx = g_perf.present_frames - g_perf.pt_start_present;
	if (idx < 8) {
		if (cmd == 0x02) g_perf.pt_fills[idx]++;
		else if (cmd != 0xF5 && cmd < 0xE0) { g_perf.pt_prims[idx]++; if (flags & 1) g_perf.pt_semi[idx]++; }
	}
	if (g_perf.pt_n < 96) {
		unsigned k = g_perf.pt_n++;
		g_perf.pt[k].present = (uint16_t)idx; g_perf.pt[k].cmd = (uint8_t)cmd; g_perf.pt[k].flags = (uint8_t)flags;
		g_perf.pt[k].abr = (uint8_t)abr; g_perf.pt[k].color = color & 0xffffff;
		g_perf.pt[k].x0 = (int16_t)x0; g_perf.pt[k].y0 = (int16_t)y0; g_perf.pt[k].x1 = (int16_t)x1; g_perf.pt[k].y1 = (int16_t)y1;
	}
}

void perf_present_tick(unsigned long long present_us)
{
	g_perf.present_frames++;
	g_perf.present_us += present_us;
	if (!g_perf.vram_dumped && g_perf.vblanks >= PERF_VRAM_DUMP_VBLANK) {
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
		fprintf(f, "inside: limit_us=%llu limit=%lu target=%lu spu_us=%llu spu=%lu hw_us=%llu hw=%lu hw_gpu_us=%llu hw_gpu=%lu\n",
			(unsigned long long)ticks_to_microsecs(g_perf.limit_ticks),
			(unsigned long)g_perf.limit_calls,
			(unsigned long)g_perf.limit_target,
			(unsigned long long)ticks_to_microsecs(g_perf.spu_ticks),
			(unsigned long)g_perf.spu_calls,
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
		if (g_perf.pt_armed) {
			unsigned k;
			fprintf(f, "ptrace: armed_at_present=%lu prims/semi/fills per present:", (unsigned long)g_perf.pt_start_present);
			/* cmd legend: 02 fill, E3/E4 draw area start/end (x,y), E5 draw offset, F5 display address (x,y),
			 * F6 horizontal range (x1,x2), F7 vertical range (y1,y2), F8 display mode (raw in col) */
			for (k = 0; k < 8; k++) fprintf(f, " %u/%u/%u", g_perf.pt_prims[k], g_perf.pt_semi[k], g_perf.pt_fills[k]);
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
		fprintf(f, "chd: hit=%lu miss=%lu err=%lu chd_us=%llu\n",
			(unsigned long)g_perf.chd_hit, (unsigned long)g_perf.chd_miss,
			(unsigned long)g_perf.chd_err, g_perf.chd_us);
		fprintf(f, "menu: frames=%lu menu_us=%llu strings=%lu glyphs=%lu texloads=%lu\n",
			(unsigned long)g_perf.menu_frames, g_perf.menu_us,
			(unsigned long)g_perf.menu_strings, (unsigned long)g_perf.menu_glyphs,
			(unsigned long)g_perf.menu_texloads);
		fclose(f);
	}

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
