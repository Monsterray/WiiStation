/* rcheevos' memory (the parsed set, leaderboards, the server's JSON) in MEM2. The rcheevos
 * objects are compiled with -Dmalloc=ws_rc_malloc ... -Dstrdup=ws_rc_strdup (Makefile_Wii,
 * Makefile_Wii_Release), so every allocation and every free of theirs comes here and none of
 * theirs reaches libc. MEM1 is the emulator's: with CTR's Official set (117 achievements, 58
 * leaderboards) in MEM1, GNU Lightning's allocations ran short in the race and its optimizer
 * hung (bench Wii, 2026-10-24). The MEM2 heap locks itself (the HTTPS worker frees here too). */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef HW_RVL
#include <stdbool.h>
#include "../mem2_manager.h"

void *ws_rc_malloc(size_t n) { return _mem2_malloc(n); }
void *ws_rc_calloc(size_t n, size_t size) { return _mem2_calloc(n, size); }
void *ws_rc_realloc(void *p, size_t n) { return _mem2_realloc(p, n); }
void ws_rc_free(void *p) { _mem2_free(p); }
char *ws_rc_strdup(const char *s) { return _mem2_strdup(s); }
#else   /* the GameCube has no MEM2 */
void *ws_rc_malloc(size_t n) { return malloc(n); }
void *ws_rc_calloc(size_t n, size_t size) { return calloc(n, size); }
void *ws_rc_realloc(void *p, size_t n) { return realloc(p, n); }
void ws_rc_free(void *p) { free(p); }
char *ws_rc_strdup(const char *s) { return strdup(s); }
#endif
