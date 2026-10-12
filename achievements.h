/* WiiStation's local rcheevos engine. All calls belong to the emulator thread. */
#ifndef WIISTATION_ACHIEVEMENTS_H
#define WIISTATION_ACHIEVEMENTS_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

extern char achievementsEnabled; /* 0 = off, 1 = local definitions (no service awards) */
extern char achievementFile[256];
extern char achievementLog[256];

#define ACHIEVEMENTS_STATE_MAX (256 * 1024)
int achievements_load(const char *game_id);
void achievements_close(void);
void achievements_pause(void);
void achievements_reset(void);
void achievements_frame(void);
void achievements_flush(void); /* outside Execute(): pending local unlocks to disk */
uint32_t achievements_read_memory(uint32_t address, uint8_t *buffer, uint32_t size, void *unused);
unsigned achievements_count(void);
unsigned achievements_earned(void);
int achievements_state_save(void *data, unsigned size); /* NULL queries required size */
int achievements_state_load(const void *data, unsigned size); /* NULL resets progress */

#ifdef __cplusplus
}
#endif
#endif
