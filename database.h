#ifndef DATABASE_H
#define DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#define AUTO_FIX_GPU_BUSY            0x100
#define AUTO_FIX_QUADS_AS_2TRIANGLES 0x200
#define AUTO_FIX_DINO_CRISIS2        0x400
#define AUTO_FIX_CHRONO_CROSS        0x2000
/* OpenGX EFB sync (GlesGpu/efbSync.inc): keep snapshots of drawn frames from the start,
 * for a game that reads its frames back (default: from its first read that needs one) */
#define AUTO_FIX_VRAM_READBACK       0x8000
/* ... or never sync the EFB into VRAM, for a game the sync would harm */
#define AUTO_FIX_EFB_SYNC_OFF        0x10000
/* soft GPU: flat-textured triangles through the texture-window rasterizers, which mask the
 * texture coordinates (P.E.Op.S. "repeated flat tex triangles", Dark Forces). It shared
 * 0x100 with AUTO_FIX_GPU_BUSY, so any game given one got both. */
#define AUTO_FIX_FLAT_TEX_WRAP       0x20000

void Apply_Hacks_Cdrom(void);
int check_unsatisfied_libcrypt(void);

#ifdef __cplusplus
}
#endif

#endif
