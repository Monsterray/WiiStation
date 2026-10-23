/* WiiStation's achievement engine (achievements.c). All calls belong to the emulator thread. */
#ifndef WIISTATION_ACHIEVEMENTS_H
#define WIISTATION_ACHIEVEMENTS_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Setting Achievements */
enum { ACH_OFF = 0, ACH_LOCAL = 1, ACH_TEST = 2, ACH_OFFICIAL = 3 };
extern char achievementsEnabled; /* an ACH_* mode */
extern char achievementFile[256];
extern char achievementLog[256];

#define ACHIEVEMENTS_STATE_MAX (256 * 1024)
int achievements_load(const char *game_id);   /* the set count, 0 = none, -1 = refused */
void achievements_close(void);
void achievements_pause(void);
void achievements_reset(void);
void achievements_frame(void);
void achievements_flush(void); /* outside Execute(): pending unlocks to the journal */
uint32_t achievements_read_memory(uint32_t address, uint8_t *buffer, uint32_t size, void *unused);
unsigned achievements_count(void);
unsigned achievements_earned(void);
int achievements_get(unsigned i, const char **title); /* 1 earned, 0 not, -1 no such entry */
int achievements_state_save(void *data, unsigned size); /* NULL queries required size */
int achievements_state_load(const void *data, unsigned size); /* NULL resets progress */

#ifdef __cplusplus
}
#endif
#endif
