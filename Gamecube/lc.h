/* lc.h - the locked cache: 16 KB of Broadway's L1 data cache used as fast scratch memory.
 *
 * Half of the 32 KB L1 data cache can be locked. It then stops caching and becomes 16 KB of
 * memory at 0xE0000000 that never misses, with its own DMA engine to and from main memory
 * (libogc's LCEnable, LCLoadData, LCStoreData). The price is that everything else runs with a
 * 16 KB data cache instead of 32 KB, so a use has to earn its place.
 *
 * This module shares the 16 KB between the parts of WiiStation that want it. Each use is one
 * REGION: one line in the table in lc.c, one bit in the LockedCache setting. A region that is
 * off, or does not fit, is simply absent -- lc_get() returns NULL -- and its user runs its
 * ordinary code instead. Every user must keep that ordinary path: it is what makes a region
 * safe to turn off, and what lets one boot compare a region on and off (a chained autoboot can
 * set LockedCache per game; see GamecubeMain.cpp).
 *
 * Dolphin cannot say whether a region helps. It runs the locked cache and its DMA correctly,
 * so a region can be proved *correct* there, but it models no cache misses and completes the
 * DMA at once, so the gain -- misses and flushes that no longer happen -- only shows on a Wii.
 * That is why every region is off by default until a hardware run has measured it
 * (Docs/LOCKED_CACHE.md).
 *
 * Adding a region: add its id to enum lc_region below, its size as an LC_..._BYTES define, and
 * its line to lc_regions[] in lc.c; then, in the user, ask lc_get() and fall back when it
 * returns NULL. Docs/LOCKED_CACHE.md has the full checklist.
 *
 * Threads: every region is used from the emulation thread only. The DMA queue is one per CPU,
 * so lc_wait() waits for every transfer in it, not only the caller's; that is only slower,
 * never wrong, while there is one thread.
 */
#ifndef WIISTATION_LC_H
#define WIISTATION_LC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* One id per use. The value is also the region's bit in the LockedCache setting, so never
 * renumber: append. */
enum lc_region {
	LC_SPU_GAUSS = 0,   /* the SPU's gaussian interpolation table (dfsound/dfspu.c) */
	LC_TEX_TILE  = 1,   /* OpenGX's tiling output, two outputs x two bands (deps/opengx/gc_gl.c) */
	LC_REGION_COUNT
};

/* Region sizes, so a user can check at compile time that its data fits. Multiples of 32. */
#define LC_SPU_GAUSS_BYTES  2048
#define LC_TEX_TILE_BAND    2048                        /* one row of 4x4 blocks, 256 texels wide */
#define LC_TEX_TILE_BYTES   (4 * LC_TEX_TILE_BAND)      /* opaque + semi-transparent, double banked */

#define LC_TOTAL_BYTES      16384

/* At the start of each game (go()): lay out the regions whose bit is set in mask and that fit,
 * copy in any fixed contents, and lock the cache -- or unlock it when no region is on. */
void lc_configure(unsigned mask);

/* The region's memory in the locked cache, or NULL when it is off. 32-byte aligned. */
void *lc_get(enum lc_region r);

/* DMA between the locked cache and main memory, asynchronous. Both addresses 32-byte aligned,
 * bytes a multiple of 32. lc_store() first drops the destination's lines from the ordinary
 * cache, so no stale line can overwrite the DMA's data later. Nothing may read the destination
 * (the CPU or the GPU) before lc_wait(). */
void lc_store(void *dst_main, const void *src_lc, unsigned bytes);
void lc_load(void *dst_lc, const void *src_main, unsigned bytes);
void lc_wait(void);

/* perf.log: which regions are on, where, and how much DMA they did. */
void lc_report(FILE *f);

/* The menu's Memory page: bytes the regions of the last lc_configure() take (0 = the cache
 * is not locked), and their names in buf, comma separated ("off" when none). */
unsigned lc_usage(char *buf, int len);

/* The LockedCache setting (Gamecube/GamecubeMain.cpp): one bit per region, 0 = all off. */
extern char lockedCache;

#ifdef __cplusplus
}
#endif

#endif /* WIISTATION_LC_H */
