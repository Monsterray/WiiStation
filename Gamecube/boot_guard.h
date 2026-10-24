/* The boot guard (boot_guard.c): start-up's watchdog until the HBC agent's takes over. */
#ifndef BOOT_GUARD_H
#define BOOT_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

void boot_guard_start(void);              /* main()'s first line */
void boot_guard_stage(const char *name);  /* a new stage: its own GUARD_S seconds */
void boot_guard_done(void);               /* the agent's watchdog is there */

#ifdef __cplusplus
}
#endif
#endif
