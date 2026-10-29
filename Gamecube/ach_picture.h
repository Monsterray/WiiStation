/* One unlock picture (ach_shot.c's PNG) as a GX texture for the book's detail page. At most one is
 * in memory: loading another, or leaving the page, frees it. Emulator/menu thread only. */
#ifndef ACH_PICTURE_H
#define ACH_PICTURE_H

#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACH_PICTURE_W 256   /* every picture is shown as 256 x 192 RGB5A3 (98 KB, one TMEM region), whatever its size */
#define ACH_PICTURE_H 192

/* Reads and decodes the PNG at path. 1: *out is the texture (linear filter, clamped; load it with
 * GX_LoadTexObjPreloaded into a region, outside GX_Begin/GX_End). 0: no such file, or not a PNG
 * (or not enough memory); nothing is kept. */
int ach_picture_load(const char *path, GXTexObj *out);

/* The picture in memory goes; the texture object must not be used after */
void ach_picture_free(void);

#ifdef __cplusplus
}
#endif
#endif
