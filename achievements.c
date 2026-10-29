/* WiiStation's achievement engine: evaluates a definition file with upstream rcheevos.
 * GPL-2.0-or-later, like the surrounding WiiStation integration code.
 *
 * The flow, all on the emulator thread:
 *   achievements_load()  -- at game load: read the set file, parse it, restore earned ones
 *   achievements_frame() -- every emulated frame: rcheevos checks every condition
 *   event()              -- an achievement triggers: mark it earned, write it to the journal
 *   achievements_close() -- at shutdown or disc swap: write what is pending, free the set
 *
 * Modes (setting Achievements): Off, Custom (the user's own definition file; unlocks are saved in
 * a journal file; called Local before 5.19.0), Test (the same evaluation, but nothing is read
 * from or written to the journal, to try out a set), Official (RetroAchievements through
 * rc_client, achievements_official.c) and Local (RetroAchievements' set, nothing ever sent:
 * every player earns into the Local journal below).
 *
 * Players (Custom and Local mode, Docs/MII_PLAN.md): who is playing is a list of Miis from the hook
 * achievements_players, or Guest alone. Each achievement keeps one bit per player ("has"); the
 * set's earned state is the union, and rcheevos keeps an achievement active while any player
 * lacks it. The journal has a line per player that earned it.
 *
 * Official mode with Local players (Docs/MII_PLAN.md step 4c): the Miis other than the Official
 * one earn rc_client's set into the same kind of journal. achievements_official.c hands over the
 * set's entries (achievements_ra_*); the player table, the bits and the journal are Local mode's. */
#include "achievements.h"
#include "Gamecube/perf_prof.h"
#include "Gamecube/ach_progress.h"
#include "deps/rcheevos/include/rc_runtime.h"
#include "deps/rcheevos/src/util/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <strings.h>
#include <ctype.h>

/* The guest memory the conditions address: PS1 RAM, then the scratchpad after it */
#define PSX_RAM_BYTES 0x200000u
#define PSX_MAP_END (PSX_RAM_BYTES + 0x400u)   /* + the 1 KiB scratchpad */
#define MAX_ACHIEVEMENTS 128
#define MAX_RA_ENTRIES 256         /* a RetroAchievements set's entries for the Local players (malloc'd) */
#define MAX_SET_SIZE (64 * 1024)
#define MAX_CONDITION 2048
#define JOURNAL_MAX (1024 * 1024) /* unlock journal cap; load and append both fail closed past it */
#define STATE_HEADER 36 /* WAC1, 16-byte definition digest, 128 earned bits */

extern unsigned char *psxM, *psxH;
extern void SysPrintf(const char *fmt, ...);
char achievementsEnabled;   /* an ACH_* mode */
char achievementFile[256], achievementLog[256];
char achievementImage[256];
char achievementUnofficial, achievementEncore, achievementSpectator;
char achievementChallenge = 1;   /* the flashing "one step away" icons (Official and Local), default on */
void (*achievements_notify)(const achievement_notice_t *notice);

/* Official mode lives in achievements_official.c (Wii builds). Weak, so a build without it
 * (the GameCube, the host tests) links and Official mode loads nothing. */
int achievements_official_load(const char *image) __attribute__((weak));
void achievements_official_frame(void) __attribute__((weak));
void achievements_official_idle(void) __attribute__((weak));
void achievements_official_close(void) __attribute__((weak));
int achievements_official_loading(void) __attribute__((weak));
void achievements_official_wait(unsigned ms) __attribute__((weak));
void achievements_official_change_media(const char *image) __attribute__((weak));
const char *achievements_official_status(void) __attribute__((weak));
const char *achievements_official_set_id(void) __attribute__((weak));
const char *achievements_official_title(void) __attribute__((weak));
unsigned achievements_official_count(void) __attribute__((weak));
unsigned achievements_official_earned(void) __attribute__((weak));
int achievements_official_entry(unsigned i, achievement_entry_t *e) __attribute__((weak));
void achievements_official_list_refresh(void) __attribute__((weak));
const char *achievements_official_presence(void) __attribute__((weak));
void achievements_official_reset(void) __attribute__((weak));
int achievements_official_state_save(void *data, unsigned size) __attribute__((weak));
int achievements_official_state_load(const void *data, unsigned size) __attribute__((weak));
unsigned achievements_official_board_count(void) __attribute__((weak));
int achievements_official_board(unsigned i, const char **title, const char **desc) __attribute__((weak));
int achievements_official_board_scores(unsigned i, achievement_score_t *top, unsigned *n, achievement_score_t *mine)
    __attribute__((weak));
void achievements_official_players_changed(void) __attribute__((weak));
/* A RetroAchievements mode (Official or Local) is on: rc_client plays through achievements_official.c */
#define RA_MODE() (achievementsEnabled == ACH_OFFICIAL || achievementsEnabled == ACH_LOCAL)
/* ... and this build has its hook `fn` */
#define RA(fn) (RA_MODE() && achievements_official_##fn)
/* Official mode only (the account's own things: rich presence, leaderboards) */
#define OFFICIAL(fn) (achievementsEnabled == ACH_OFFICIAL && achievements_official_##fn)

static rc_runtime_t runtime;
typedef struct {
    uint32_t id;
    char title[80];
    char desc[96];           /* optional */
    unsigned short points;   /* optional; 0 = none */
    unsigned char type;      /* ACH_TYPE_*, optional */
    uint32_t has;            /* the players (bit = index in player[]) that earned it: this session, the journal or a state */
    uint32_t written;        /* of those, the ones already in the journal */
    unsigned char measured;  /* the measured step (percent / 10) last seen, ACH_STEP_NONE before a reading: the pop-up rule, Gamecube/ach_progress.h */
    unsigned char unofficial;   /* RetroAchievements: an unpromoted achievement */
    int rarity_x100;         /* RetroAchievements: the share of players x 100; -1 = unknown */
    char badge[16];          /* RetroAchievements: the badge name */
} ach_entry_t;
static ach_entry_t static_entries[MAX_ACHIEVEMENTS];
static ach_entry_t *entries = static_entries;   /* a RetroAchievements set's own table while its Local side plays */
static unsigned count;
static char set_title[48];              /* the set's Title= line: the game's name for the toast */
#define MAX_SET_DISCS 8
static char set_games[MAX_SET_DISCS][10]; /* the set's Game= serials: every disc of the game */
static unsigned set_game_count;
static uint32_t beaten_seen, mastered_seen;  /* per player: already true, so no second Beaten or Mastered toast */
static achievement_player_t player[ACH_PLAYERS_MAX];   /* who plays: the seated Miis, or Guest alone (key "") */
static unsigned players;       /* how many of player[] */
static uint32_t all_players;   /* a bit for each */
static unsigned view;          /* the player the page shows */
static char load_game[32];     /* the game id of the loaded set: a new set of players loads it again */
/* Official mode with Local players: the page's first player is the Official side (ra_first = 1)
 * unless its login failed (0); ra_local: the set's Local side plays, with entries from rc_client */
static int ra_local;
static unsigned ra_first;
static char ra_off_key[ACH_KEY_LEN + 1], ra_off_name[32];   /* the Official Mii, "" for this Wii's account */
static achievement_player_t ra_plan_list[ACH_PLAYERS_MAX];  /* the Local players asked for at the game load */
static unsigned ra_plan_n;
static char ra_note_key[ACH_KEY_LEN + 1], ra_note[64];
unsigned (*achievements_players)(achievement_player_t *out);
int (*achievements_player_lookup)(const char *key, char *name, unsigned size);
static int active, running;   /* a set is loaded; evaluation is allowed (not during BIOS boot) */
static unsigned settle;       /* frames left of the settling time: progress readings only set the baseline (ach_progress.h) */
static unsigned char digest[16];   /* MD5 of the definition file: the set's identity */
static char digest_text[33], log_path[256];

/* rcheevos reads guest memory through this: raw little-endian bytes, clipped to the map */
uint32_t achievements_read_memory(uint32_t address, uint8_t *buffer, uint32_t size, void *unused)
{
    uint32_t n, done = 0;
    (void)unused;
    if (!buffer || !psxM || !psxH || address >= PSX_MAP_END)
        return 0;
    if (size > PSX_MAP_END - address)
        size = PSX_MAP_END - address;
    if (address < PSX_RAM_BYTES) {
        n = PSX_RAM_BYTES - address;
        if (n > size) n = size;
        memcpy(buffer, psxM + address, n);
        address += n;
        done = n;
    }
    if (done < size)
        memcpy(buffer + done, psxH + address - PSX_RAM_BYTES, size - done);
    return size;
}

/* The union of the players' unlocks: the set's state in the game */
static unsigned local_earned(void)
{
    unsigned i, n = 0;
    for (i = 0; i < count; ++i) n += (entries[i].has & all_players) != 0;
    return n;
}

/* Every player has it: rcheevos stops checking it */
static int done(unsigned i) { return (entries[i].has & all_players) == all_players; }

static unsigned player_earned(unsigned p)
{
    unsigned i, n = 0;
    for (i = 0; i < count; ++i) n += (entries[i].has >> p) & 1;
    return n;
}

static int is_key(const char *s)
{
    unsigned i;
    for (i = 0; i < ACH_KEY_LEN; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

/* The players now: the hook's Miis (valid, once each), else Guest alone. Test mode has no
 * players: nothing is kept. Returns how many, in out (room for ACH_PLAYERS_MAX). */
static unsigned collect_players(achievement_player_t *out)
{
    achievement_player_t got[ACH_PLAYERS_MAX];
    unsigned n = 0, i, j, have = 0;
    if (achievementsEnabled == ACH_CUSTOM && achievements_players) n = achievements_players(got);
    if (n > ACH_PLAYERS_MAX) n = ACH_PLAYERS_MAX;
    memset(out, 0, sizeof(*out) * ACH_PLAYERS_MAX);
    for (i = 0; i < n; ++i) {
        got[i].key[ACH_KEY_LEN] = 0;
        got[i].name[sizeof got[i].name - 1] = 0;
        if (!is_key(got[i].key)) continue;
        for (j = 0; j < have && strcmp(out[j].key, got[i].key); ++j) ;
        if (j < have) continue;
        out[have++] = got[i];
    }
    return have ? have : 1;   /* Guest alone: one player with the key "" */
}

static void pick_players(void)
{
    players = collect_players(player);
    all_players = (1u << players) - 1;
    if (view >= players) view = 0;
}

/* The table index of a journal key ("" = Guest), or -1 */
static int player_of(const char *key)
{
    unsigned p;
    for (p = 0; p < players; ++p)
        if (!strcmp(player[p].key, key)) return (int)p;
    return -1;
}

/* The page shows rc_client's own list: not for a Local player of the same set */
static int official_view(void) { return OFFICIAL(count) && !(ra_local && view >= ra_first); }
/* The shown player's index in player[] (Local mode, or the Local side of Official mode) */
static unsigned local_view(void) { return view >= ra_first ? view - ra_first : 0; }

unsigned achievements_count(void)
{
    if (official_view()) return achievements_official_count();
    return active || ra_local ? count : 0;
}

unsigned achievements_earned(void)
{
    if (OFFICIAL(earned)) return achievements_official_earned();   /* Local mode: the union of its players, as Custom */
    return local_earned();
}

/* ---- the players, for the menus ---- */

unsigned achievements_player_count(void)
{
    if (ra_local) return ra_first + players;
    return OFFICIAL(count) || !active ? 1 : players;   /* Local mode's players are ra_local's (above) */
}
/* Official mode, no Local player: the page is the Official side alone, and it names who plays it
 * (the Mii the account is linked to, else this Wii's account), not Guest */
static int official_alone(void) { return OFFICIAL(count) && !ra_local; }
const char *achievements_player_key(unsigned i)
{
    if (official_alone()) return i == 0 ? ra_off_key : "";
    if (ra_local) return i < ra_first ? ra_off_key : i - ra_first < players ? player[i - ra_first].key : "";
    return active && !OFFICIAL(count) && i < players ? player[i].key : "";
}
const char *achievements_player_name(unsigned i)
{
    if (official_alone()) return i == 0 ? ra_off_name : "";
    if (ra_local) return i < ra_first ? ra_off_name : i - ra_first < players ? player[i - ra_first].name : "";
    return active && !OFFICIAL(count) && i < players ? player[i].name : "";
}
unsigned achievements_player_earned(unsigned i)
{
    if (ra_local) return i < ra_first ? (achievements_official_earned ? achievements_official_earned() : 0) : i - ra_first < players ? player_earned(i - ra_first) : 0;
    if (OFFICIAL(earned)) return achievements_official_earned();
    return active && i < players ? player_earned(i) : 0;
}
void achievements_view_player(unsigned i) { view = i < achievements_player_count() ? i : 0; }
unsigned achievements_viewed_player(void) { return view < achievements_player_count() ? view : 0; }
const char *achievements_player_note(unsigned i)
{
    return ra_local && i >= ra_first && i - ra_first < players && ra_note[0] && !strcmp(player[i - ra_first].key, ra_note_key)
               ? ra_note : "";
}

const char *achievements_title(void)
{
    if (RA(title)) return achievements_official_title();
    return active ? set_title : "";
}

const char *achievements_set_id(void)
{
    static char id[ACH_SET_ID_MAX];
    if (RA(set_id)) return achievements_official_set_id();
    if (!active || achievementsEnabled != ACH_CUSTOM) return "";   /* Test mode keeps nothing, so no pictures */
    snprintf(id, sizeof id, "%.16s", digest_text);
    return id;
}

void achievements_list_refresh(void)
{
    if (RA(list_refresh)) achievements_official_list_refresh();
}

/* For the menu's list: the i-th achievement, with its measured progress ("12/40") */
int achievements_entry(unsigned i, achievement_entry_t *e)
{
    unsigned value = 0, target = 0;
    if (official_view() && achievements_official_entry) return achievements_official_entry(i, e);
    if (!(active || ra_local) || i >= count) return -1;
    memset(e, 0, sizeof *e);
    e->title = entries[i].title;
    e->desc = entries[i].desc;
    e->badge = entries[i].badge;
    e->id = entries[i].id;
    e->points = entries[i].points;
    e->rarity_x100 = ra_local ? entries[i].rarity_x100 : -1;
    e->earned = (entries[i].has >> local_view()) & 1;   /* the shown player's */
    e->type = entries[i].type;
    e->unofficial = entries[i].unofficial;
    if (active && !e->earned && rc_runtime_get_achievement_measured(&runtime, e->id, &value, &target) && target) {
        rc_runtime_format_achievement_measured(&runtime, e->id, e->progress, sizeof e->progress);
        e->percent = value >= target ? 100 : (unsigned char)((unsigned long long)value * 100 / target);
    }
    return 0;
}

const char *achievements_presence(void)
{
    return OFFICIAL(presence) ? achievements_official_presence() : "";
}

unsigned achievements_board_count(void)
{
    return OFFICIAL(board_count) ? achievements_official_board_count() : 0;
}

int achievements_board(unsigned i, const char **title, const char **desc)
{
    return OFFICIAL(board) ? achievements_official_board(i, title, desc) : -1;
}

int achievements_board_scores(unsigned i, achievement_score_t *top, unsigned *n, achievement_score_t *mine)
{
    return OFFICIAL(board_scores) ? achievements_official_board_scores(i, top, n, mine) : -1;
}

/* ---- the unlock journal: "<set digest> <id> <player key> <title>" per player that earned it ----
 * A line without a key ("<set digest> <id> <title>": every journal written before the Players
 * menu) is Guest's. Guest's new lines have none either, unless the title itself starts like a
 * key: then "-" stands where the key goes. */

/* The title starts with a word that reads as a key */
static int title_is_key_like(const char *title)
{
    return strlen(title) >= ACH_KEY_LEN && is_key(title) && (!title[ACH_KEY_LEN] || title[ACH_KEY_LEN] == ' ');
}

/* Append one unlock record for player p. Past JOURNAL_MAX, the next load would refuse the
 * whole journal, so refuse the append too: fail closed, never grow the file past the cap. */
static int journal_append(unsigned index, unsigned p)
{
    char line[256];
    FILE *f;
    long size;
    const char *key = player[p].key;
    int len = key[0] ? snprintf(line, sizeof line, "%s %u %s %s\n", digest_text, (unsigned)entries[index].id,
                                key, entries[index].title)
                     : snprintf(line, sizeof line, title_is_key_like(entries[index].title) ? "%s %u - %s\n" : "%s %u %s\n",
                                digest_text, (unsigned)entries[index].id, entries[index].title);
    int ok;
    if (len < 0 || len >= (int)sizeof line) return 0;
    f = fopen(log_path, "a+b");
    if (!f) {
        SysPrintf("Achievements: cannot append unlock log %s\n", log_path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || size + len + 1 > JOURNAL_MAX) {
        fclose(f);
        SysPrintf("Achievements: unlock log full or unreadable, not appending %s\n", log_path);
        return 0;
    }
    /* A power loss can leave the last record cut short, with no newline. Start on a fresh
     * line, or this record would be glued to the fragment and lost with it. */
    ok = 1;
    if (size > 0 && (fseek(f, -1, SEEK_END) || fgetc(f) != '\n'))
        ok = fseek(f, 0, SEEK_END) == 0 && fputc('\n', f) == '\n';
    else
        fseek(f, 0, SEEK_END);   /* "a" writes at the end anyway; a seek between a read and a write is required */
    ok = ok && fwrite(line, 1, (size_t)len, f) == (size_t)len;
    if (fclose(f) != 0) ok = 0;
    if (!ok) SysPrintf("Achievements: unlock log write failed\n");
    return ok;
}

/* Calls fn(ctx, entry, key) for every whole record of this set in the journal: the entry it
 * names and the player's key ("" = Guest). Only whole records count: a line cut short by a
 * power loss ("<digest> 1" of "<digest> 12 Twelve") must not restore id 1. Returns 0 when
 * the journal is too large: reading part of it would earn the rest again and append
 * duplicates, so the caller refuses the whole set. */
static int read_journal(void (*fn)(void *ctx, unsigned entry, const char *key), void *ctx)
{
    char record[256], hash[33], key[ACH_KEY_LEN + 1];
    unsigned saved_id, i;
    long size;
    FILE *f = fopen(log_path, "r");
    if (!f) return 1;   /* no journal yet */
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || size > JOURNAL_MAX || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return 0;
    }
    while (fgets(record, sizeof(record), f)) {
        size_t len = strlen(record);
        int end = 0;
        const char *rest;
        if (!len || record[len - 1] != '\n') continue;
        if (sscanf(record, "%32s %u%n", hash, &saved_id, &end) != 2 || record[end] != ' ' ||
            strcmp(hash, digest_text)) continue;
        rest = record + end + 1;
        key[0] = 0;
        if (strlen(rest) > ACH_KEY_LEN && is_key(rest) && rest[ACH_KEY_LEN] == ' ') {
            memcpy(key, rest, ACH_KEY_LEN);
            key[ACH_KEY_LEN] = 0;
        }
        for (i = 0; i < count; ++i)
            if (entries[i].id == saved_id) fn(ctx, i, key);
    }
    fclose(f);
    return 1;
}

static void restore_one(void *ctx, unsigned entry, const char *key)
{
    int p = player_of(key);
    (void)ctx;
    if (p >= 0) entries[entry].has |= entries[entry].written |= 1u << p;
}

/* Marks earned, for each player, what the journal records. 0: the journal is too large. */
static int restore_journal(void) { return read_journal(restore_one, NULL); }

struct tally { const char *key; unsigned char seen[MAX_ACHIEVEMENTS]; };
static void tally_one(void *ctx, unsigned entry, const char *key)
{
    struct tally *t = ctx;
    if (!strcmp(key, t->key)) t->seen[entry] = 1;
}

/* A Mii's unlocks in the loaded set, a player or not */
int achievements_earned_for(const char *key)
{
    struct tally t;
    unsigned i, n = 0;
    int p;
    if (!(active || (ra_local && achievementsEnabled == ACH_LOCAL)) || (achievementsEnabled != ACH_CUSTOM && achievementsEnabled != ACH_LOCAL) || !key)
        return -1;
    if ((p = player_of(key)) >= 0) return (int)player_earned((unsigned)p);
    memset(&t, 0, sizeof t);
    t.key = key;
    if (!read_journal(tally_one, &t)) return -1;
    for (i = 0; i < count; ++i) n += t.seen[i];
    return (int)n;
}

void achievements_flush(void)
{
    unsigned i, p;
    if (!(active || ra_local) || achievementsEnabled == ACH_TEST) return;   /* Test mode keeps no record */
    for (i = 0; i < count; ++i)
        for (p = 0; p < players; ++p) {
            if (!((entries[i].has & ~entries[i].written) >> p & 1)) continue;
            if (!journal_append(i, p)) return; /* retry at the next menu/state boundary, never per frame */
            entries[i].written |= 1u << p;
            SysPrintf("Achievement unlocked: %u %s%s%.8s\n", (unsigned)entries[i].id, entries[i].title,
                      player[p].key[0] ? " by " : "", player[p].key);   /* a key's first 8 characters, never a name */
        }
}

/* ---- evaluation ---- */

static int valid_address(uint32_t address) { return address < PSX_MAP_END; }

/* Beaten, as RetroAchievements counts it: every Progression achievement earned, and a Win
 * earned if the set has any. A set with neither type cannot be beaten. */
static int is_beaten(unsigned p)
{
    unsigned i;
    int typed = 0, wins = 0, won = 0;
    for (i = 0; i < count; ++i) {
        int has = (int)((entries[i].has >> p) & 1);
        if (entries[i].type == ACH_TYPE_PROGRESSION) {
            typed = 1;
            if (!has) return 0;
        } else if (entries[i].type == ACH_TYPE_WIN) {
            typed = wins = 1;
            won |= has;
        }
    }
    return typed && (!wins || won);
}

static int is_mastered(unsigned p) { return count && player_earned(p) == count; }

/* The players for whom the game is beaten, or the set mastered, now (a bit each) */
static uint32_t beaten_now(void)
{
    uint32_t m = 0;
    unsigned p;
    for (p = 0; p < players; ++p) m |= (uint32_t)is_beaten(p) << p;
    return m;
}

static uint32_t mastered_now(void)
{
    uint32_t m = 0;
    unsigned p;
    for (p = 0; p < players; ++p) m |= (uint32_t)is_mastered(p) << p;
    return m;
}

/* "Kat", "Kat and Ollie", "Kat, Ollie and Rina", "4 players"; "" for Guest */
static const char *who_text(uint32_t mask)
{
    static char text[96];
    const char *names[3] = { "", "", "" };
    unsigned p, n = 0;
    for (p = 0; p < players; ++p)
        if ((mask >> p & 1) && player[p].key[0]) {
            if (n < 3) names[n] = player[p].name[0] ? player[p].name : "Mii";
            ++n;
        }
    if (!n) return "";
    if (n == 1) snprintf(text, sizeof text, "%s", names[0]);
    else if (n == 2) snprintf(text, sizeof text, "%s and %s", names[0], names[1]);
    else if (n == 3) snprintf(text, sizeof text, "%s, %s and %s", names[0], names[1], names[2]);
    else snprintf(text, sizeof text, "%u players", n);
    return text;
}

/* The players in mask earned it: who they are, their progress (the best of them), and whether
 * it beat the game or completed the set for one of them (once for each) */
static void who_fields(achievement_notice_t *n, uint32_t mask)
{
    unsigned p;
    uint32_t beaten = beaten_now(), mastered = mastered_now();
    n->beaten = (beaten & mask & ~beaten_seen) != 0;
    n->mastered = (mastered & mask & ~mastered_seen) != 0;
    beaten_seen |= beaten & mask;
    mastered_seen |= mastered & mask;
    n->who = who_text(mask);
    n->earned = 0;
    for (p = 0; p < players; ++p)
        if ((mask >> p & 1) && player_earned(p) > n->earned) n->earned = player_earned(p);
    n->count = count;
}

/* Tell the toast about a new unlock by the players in mask */
static void notify(unsigned i, uint32_t mask)
{
    achievement_notice_t n;
    unsigned k;
    memset(&n, 0, sizeof n);
    who_fields(&n, mask);
    if (!achievements_notify) return;
    n.id = entries[i].id;
    n.badge = entries[i].badge;
    n.title = entries[i].title;
    n.desc = entries[i].desc;
    n.game = set_title;
    n.points = entries[i].points;
    for (k = 0; k < count; ++k) n.total_points += entries[k].points;
    n.rarity_x100 = -1;   /* a Local set has no player counts */
    n.type = entries[i].type;
    achievements_notify(&n);
}

/* A challenge (primed) or measured progress, for the small indicators */
static void indicator(unsigned char kind, unsigned i)
{
    achievement_notice_t n;
    char progress[24] = "";
    unsigned value = 0, target = 0;
    if (!achievements_notify) return;
    memset(&n, 0, sizeof n);
    n.kind = kind;
    n.id = entries[i].id;
    n.title = entries[i].title;
    n.badge = "";
    n.rarity_x100 = -1;
    if (kind == ACH_NOTICE_PROGRESS) {
        if (!rc_runtime_get_achievement_measured(&runtime, n.id, &value, &target) || !target || value >= target)
            return;   /* none, or done: the unlock says it */
        n.percent = (unsigned char)((unsigned long long)value * 100 / target);
        if (!ach_step_update(&entries[i].measured, n.percent) || settle)
            return;   /* only a new 10 % step; the first reading after a load, reset or state load is the baseline */
        rc_runtime_format_achievement_measured(&runtime, n.id, progress, sizeof progress);
        n.progress = progress;
    }
    achievements_notify(&n);
}

/* The settling time is over: every measured achievement's step now is its baseline, so the
 * first change afterwards counts (rc_runtime sends no event for a first reading) */
static void progress_seed(void)
{
    unsigned i, value, target;
    for (i = 0; i < count; ++i)
        if (!done(i) && rc_runtime_get_achievement_measured(&runtime, entries[i].id, &value, &target) && target && value < target)
            ach_step_update(&entries[i].measured, (unsigned)((unsigned long long)value * 100 / target));
}

static void event(const rc_runtime_event_t *e)
{
    unsigned i;
    if (e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED || e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED ||
        e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED) {
        for (i = 0; i < count; ++i)
            if (entries[i].id == e->id && !done(i))
                indicator(e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED ? ACH_NOTICE_CHALLENGE_ON :
                          e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED ? ACH_NOTICE_CHALLENGE_OFF : ACH_NOTICE_PROGRESS, i);
        return;
    }
    if (e->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) return;
    for (i = 0; i < count; ++i)
        if (entries[i].id == e->id && !done(i)) {
            uint32_t lacking = all_players & ~entries[i].has;   /* only they get it */
            entries[i].has |= lacking;
            notify(i, lacking);
        }
    achievements_flush(); /* a trigger is rare: one short journal line per player, written at once */
}

/* An achievement every player has stays earned: rcheevos stops checking it */
static void keep_earned(void)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        if (done(i)) rc_runtime_deactivate_achievement(&runtime, entries[i].id);
}

void achievements_pause(void) { running = 0; }
void achievements_reset(void)
{
    if (RA(reset)) {
        achievements_official_reset();
        return;
    }
    if (!active) return;
    rc_runtime_reset(&runtime);
    for (unsigned i = 0; i < count; ++i) entries[i].measured = ACH_STEP_NONE;   /* progress starts again: the next reading is the baseline */
    settle = ACH_SETTLE_FRAMES;
    keep_earned();
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per reset */
    running = 1;
}

void achievements_frame(void)
{
    int official = RA_MODE();
    if (official ? !achievements_official_frame : (!active || !running || !achievementsEnabled)) return;
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long started = perf_now_ticks();
#endif
    if (official)
        achievements_official_frame();   /* rc_client: completions, then evaluation */
    else {
        rc_runtime_do_frame(&runtime, event, achievements_read_memory, NULL, NULL);
        if (settle && --settle == 0) progress_seed();
    }
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long elapsed = perf_now_ticks() - started;
    PERF_ADD(achievement_ticks, elapsed);
    PERF_MAX(achievement_max_ticks, elapsed);
    PERF_INC(achievement_frames);
#endif
}

/* Official mode: the set is still loading (the menu waits for it before a game starts) */
int achievements_loading(void)
{
    return RA_MODE() && achievements_official_loading && achievements_official_loading();
}

void achievements_wait_loaded(unsigned ms)
{
    if (RA_MODE() && achievements_official_wait) achievements_official_wait(ms);
}

/* The menu and the pause, once per frame: Official mode's completions and rc_client_idle */
void achievements_idle(void)
{
    if (RA_MODE() && achievements_official_idle) achievements_official_idle();
}

/* What achievements do for the loaded game, for the menus (ISO Info, the list): at most 31
 * characters, the width of a readout (OptionsFrame VALUE_LEN) */
const char *achievements_status(void)
{
    static char line[96];
    size_t n = strlen(achievementImage);
    if (n > 4 && !strcasecmp(achievementImage + n - 4, ".exe"))
        return "None: a .exe has no disc";
    switch (achievementsEnabled) {
    case ACH_OFF:
        return "Off (Achievements)";
    case ACH_OFFICIAL:
    case ACH_LOCAL:
        return achievements_official_status ? achievements_official_status() : "Official: not in this build";
    default:
        if (!active)
            return "No set (AchievementFile)";
        snprintf(line, sizeof line, "%u of %u earned (%s)", achievements_earned(), count,
                 achievementsEnabled == ACH_TEST ? "Test" : "Custom");
        return line;
    }
}

/* A disc serial the set's Game= lines name */
static int set_has_game(const char *game_id)
{
    unsigned i;
    for (i = 0; game_id && i < set_game_count; ++i)
        if (!strcasecmp(set_games[i], game_id)) return 1;
    return 0;
}

/* A disc swap, the new disc open (game_id: its serial). Official mode checks the disc belongs
 * to the loaded game and goes on; a Local or Test set goes on when one of its Game= lines names
 * the disc, and otherwise closes until the next full game load. */
void achievements_change_media(const char *image, const char *game_id)
{
    snprintf(achievementImage, sizeof achievementImage, "%s", image ? image : "");
    if (RA_MODE() && achievements_official_change_media)
        achievements_official_change_media(image);
    else if (active && set_has_game(game_id)) {
        unsigned i;
        SysPrintf("Achievements: disc %s is in the set, it goes on\n", game_id);
        for (i = 0; i < count; ++i) entries[i].measured = ACH_STEP_NONE;   /* another disc, other memory: a new baseline */
        settle = ACH_SETTLE_FRAMES;
    }
    else {
        if (active) SysPrintf("Achievements: disc %s is not in the set, it closes\n", game_id ? game_id : "?");
        achievements_close();
    }
}

/* ---- Official mode with Local players (Docs/MII_PLAN.md step 4c) ----
 * The Official side lives in achievements_official.c. Here: the Miis that earn Local next to it,
 * and rc_client's set as this file's entries (a malloc'd table, sorted by id), so the journal, the
 * bits, the toast's names and the per-player page are the Local mode's. */

static void ra_drop(void)
{
    if (entries != static_entries) free(entries);
    entries = static_entries;
    ra_local = ra_first = 0;
    ra_note[0] = ra_note_key[0] = 0;
}

/* guest: Local mode, no Official side: with no Mii seated, Guest alone is the one player */
static unsigned ra_plan_build(const char *official_key, int guest)
{
    achievement_player_t got[ACH_PLAYERS_MAX];
    unsigned n = 0, i, j;
    int named = 0;
    ra_plan_n = 0;
    snprintf(ra_off_key, sizeof ra_off_key, "%s", official_key ? official_key : "");
    for (i = 0; ra_off_key[i]; ++i) ra_off_key[i] = (char)tolower((unsigned char)ra_off_key[i]);
    snprintf(ra_off_name, sizeof ra_off_name, "%s", ra_off_key[0] ? "Mii" : "This Wii");
    if (achievements_players) n = achievements_players(got);
    if (n > ACH_PLAYERS_MAX) n = ACH_PLAYERS_MAX;
    for (i = 0; i < n; ++i) {
        got[i].key[ACH_KEY_LEN] = 0;
        got[i].name[sizeof got[i].name - 1] = 0;
        if (!is_key(got[i].key)) continue;
        if (!strcmp(got[i].key, ra_off_key)) {   /* the Official Mii: its name is for the page */
            if (got[i].name[0]) {
                snprintf(ra_off_name, sizeof ra_off_name, "%s", got[i].name);
                named = 1;
            }
            continue;
        }
        for (j = 0; j < ra_plan_n && strcmp(ra_plan_list[j].key, got[i].key); ++j) ;
        if (j < ra_plan_n) continue;
        ra_plan_list[ra_plan_n++] = got[i];
    }
    if (guest && !ra_plan_n) {   /* Guest earns when no Mii does: a player with no key (journal lines without one) */
        memset(&ra_plan_list[0], 0, sizeof ra_plan_list[0]);
        ra_plan_n = 1;
    }
    if (ra_off_key[0] && !named && achievements_player_lookup) {   /* seated, but its pad is not on a connected seat: still its name */
        char name[sizeof ra_off_name];
        if (achievements_player_lookup(ra_off_key, name, sizeof name) == 1 && name[0])
            snprintf(ra_off_name, sizeof ra_off_name, "%s", name);
    }
    return ra_plan_n;
}

unsigned achievements_ra_plan(const char *official_key) { return ra_plan_build(official_key, 0); }
unsigned achievements_ra_plan_all(void) { return ra_plan_build("", 1); }

/* The seats or the pads changed (the menu was open): 1 when the players of the plan now are not
 * the Local side's. The old players' unlocks are written first. */
int achievements_ra_replan(const char *official_key, int all)
{
    unsigned n, p;
    int same;
    if (!ra_local) return 0;
    n = all ? achievements_ra_plan_all() : achievements_ra_plan(official_key);
    same = n == players;
    for (p = 0; same && p < n; ++p) same = !strcmp(ra_plan_list[p].key, player[p].key);
    if (same) {
        for (p = 0; p < n; ++p) player[p] = ra_plan_list[p];   /* the same people: their names, perhaps */
        return 0;
    }
    achievements_flush();
    return 1;
}

unsigned achievements_ra_begin(const char *digest, const char *journal, int official_plays, const char *official_name,
                               const char *note_key, const char *note)
{
    ach_entry_t *table;
    if (!ra_plan_n || !digest || !journal) return 0;
    if (!(table = calloc(MAX_RA_ENTRIES, sizeof *table))) {
        SysPrintf("Achievements: out of memory for the Local players of a RetroAchievements set\n");
        return 0;
    }
    ra_drop();
    entries = table;
    count = 0;
    memcpy(player, ra_plan_list, sizeof(*player) * ra_plan_n);
    players = ra_plan_n;
    all_players = (1u << players) - 1;
    view = 0;
    ra_first = official_plays ? 1 : 0;
    if (official_name && *official_name) snprintf(ra_off_name, sizeof ra_off_name, "%s", official_name);
    snprintf(digest_text, sizeof digest_text, "%s", digest);
    snprintf(log_path, sizeof log_path, "%s", journal);
    snprintf(ra_note_key, sizeof ra_note_key, "%s", note_key ? note_key : "");
    snprintf(ra_note, sizeof ra_note, "%s", note ? note : "");
    ra_local = 1;
    return players;
}

void achievements_ra_add(unsigned id, const char *title, const char *desc, const char *badge, unsigned points,
                         int rarity_x100, unsigned char type, unsigned char unofficial)
{
    ach_entry_t *e;
    if (!ra_local) return;
    if (count >= MAX_RA_ENTRIES) {
        SysPrintf("Achievements: more than %u achievements: the Local players leave out the rest\n", MAX_RA_ENTRIES);
        return;
    }
    e = &entries[count++];
    e->id = id;
    snprintf(e->title, sizeof e->title, "%s", title ? title : "");
    snprintf(e->desc, sizeof e->desc, "%s", desc ? desc : "");
    snprintf(e->badge, sizeof e->badge, "%s", badge ? badge : "");
    e->points = (unsigned short)(points > 1000 ? 1000 : points);
    e->rarity_x100 = rarity_x100;
    e->type = type <= ACH_TYPE_WIN ? type : ACH_TYPE_STANDARD;
    e->unofficial = unofficial;
}

static int cmp_entry(const void *a, const void *b)
{
    uint32_t x = ((const ach_entry_t *)a)->id, y = ((const ach_entry_t *)b)->id;
    return x < y ? -1 : x > y;
}

int achievements_ra_end(void)
{
    unsigned p;
    if (!ra_local) return 0;
    qsort(entries, count, sizeof *entries, cmp_entry);
    if (!restore_journal()) {
        SysPrintf("Achievements: unlock log too large or unreadable, no Local players for this set: %s\n", log_path);
        ra_drop();
        count = 0;
        players = all_players = 0;
        return 0;
    }
    beaten_seen = beaten_now();     /* earned before this session: no toast for it */
    mastered_seen = mastered_now();
    SysPrintf("Achievements: %u Local player%s on the RetroAchievements set (%u achievements)\n", players,
              players == 1 ? "" : "s", count);
    for (p = 0; p < players; ++p)   /* a key's first 8 characters, never a name */
        SysPrintf("Achievements: Local player %u is %s%.8s (%u earned)\n", p + 1, player[p].key[0] ? "" : "Guest", player[p].key, player_earned(p));
    return 1;
}

unsigned achievements_ra_trigger(unsigned id, achievement_notice_t *n)
{
    ach_entry_t key, *e;
    uint32_t lacking;
    if (!ra_local) return 0;
    key.id = id;
    e = bsearch(&key, entries, count, sizeof *entries, cmp_entry);
    if (!e || !(lacking = all_players & ~e->has)) return 0;
    e->has |= lacking;
    who_fields(n, lacking);
    achievements_flush();   /* one short journal line per Local player, at once */
    return lacking;
}

int achievements_ra_active(void) { return ra_local; }

void achievements_ra_summary(unsigned *earned, unsigned *total, unsigned *points, unsigned *total_points)
{
    unsigned i, pts = 0, all = 0;
    for (i = 0; ra_local && i < count; ++i) {
        all += entries[i].points;
        if (entries[i].has & all_players) pts += entries[i].points;
    }
    if (earned) *earned = ra_local ? local_earned() : 0;
    if (total) *total = ra_local ? count : 0;
    if (points) *points = pts;
    if (total_points) *total_points = all;
}

void achievements_close(void)
{
    if (achievements_official_close) achievements_official_close();
    achievements_flush();
    if (active) rc_runtime_destroy(&runtime);
    active = running = 0;
    ra_drop();
    count = 0;
    memset(static_entries, 0, sizeof static_entries);
    set_title[0] = 0;
    set_game_count = 0;
    players = all_players = 0;
}

/* ---- loading a set ---- */

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
    *end = 0;
    return s;
}

/* The whole definition file as one string (at most MAX_SET_SIZE, no NUL bytes), its MD5 in
 * digest/digest_text, and the journal path in log_path. NULL when any of that fails. */
static char *read_set(void)
{
    FILE *f;
    char *data;
    long size;
    md5_state_t md5;
    unsigned i;
    int n;
    f = fopen(achievementFile, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || size > MAX_SET_SIZE || fseek(f, 0, SEEK_SET) ||
        !(data = malloc((size_t)size + 1))) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size || memchr(data, 0, (size_t)size)) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[size] = 0;
    md5_init(&md5);
    md5_append(&md5, (const unsigned char *)data, (int)size);
    md5_finish(&md5, digest);
    for (i = 0; i < 16; ++i) snprintf(digest_text + 2 * i, 3, "%02x", digest[i]);
    /* the journal: AchievementLog, or the definition file's name + ".unlocks" -- never the set itself */
    n = achievementLog[0] ? snprintf(log_path, sizeof log_path, "%s", achievementLog)
                          : snprintf(log_path, sizeof log_path, "%s.unlocks", achievementFile);
    if (n < 0 || n >= (int)sizeof log_path || !strcmp(log_path, achievementFile)) {
        free(data);
        return NULL;
    }
    return data;
}

/* The optional fields after the condition: "|points|type|description", each may be empty.
 * points 0..1000; type standard, missable, progression or win. 0 on a bad field. */
static int parse_extras(char *extras, unsigned index)
{
    static const char *const TYPES[] = { "standard", "missable", "progression", "win" };
    char *type = NULL, *desc = NULL, *end;
    unsigned long points;
    unsigned t;
    if ((type = strchr(extras, '|'))) {
        *type++ = 0;
        if ((desc = strchr(type, '|'))) *desc++ = 0;   /* the description is the rest of the line */
    }
    extras = trim(extras);
    if (*extras) {
        errno = 0;
        points = strtoul(extras, &end, 10);
        if (errno || *trim(end) || points > 1000 || *extras == '-') return 0;
        entries[index].points = (unsigned short)points;
    }
    if (type && *(type = trim(type))) {
        for (t = 0; t < 4 && strcasecmp(type, TYPES[t]); ++t) ;
        if (t == 4) return 0;
        entries[index].type = (unsigned char)t;
    }
    if (desc) {
        desc = trim(desc);
        if (strlen(desc) >= sizeof(entries[0].desc)) return 0;
        strcpy(entries[index].desc, desc);
    }
    return 1;
}

/* "Game=A,B,..." (the game's disc serials, one of them the loaded disc's CdromId) */
static int parse_games(char *list)
{
    char *serial, *comma;
    for (serial = list; serial; serial = comma) {
        if ((comma = strchr(serial, ','))) *comma++ = 0;
        serial = trim(serial);
        if (!*serial || strlen(serial) >= sizeof(set_games[0]) || set_game_count == MAX_SET_DISCS) return 0;
        strcpy(set_games[set_game_count++], serial);
    }
    return 1;
}

/* "Game=<serials>" first (more Game= lines may follow, all before the achievements: together they
 * name every disc of the game, see parse_games), an optional "Title=<game name>", then one
 * "id|title|condition[|points|type|description]" line per achievement; blank lines and lines
 * starting with # or ; are skipped. Each achievement is activated in rcheevos as it is read.
 * Returns 0, or the number of the first bad line: any bad line rejects the whole set, so an
 * edited file cannot silently change what it covers. A set that does not name the loaded disc
 * is refused at its first Game= line. */
static unsigned parse_set(char *data, const char *game_id)
{
    char *line, *next, *title, *condition, *extras, *end;
    unsigned long id;
    unsigned i, line_no = 0, game_line = 0;
    for (line = data; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ++line_no;
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (!game_line && strncmp(line, "Game=", 5)) return line_no;   /* the first line names the discs */
        if (!strncmp(line, "Game=", 5)) {
            if (!game_line) game_line = line_no;
            if (count || !parse_games(line + 5)) return line_no;
            continue;
        }
        if (!set_has_game(game_id)) return game_line;
        if (!strncmp(line, "Title=", 6)) {
            snprintf(set_title, sizeof set_title, "%s", trim(line + 6));
            continue;
        }
        if (!(title = strchr(line, '|')) || !(condition = strchr(title + 1, '|'))) return line_no;
        *title++ = 0;
        *condition++ = 0;
        if ((extras = strchr(condition, '|'))) *extras++ = 0;
        title = trim(title);
        condition = trim(condition);
        errno = 0;
        id = strtoul(line, &end, 10);
        if (errno || *trim(end) || !id || id > UINT32_MAX || *line == '-') return line_no;   /* a positive 32-bit id */
        if (count == MAX_ACHIEVEMENTS) return line_no;
        for (i = 0; i < count; ++i)
            if (entries[i].id == id) return line_no;                                         /* unique */
        if (!*title || strlen(title) >= sizeof(entries[0].title)) return line_no;
        if (!*condition || strlen(condition) > MAX_CONDITION) return line_no;
        if (rc_runtime_activate_achievement(&runtime, (uint32_t)id, condition, NULL, 0) != RC_OK) {
            SysPrintf("Achievements: line %u: rcheevos cannot read the condition\n", line_no);
            return line_no;
        }
        if (extras && !parse_extras(extras, count)) return line_no;
        entries[count].id = (uint32_t)id;
        entries[count].measured = ACH_STEP_NONE;
        strcpy(entries[count++].title, title);
    }
    if (game_line && !set_has_game(game_id)) return game_line;
    return count ? 0 : line_no + 1;   /* no Game= line, or no achievements */
}

/* The set of game_id from achievementFile, for the players now. announce: the game card. */
static int load_set(const char *game_id, int announce)
{
    char *data;
    unsigned bad_line, p;
    if (!achievementsEnabled || !achievementFile[0]) return 0;
    if (!game_id || !*game_id) return -1;
    if (!(data = read_set())) {
        SysPrintf("Achievements: cannot read the set %s (missing, empty, over 64 KiB, or a bad journal path)\n",
                  achievementFile);
        return -1;
    }
    rc_runtime_init(&runtime);
    active = 1;
    settle = ACH_SETTLE_FRAMES;
    bad_line = parse_set(data, game_id);
    free(data);
    if (bad_line) {
        SysPrintf("Achievements: the set is refused at line %u (wrong game, or a bad line)\n", bad_line);
        achievements_close();
        return -1;
    }
    snprintf(load_game, sizeof load_game, "%s", game_id);
    pick_players();
    /* Test mode starts from nothing each time; Local restores this exact set's unlocks, each
     * player its own */
    if (achievementsEnabled != ACH_TEST && !restore_journal()) {
        SysPrintf("Achievements: unlock log too large or unreadable, set refused: %s\n", log_path);
        achievements_close();
        return -1;
    }
    keep_earned();
    beaten_seen = beaten_now();     /* earned before this session: no toast for it */
    mastered_seen = mastered_now();
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per load */
    running = 1;
    SysPrintf("Achievements: loaded %u for %s (%u earned)\n", count, game_id, local_earned());
    for (p = 0; p < players && player[p].key[0]; ++p)   /* a key's first 8 characters, never a name */
        SysPrintf("Achievements: player %u is %.8s (%u earned)\n", p + 1, player[p].key, player_earned(p));
    if (announce && achievements_notify) {   /* the game card: the set is on */
        achievement_notice_t n;
        unsigned i;
        memset(&n, 0, sizeof n);
        n.kind = ACH_NOTICE_GAME;
        n.game = set_title[0] ? set_title : game_id;
        n.desc = achievementsEnabled == ACH_TEST ? "Test" : "Custom";
        n.rarity_x100 = -1;
        n.earned = local_earned();
        n.count = count;
        for (i = 0; i < count; ++i) {
            n.total_points += entries[i].points;
            if (entries[i].has & all_players) n.points += entries[i].points;
        }
        achievements_notify(&n);
    }
    return (int)count;
}

int achievements_load(const char *game_id)
{
    achievements_close();
    if (RA_MODE()) {
        if (achievements_official_load) return achievements_official_load(achievementImage);
        SysPrintf("Achievements: RetroAchievements (Official, Local) is not in this build\n");
        return 0;
    }
    return load_set(game_id, 1);
}

/* The seats or the pads may have changed: with other players, the set loads again for them
 * (the unlocks pending for the old players are written first) */
void achievements_players_changed(void)
{
    achievement_player_t now[ACH_PLAYERS_MAX];
    char game[sizeof load_game];
    unsigned n, p;
    int same;
    if (ra_local && achievementsEnabled == ACH_LOCAL) {   /* RetroAchievements' set: rc_client's entries go in again */
        if (achievements_official_players_changed) achievements_official_players_changed();
        return;
    }
    if (!active || achievementsEnabled != ACH_CUSTOM) return;
    n = collect_players(now);
    same = n == players;
    for (p = 0; same && p < n; ++p) same = !strcmp(now[p].key, player[p].key);
    if (same) {
        for (p = 0; p < n; ++p) player[p] = now[p];   /* the same people: their names, perhaps */
        return;
    }
    achievements_flush();
    snprintf(game, sizeof game, "%s", load_game);
    achievements_close();
    SysPrintf("Achievements: the players changed (%u now), the set loads again\n", n);
    load_set(game, 0);
}

/* ---- save states: the WSXE "ACH1" section ----
 * WAC1, the set's digest, 128 earned bits (the union), rcheevos' progress; and, when Miis were
 * playing, after the progress: their keys (16 bytes each), their count and "WK". A state with no
 * keys was made by Guest (so is every state from before the Players menu). */

#define STATE_KEYS_MAX (ACH_PLAYERS_MAX * ACH_KEY_LEN + 3)

/* The size of the keys at the end of a state: 0 when Guest played */
static unsigned state_keys_size(void) { return players && player[0].key[0] ? players * ACH_KEY_LEN + 3 : 0; }

int achievements_state_save(void *data, unsigned size)
{
    unsigned i, keys;
    uint32_t n;
    unsigned char *out = data;
    if (RA(state_save)) return achievements_official_state_save(data, size);
    if (!active) return 0;
    n = rc_runtime_progress_size(&runtime, NULL);
    if (n > ACHIEVEMENTS_STATE_MAX - STATE_HEADER - STATE_KEYS_MAX) return 0;
    keys = state_keys_size();
    if (!data) return (int)(n + STATE_HEADER + keys);
    if (size < n + STATE_HEADER + keys) return 0;
    memcpy(out, "WAC1", 4);
    memcpy(out + 4, digest, 16);
    memset(out + 20, 0, 16);
    for (i = 0; i < count; ++i)
        if (entries[i].has & all_players) out[20 + i / 8] |= 1u << (i % 8);
    if (rc_runtime_serialize_progress_sized(out + STATE_HEADER, n, &runtime, NULL) != RC_OK) return 0;
    if (keys) {
        unsigned char *tail = out + STATE_HEADER + n;
        for (i = 0; i < players; ++i) memcpy(tail + i * ACH_KEY_LEN, player[i].key, ACH_KEY_LEN);
        tail[players * ACH_KEY_LEN] = (unsigned char)players;
        tail[players * ACH_KEY_LEN + 1] = 'W';
        tail[players * ACH_KEY_LEN + 2] = 'K';
    }
    return (int)(n + STATE_HEADER + keys);
}

/* The keys a state ends with (saved: how many, 0 = Guest) and where rcheevos' progress ends */
static unsigned state_keys(const unsigned char *in, unsigned size, unsigned *saved, const unsigned char **keys)
{
    unsigned n, i, j;
    *saved = 0;
    *keys = NULL;
    if (size < STATE_HEADER + 3 || in[size - 2] != 'W' || in[size - 1] != 'K') return size;
    n = in[size - 3];
    if (!n || n > ACH_PLAYERS_MAX || size < STATE_HEADER + 3 + n * ACH_KEY_LEN) return size;
    for (i = 0; i < n; ++i)
        for (j = 0; j < ACH_KEY_LEN; ++j) {
            unsigned char c = in[size - 3 - n * ACH_KEY_LEN + i * ACH_KEY_LEN + j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return size;
        }
    *saved = n;
    *keys = in + size - 3 - n * ACH_KEY_LEN;
    return size - 3 - n * ACH_KEY_LEN;
}

int achievements_state_load(const void *data, unsigned size)
{
    const unsigned char *in = data, *keys = NULL;
    unsigned i, saved = 0, end = 0;
    int ok = 0;
    if (RA(state_load)) return achievements_official_state_load(data, size);
    if (!active) return 0;
    /* only a state made with this exact set restores its progress */
    if (in && size >= STATE_HEADER && size <= ACHIEVEMENTS_STATE_MAX &&
        !memcmp(in, "WAC1", 4) && !memcmp(in + 4, digest, 16)) {
        end = state_keys(in, size, &saved, &keys);
        ok = rc_runtime_deserialize_progress_sized(&runtime, in + STATE_HEADER, end - STATE_HEADER, NULL) == RC_OK;
        /* The earned bits are the union of those who played: whose they are is known only when
         * one player made the state and one plays now, and it is the same one. A state never
         * moves one Mii's unlocks to another; the journals are what each Mii has earned. */
        if (ok && players == 1 && saved <= 1 &&
            (saved ? !memcmp(keys, player[0].key, ACH_KEY_LEN) : !player[0].key[0]))
            for (i = 0; i < count; ++i)
                if (in[20 + i / 8] & (1u << (i % 8))) entries[i].has |= 1;
    }
    if (!ok) rc_runtime_reset(&runtime);
    for (i = 0; i < count; ++i) entries[i].measured = ACH_STEP_NONE;   /* the state's progress is the new start: the next reading is the baseline */
    settle = ACH_SETTLE_FRAMES;
    keep_earned(); /* loading an older state never revokes an earned achievement */
    beaten_seen |= beaten_now();
    mastered_seen |= mastered_now();
    if (in) {
        SysPrintf("Achievements: a state loaded (%s, made by %u Mii%s, %u play now)\n",
                  ok ? "its progress applies" : "no progress", saved, saved == 1 ? "" : "s", players);
        for (i = 0; i < players && player[i].key[0]; ++i)   /* a key's first 8 characters, never a name */
            SysPrintf("Achievements: player %u is %.8s (%u earned)\n", i + 1, player[i].key, player_earned(i));
    }
    return ok;
}
