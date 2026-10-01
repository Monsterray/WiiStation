/* ws_crash.h - WiiStation's additions to the HBC agent's crash reports (Gamecube/ws_crash.c). */
#ifndef WS_CRASH_H
#define WS_CRASH_H

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The app codes in an hbc_agent_fatal() report (hbc.py shows them, never interprets them) */
#define WS_CRASH_GUEST_SEGFAULT 0x82   /* Lightrec stopped on a guest access to no memory */
#define WS_CRASH_NO_AUDIO       0x83   /* no sound output driver opened */

/* Progress, for the agent's hang watchdog: a PS1 vblank, a menu frame, a lab transfer chunk.
 * The first call arms it (hang_s, 60 s). */
void hbc_agent_alive(void);
#define ws_alive() hbc_agent_alive()

/* Send the results so far in lab mode, then stop with an agent fatal report. */
void ws_fatal(u32 code, const char *reason) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
