/* ws_crash.c - reports for the ways WiiStation stops that are not an exception.
 *
 * The HBC agent (deps/hbc_agent) records an exception in its crash block at HBC_CRASH_ADDR,
 * and HBC reports it after the reload (`hbc.py crash`). Three ways WiiStation stops are no
 * exception, so before 2026-10-01 they left no report at all:
 *   - a hang: the emulation and menu thread stops making progress;
 *   - a guest segfault, which Lightrec turned into exit(1);
 *   - no sound driver, which was abort().
 * For these WiiStation writes the same block itself, with its own exception codes
 * (ws_crash.h), then returns to HBC.
 *
 * Fields with a WiiStation code: pc, lr, sp and frames are the call chain of the thread that
 * stopped (for a hang, the main thread's context, saved when the watchdog preempted it);
 * dar is the guest PC as last synced (psxRegs.pc); dsisr is the PS1 vblank count;
 * uptime_ms counts from ws_watchdog_start(). `hbc.py crash --elf WiiSXRX_*.elf` resolves the addresses.
 */
#include <gccore.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_threads.h>
#include <ogc/lwp_watchdog.h>
#define HBC_AGENT_LAYOUT_ONLY
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "../psxcommon.h"
#include "../r3000a.h"
#include "ws_crash.h"

#define WS_HANG_S 60   /* a game load, a big directory or an SMB listing takes seconds, not this */

extern u32 frame_counter;          /* psxcounters.c: +1 per PS1 vblank */
extern void __reload(void);        /* libogc2 system.c: to HBC's reload stub, no clean-up */
extern int lab_active(void);       /* lab_net.c */
extern void lab_report(void);

volatile u32 ws_progress;
static u64 ws_t0;   /* the time base when the watchdog started: it does not start at 0 at boot */
volatile int ws_watchdog_hold;

static int ram_word(u32 a)
{
	return !(a & 3) && ((a >= 0x80000000 && a < 0x81800000) || (a >= 0x90000000 && a < 0x94000000));
}

static void ws_crash_record(u32 code, u32 pc, u32 lr, u32 sp)
{
	hbc_crash_block *b = (hbc_crash_block *)HBC_CRASH_ADDR;
	u32 i;

	memset(b, 0, sizeof *b);
	b->magic = HBC_CRASH_MAGIC;
	b->version = HBC_CRASH_VERSION;
	b->exception = code;
	b->pc = pc;
	b->lr = lr;
	b->sp = sp;
	b->dar = psxRegs.pc;
	b->dsisr = frame_counter;
	b->uptime_ms = (u32)ticks_to_millisecs(gettime() - ws_t0);
	for (i = 0; i < HBC_CRASH_FRAMES && ram_word(sp); i++) {
		u32 next = *(u32 *)sp;
		if (!next)
			break;
		next |= 0x80000000;   /* an exception frame's back chain is a physical address */
		if (!ram_word(next) || next <= sp)
			break;
		b->frames[i] = *(u32 *)(next + 4);
		sp = next;
	}
	strcpy(b->app, "WiiStation");
	b->check = hbc_crash_check(b);
	DCFlushRange(b, sizeof *b);
}

/* Every crash screen goes through libogc's c_default_exceptionhandler(), which the build wraps
 * (-Wl,--wrap,c_default_exceptionhandler in Makefile_Wii*). The agent's hook sees only
 * exceptions that come through libogc's handler table. libogc's vector code sends an exception
 * taken with MSR[RI] clear straight to default_exceptionhandler instead, so the agent never
 * recorded those and HBC reported nothing (the hprof runs of 2026-10-01). This records them,
 * with the agent's vector numbering, unless the agent already recorded this same crash. */
void __real_c_default_exceptionhandler(frame_context *ctx);

void __wrap_c_default_exceptionhandler(frame_context *ctx)
{
	static const u8 vector[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0c, 0x0d, 0x0f, 0x13, 0x14, 0x17 };
	hbc_crash_block *b = (hbc_crash_block *)HBC_CRASH_ADDR;
	u32 n = ctx->EXCPT_Number;

	if (!(b->magic == HBC_CRASH_MAGIC && b->check == hbc_crash_check(b) && b->pc == ctx->SRR0)) {
		ws_crash_record(n < sizeof vector ? vector[n] : n, ctx->SRR0, ctx->LR, ctx->GPR[1]);
		b->msr = ctx->SRR1;
		b->cr = ctx->CR;
		b->ctr = ctx->CTR;
		b->dar = mfspr(19);    /* DAR and DSISR, as the agent records them */
		b->dsisr = mfspr(18);
		b->check = hbc_crash_check(b);
		DCFlushRange(b, sizeof *b);
	}
	__real_c_default_exceptionhandler(ctx);
}

void ws_fatal(u32 code)
{
	u32 sp;

	__asm__ volatile ("mr %0,1" : "=r" (sp));
	ws_crash_record(code, (u32)__builtin_return_address(0), (u32)__builtin_return_address(0), sp);
	if (lab_active())
		lab_report();   /* the results so far; wii_lab's job then asks HBC for this report */
	exit(0);
}

/* The watchdog: the highest-priority thread, so a spinning emulation thread cannot keep it
 * from running. It cannot use exit(): the stuck thread may hold the SD card's lock. */
static lwp_cntrl *ws_main;
static lwp_t wd_thread = LWP_THREAD_NULL;
static u8 wd_stack[8192] ATTRIBUTE_ALIGN(32);

static void *wd_main(void *arg)
{
	u32 v = frame_counter, m = ws_progress, still = 0, level;

	(void)arg;
	for (;;) {
		usleep(1000000);
		if (frame_counter != v || ws_progress != m || ws_watchdog_hold > 0) {
			v = frame_counter;
			m = ws_progress;
			still = 0;
			continue;
		}
		if (++still < WS_HANG_S)
			continue;
		_CPU_ISR_Disable(level);
		ws_crash_record(WS_CRASH_HANG, ws_main->context.LR, ws_main->context.LR,
				ws_main->context.GPR[1]);
		__reload();
	}
	return NULL;
}

void ws_watchdog_start(void)
{
	if (wd_thread != LWP_THREAD_NULL)
		return;
	ws_main = _thr_executing;
	ws_t0 = gettime();
	LWP_CreateThread(&wd_thread, wd_main, NULL, wd_stack, sizeof wd_stack, LWP_PRIO_HIGHEST);
}
