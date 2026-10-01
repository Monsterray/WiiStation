/* hprof.c - a sampling profiler on the Wii itself (PROBES=hprof).
 *
 * PMC1 counts processor cycles. When its top bit sets, Broadway takes the
 * performance-monitor exception (0xF00); hprof_entry.s calls hprof_sample() with the
 * interrupted PC, which counts it and loads PMC1 for the next period. Dolphin emulates the
 * same exception (PowerPC.cpp UpdatePerformanceMonitor), so the profiler runs there too,
 * but there it counts Dolphin's cycle estimate, not cache misses.
 *
 * Text PCs go in a histogram of 32-byte buckets; PCs in Lightrec's code buffer and anywhere
 * else are only counted. Each chained game writes hprof_NN.bin at its end (perf_vtl_flush);
 * scripts/hprof_view.py adds the buckets up per function with the ELF's symbols.
 *
 * Samples are taken only while MSR[EE] is on: time in exception handlers and other
 * code with interrupts off is charged to the first instruction after it.
 */
#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <ogc/machine/processor.h>
#include "perf_prof.h"
#include "../mem2_manager.h"
#include "MEM2.h"   /* LIGHTREC_CODE_SIZE */

#if PERF_PROF_HPROF
#if PERF_PROF_PMC
#error "PERF_PROF_HPROF and PERF_PROF_PMC both use PMC1"
#endif

#define HPROF_PERIOD 72900u                 /* cycles: 100 us at 729 MHz */
#define HPROF_BASE 0x80000000u
#define HPROF_SPAN (4u << 20)               /* .text is 0x80004000-0x8021b700 today */
#define HPROF_SHIFT 5                       /* 32-byte buckets */
#define HPROF_N (HPROF_SPAN >> HPROF_SHIFT)
/* MMCR0: ENINT (hardware clears it at each exception), PMC1CE, PMC1SELECT = 1 (cycles) */
#define HPROF_MMCR0 (0x04000000u | 0x00008000u | (1u << 6))

extern char code_buffer_mem1[];             /* lightrec.c */
extern void hprof_entry(void);              /* hprof_entry.s */
void __exception_sethandler(u32 nExcept, void (*pHndl)(frame_context *));   /* libogc, no header */

/* libgcc's 64-bit divide helpers: leaves, so LR at a sample in them is the caller.
 * Their samples also go in a second histogram by LR (hprof_view.py --callers). */
extern char __divdi3[], __moddi3[], __udivdi3[], __umoddi3[];

static u32 *hist, *lr_hist;
static u32 hp_total, hp_jit, hp_other, watch_lo, watch_hi;

void hprof_sample(u32 pc, u32 lr)
{
	u32 o = pc - HPROF_BASE;
	hp_total++;
	if (o < HPROF_SPAN) {
		hist[o >> HPROF_SHIFT]++;
		if (pc - watch_lo < watch_hi - watch_lo && lr - HPROF_BASE < HPROF_SPAN)
			lr_hist[(lr - HPROF_BASE) >> HPROF_SHIFT]++;
	}
	else if (pc - (u32)code_buffer_mem1 < LIGHTREC_CODE_SIZE)
		hp_jit++;
	else
		hp_other++;
	mtpmc1(0x80000000u - HPROF_PERIOD);
	mtmmcr0(HPROF_MMCR0);
}

/* perf_reset(): an empty histogram, then sampling on */
void hprof_start(void)
{
	u32 a[4] = { (u32)__divdi3, (u32)__moddi3, (u32)__udivdi3, (u32)__umoddi3 };
	int i;
	mtmmcr0(0);
	if (!hist && !(hist = (u32 *)_mem2_malloc(2 * HPROF_N * sizeof(u32))))
		return;
	lr_hist = hist + HPROF_N;
	memset(hist, 0, 2 * HPROF_N * sizeof(u32));
	hp_total = hp_jit = hp_other = 0;
	watch_lo = watch_hi = a[0];
	for (i = 1; i < 4; i++) {
		if (a[i] < watch_lo) watch_lo = a[i];
		if (a[i] > watch_hi) watch_hi = a[i];
	}
	watch_hi += 0x400;   /* the last helper's size, about 0x390 */
	__exception_sethandler(EX_PERF, (void (*)(frame_context *))hprof_entry);
	mtmmcr1(0);
	mtpmc1(0x80000000u - HPROF_PERIOD);
	mtmmcr0(HPROF_MMCR0);
}

/* the perf.log "hprof:" line */
void hprof_report(FILE *f)
{
	fprintf(f, "hprof: samples=%u text=%u jit=%u other=%u period=%u\n", hp_total,
		hp_total - hp_jit - hp_other, hp_jit, hp_other, HPROF_PERIOD);
}

/* A chained game's end: header "HPR2", base, shift, bucket count, period, total, jit, other;
 * then the PC buckets, then the LR buckets of the divide helpers' samples. */
void hprof_flush(int game)
{
	char path[40];
	FILE *f;
	u32 hdr[8] = { 0x48505232, HPROF_BASE, HPROF_SHIFT, HPROF_N, HPROF_PERIOD, hp_total, hp_jit, hp_other };
	if (!hist)
		return;
	snprintf(path, sizeof path, "sd:/wiisxrx/hprof_%02d.bin", game);
	if ((f = fopen(path, "wb"))) {
		fwrite(hdr, sizeof hdr, 1, f);
		fwrite(hist, sizeof(u32), 2 * HPROF_N, f);
		fclose(f);
	}
}
#endif
