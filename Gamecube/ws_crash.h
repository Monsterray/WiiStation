/* ws_crash.h - reports for the ways WiiStation stops that are not an exception
 * (Gamecube/ws_crash.c). */
#ifndef WS_CRASH_H
#define WS_CRASH_H

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exception codes in the HBC crash block, above every PowerPC vector number (0x01-0x17). */
#define WS_CRASH_HANG           0x81   /* no vblank and no ws_progress for WS_HANG_S seconds */
#define WS_CRASH_GUEST_SEGFAULT 0x82   /* Lightrec stopped on a guest access to no memory */
#define WS_CRASH_NO_AUDIO       0x83   /* no sound output driver opened */

void ws_watchdog_start(void);           /* from the main (emulation and menu) thread */
extern volatile u32 ws_progress;        /* +1 per menu frame and per network chunk (lab_net.c) */
extern volatile int ws_watchdog_hold;   /* > 0: a known long wait (the HBC HOME menu) */

/* Record the crash block for code, send the results so far in lab mode, and leave. */
void ws_fatal(u32 code) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
