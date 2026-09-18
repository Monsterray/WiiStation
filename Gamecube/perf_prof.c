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

void perf_present_tick(unsigned long long present_us)
{
	g_perf.present_frames++;
	g_perf.present_us += present_us;
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
		fprintf(f, "ram: mem1_free_kb=%lu mem2=%lu/%luKB peak=%luKB fails=%lu null_read=%lu\n",
			(unsigned long)mem1_kb, (unsigned long)m2used, (unsigned long)m2tot,
			(unsigned long)g_perf.mem2_peak_kb, (unsigned long)g_perf.mem2_alloc_fails,
			(unsigned long)g_perf.mem_null_read);
		fprintf(f, "gpu: tex_hit=%lu miss=%lu resets=%lu loads=%lu bytes=%llu batches=%lu\n",
			(unsigned long)g_perf.gx_tex_hits, (unsigned long)g_perf.gx_tex_misses,
			(unsigned long)g_perf.gx_tex_resets, (unsigned long)g_perf.gx_tex_loads,
			g_perf.gx_tex_bytes, (unsigned long)g_perf.gx_batches);
		fprintf(f, "gpu: drawdone=%lu drawdone_us=%llu convert_us=%llu present_us=%llu\n",
			(unsigned long)g_perf.gx_drawdone, g_perf.gx_drawdone_us,
			g_perf.gx_convert_us, g_perf.present_us);
		fprintf(f, "ogx: gc=%lu sub_new=%lu sub_hit=%lu skip=%lu unaligned=%lu\n",
			(unsigned long)g_perf.ogx_gc, (unsigned long)g_perf.ogx_sub_new,
			(unsigned long)g_perf.ogx_sub_hit, (unsigned long)g_perf.ogx_skip,
			(unsigned long)g_perf.ogx_unaligned);
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
