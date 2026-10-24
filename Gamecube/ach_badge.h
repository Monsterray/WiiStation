/* RetroAchievements badge pictures as 64 x 64 GX textures, for the unlock card and the
 * achievement list. Emulator thread only (the thread that runs ra_transport_poll()). */
#ifndef ACH_BADGE_H
#define ACH_BADGE_H

#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

/* badge_name: rc_client_achievement_t.badge_name (digits). locked: the grey picture.
 *   1  *out is a ready GX_TF_RGB5A3 texture object (64 x 64, nearest filter, clamped). It is
 *      not loaded into the GX texture memory: GX_LoadTexObj / GX_LoadTexObjPreloaded it.
 *   0  not yet: a load or a download was started (or waits its turn); call again next frame.
 *  -1  it cannot be had: bad name, no HTTPS stack and no file on the card, the download
 *      failed, or the picture is not a PNG. Not tried again until ach_badge_flush().
 * Call it for every badge drawn, every frame: that is what keeps a texture in the cache.
 * A texture is only taken away (its memory is reused) once it has not been returned for
 * 200 ms, so one that is drawn each frame is never pulled from under the GP. */
int ach_badge_get(const char *badge_name, int locked, GXTexObj *out);

/* A new game: forget the pictures in memory, the queue and the failed names. A download
 * still on its way is dropped when it arrives. The files on the card stay. */
void ach_badge_flush(void);

#ifdef __cplusplus
}
#endif
#endif
