/*
 * wpad_leds.c -- WPAD_ControlLed for libogc v3 (LIBOGC=3 only; libogc2 has its own).
 *
 * Kept out of ogc_compat.c: wiiuse.h pulls in the bte/bluetooth headers, which define
 * static inlines that ogc_compat.c defines out of line (DCFlushRange and friends).
 * Makefile_Wii and Makefile_Wii_Release leave this file out for LIBOGC=2.
 */
#include <gctypes.h>
#include <wiiuse/wiiuse.h>
#include <wiiuse/wpad.h>

/* WPAD_ControlLed(): libogc2's wpad.c keeps its table of wiimotes static, and so does v3
 * (wiiuse/wpad.c __wpads, wiiuse/wiiuse.c __wm), so the wiimote_t of a channel cannot be
 * reached by name. __wrap_wiiuse_set_leds records the wiimote_t on every call instead.
 * wiiuse_set_leds() runs for each wiimote as it connects (wpad.c WIIUSE_CONNECT, and the
 * handshake in io.c), so the table is filled before a game can light an LED. wiiuse_init
 * allocates the wiimote_t objects once and WPAD_Shutdown does not free them, so a recorded
 * pointer stays valid. Link flags for LIBOGC=3:
 *   -Wl,-wrap,wiiuse_set_leds   (Makefile_Wii, Makefile_Wii_Release)
 * The LED bits follow libogc2's WPAD_ControlLed: bit 0 is LED 1, moved into the upper nibble
 * the Wiimote takes (WIIMOTE_LED_1 = 0x10). Channels are 0..WPAD_BALANCE_BOARD, as in libogc2. */
extern void __real_wiiuse_set_leds(struct wiimote_t *wm, int leds, cmd_blk_cb cb);

static struct wiimote_t *ws_wm_by_chan[WPAD_BALANCE_BOARD + 1];

void __wrap_wiiuse_set_leds(struct wiimote_t *wm, int leds, cmd_blk_cb cb)
{
	if (wm && wm->unid >= 0 && wm->unid <= WPAD_BALANCE_BOARD)
		ws_wm_by_chan[wm->unid] = wm;
	__real_wiiuse_set_leds(wm, leds, cb);
}

s32 WPAD_ControlLed(s32 chan, s32 leds)
{
	s32 i;

	if (chan == WPAD_CHAN_ALL) {
		for (i = WPAD_CHAN_0; i <= WPAD_BALANCE_BOARD; i++)
			WPAD_ControlLed(i, leds);
		return WPAD_ERR_NONE;
	}
	if (chan < WPAD_CHAN_0 || chan > WPAD_BALANCE_BOARD)
		return WPAD_ERR_BAD_CHANNEL;
	/* No wiimote recorded for the channel yet: nothing to light, as in libogc2. */
	if (ws_wm_by_chan[chan])
		wiiuse_set_leds(ws_wm_by_chan[chan], (leds << 4) & 0xf0, NULL);
	return WPAD_ERR_NONE;
}
