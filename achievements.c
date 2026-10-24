/* WiiStation's achievement engine: evaluates a definition file with upstream rcheevos.
 * GPL-2.0-or-later, like the surrounding WiiStation integration code.
 *
 * The flow, all on the emulator thread:
 *   achievements_load()  -- at game load: read the set file, parse it, restore earned ones
 *   achievements_frame() -- every emulated frame: rcheevos checks every condition
 *   event()              -- an achievement triggers: mark it earned, write it to the journal
 *   achievements_close() -- at shutdown or disc swap: write what is pending, free the set
 *
 * Modes (setting Achievements): Off, Local (unlocks are saved in a journal file), Test (the
 * same evaluation, but nothing is read from or written to the journal, to try out a set),
 * Official (RetroAchievements; not built yet, so it loads nothing). */
#include "achievements.h"
#include "Gamecube/perf_prof.h"
#include "deps/rcheevos/include/rc_runtime.h"
#include "deps/rcheevos/src/util/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <strings.h>

/* The guest memory the conditions address: PS1 RAM, then the scratchpad after it */
#define PSX_RAM_BYTES 0x200000u
#define PSX_MAP_END (PSX_RAM_BYTES + 0x400u)   /* + the 1 KiB scratchpad */
#define MAX_ACHIEVEMENTS 128
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
void (*achievements_notify)(const achievement_notice_t *notice);

/* Official mode lives in achievements_official.c (Wii builds). Weak, so a build without it
 * (the GameCube, the host tests) links and Official mode loads nothing. */
int achievements_official_load(const char *image) __attribute__((weak));
void achievements_official_frame(void) __attribute__((weak));
void achievements_official_idle(void) __attribute__((weak));
void achievements_official_close(void) __attribute__((weak));
void achievements_official_change_media(const char *image) __attribute__((weak));
const char *achievements_official_status(void) __attribute__((weak));
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
/* Official mode is on and this build has its hook `fn` */
#define OFFICIAL(fn) (achievementsEnabled == ACH_OFFICIAL && achievements_official_##fn)

static rc_runtime_t runtime;
static struct {
    uint32_t id;
    char title[80];
    char desc[96];           /* optional */
    unsigned short points;   /* optional; 0 = none */
    unsigned char type;      /* ACH_TYPE_*, optional */
    unsigned char earned;    /* triggered this session, or restored from the journal/state */
    unsigned char written;   /* already in the journal */
    unsigned char measured;  /* the highest measured percent shown so far: a toast only when it grows */
} entries[MAX_ACHIEVEMENTS];
static unsigned count;
static char set_title[48];              /* the set's Title= line: the game's name for the toast */
#define MAX_SET_DISCS 8
static char set_games[MAX_SET_DISCS][10]; /* the set's Game= serials: every disc of the game */
static unsigned set_game_count;
static int beaten_seen, mastered_seen;  /* already true, so no second Beaten or Mastered toast */
static int active, running;   /* a set is loaded; evaluation is allowed (not during BIOS boot) */
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

static unsigned local_earned(void)
{
    unsigned i, n = 0;
    for (i = 0; i < count; ++i) n += entries[i].earned != 0;
    return n;
}

unsigned achievements_count(void)
{
    if (OFFICIAL(count)) return achievements_official_count();
    return active ? count : 0;
}

unsigned achievements_earned(void)
{
    if (OFFICIAL(earned)) return achievements_official_earned();
    return local_earned();
}

void achievements_list_refresh(void)
{
    if (OFFICIAL(list_refresh)) achievements_official_list_refresh();
}

/* For the menu's list: the i-th achievement, with its measured progress ("12/40") */
int achievements_entry(unsigned i, achievement_entry_t *e)
{
    unsigned value = 0, target = 0;
    if (OFFICIAL(entry)) return achievements_official_entry(i, e);
    if (!active || i >= count) return -1;
    memset(e, 0, sizeof *e);
    e->title = entries[i].title;
    e->desc = entries[i].desc;
    e->badge = "";
    e->id = entries[i].id;
    e->points = entries[i].points;
    e->rarity_x100 = -1;
    e->earned = entries[i].earned;
    e->type = entries[i].type;
    if (!e->earned && rc_runtime_get_achievement_measured(&runtime, e->id, &value, &target) && target) {
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

/* ---- the unlock journal: one "<set digest> <id> <title>" line per unlock ---- */

/* Append one unlock record. Past JOURNAL_MAX, the next load would refuse the whole
 * journal, so refuse the append too: fail closed, never grow the file past the cap. */
static int journal_append(unsigned index)
{
    char line[256];
    FILE *f;
    long size;
    int len = snprintf(line, sizeof line, "%s %u %s\n", digest_text,
                       (unsigned)entries[index].id, entries[index].title);
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

/* Mark earned every achievement this set's journal records. Only whole records count:
 * a line cut short by a power loss ("<digest> 1" of "<digest> 12 Twelve") must not restore
 * id 1. Returns 0 when the journal is too large: restoring part of it would re-earn the rest
 * and append duplicates, so the caller refuses the whole set. */
static int restore_journal(void)
{
    char record[256], hash[33];
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
        if (!len || record[len - 1] != '\n') continue;
        if (sscanf(record, "%32s %u%n", hash, &saved_id, &end) != 2 || record[end] != ' ' ||
            strcmp(hash, digest_text)) continue;
        for (i = 0; i < count; ++i)
            if (entries[i].id == saved_id) entries[i].earned = entries[i].written = 1;
    }
    fclose(f);
    return 1;
}

void achievements_flush(void)
{
    unsigned i;
    if (!active || achievementsEnabled == ACH_TEST) return;   /* Test mode keeps no record */
    for (i = 0; i < count; ++i) {
        if (!entries[i].earned || entries[i].written) continue;
        if (!journal_append(i)) return; /* retry at the next menu/state boundary, never per frame */
        entries[i].written = 1;
        SysPrintf("Achievement unlocked: %u %s\n", (unsigned)entries[i].id, entries[i].title);
    }
}

/* ---- evaluation ---- */

static int valid_address(uint32_t address) { return address < PSX_MAP_END; }

/* Beaten, as RetroAchievements counts it: every Progression achievement earned, and a Win
 * earned if the set has any. A set with neither type cannot be beaten. */
static int is_beaten(void)
{
    unsigned i;
    int typed = 0, wins = 0, won = 0;
    for (i = 0; i < count; ++i) {
        if (entries[i].type == ACH_TYPE_PROGRESSION) {
            typed = 1;
            if (!entries[i].earned) return 0;
        } else if (entries[i].type == ACH_TYPE_WIN) {
            typed = wins = 1;
            won |= entries[i].earned;
        }
    }
    return typed && (!wins || won);
}

static int is_mastered(void) { return count && local_earned() == count; }

/* Tell the toast about a new unlock, and whether it beat the game or completed the set */
static void notify(unsigned i)
{
    achievement_notice_t n;
    unsigned k;
    memset(&n, 0, sizeof n);
    n.beaten = !beaten_seen && is_beaten();
    n.mastered = !mastered_seen && is_mastered();
    beaten_seen |= n.beaten;
    mastered_seen |= n.mastered;
    if (!achievements_notify) return;
    n.title = entries[i].title;
    n.desc = entries[i].desc;
    n.game = set_title;
    n.points = entries[i].points;
    for (k = 0; k < count; ++k) n.total_points += entries[k].points;
    n.rarity_x100 = -1;   /* a Local set has no player counts */
    n.earned = local_earned();
    n.count = count;
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
        if (n.percent <= entries[i].measured)
            return;   /* only a whole percent more; a value that falls back and climbs again is not news */
        entries[i].measured = n.percent;
        rc_runtime_format_achievement_measured(&runtime, n.id, progress, sizeof progress);
        n.progress = progress;
    }
    achievements_notify(&n);
}

static void event(const rc_runtime_event_t *e)
{
    unsigned i;
    if (e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED || e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED ||
        e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED) {
        for (i = 0; i < count; ++i)
            if (entries[i].id == e->id && !entries[i].earned)
                indicator(e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED ? ACH_NOTICE_CHALLENGE_ON :
                          e->type == RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED ? ACH_NOTICE_CHALLENGE_OFF : ACH_NOTICE_PROGRESS, i);
        return;
    }
    if (e->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) return;
    for (i = 0; i < count; ++i)
        if (entries[i].id == e->id && !entries[i].earned) {
            entries[i].earned = 1;
            notify(i);
        }
    achievements_flush(); /* a trigger is rare: one short journal line, written at once */
}

/* An earned achievement stays earned: rcheevos stops checking it */
static void keep_earned(void)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        if (entries[i].earned) rc_runtime_deactivate_achievement(&runtime, entries[i].id);
}

void achievements_pause(void) { running = 0; }
void achievements_reset(void)
{
    if (OFFICIAL(reset)) {
        achievements_official_reset();
        return;
    }
    if (!active) return;
    rc_runtime_reset(&runtime);
    for (unsigned i = 0; i < count; ++i) entries[i].measured = 0;   /* progress starts again */
    keep_earned();
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per reset */
    running = 1;
}

void achievements_frame(void)
{
    int official = achievementsEnabled == ACH_OFFICIAL;
    if (official ? !achievements_official_frame : (!active || !running || !achievementsEnabled)) return;
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long started = perf_now_ticks();
#endif
    if (official)
        achievements_official_frame();   /* rc_client: completions, then evaluation */
    else
        rc_runtime_do_frame(&runtime, event, achievements_read_memory, NULL, NULL);
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long elapsed = perf_now_ticks() - started;
    PERF_ADD(achievement_ticks, elapsed);
    PERF_MAX(achievement_max_ticks, elapsed);
    PERF_INC(achievement_frames);
#endif
}

/* The menu and the pause, once per frame: Official mode's completions and rc_client_idle */
void achievements_idle(void)
{
    if (achievementsEnabled == ACH_OFFICIAL && achievements_official_idle) achievements_official_idle();
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
        return "Off (Cheevos)";
    case ACH_OFFICIAL:
        return achievements_official_status ? achievements_official_status() : "Official: not in this build";
    default:
        if (!active)
            return "No set (AchievementFile)";
        snprintf(line, sizeof line, "%u of %u earned (%s)", achievements_earned(), count,
                 achievementsEnabled == ACH_TEST ? "Test" : "Local");
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
    if (achievementsEnabled == ACH_OFFICIAL && achievements_official_change_media)
        achievements_official_change_media(image);
    else if (active && set_has_game(game_id))
        SysPrintf("Achievements: disc %s is in the set, it goes on\n", game_id);
    else {
        if (active) SysPrintf("Achievements: disc %s is not in the set, it closes\n", game_id ? game_id : "?");
        achievements_close();
    }
}

void achievements_close(void)
{
    if (achievements_official_close) achievements_official_close();
    achievements_flush();
    if (active) rc_runtime_destroy(&runtime);
    active = running = 0;
    count = 0;
    memset(entries, 0, sizeof(entries));
    set_title[0] = 0;
    set_game_count = 0;
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
        strcpy(entries[count++].title, title);
    }
    if (game_line && !set_has_game(game_id)) return game_line;
    return count ? 0 : line_no + 1;   /* no Game= line, or no achievements */
}

int achievements_load(const char *game_id)
{
    char *data;
    unsigned bad_line;
    achievements_close();
    if (achievementsEnabled == ACH_OFFICIAL) {
        if (achievements_official_load) return achievements_official_load(achievementImage);
        SysPrintf("Achievements: Official (RetroAchievements) mode is not in this build\n");
        return 0;
    }
    if (!achievementsEnabled || !achievementFile[0]) return 0;
    if (!game_id || !*game_id) return -1;
    if (!(data = read_set())) {
        SysPrintf("Achievements: cannot read the set %s (missing, empty, over 64 KiB, or a bad journal path)\n",
                  achievementFile);
        return -1;
    }
    rc_runtime_init(&runtime);
    active = 1;
    bad_line = parse_set(data, game_id);
    free(data);
    if (bad_line) {
        SysPrintf("Achievements: the set is refused at line %u (wrong game, or a bad line)\n", bad_line);
        achievements_close();
        return -1;
    }
    /* Test mode starts from nothing each time; Local restores this exact set's unlocks */
    if (achievementsEnabled != ACH_TEST && !restore_journal()) {
        SysPrintf("Achievements: unlock log too large or unreadable, set refused: %s\n", log_path);
        achievements_close();
        return -1;
    }
    keep_earned();
    beaten_seen = is_beaten();     /* earned before this session: no toast for it */
    mastered_seen = is_mastered();
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per load */
    running = 1;
    SysPrintf("Achievements: loaded %u for %s (%u earned)\n", count, game_id, local_earned());
    if (achievements_notify) {   /* the game card: the set is on */
        achievement_notice_t n;
        unsigned i;
        memset(&n, 0, sizeof n);
        n.kind = ACH_NOTICE_GAME;
        n.game = set_title[0] ? set_title : game_id;
        n.desc = achievementsEnabled == ACH_TEST ? "Test" : "Local";
        n.rarity_x100 = -1;
        n.earned = local_earned();
        n.count = count;
        for (i = 0; i < count; ++i) {
            n.total_points += entries[i].points;
            if (entries[i].earned) n.points += entries[i].points;
        }
        achievements_notify(&n);
    }
    return (int)count;
}

/* ---- save states: the WSXE "ACH1" section ---- */

int achievements_state_save(void *data, unsigned size)
{
    unsigned i;
    uint32_t n;
    unsigned char *out = data;
    if (OFFICIAL(state_save)) return achievements_official_state_save(data, size);
    if (!active) return 0;
    n = rc_runtime_progress_size(&runtime, NULL);
    if (n > ACHIEVEMENTS_STATE_MAX - STATE_HEADER) return 0;
    if (!data) return (int)(n + STATE_HEADER);
    if (size < n + STATE_HEADER) return 0;
    memcpy(out, "WAC1", 4);
    memcpy(out + 4, digest, 16);
    memset(out + 20, 0, 16);
    for (i = 0; i < count; ++i)
        if (entries[i].earned) out[20 + i / 8] |= 1u << (i % 8);
    if (rc_runtime_serialize_progress_sized(out + STATE_HEADER, n, &runtime, NULL) != RC_OK) return 0;
    return (int)(n + STATE_HEADER);
}

int achievements_state_load(const void *data, unsigned size)
{
    const unsigned char *in = data;
    unsigned i;
    int ok = 0;
    if (OFFICIAL(state_load)) return achievements_official_state_load(data, size);
    if (!active) return 0;
    /* only a state made with this exact set restores its progress and earned bits */
    if (in && size >= STATE_HEADER && size <= ACHIEVEMENTS_STATE_MAX &&
        !memcmp(in, "WAC1", 4) && !memcmp(in + 4, digest, 16)) {
        ok = rc_runtime_deserialize_progress_sized(&runtime, in + STATE_HEADER, size - STATE_HEADER, NULL) == RC_OK;
        if (ok)
            for (i = 0; i < count; ++i)
                if (in[20 + i / 8] & (1u << (i % 8))) entries[i].earned = 1;
    }
    if (!ok) rc_runtime_reset(&runtime);
    for (i = 0; i < count; ++i) entries[i].measured = 0;   /* the state's progress is the new start */
    keep_earned(); /* loading an older state never revokes an earned achievement */
    beaten_seen |= is_beaten();
    mastered_seen |= is_mastered();
    return ok;
}
