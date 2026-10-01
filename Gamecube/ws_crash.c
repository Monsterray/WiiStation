/* ws_crash.c - what WiiStation adds to the HBC agent's crash reports (deps/hbc_agent, 1.9).
 *
 * The agent records an exception, an hbc_agent_fatal() and a hang (hbc_agent_alive() not
 * called for hang_s seconds) in its crash block, keeps the app's last output, and HBC reports
 * both after the reload (`hbc.py crash`, `hbc.py lastlog`). WiiStation adds two things:
 *   - ws_fatal(): its own stops that are no exception (a guest segfault, which Lightrec turned
 *     into exit(1); no sound driver, which was abort()), with the lab's results sent first;
 *   - the crash screens the agent cannot see: libogc's vector code sends an exception taken
 *     with MSR[RI] clear straight to default_exceptionhandler, past the agent's table hook
 *     (the hprof runs of 2026-10-01). The build wraps c_default_exceptionhandler
 *     (-Wl,--wrap in Makefile_Wii*), which every crash screen goes through.
 * `hbc.py crash --elf WiiSXRX_*.elf` resolves the addresses.
 */
#include <gccore.h>
#include <ogc/machine/processor.h>   /* mfspr */
#include <string.h>
#include <stdio.h>
#define HBC_AGENT_LAYOUT_ONLY
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "ws_crash.h"

extern int lab_active(void);       /* lab_net.c */
extern void lab_report(void);
void hbc_agent_fatal(u32 code, const char *fmt, ...) __attribute__((noreturn));   /* hbc_agent.h */

static int ram_word(u32 a)
{
	return !(a & 3) && ((a >= 0x80000000 && a < 0x81800000) || (a >= 0x90000000 && a < 0x94000000));
}

void __real_c_default_exceptionhandler(frame_context *ctx);

/* Record the exception the way the agent would, unless the agent already recorded this one. */
void __wrap_c_default_exceptionhandler(frame_context *ctx)
{
	static const u8 vector[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0c, 0x0d, 0x0f, 0x13, 0x14, 0x17 };
	hbc_crash_block *b = (hbc_crash_block *)HBC_CRASH_ADDR;
	u32 n = ctx->EXCPT_Number, sp = ctx->GPR[1], i;

	if (!(b->magic == HBC_CRASH_MAGIC && b->check == hbc_crash_check(b) && b->pc == ctx->SRR0)) {
		memset(b, 0, sizeof *b);
		b->magic = HBC_CRASH_MAGIC;
		b->version = HBC_CRASH_VERSION;
		b->kind = HBC_CRASH_EXCEPTION;
		b->exception = n < sizeof vector ? vector[n] : n;
		b->pc = ctx->SRR0;
		b->msr = ctx->SRR1;
		b->lr = ctx->LR;
		b->cr = ctx->CR;
		b->ctr = ctx->CTR;
		b->dar = mfspr(19);
		b->dsisr = mfspr(18);
		b->sp = sp;
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
		strcpy(b->reason, "past the agent's hook (MSR[RI] clear?)");
		b->check = hbc_crash_check(b);
		DCFlushRange(b, sizeof *b);
	}
	__real_c_default_exceptionhandler(ctx);
}

void ws_fatal(u32 code, const char *reason)
{
	if (lab_active())
		lab_report();   /* the results so far; wii_lab's job then asks HBC for the report */
	hbc_agent_fatal(code, "%s", reason);
}
