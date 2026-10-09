/* hb_probe.h - the heartbeat log and the IOS-call watch, debug builds only (hb_probe.c).
 *
 * sd:/wiistation/hb.log gets a line every 2 s from a thread above the emulator's priority:
 * what WiiStation is doing (the last hb_phase()), for how long, the VI and PS1 frame counts,
 * the guest PC, and every IOS call in flight for more than half a second, with its device
 * and the thread that made it. Every IOS_Open/Close/Read/Write/Seek/Ioctl/Ioctlv in the
 * program goes through it (the debug link wraps them: Makefile_Wii), libfat's, libdi's, the
 * network's and libsmb2's included. A freeze then says what it froze in, and whether a
 * library call into IOS is what never came back. Release builds compile it all away. */
#ifndef HB_PROBE_H
#define HB_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PERF_PROF
void hb_start(void);                                   /* once the SD card is mounted */
void hb_phase(const char *what, const char *detail);   /* both must be static strings */
#else
#define hb_start() ((void)0)
#define hb_phase(what, detail) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
