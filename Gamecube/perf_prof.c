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
	return ticks_to_microsecs(gettick());
}

void perf_reset(void)
{
	memset(&g_perf, 0, sizeof(g_perf));
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
			g_perf.cpu_us, (unsigned long)g_perf.jit_resets_full,
			(unsigned long)g_perf.jit_resets_partial,
			(unsigned long)g_perf.jit_interp_fallbacks,
			(unsigned long)g_perf.jit_hle, (unsigned long)g_perf.jit_exceptions);
		fprintf(f, "jitmem: code=%lu peak=%lu meta=%lu oldrec_slices=%lu oldrec_resets=%lu oldrec_peak_kb=%lu\n",
			(unsigned long)g_perf.jit_code_bytes, (unsigned long)g_perf.jit_code_peak,
			(unsigned long)g_perf.jit_meta_bytes,
			(unsigned long)g_perf.oldrec_slices, (unsigned long)g_perf.oldrec_resets,
			(unsigned long)g_perf.oldrec_peak_kb);
		{
			/* Top GTE ops by count (func & 0x3f index into adapter table) */
			unsigned i, n = 0;
			uint32_t top_v[8] = {0};
			unsigned top_i[8] = {0};
			for (i = 0; i < 64; i++) {
				unsigned k;
				for (k = 0; k < 8; k++) {
					if (g_perf.gte_counts[i] > top_v[k]) {
						unsigned m;
						for (m = 7; m > k; m--) {
							top_v[m] = top_v[m-1];
							top_i[m] = top_i[m-1];
						}
						top_v[k] = g_perf.gte_counts[i];
						top_i[k] = i;
						break;
					}
				}
			}
			fprintf(f, "gte top:");
			for (n = 0; n < 8 && top_v[n]; n++)
				fprintf(f, " %02x=%lu", top_i[n], (unsigned long)top_v[n]);
			fprintf(f, "\n");
		}
		fprintf(f, "ram: mem1_free_kb=%lu mem2=%lu/%luKB peak=%luKB fails=%lu null_read=%lu\n",
			(unsigned long)mem1_kb, (unsigned long)m2used, (unsigned long)m2tot,
			(unsigned long)g_perf.mem2_peak_kb, (unsigned long)g_perf.mem2_alloc_fails,
			(unsigned long)g_perf.mem_null_read);
		fprintf(f, "gpu: tex_hit=%lu miss=%lu resets=%lu evicts=%lu loads=%lu bytes=%llu batches=%lu\n",
			(unsigned long)g_perf.gx_tex_hits, (unsigned long)g_perf.gx_tex_misses,
			(unsigned long)g_perf.gx_tex_resets, (unsigned long)g_perf.gx_tex_evicts,
			(unsigned long)g_perf.gx_tex_loads,
			g_perf.gx_tex_bytes, (unsigned long)g_perf.gx_batches);
		fprintf(f, "gpu: drawdone=%lu drawdone_us=%llu present_us=%llu\n",
			(unsigned long)g_perf.gx_drawdone, g_perf.gx_drawdone_us,
			g_perf.present_us);
		fprintf(f, "cd: reads=%lu bytes=%llu seq=%lu rand=%lu worst_us=%lu total_us=%llu win=%lu\n",
			(unsigned long)g_perf.cd_reads, g_perf.cd_bytes,
			(unsigned long)g_perf.cd_seq, (unsigned long)g_perf.cd_rand,
			(unsigned long)g_perf.io_worst_us, g_perf.io_total_us,
			(unsigned long)g_perf.cd_win);
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
		g_perf.cpu_us / 1000,
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
