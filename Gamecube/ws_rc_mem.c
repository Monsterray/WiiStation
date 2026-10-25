/* rcheevos' memory (the parsed set, leaderboards, the server's JSON). The rcheevos objects are
 * compiled with -Dmalloc=ws_rc_malloc ... -Dstrdup=ws_rc_strdup (Makefile_Wii,
 * Makefile_Wii_Release), so every allocation and every free of theirs comes here and none of
 * theirs reaches libc.
 * - MEM1 first, up to RC_MEM1_MAX: the set is walked every frame, and from MEM2 that walk cost
 *   Spyro 1.14 ms a frame instead of 0.74 ms (CTR 0.59 / 0.34; Official mode's load +7.9 /
 *   +5.2 points; bench Wii A/B, .runs/ach_pre_mem2 and ach_pre_mem1, 2026-10-24). CTR's set takes
 *   about 350 KB.
 * - Past the cap, or when MEM1 has no room, MEM2. MEM1 was where CTR after Spyro ran out
 *   (2026-10-24); since Lightrec and the XFBs left it, it has 3.4 MB free in a game, and the cap
 *   keeps rcheevos' part bounded. free/realloc tell the heaps apart by address.
 * The heaps lock themselves (the HTTPS worker frees here too); rc_mem1 is atomic. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#ifdef HW_RVL
#include <stdbool.h>
#include <stdint.h>
#include "../mem2_manager.h"

#define RC_MEM1_MAX (1u << 20)
static unsigned rc_mem1;   /* bytes rcheevos holds in MEM1 */

static int in_mem2(const void *p) { return (uintptr_t)p >= 0x90000000u; }

void *ws_rc_malloc(size_t n)
{
	void *p = NULL;
	if (__atomic_load_n(&rc_mem1, __ATOMIC_RELAXED) + n <= RC_MEM1_MAX && (p = malloc(n)))
		__atomic_fetch_add(&rc_mem1, malloc_usable_size(p), __ATOMIC_RELAXED);
	return p ? p : _mem2_malloc(n);
}

void ws_rc_free(void *p)
{
	if (!p) return;
	if (in_mem2(p)) {
		_mem2_free(p);
		return;
	}
	__atomic_fetch_sub(&rc_mem1, malloc_usable_size(p), __ATOMIC_RELAXED);
	free(p);
}

void *ws_rc_calloc(size_t n, size_t size)
{
	void *p = n && size > (size_t)-1 / n ? NULL : ws_rc_malloc(n * size);
	if (p) memset(p, 0, n * size);
	return p;
}

void *ws_rc_realloc(void *p, size_t n)
{
	void *q;
	size_t old;
	if (!p) return ws_rc_malloc(n);
	if (!n) { ws_rc_free(p); return NULL; }
	if (in_mem2(p)) return _mem2_realloc(p, n);
	old = malloc_usable_size(p);
	if (old >= n) return p;
	if (!(q = ws_rc_malloc(n))) return NULL;
	memcpy(q, p, old);
	ws_rc_free(p);
	return q;
}

char *ws_rc_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = ws_rc_malloc(n);
	if (d) memcpy(d, s, n);
	return d;
}
#else   /* the GameCube has no MEM2 */
void *ws_rc_malloc(size_t n) { return malloc(n); }
void *ws_rc_calloc(size_t n, size_t size) { return calloc(n, size); }
void *ws_rc_realloc(void *p, size_t n) { return realloc(p, n); }
void ws_rc_free(void *p) { free(p); }
char *ws_rc_strdup(const char *s) { return strdup(s); }
#endif
