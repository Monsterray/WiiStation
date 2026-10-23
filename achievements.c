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

static rc_runtime_t runtime;
static struct {
    uint32_t id;
    char title[80];
    unsigned char earned;    /* triggered this session, or restored from the journal/state */
    unsigned char written;   /* already in the journal */
} entries[MAX_ACHIEVEMENTS];
static unsigned count;
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

unsigned achievements_count(void) { return active ? count : 0; }
unsigned achievements_earned(void)
{
    unsigned i, n = 0;
    for (i = 0; i < count; ++i) n += entries[i].earned != 0;
    return n;
}

/* For the menu's list: the i-th achievement's title, and whether it is earned */
int achievements_get(unsigned i, const char **title)
{
    if (!active || i >= count) return -1;
    if (title) *title = entries[i].title;
    return entries[i].earned != 0;
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

static void event(const rc_runtime_event_t *e)
{
    unsigned i;
    if (e->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) return;
    for (i = 0; i < count; ++i)
        if (entries[i].id == e->id) entries[i].earned = 1;
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
    if (!active) return;
    rc_runtime_reset(&runtime);
    keep_earned();
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per reset */
    running = 1;
}

void achievements_frame(void)
{
    if (!active || !running || !achievementsEnabled) return;
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long started = perf_now_ticks();
#endif
    rc_runtime_do_frame(&runtime, event, achievements_read_memory, NULL, NULL);
#if defined(PERF_PROF) && PERF_PROF_ACHIEVEMENTS
    unsigned long long elapsed = perf_now_ticks() - started;
    PERF_ADD(achievement_ticks, elapsed);
    PERF_MAX(achievement_max_ticks, elapsed);
    PERF_INC(achievement_frames);
#endif
}

void achievements_close(void)
{
    achievements_flush();
    if (active) rc_runtime_destroy(&runtime);
    active = running = 0;
    count = 0;
    memset(entries, 0, sizeof(entries));
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

/* "Game=<serial>" (the loaded disc's CdromId), then one "id|title|condition" line per
 * achievement; blank lines and lines starting with # or ; are skipped. Each achievement is
 * activated in rcheevos as it is read. Returns 0, or the number of the first bad line: any
 * bad line rejects the whole set, so an edited file cannot silently change what it covers. */
static unsigned parse_set(char *data, const char *game_id)
{
    char *line, *next, *title, *condition, *end;
    unsigned long id;
    unsigned i, line_no = 0;
    int identified = 0;
    for (line = data; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ++line_no;
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (!identified) {   /* the first line names the disc */
            if (strncmp(line, "Game=", 5) || strcmp(trim(line + 5), game_id)) return line_no;
            identified = 1;
            continue;
        }
        if (!(title = strchr(line, '|')) || !(condition = strchr(title + 1, '|'))) return line_no;
        *title++ = 0;
        *condition++ = 0;
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
        entries[count].id = (uint32_t)id;
        strcpy(entries[count++].title, title);
    }
    return identified && count ? 0 : line_no + 1;   /* no Game= line, or no achievements */
}

int achievements_load(const char *game_id)
{
    char *data;
    unsigned bad_line;
    achievements_close();
    if (achievementsEnabled == ACH_OFFICIAL) {
        SysPrintf("Achievements: Official (RetroAchievements) mode is not available yet\n");
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
    rc_runtime_validate_addresses(&runtime, event, valid_address); /* address validity: once per load */
    running = 1;
    SysPrintf("Achievements: loaded %u for %s (%u earned)\n", count, game_id, achievements_earned());
    return (int)count;
}

/* ---- save states: the WSXE "ACH1" section ---- */

int achievements_state_save(void *data, unsigned size)
{
    unsigned i;
    uint32_t n;
    unsigned char *out = data;
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
    keep_earned(); /* loading an older state never revokes an earned achievement */
    return ok;
}
