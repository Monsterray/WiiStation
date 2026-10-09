/* hb_probe.c - the heartbeat log and the IOS-call watch (hb_probe.h). Debug builds only. */
#ifdef PERF_PROF

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "hb_probe.h"
#include "../psxcommon.h"
#include "../r3000a.h"

#define HB_PERIOD_MS   2000
#define HB_SLOW_MS     500     /* an IOS call in flight longer than this is reported */
#define HB_SLOTS       12      /* IOS calls in flight at once, across threads */
#define HB_FDS         32
#define HB_TRANSITIONS 24      /* phase changes kept between two flushes */
#define HB_PRIO        110     /* above the emulator (64), the SMB pump (67), the CD read-ahead (66) */

extern u32 frame_counter;      /* psxcounters.c: PS1 vblanks of the running game */

static volatile const char *phase = "boot", *phase_detail = "";
static volatile u64 phase_t0;
static struct { const char *what, *detail; u64 t; } trans[HB_TRANSITIONS];
static volatile unsigned trans_n, trans_lost;
static lwp_t hb_thread = LWP_THREAD_NULL;
static u64 boot_t;

/* The IOS calls in flight. A slot is claimed with interrupts off, so any thread may. */
static struct {
	volatile u32 busy;
	s32 fd, cmd;
	const char *op;
	u64 t0;
	lwp_t thr;
} slots[HB_SLOTS];
static char fd_name[HB_FDS][24];
static volatile unsigned ios_calls, ios_slow_max_ms;

static u32 ms_since(u64 t0)
{
	return ticks_to_millisecs(diff_ticks(t0, gettime()));
}

void hb_phase(const char *what, const char *detail)
{
	u32 level;
	_CPU_ISR_Disable(level);
	phase = what;
	phase_detail = detail ? detail : "";
	phase_t0 = gettime();
	if (trans_n < HB_TRANSITIONS) {
		trans[trans_n].what = what;
		trans[trans_n].detail = detail ? detail : "";
		trans[trans_n].t = phase_t0;
		trans_n++;
	} else
		trans_lost++;
	_CPU_ISR_Restore(level);
}

static int slot_take(const char *op, s32 fd, s32 cmd)
{
	u32 level;
	int i;

	if (hb_thread != LWP_THREAD_NULL && LWP_GetSelf() == hb_thread)
		return -1;   /* the log's own writes */
	_CPU_ISR_Disable(level);
	ios_calls++;
	for (i = 0; i < HB_SLOTS; i++)
		if (!slots[i].busy) {
			slots[i].busy = 1;
			slots[i].fd = fd;
			slots[i].cmd = cmd;
			slots[i].op = op;
			slots[i].t0 = gettime();
			slots[i].thr = LWP_GetSelf();
			break;
		}
	_CPU_ISR_Restore(level);
	return i < HB_SLOTS ? i : -1;
}

static void slot_give(int i)
{
	if (i < 0)
		return;
	u32 ms = ms_since(slots[i].t0);
	if (ms > ios_slow_max_ms)
		ios_slow_max_ms = ms;
	slots[i].busy = 0;
}

/* The wrapped IOS entry points (-Wl,--wrap=IOS_... in Makefile_Wii) */
s32 __real_IOS_Open(const char *path, u32 mode);
s32 __real_IOS_Close(s32 fd);
s32 __real_IOS_Read(s32 fd, void *buf, s32 len);
s32 __real_IOS_Write(s32 fd, const void *buf, s32 len);
s32 __real_IOS_Seek(s32 fd, s32 where, s32 whence);
s32 __real_IOS_Ioctl(s32 fd, s32 ioctl, void *bi, s32 li, void *bo, s32 lo);
s32 __real_IOS_Ioctlv(s32 fd, s32 ioctl, s32 cin, s32 cio, ioctlv *argv);

__attribute__((used)) s32 __wrap_IOS_Open(const char *path, u32 mode)
{
	int k = slot_take("open", -1, (s32)mode);
	s32 r = __real_IOS_Open(path, mode);
	slot_give(k);
	if (r >= 0 && r < HB_FDS && path)
		snprintf(fd_name[r], sizeof fd_name[r], "%s", path);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Close(s32 fd)
{
	int k = slot_take("close", fd, 0);
	s32 r = __real_IOS_Close(fd);
	slot_give(k);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Read(s32 fd, void *buf, s32 len)
{
	int k = slot_take("read", fd, len);
	s32 r = __real_IOS_Read(fd, buf, len);
	slot_give(k);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Write(s32 fd, const void *buf, s32 len)
{
	int k = slot_take("write", fd, len);
	s32 r = __real_IOS_Write(fd, buf, len);
	slot_give(k);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Seek(s32 fd, s32 where, s32 whence)
{
	int k = slot_take("seek", fd, where);
	s32 r = __real_IOS_Seek(fd, where, whence);
	slot_give(k);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Ioctl(s32 fd, s32 ioctl, void *bi, s32 li, void *bo, s32 lo)
{
	int k = slot_take("ioctl", fd, ioctl);
	s32 r = __real_IOS_Ioctl(fd, ioctl, bi, li, bo, lo);
	slot_give(k);
	return r;
}

__attribute__((used)) s32 __wrap_IOS_Ioctlv(s32 fd, s32 ioctl, s32 cin, s32 cio, ioctlv *argv)
{
	int k = slot_take("ioctlv", fd, ioctl);
	s32 r = __real_IOS_Ioctlv(fd, ioctl, cin, cio, argv);
	slot_give(k);
	return r;
}

static const char *name_of(s32 fd)
{
	return fd >= 0 && fd < HB_FDS && fd_name[fd][0] ? fd_name[fd] : "?";
}

/* One flush: the phase changes since the last one, then the heartbeat line */
static void flush(FILE *f)
{
	unsigned n, i, lost;
	u32 level;
	static struct { const char *what, *detail; u64 t; } copy[HB_TRANSITIONS];

	_CPU_ISR_Disable(level);
	n = trans_n;
	lost = trans_lost;
	memcpy(copy, trans, n * sizeof copy[0]);
	trans_n = 0;
	trans_lost = 0;
	_CPU_ISR_Restore(level);
	for (i = 0; i < n; i++)
		fprintf(f, "ph %7.1f %s %s\n", copy[i].t < boot_t ? 0.0   /* before hb_start() */
		        : ticks_to_millisecs(diff_ticks(boot_t, copy[i].t)) / 1000.0,
		        copy[i].what, copy[i].detail);
	if (lost)
		fprintf(f, "ph (%u more changes not kept)\n", lost);

	fprintf(f, "hb %7.1f vi=%u psxvbl=%u pc=%08x calls=%u slowest=%ums | %s %.1fs %s",
	        ms_since(boot_t) / 1000.0, (unsigned)VIDEO_GetRetraceCount(), (unsigned)frame_counter,
	        (unsigned)psxRegs.pc, ios_calls, ios_slow_max_ms, phase,
	        ms_since(phase_t0) / 1000.0, phase_detail);
	for (i = 0; i < HB_SLOTS; i++)
		if (slots[i].busy) {
			u32 ms = ms_since(slots[i].t0);
			if (ms >= HB_SLOW_MS)
				fprintf(f, " | IOS %s fd%d(%s) cmd=%d for %.1fs by thread %p", slots[i].op,
				        (int)slots[i].fd, slots[i].fd < 0 ? "-" : name_of(slots[i].fd),
				        (int)slots[i].cmd, ms / 1000.0, (void *)slots[i].thr);
		}
	fputc('\n', f);
}

static void *hb_main(void *arg)
{
	(void)arg;
	for (;;) {
		FILE *f = fopen("sd:/wiistation/hb.log", "a");
		if (f) {
			flush(f);
			fclose(f);
		}
		usleep(HB_PERIOD_MS * 1000);
	}
	return NULL;
}

void hb_start(void)
{
	FILE *f;

	if (hb_thread != LWP_THREAD_NULL)
		return;
	boot_t = gettime();
	phase_t0 = boot_t;
	f = fopen("sd:/wiistation/hb.log", "w");   /* a new log each boot */
	if (f) {
		fprintf(f, "# WiiStation heartbeat (Gamecube/hb_probe.c): every %d s; IOS calls in flight > %d ms\n",
		        HB_PERIOD_MS / 1000, HB_SLOW_MS);
		fclose(f);
	}
	LWP_CreateThread(&hb_thread, hb_main, NULL, NULL, 16 * 1024, HB_PRIO);
}

#endif
