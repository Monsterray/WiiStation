/* A cover picture (a JPEG or PNG of the cover pack) as a small GX texture, for the explorer's facts
 * strip. Decoded with stb_image (deps/stb, JPEG and PNG only), boxed down to the slot's size as
 * RGB5A3. At most one texture is in memory: loading another, or leaving the page, frees it.
 * Emulator/menu thread only. */
#ifndef COVER_ART_H
#define COVER_ART_H

#include <stddef.h>
#include <stdint.h>
#ifdef HW_RVL
#include <gccore.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The libogc-free part (the host test runs it): the bytes of a JPEG or PNG file (any size up to
 * 4096 x 4096) as ow x oh RGB5A3 in GX's 4 x 4 tile order (ow and oh multiples of 4, at most 512).
 * 1: out is filled; 0: not a JPEG or PNG, damaged, too big, or out of memory (out untouched). */
int cover_art_decode(const uint8_t *file, size_t n, uint16_t *out, unsigned ow, unsigned oh);

#ifdef HW_RVL
/* Reads and decodes the file at path as a texture of ow x oh. 1: *out is the texture (linear filter,
 * clamped; load it with GX_LoadTexObjPreloaded into a region, outside GX_Begin/GX_End). 0: no such
 * file, or not a usable picture; nothing is kept. */
int cover_art_load(const char *path, GXTexObj *out, unsigned ow, unsigned oh);

/* The same, the texels in a buffer of the caller's (*buf; free it with cover_art_buf_free, after which
 * the texture object must not be used): for a cache of several covers. */
int cover_art_load_buf(const char *path, GXTexObj *out, unsigned ow, unsigned oh, void **buf);
void cover_art_buf_free(void *buf);

/* The picture in memory goes; the texture object must not be used after */
void cover_art_free(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
