/* lc.c - the locked cache's regions. The why and the rules are in lc.h; the procedure for a
 * hardware measurement is in Docs/LOCKED_CACHE.md. */
#include <string.h>
#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/lwp_watchdog.h>   /* ticks_to_microsecs */
#include "lc.h"
#include "perf_prof.h"

char lockedCache = 0;   /* the setting; all regions off until a Wii has measured them */

/* ---------------------------------------------------------------------------------------
 * THE TABLE. One line per use of the locked cache.
 *
 *   name     what perf.log calls it
 *   bytes    its size; the regions that are on are packed in this order
 *   owner    the file that uses it, and so the file with its fallback path
 *   why      the measurement that justified trying it (share of wall, 11-game chain, Dolphin)
 *   fill     fixed contents to copy in when it is laid out, or NULL for scratch
 *
 * Candidates that are not here yet, and why:
 *   - the PSX scratchpad (1 KB, the guest's hottest data). Lightrec reaches guest memory
 *     through MMU page mappings (Gamecube/vm/vm.c), 4 KB pages, and the scratchpad shares its
 *     page with the hardware registers; putting it here needs a page mapped onto the locked
 *     cache's physical range. Try it with a Wii to hand.
 *   - the FMV frame conversion (UploadScreen / LoadTextureMovie, 4.2% of wall in Micro
 *     Machines): the same band pattern as LC_TEX_TILE, once that one is measured.
 * --------------------------------------------------------------------------------------- */
extern const short gauss[];   /* dfsound/gauss_i.h, via dfsound/dfspu.c */

static const struct {
	const char *name;
	unsigned bytes;
	const char *owner;
	const char *why;
	const void *fill;
} lc_regions[LC_REGION_COUNT] = {
	[LC_SPU_GAUSS] = { "spu-gauss", LC_SPU_GAUSS_BYTES, "dfsound/dfspu.c",
		"read 4x per sample per voice; the SPU is 1.8-10.9% of wall", gauss },
	[LC_TEX_TILE]  = { "tex-tile",  LC_TEX_TILE_BYTES,  "deps/opengx/gc_gl.c",
		"tiling output; the tiling pass is 4.0% of wall in Crash Bash", NULL },
};

/* The table must fit even with every region on; a region that would not fit is a build error,
 * not a region that is silently left out. */
#define LC_SUM (LC_SPU_GAUSS_BYTES + LC_TEX_TILE_BYTES)
_Static_assert(LC_SUM <= LC_TOTAL_BYTES, "locked-cache regions exceed 16 KB");
_Static_assert(LC_REGION_COUNT == 2, "a region was added: add its size to LC_SUM above");

static void *lc_base[LC_REGION_COUNT];
static unsigned lc_active;   /* the mask in effect */

void lc_configure(unsigned mask)
{
	unsigned off = 0;
	int r;

	mask &= (1u << LC_REGION_COUNT) - 1;
	lc_wait();
	memset(lc_base, 0, sizeof lc_base);
	lc_active = mask;
	if (!mask) {
		if (LCIsEnable())
			LCDisable();   /* give the data cache back its 16 KB */
		return;
	}
	if (!LCIsEnable())
		LCEnable();
	for (r = 0; r < LC_REGION_COUNT; r++) {
		if (!(mask & (1u << r)))
			continue;
		lc_base[r] = (unsigned char *)LCGetBase() + off;
		off += (lc_regions[r].bytes + 31) & ~31u;
		if (lc_regions[r].fill)
			memcpy(lc_base[r], lc_regions[r].fill, lc_regions[r].bytes);
	}
}

void *lc_get(enum lc_region r)
{
	return (unsigned)r < LC_REGION_COUNT ? lc_base[r] : NULL;
}

/* The queue holds 15 transfers; stay well under it rather than find out what 16 does. */
#define LC_QUEUE_LIMIT 12

void lc_store(void *dst_main, const void *src_lc, unsigned bytes)
{
	DCInvalidateRange(dst_main, bytes);
	if (LCQueueLength() >= LC_QUEUE_LIMIT)
		LCQueueWait(LC_QUEUE_LIMIT / 2);
	LCStoreData(dst_main, (void *)src_lc, bytes);
	PERF_INC(lc_stores);
	PERF_ADD(lc_store_bytes, bytes);
}

void lc_load(void *dst_lc, const void *src_main, unsigned bytes)
{
	DCFlushRange((void *)src_main, bytes);   /* the DMA reads memory, not the cache */
	if (LCQueueLength() >= LC_QUEUE_LIMIT)
		LCQueueWait(LC_QUEUE_LIMIT / 2);
	LCLoadData(dst_lc, (void *)src_main, bytes);
	PERF_INC(lc_loads);
	PERF_ADD(lc_load_bytes, bytes);
}

void lc_wait(void)
{
#ifdef PERF_PROF
	unsigned long long t0 = perf_now_ticks();
	LCQueueWait(0);
	PERF_ADD(lc_wait_ticks, perf_now_ticks() - t0);
#else
	LCQueueWait(0);
#endif
}

unsigned lc_usage(char *buf, int len)
{
	unsigned used = 0;
	int r, n = 0;
	if (len > 0)
		buf[0] = 0;
	for (r = 0; r < LC_REGION_COUNT; r++)
		if (lc_base[r]) {
			used += (lc_regions[r].bytes + 31) & ~31u;
			if (n < len)
				n += snprintf(buf + n, len - n, "%s%s", n ? ", " : "", lc_regions[r].name);
		}
	if (!used && len > 0)
		snprintf(buf, len, "off");
	return used;
}

void lc_report(FILE *f)
{
	int r;
	fprintf(f, "lc: mask=%u", lc_active);
	for (r = 0; r < LC_REGION_COUNT; r++)
		if (lc_base[r])
			fprintf(f, " %s@%04x+%u", lc_regions[r].name,
				(unsigned)((unsigned char *)lc_base[r] - (unsigned char *)LCGetBase()),
				lc_regions[r].bytes);
#ifdef PERF_PROF
	fprintf(f, " | stores=%lu store_kb=%llu loads=%lu load_kb=%llu wait_us=%llu",
		(unsigned long)g_perf.lc_stores, g_perf.lc_store_bytes >> 10,
		(unsigned long)g_perf.lc_loads, g_perf.lc_load_bytes >> 10,
		(unsigned long long)ticks_to_microsecs(g_perf.lc_wait_ticks));
#endif
	fprintf(f, "\n");
}
