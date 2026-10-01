/* framecap_wait.c -- the frame limiter's sleeping wait (LimiterWait = 1).
 *
 * SoftGPU/oldGpuFps.c FrameCap() spins until the next frame is due, so no thread below the
 * emulator gets the CPU while a game runs: the HBC agent, the CD read-ahead, the network.
 * With LimiterWait = 1 it first calls framecap_sleep(): sleep on a timer alarm for all but the
 * last millisecond of the wait, then the spin finishes it exactly. perf.log "limitwait:" has
 * the sleeps, the time asleep and how late the wake-ups were. The limiter's tick is 10 us
 * (timeGetTime() is microseconds / 10, TIMEBASE 100000 a second), although its comments said
 * 100 us: the usleep() tried on 2026-09-29, which "overslept ~56 ms", most likely asked for
 * ten times the wait, and so did this file's first version (Spyro 0.66x in Dolphin). (In its own file: oldGpuFps.c declares its own gettime() and types that
 * clash with libogc's headers.) */
#include <gccore.h>
#include <ogc/system.h>
#include <ogc/semaphore.h>
#include <ogc/lwp_watchdog.h>
#include "perf_prof.h"

static syswd_t fc_alarm = SYS_WD_NULL;
static sem_t fc_sem = LWP_SEM_NULL;

static void fc_alarm_cb(syswd_t alarm, void *arg)
{
	(void)alarm; (void)arg;
	LWP_SemPost(fc_sem);
}

void framecap_sleep(long wait_ticks)   /* the wait ahead, in the limiter's 10 us ticks */
{
	long us = wait_ticks * 10 - 1000;    /* wake 1 ms early; the spin does the rest */
	struct timespec ts;
	u64 t0, target;

	if (us < 500)
		return;
	if (fc_sem == LWP_SEM_NULL) {
		if (LWP_SemInit(&fc_sem, 0, 1) != 0) {
			fc_sem = LWP_SEM_NULL;
			return;
		}
		if (SYS_CreateAlarm(&fc_alarm) != 0) {
			LWP_SemDestroy(fc_sem);
			fc_sem = LWP_SEM_NULL;
			return;
		}
	}
	ts.tv_sec = us / 1000000;
	ts.tv_nsec = (us % 1000000) * 1000;
	t0 = gettime();
	target = t0 + microsecs_to_ticks(us);
	SYS_SetAlarm(fc_alarm, &ts, fc_alarm_cb, NULL);
	LWP_SemWait(fc_sem);
#ifdef PERF_PROF
	{
		u64 now = gettime();
		g_perf.limit_sleeps++;
		g_perf.limit_sleep_ticks += now - t0;
		if (now > target) {
			u64 over = now - target;
			if (over > g_perf.limit_over_max)
				g_perf.limit_over_max = over;
			if (over > microsecs_to_ticks(500))
				g_perf.limit_overs++;
		}
	}
#else
	(void)target;
#endif
}
