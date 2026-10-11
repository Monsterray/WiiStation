/*
 * ogc_compat.c -- libogc v3 definitions for names WiiStation declares as plain externs.
 *
 * libogc v3 turned these into static inline functions in its headers, so no symbol is
 * emitted for them. Our code (and HidController/global.h, lightrec.c, DEBUG.c, profile.c,
 * SoftGPU/oldGpuFps.c) declares them as ordinary externs and links against the symbol.
 * This file gives the symbols a definition. It is compiled only for LIBOGC=3
 * (Makefile_Wii / Makefile_Wii_Release add it there); libogc2 keeps its own.
 *
 * This file must NOT include <ogc/lwp_watchdog.h> or <ogc/cache.h>: those define the
 * same names as static inline, and a second definition with external linkage in the same
 * translation unit is a compile error. The bodies use the lower-level tuxedo helpers.
 */
#include <gctypes.h>
#include <stddef.h>
#include <ogc/machine/processor.h>
#include <ogc/video.h>
#include <tuxedo/ppc/cache.h>
#include <tuxedo/ppc/clock.h>

/* VIDEO_GetRetraceCount(): libogc2 exported it; v3 keeps the count in a static and exports no
 * getter. v3 passes the count to every post-retrace callback, so a trampoline in that slot
 * can record it. The trampoline is installed by __wrap_VIDEO_Init (VIDEO_Init clears both
 * retrace slots and the count), and __wrap_VIDEO_SetPostRetraceCallback stores the program's
 * callback instead of replacing the trampoline. The program's callback still gets the same
 * arguments, and the count keeps running whatever it registers. Link flags for LIBOGC=3:
 *   -Wl,-wrap,VIDEO_Init,-wrap,VIDEO_SetPostRetraceCallback   (Makefile_Wii, Makefile_Wii_Release)
 */
extern void __real_VIDEO_Init(void);
extern VIRetraceCallback __real_VIDEO_SetPostRetraceCallback(VIRetraceCallback callback);

static VIRetraceCallback ws_user_post_cb = NULL;
static volatile u32 ws_retrace_count = 0;

static void ws_post_trampoline(u32 retraceCnt)
{
	ws_retrace_count = retraceCnt;
	if (ws_user_post_cb)
		ws_user_post_cb(retraceCnt);
}

void __wrap_VIDEO_Init(void)
{
	__real_VIDEO_Init();
	ws_retrace_count = 0;
	__real_VIDEO_SetPostRetraceCallback(ws_post_trampoline);
}

VIRetraceCallback __wrap_VIDEO_SetPostRetraceCallback(VIRetraceCallback callback)
{
	u32 level;
	VIRetraceCallback old;

	_CPU_ISR_Disable(level);
	old = ws_user_post_cb;
	ws_user_post_cb = callback;
	_CPU_ISR_Restore(level);
	return old;
}

u32 VIDEO_GetRetraceCount(void)
{
	return ws_retrace_count;
}

/* gettime(): lwp_watchdog.h makes it static inline in v3. HidController/global.h
 * and the debug/profile files declare it extern. */
u64 gettime(void)
{
	return PPCGetTickCount();
}

/* diff_usec(): same story. The diff_ticks() expression is lwp_watchdog.h's, copied
 * so the two stay identical on wrap-around. */
u32 diff_usec(u64 start, u64 end)
{
	u64 diff = ((u64)(end) < (u64)(start))
		? ((u64)-1 - (u64)(start) + (u64)(end))
		: ((u64)(end) - (u64)(start));
	return (u32)PPCTicksToUs(diff);
}

/* diff_sec(): the same (debug probes call it out of line) */
u32 diff_sec(u64 start, u64 end)
{
	u64 diff = ((u64)(end) < (u64)(start))
		? ((u64)-1 - (u64)(start) + (u64)(end))
		: ((u64)(end) - (u64)(start));
	return (u32)(diff / PPC_TIMER_CLOCK);
}

/* lightrec.c declares these as externs; v3's cache.h has them as static inline
 * (DCFlushRange) or as asm aliases the header does not export (ICInvalidateRange). */
void DCFlushRange(void *startaddress, u32 len)
{
	PPCDCacheFlush(startaddress, len);
}

void ICInvalidateRange(void *startaddress, u32 len)
{
	PPCICacheInvalidate(startaddress, len);
}

/* SDL_video.o (pulled in by SDL_Init for dfsound's audio subsystem) references SDL's OGC video
 * bootstrap. That lives in SDL_ogc_video.o, which also defines vmode and xfb, and WiiStation
 * defines those itself, so the link fails with "multiple definition". WiiStation uses SDL for
 * audio only (SDL_INIT_AUDIO), so no video driver is ever initialised: this stand-in reports
 * none (available() returns 0) and SDL_ogc_video.o is never pulled in. Layout is SDL 1.2's
 * VideoBootStrap {name, desc, available, create}; SDL_video.o reads name at +0 and available
 * at +8. */
static int ws_no_video_available(void)
{
	return 0;
}

static void *ws_no_video_create(int devindex)
{
	(void)devindex;
	return NULL;
}

struct ws_video_bootstrap {
	const char *name;
	const char *desc;
	int (*available)(void);
	void *(*create)(int devindex);
};

struct ws_video_bootstrap OGC_bootstrap = {
	"ogc", "GameCube/Wii video (WiiStation owns the display; SDL video is unused)",
	ws_no_video_available, ws_no_video_create
};

/* SDL_wiievents.o (PumpEvents' mouse scaling) reads vresx/vresy, which SDL_ogc_video.o sets
 * when its driver starts. That driver never starts here, so these hold its default mode. */
int vresx = 640;
int vresy = 480;

/* SDL_wiievents.o (portlibs/wii's libSDL.a) reads TerminateRequested and calls Terminate().
 * In libogc2 both come from SDL's Wii glue. In v3 they live in libSDLmain.a, which also
 * defines main(), so it cannot be linked next to WiiStation's own main(). The glue only
 * sets the flag from the power/reset/shutdown callbacks it registers in its own main(),
 * which this build never runs, so the flag stays 0 and Terminate() is never reached. */
u8 TerminateRequested = 0;

void Terminate(void)
{
}
