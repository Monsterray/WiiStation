/* The boot guard: start-up has no watchdog until the HBC agent's own takes over (it starts
 * after the menu, hbc_home_start()), so a hang there left the Wii frozen until someone at it
 * pulled the power (bench Wii, 2026-10-24, twice). This one starts on main()'s first line: a
 * thread at the highest priority that, when one start-up stage lasts more than GUARD_S
 * seconds, reports "boot hang at <stage>" as a crash (`hbc.py crash`, code 0x85) and goes
 * back to HBC through the reload stub (hbc_agent_fatal_now works before hbc_agent_init).
 * Each boot_guard_stage() call starts the stage's clock again; boot_guard_done() ends it once
 * the agent's watchdog is there. A hang with interrupts off, or IOS itself stuck, stops it
 * too: those still need a power cycle. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>
#include "boot_guard.h"

void hbc_agent_fatal_now(u32 code, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));

#define GUARD_S 90           /* per stage: a USB drive spinning up and a slow mount fit easily */
#define GUARD_STACK 8192

static volatile const char *stage = "start";
static volatile u64 stage_at;
static volatile int done;
static lwp_t thread = LWP_THREAD_NULL;
static u8 stack[GUARD_STACK] __attribute__((aligned(32)));

static void *guard(void *arg)
{
	(void)arg;
	while (!done) {
		usleep(250 * 1000);
		if (!done && diff_sec(stage_at, gettime()) > GUARD_S)
			hbc_agent_fatal_now(0x85, "boot hang at %s (%u s)", (const char *)stage, (unsigned)GUARD_S);
	}
	return NULL;
}

void boot_guard_start(void)
{
	stage_at = gettime();
	if (thread == LWP_THREAD_NULL)
		LWP_CreateThread(&thread, guard, NULL, stack, GUARD_STACK, LWP_PRIO_HIGHEST);
}

void boot_guard_stage(const char *name)
{
	stage = name;
	stage_at = gettime();
}

void boot_guard_done(void)
{
	done = 1;   /* the thread ends at its next wake-up */
}
