/*
 * ogc_compat.h -- the libogc2 / libogc v3 differences WiiStation touches, in one place.
 *
 * WiiStation builds against two libogc generations: Extrems' libogc2 (devkitPPC r41-2,
 * GCC 12) and devkitPro's libogc v3 (devkitPPC, GCC 16). ogc/libversion.h carries the
 * generation: _V_MAJOR_ is 2 in libogc2 and 3 in v3. Nothing here changes a libogc2 build.
 *
 * Include this (directly, or through psxcommon.h) before using any name defined below.
 * Everything is guarded by GEKKO, which the devkitPPC rules define, so host-compiled
 * tools that include a shared header are unaffected.
 */
#ifndef WS_OGC_COMPAT_H
#define WS_OGC_COMPAT_H

#ifdef GEKKO

#include <ogc/libversion.h>

#define WS_LIBOGC3 (_V_MAJOR_ >= 3)

#if WS_LIBOGC3

/* s32 for the WPAD stub below; the full gccore.h is not needed here. */
#include <gctypes.h>

/* TRUE / FALSE / BOOL: libogc2 defined them in gctypes.h; v3 moved them to gcbool.h. */
#include <gcbool.h>

/* POLLIN / POLLOUT / POLLERR / POLLHUP: libogc2 defined them in network.h; v3 has poll.h. */
#include <poll.h>

/* GX_RedirectWriteGatherPipe() returns the PHYSICAL pipe address 0x0C008000 in v3 (libogc2:
 * 0xCC008000). Tuxedo maps no BAT at 0x0C000000, so a write through it is a DSI (found
 * 2026-10-11: the soft GPU's GX_Flip crashed 2.4 s into a game on the bench Wii). */
#define WS_WGPIPE(p) ((void *)(0xC0000000u | ((u32)(p) & 0x0FFFFFFFu)))

/* CARD_WORKAREA was renamed CARD_WORKAREA_SIZE in v3 (same value). */
#define CARD_WORKAREA CARD_WORKAREA_SIZE

/* VIDEO_GetRetraceCount(): v3's libogc.a still exports it, but its video.h dropped the
 * prototype. */
u32 VIDEO_GetRetraceCount(void);

/* WPAD_LED_1 kept its libogc2 value; v3 has no WPAD LED names (see WPAD_ControlLed). */
#define WPAD_LED_1 0x01

/* v3 has no WPAD_ControlLed(chan, leds). Defined in wpad_leds.c: it lights the Wiimote
 * LEDs of a channel with libogc2's bit layout (bit 0 = LED 1), through wiiuse_set_leds()
 * on the wiimote_t that __wrap_wiiuse_set_leds recorded for that channel. */
s32 WPAD_ControlLed(s32 chan, s32 leds);

#endif /* WS_LIBOGC3 */

/* if_config(): v3 added a max_retries argument. Call sites use ws_if_config(). */
#if WS_LIBOGC3
#define WS_IF_CONFIG_RETRIES 3
#define ws_if_config(ip, mask, gw, dhcp) \
	if_config((ip), (mask), (gw), (dhcp), WS_IF_CONFIG_RETRIES)
#else
#define ws_if_config(ip, mask, gw, dhcp) \
	if_config((ip), (mask), (gw), (dhcp))
#endif

#endif /* GEKKO */
#ifndef WS_WGPIPE
#define WS_WGPIPE(p) ((void *)(p))
#endif
#endif /* WS_OGC_COMPAT_H */
