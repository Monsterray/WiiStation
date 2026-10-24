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

/* An achievement's type: the values RetroAchievements uses (rc_client.h) */
enum { ACH_TYPE_STANDARD = 0, ACH_TYPE_MISSABLE = 1, ACH_TYPE_PROGRESSION = 2, ACH_TYPE_WIN = 3 };

/* One unlock, for the toast (Gamecube/ach_toast.c). Strings live until the set is closed. */
typedef struct {
    const char *title, *desc, *game;   /* game: the set's Title= line, or "" */
    unsigned points, total_points;     /* 0 = the set gives none */
    int rarity_x100;                   /* share of players x 100 (812 = 8.12 %); -1 = unknown (Local) */
    unsigned earned, count;            /* the set's progress after this unlock */
    unsigned char type;                /* ACH_TYPE_* */
    unsigned char beaten, mastered;    /* this unlock beat the game / completed the set */
} achievement_notice_t;
extern void (*achievements_notify)(const achievement_notice_t *notice);   /* NULL: nobody listens */

#ifdef __cplusplus
}
#endif
#endif
