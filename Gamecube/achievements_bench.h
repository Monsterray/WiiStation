/* Test-only achievement evidence for the bench (ACHIEVEMENTS_BENCH). Without that macro,
 * every call is a no-op and the product build contains no bench code. */
#ifndef WIISTATION_ACHIEVEMENTS_BENCH_H
#define WIISTATION_ACHIEVEMENTS_BENCH_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ACHIEVEMENTS_BENCH
extern char achievementBenchRoot[256]; /* settings key AchievementBenchRoot */
void achievements_bench_isolate_cards(void);            /* after achievements_load in loadISO */
void achievements_bench_chain_end(FILE *f, int has_input); /* writes the achievement-bench lines */
int achievements_bench_hbc_return(void);                /* 1 = exit to HBC, do not power off */
#else
static inline void achievements_bench_isolate_cards(void) {}
static inline void achievements_bench_chain_end(FILE *f, int has_input) { (void)f; (void)has_input; }
static inline int achievements_bench_hbc_return(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif

#endif
