/* WiiStation: included first in every libchdr source (Makefile.wii -include). libchdr's
 * allocations -- the hunk map, caches, LZMA/zlib/FLAC state, and zstd's through
 * chdr_zstd_mem -- go to chdr_malloc/chdr_calloc/chdr_realloc/chdr_free, defined in
 * cdriso.c: MEM2, which leaves the newlib heap (MEM1) to the emulator. */
#ifndef WII_ALLOC_H
#define WII_ALLOC_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void *chdr_malloc(size_t n);
void *chdr_calloc(size_t n, size_t s);
void *chdr_realloc(void *p, size_t n);
void chdr_free(void *p);

#define malloc(n) chdr_malloc(n)
#define calloc(n, s) chdr_calloc(n, s)
#define realloc(p, n) chdr_realloc(p, n)
#define free(p) chdr_free(p)

/* zstd allocates with its own malloc unless given a ZSTD_customMem (zstd_codec_init) */
#define ZSTD_STATIC_LINKING_ONLY
static void *chdr_zstd_alloc(void *opaque, size_t n) { (void)opaque; return chdr_malloc(n); }
static void chdr_zstd_free(void *opaque, void *p) { (void)opaque; chdr_free(p); }
#define chdr_zstd_mem ((ZSTD_customMem){ chdr_zstd_alloc, chdr_zstd_free, NULL })

#endif
