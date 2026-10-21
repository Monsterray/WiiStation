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
#ifndef MSR_PM
#define MSR_PM 0x00000004       /* the 750 performance-monitor mark bit (ogc/machine/asm.h, assembly only) */
#endif
#include "ogc_compat.h"            /* WS_LIBOGC3 */
#if WS_LIBOGC3
#include <tuxedo/ppc/exception.h>   /* PPCExcptCurPanicFn, PPCExcptSetHandler, PPC_EXCPT_PM */
#include <tuxedo/thread.h>          /* KThread, KTHR_MIN_PRIO */
#else
#include <ogc/lwp_objmgr.h>
#include <ogc/lwp_threads.h>
#endif
#include "perf_prof.h"
#include "../mem2_manager.h"
#include "MEM2.h"   /* LIGHTREC_CODE_SIZE */

/* "hprof <from> <to>" input lines (PadWiiSX.c): sample only inside those vblank windows,
 * e.g. the slow stretches vtl_view.py lists; none = the whole game. Under Dolphin the
 * profile is of instructions (its cycle estimate), so a window's profile is repeatable.
 * Defined in every build so the input parser links; only an hprof build reads them. */
unsigned hprof_win_lo[8], hprof_win_hi[8];
int hprof_win_n;

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
/* + FCM0 (0x08000000): counters frozen while MSR[PM] = 0. hprof_start sets MSR[PM] in every
 * thread but the idle one, whose loop keeps writing its boot-time MSR (PM 0) with POW. A real
 * Wii crashed when the counter overflowed in that doze: a second performance-monitor
 * exception at 0x00000F00, or a program exception at idle's mtmsr (2026-10-01). Idle samples
 * were none anyway: the doze stops the clock. */
#define HPROF_MMCR0 (0x04000000u | 0x08000000u | 0x00008000u | (1u << 6))

extern char code_buffer_mem1[];             /* lightrec.c */
#if !WS_LIBOGC3
extern void hprof_entry(void);              /* hprof_entry.s */
void __exception_sethandler(u32 nExcept, void (*pHndl)(frame_context *));   /* libogc, no header */
#endif

/* libgcc's 64-bit divide helpers: leaves, so LR at a sample in them is the caller.
 * Their samples also go in a second histogram by LR (hprof_view.py --callers). */
extern char __divdi3[], __moddi3[], __udivdi3[], __umoddi3[];

static u32 *hist, *lr_hist;
static u32 hp_total, hp_jit, hp_other, watch_lo, watch_hi;

extern unsigned int frame_counter;

/* Called from assembly only (hprof_entry.s, hprof_pm_entry), which LTO cannot see. */
__attribute__((used, externally_visible)) void hprof_sample(u32 pc, u32 lr)
{
	u32 o = pc - HPROF_BASE;
	if (hprof_win_n) {
		int w, in = 0;
		for (w = 0; w < hprof_win_n; w++)
			in |= frame_counter >= hprof_win_lo[w] && frame_counter < hprof_win_hi[w];
		if (!in)
			goto rearm;
	}
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
rearm:
	/* Re-arm in order: interrupt off, the counter below its top bit, then the interrupt on,
	 * each SPR write finished (isync) before the next. Written as PMC1 then MMCR0 with no
	 * isync, a real Wii took a second performance-monitor exception at 0x00000F00 itself
	 * (SRR0 = 0xF00, MSR[RI] clear: an unrecoverable crash, 2026-10-01); Dolphin never did. */
	mtmmcr0(0);
	__asm__ volatile ("isync");
	mtpmc1(0x80000000u - HPROF_PERIOD);
	__asm__ volatile ("isync");
	mtmmcr0(HPROF_MMCR0);
	__asm__ volatile ("isync");
}

#if WS_LIBOGC3
/* libogc v3 (tuxedo) has no frame_context, no table of assembly entries for EX_PERF, and no
 * lwp object table, so the sampling is wired differently here (hprof_entry.s is libogc2 only):
 *  - The 0xF00 vector's handler is hprof_pm_entry below, not PPCExcptDefaultHandler. The
 *    default handler is for crashes: its "force spilling FP regs" stfiwx takes a nested FPU
 *    exception that saves the FPU and sets the FPU owner (SPRG3) to NULL. The interrupted
 *    thread then runs on with live FP registers that no one owns; at its next IRQ return
 *    MSR[FP] goes, and its next FP instruction reloads the copy from the sample time. At
 *    10000 samples a second every thread's FP registers kept rolling back: under Dolphin the
 *    float loop in GX_GetYScaleFactor (RestoreDispCopyInfo) never ended, the HBC agent's hang
 *    watchdog fired 60 guest seconds later, and its __reload() found no HBC stub to return to
 *    (a 28 GB dolphin.log of MMU warnings, 2026-10-20). hprof_pm_entry saves the volatile
 *    GPRs and the special registers on the exception stack, calls hprof_sample(pc, lr) and
 *    returns; it touches no FP register and no FPU owner. hprof_sample is integer code.
 *  - A PM exception taken with MSR[RI] clear (a nested one) still goes to the default
 *    handler, which calls PPCExcptCurPanicFn: hprof_panic samples it there (counted as
 *    "nested" in the hprof: line) and chains every other exception to the previous hook (the
 *    HBC agent's, or vm.c's).
 *  - MSR[PM] goes into each thread's saved context. tuxedo keeps its thread list through
 *    KThread::next from s_firstThread (thread.c) and exposes no listing, so this reads that
 *    libogc-private symbol. The idle thread is the one at KTHR_MIN_PRIO+1, above every app
 *    thread's priority. The executing thread gets the bit in its live MSR instead.
 * The hook is installed once: a second install would make it chain to itself. */
static PPCExcptPanicFn hp_prev_panic;
static int hp_hooked;
static u32 hp_nested;
extern KThread *s_firstThread;              /* tuxedo/thread.c */
void hprof_pm_entry(void);

/* PPCExcptEntryGeneral comes here with translation on, EE and RI off, r11 and r12 in SPRG0 and
 * SPRG1, and the interrupted PC, MSR and CR in __ppc_excpt_buf. Only its non-nested path comes
 * here, so the exception stack is free (everything else on it runs with EE off). The frame
 * keeps all it restores, so a fault inside hprof_sample (nested: the panic hook) cannot leave
 * the buffer or the SPRGs wrong for the rfi. r2 and r13 are reloaded: Lightrec's code may
 * hold anything in them. */
#define HP_STR_(x) #x
#define HP_STR(x) HP_STR_(x)
__asm__(
	"	.section .text.hprof_pm_entry,\"ax\",@progbits\n"
	"	.globl hprof_pm_entry\n"
	"	.type hprof_pm_entry,@function\n"
	"hprof_pm_entry:\n"
	"	mr      12,1\n"
	"	lis     1,__ppc_excpt_sp@ha\n"
	"	lwz     1,__ppc_excpt_sp@l(1)\n"
	"	stwu    12,-96(1)\n"               /* 0(1): back chain = the interrupted sp */
	"	stw     0,8(1)\n"
	"	stw     2,12(1)\n"
	"	stw     3,16(1)\n"
	"	stw     4,20(1)\n"
	"	stw     5,24(1)\n"
	"	stw     6,28(1)\n"
	"	stw     7,32(1)\n"
	"	stw     8,36(1)\n"
	"	stw     9,40(1)\n"
	"	stw     10,44(1)\n"
	"	stw     13,48(1)\n"
	"	mflr    3\n"
	"	stw     3,52(1)\n"
	"	mfctr   3\n"
	"	stw     3,56(1)\n"
	"	mfxer   3\n"
	"	stw     3,60(1)\n"
	"	lis     10,__ppc_excpt_buf@ha\n"
	"	addi    10,10,__ppc_excpt_buf@l\n"
	"	lwz     3," HP_STR(PPC_CONTEXT_MSR) "(10)\n"
	"	stw     3,68(1)\n"
	"	lwz     3," HP_STR(PPC_CONTEXT_CR) "(10)\n"
	"	stw     3,72(1)\n"
	"	mfsprg0 3\n"
	"	stw     3,76(1)\n"
	"	mfsprg1 3\n"
	"	stw     3,80(1)\n"
	"	lwz     3," HP_STR(PPC_CONTEXT_PC) "(10)\n"
	"	stw     3,64(1)\n"
	"	lwz     4,52(1)\n"                 /* hprof_sample(pc, lr) */
	"	lis     2,_SDA2_BASE_@h\n"
	"	ori     2,2,_SDA2_BASE_@l\n"
	"	lis     13,_SDA_BASE_@h\n"
	"	ori     13,13,_SDA_BASE_@l\n"
	"	bl      hprof_sample\n"
	"	lwz     3,52(1)\n"
	"	mtlr    3\n"
	"	lwz     3,56(1)\n"
	"	mtctr   3\n"
	"	lwz     3,60(1)\n"
	"	mtxer   3\n"
	"	lwz     3,72(1)\n"
	"	mtcr    3\n"
	"	lwz     3,64(1)\n"
	"	mtsrr0  3\n"
	"	lwz     3,68(1)\n"
	"	mtsrr1  3\n"
	"	lwz     0,8(1)\n"
	"	lwz     2,12(1)\n"
	"	lwz     3,16(1)\n"
	"	lwz     4,20(1)\n"
	"	lwz     5,24(1)\n"
	"	lwz     6,28(1)\n"
	"	lwz     7,32(1)\n"
	"	lwz     8,36(1)\n"
	"	lwz     9,40(1)\n"
	"	lwz     10,44(1)\n"
	"	lwz     13,48(1)\n"
	"	lwz     11,76(1)\n"
	"	lwz     12,80(1)\n"
	"	lwz     1,0(1)\n"
	"	rfi\n"
	"	.size hprof_pm_entry,.-hprof_pm_entry\n"
	"	.previous\n");

static void hprof_panic(unsigned exid, PPCContext *ctx)
{
	if (exid == PPC_EXCPT_PM) {
		hp_nested++;
		hprof_sample(ctx->pc, ctx->lr);
		return;
	}
	if (hp_prev_panic)
		hp_prev_panic(exid, ctx);
}

static void hprof_arm_v3(void)
{
	KThread *self = KThreadGetSelf(), *t;

	if (!hp_hooked) {
		hp_prev_panic = PPCExcptCurPanicFn;
		PPCExcptCurPanicFn = hprof_panic;
		hp_hooked = 1;
	}
	PPCExcptSetHandler(PPC_EXCPT_PM, hprof_pm_entry);
	for (t = s_firstThread; t; t = t->next)
		if (t != self && t->prio != KTHR_MIN_PRIO + 1)
			t->ctx.msr |= MSR_PM;
	mtmsr(mfmsr() | MSR_PM);
}
#endif

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
#if WS_LIBOGC3
	hprof_arm_v3();
#else
	__exception_sethandler(EX_PERF, (void (*)(frame_context *))hprof_entry);
	{   /* MSR[PM] = 1: count in every thread but idle (HPROF_MMCR0 FCM0) */
		extern lwp_objinfo _lwp_thr_objects;
		u32 k;
		for (k = 0; k < _lwp_thr_objects.max_nodes; k++) {
			lwp_cntrl *t = (lwp_cntrl *)_lwp_thr_objects.local_table[k];
			if (t && t != _thr_idle && t != _thr_executing)
				t->context.MSR |= MSR_PM;
		}
		mtmsr(mfmsr() | MSR_PM);
	}
#endif
	mtmmcr1(0);
	mtpmc1(0x80000000u - HPROF_PERIOD);
	__asm__ volatile ("isync");
	mtmmcr0(HPROF_MMCR0);
	__asm__ volatile ("isync");
}

/* the perf.log "hprof:" line */
void hprof_report(FILE *f)
{
	fprintf(f, "hprof: samples=%u text=%u jit=%u other=%u period=%u", hp_total,
		hp_total - hp_jit - hp_other, hp_jit, hp_other, HPROF_PERIOD);
#if WS_LIBOGC3
	fprintf(f, " nested=%u", hp_nested);   /* through the default handler, which resets the FPU owner */
#endif
	fputc('\n', f);
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
	snprintf(path, sizeof path, "sd:/wiistation/hprof_%02d.bin", game);
	if ((f = fopen(path, "wb"))) {
		fwrite(hdr, sizeof hdr, 1, f);
		fwrite(hist, sizeof(u32), 2 * HPROF_N, f);
		fclose(f);
	}
}
#endif
