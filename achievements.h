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
extern char achievementImage[256];   /* the loaded image's path: Official mode hashes it */
extern char achievementUnofficial;    /* Official: the set's unpromoted achievements too (0/1) */
extern char achievementEncore;        /* Official: earned ones can be earned again this session (0/1) */
extern char achievementSpectator;     /* Official, hidden (test runs): evaluate, but send no unlock (0/1) */

#define ACHIEVEMENTS_STATE_MAX (256 * 1024)
int achievements_load(const char *game_id);   /* the set count, 0 = none, -1 = refused */
void achievements_close(void);
void achievements_pause(void);
void achievements_reset(void);
void achievements_frame(void);
void achievements_flush(void); /* outside Execute(): pending unlocks to the journal */
void achievements_idle(void);
int achievements_loading(void);               /* Official: the set is still on its way */
void achievements_wait_loaded(unsigned ms);   /* poll until it is in, at most ms */  /* menu and pause, once per frame (Official mode) */
void achievements_change_media(const char *image, const char *game_id);   /* a disc swap: the new disc open, its serial */
const char *achievements_status(void);   /* one line for the menus: what achievements do for this game */
uint32_t achievements_read_memory(uint32_t address, uint8_t *buffer, uint32_t size, void *unused);
unsigned achievements_count(void);    /* the loaded set's achievements, in either mode */
unsigned achievements_earned(void);   /* Local: the union of the players' unlocks (below) */
void achievements_list_refresh(void); /* the menu opens the list: Official mode reads it again */
int achievements_state_save(void *data, unsigned size); /* NULL queries required size */
int achievements_state_load(const void *data, unsigned size); /* NULL resets progress */

/* ---- the players (Local mode; Docs/MII_PLAN.md): a Mii earns its own unlocks ----
 *
 * The journal keeps one line per Mii that earned an achievement. Who is playing comes from
 * the hook: the Miis seated on a port whose pad is connected (Gamecube/ach_players.c). With
 * none, the player is Guest (a journal line without a key, as every line written before the
 * Players menu). The set's earned state is the union of the players' unlocks; an achievement
 * stays active while any player lacks it, and when it triggers only those get the line. */
#define ACH_PLAYERS_MAX 26     /* one per seat (PLAYERS_SEATS) */
#define ACH_KEY_LEN 16         /* a Mii's key: 16 hex characters */
typedef struct {
    char key[ACH_KEY_LEN + 1]; /* 16 lowercase hex characters */
    char name[32];             /* UTF-8; for the toast and the page */
} achievement_player_t;
/* Fills out (room for ACH_PLAYERS_MAX) with the players, in seat order; returns how many. NULL
 * or 0: Guest alone. Called at a game load and by achievements_players_changed(). */
extern unsigned (*achievements_players)(achievement_player_t *out);
/* The seats or the pads may have changed (the menu was open): asks the hook again and, when the
 * players differ, loads the set again for them. Call before the game goes on. */
void achievements_players_changed(void);
/* The players the page can show (Guest alone: 1, no name), and which one it shows. */
unsigned achievements_player_count(void);
const char *achievements_player_key(unsigned i);    /* "" for Guest */
const char *achievements_player_name(unsigned i);   /* "" for Guest */
unsigned achievements_player_earned(unsigned i);    /* that player's unlocks in the loaded set */
void achievements_view_player(unsigned i);          /* achievements_entry's earned flag is that player's */
unsigned achievements_viewed_player(void);
/* A Mii's unlocks in the loaded set (Local mode), also when it is not a player now; -1: no
 * set, or Official mode. For the Players menu. */
int achievements_earned_for(const char *key);

/* An achievement's type: the values RetroAchievements uses (rc_client.h) */
enum { ACH_TYPE_STANDARD = 0, ACH_TYPE_MISSABLE = 1, ACH_TYPE_PROGRESSION = 2, ACH_TYPE_WIN = 3 };

/* One achievement as the menus list it (achievements_entry). Strings live until the next
 * achievements_list_refresh or the set closes. */
typedef struct {
    const char *title, *desc;
    const char *badge;                 /* RetroAchievements badge name; "" in a local set */
    unsigned id, points;
    int rarity_x100;                   /* share of players x 100; -1 = unknown (Local) */
    unsigned char earned, type;        /* type: ACH_TYPE_* */
    unsigned char unofficial;          /* an unpromoted (Unofficial) achievement */
    unsigned char percent;             /* measured progress 0-100; 0 = none */
    char progress[24];                 /* measured progress as text ("12/40"); "" = none */
} achievement_entry_t;
int achievements_entry(unsigned i, achievement_entry_t *e);   /* 0, or -1: no such entry */
const char *achievements_presence(void);   /* rich presence (Official): what the player is doing; "" = none */

/* Leaderboards (Official mode; a local set has none). The scores of one board at a time are
 * fetched from the server when asked for. */
typedef struct { unsigned rank; char user[32], score[24]; } achievement_score_t;
unsigned achievements_board_count(void);
int achievements_board(unsigned i, const char **title, const char **desc);   /* 0, or -1: none */
/* Board i's best `*n` scores (in: room, out: how many) and the player's own (rank 0: none).
 * 1 ready, 0 being fetched (ask again), -1 failed. */
int achievements_board_scores(unsigned i, achievement_score_t *top, unsigned *n, achievement_score_t *mine);

/* What a notice is (achievement_notice_t.kind) */
enum {
    ACH_NOTICE_UNLOCK = 0,      /* an achievement unlocked: every field */
    ACH_NOTICE_GAME,            /* a set loaded: game, earned, count, points (earned), total_points, desc (the mode) */
    ACH_NOTICE_PROGRESS,        /* measured progress moved on: title, badge, progress, percent */
    ACH_NOTICE_CHALLENGE_ON,    /* an achievement is one step away (primed): id, title, badge */
    ACH_NOTICE_CHALLENGE_OFF,   /* that is over: id */
    ACH_NOTICE_WARNING          /* a notice about the set, not an achievement: title, desc (a black banner) */
};

/* One notice for the toast (Gamecube/ach_toast.c). Strings live until the set is closed. */
typedef struct {
    unsigned char kind;                /* ACH_NOTICE_* */
    unsigned id;
    const char *badge;                 /* RetroAchievements badge name, or "" */
    const char *progress;              /* ACH_NOTICE_PROGRESS: "12/40" */
    unsigned char percent;
    const char *title, *desc, *game;   /* game: the set's Title= line, or "" */
    unsigned points, total_points;     /* 0 = the set gives none */
    int rarity_x100;                   /* share of players x 100 (812 = 8.12 %); -1 = unknown (Local) */
    unsigned earned, count;            /* the set's progress after this unlock */
    unsigned char type;                /* ACH_TYPE_* */
    unsigned char beaten, mastered;    /* this unlock beat the game / completed the set */
    const char *who;                   /* Local: the Miis that earned it ("Kat", "Kat and Ollie", "4 players"); NULL or "": Guest */
} achievement_notice_t;
extern void (*achievements_notify)(const achievement_notice_t *notice);   /* NULL: nobody listens */

#ifdef __cplusplus
}
#endif
#endif
