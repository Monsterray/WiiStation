/* Local-only achievement evaluation using upstream rcheevos. No official submissions.
 * GPL-2.0-or-later, like the surrounding WiiStation integration code. */
#include "achievements.h"
#include "deps/rcheevos/include/rc_runtime.h"
#include "deps/rcheevos/src/util/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define MAX_ACHIEVEMENTS 128
#define MAX_SET_SIZE (64 * 1024)
#define STATE_HEADER 36 /* WAC1, 16-byte definition digest, 128 earned bits */

extern unsigned char *psxM, *psxH;
extern void SysPrintf(const char *fmt, ...);
char achievementsEnabled;
char achievementFile[256], achievementLog[256];

static rc_runtime_t runtime;
static struct {
    uint32_t id;
    char title[80];
    unsigned char earned, written;
} entries[MAX_ACHIEVEMENTS];
static unsigned count;
static int active, running;
static unsigned char digest[16];
static char digest_text[33], log_path[256];

uint32_t achievements_read_memory(uint32_t address, uint8_t *buffer, uint32_t size, void *unused)
{
    uint32_t n, done = 0;
    (void)unused;
    if (!buffer || !psxM || !psxH || address >= 0x200400)
        return 0;
    if (size > 0x200400 - address)
        size = 0x200400 - address;
    if (address < 0x200000) {
        n = 0x200000 - address;
        if (n > size) n = size;
        memcpy(buffer, psxM + address, n);
        address += n;
        done = n;
    }
    if (done < size)
        memcpy(buffer + done, psxH + address - 0x200000, size - done);
    return size;
}

unsigned achievements_count(void) { return active ? count : 0; }
unsigned achievements_earned(void)
{
    unsigned i, n = 0;
    for (i = 0; i < count; ++i) n += entries[i].earned != 0;
    return n;
}

void achievements_flush(void)
{
    FILE *f;
    unsigned i;
    if (!active) return;
    for (i = 0; i < count; ++i) {
        if (!entries[i].earned || entries[i].written) continue;
        f = fopen(log_path, "a");
        if (!f) {
            SysPrintf("Local achievements: cannot append unlock log %s\n", log_path);
            return; /* retry on the next menu/state boundary, never per frame */
        }
        {
            int ok = fprintf(f, "%s %u %s\n", digest_text, (unsigned)entries[i].id,
                             entries[i].title) >= 0;
            if (fclose(f) != 0) ok = 0;
            if (!ok) {
                SysPrintf("Local achievements: unlock log write failed\n");
                return;
            }
        }
        entries[i].written = 1;
        SysPrintf("Local achievement unlocked: %u %s\n", (unsigned)entries[i].id, entries[i].title);
    }
}

void achievements_close(void)
{
    achievements_flush();
    if (active) rc_runtime_destroy(&runtime);
    active = running = 0;
    count = 0;
    memset(entries, 0, sizeof(entries));
}

static int valid_address(uint32_t address) { return address < 0x200400; }
static void event(const rc_runtime_event_t *e)
{
    unsigned i;
    if (e->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) return;
    for (i = 0; i < count; ++i)
        if (entries[i].id == e->id) entries[i].earned = 1;
}

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
    running = 1;
}

void achievements_frame(void)
{
    if (!active || !running || !achievementsEnabled) return;
    rc_runtime_validate_addresses(&runtime, event, valid_address);
    rc_runtime_do_frame(&runtime, event, achievements_read_memory, NULL, NULL);
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
    *end = 0;
    return s;
}

/* Game=<serial> followed by id|title|rcheevos-condition lines. Fail the whole set
 * on a malformed/duplicate definition, so an edited file cannot silently change scope. */
int achievements_load(const char *game_id)
{
    FILE *f;
    char *data, *line, *next, *a, *b, *end;
    long size;
    md5_state_t md5;
    unsigned i, line_no = 0;
    int identified = 0, result;
    unsigned long id;
    achievements_close();
    if (!achievementsEnabled || !achievementFile[0]) return 0;
    if (!game_id || !*game_id) return -1;
    f = fopen(achievementFile, "rb");
    if (!f) goto fail_open;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || size > MAX_SET_SIZE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        goto fail_open;
    }
    data = malloc((size_t)size + 1);
    if (!data) { fclose(f); goto fail_open; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size || memchr(data, 0, (size_t)size)) {
        fclose(f); free(data); goto fail_open;
    }
    fclose(f);
    data[size] = 0;
    md5_init(&md5);
    md5_append(&md5, (const unsigned char *)data, (int)size);
    md5_finish(&md5, digest);
    for (i = 0; i < 16; ++i) snprintf(digest_text + 2 * i, 3, "%02x", digest[i]);
    result = snprintf(log_path, sizeof(log_path), "%s%s", achievementLog[0] ? achievementLog : achievementFile,
                      achievementLog[0] ? "" : ".unlocks");
    if (result < 0 || result >= (int)sizeof(log_path) || !strcmp(log_path, achievementFile)) {
        free(data); goto fail_open;
    }
    rc_runtime_init(&runtime);
    active = 1;
    for (line = data; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ++line_no;
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (!identified) {
            if (strncmp(line, "Game=", 5) || strcmp(trim(line + 5), game_id)) goto fail;
            identified = 1;
            continue;
        }
        a = strchr(line, '|');
        if (!a) goto fail;
        *a++ = 0;
        b = strchr(a, '|');
        if (!b) goto fail;
        *b++ = 0;
        errno = 0;
        id = strtoul(line, &end, 10);
        if (errno || *trim(end) || !id || id > UINT32_MAX || *line == '-' || count == MAX_ACHIEVEMENTS) goto fail;
        for (i = 0; i < count; ++i) if (entries[i].id == id) goto fail;
        a = trim(a); b = trim(b);
        if (!*a || strlen(a) >= sizeof(entries[0].title) || !*b || strlen(b) > 2048) goto fail;
        result = rc_runtime_activate_achievement(&runtime, (uint32_t)id, b, NULL, 0);
        if (result != RC_OK) {
            SysPrintf("Local achievements: condition error %s\n", rc_error_str(result));
            goto fail;
        }
        entries[count].id = (uint32_t)id;
        strcpy(entries[count++].title, a);
    }
    free(data);
    if (!identified || !count) { achievements_close(); return -1; }
    /* Only this exact definition-file digest can restore local unlocks. */
    f = fopen(log_path, "r");
    if (f) {
        char record[256], hash[33];
        unsigned saved_id;
        size_t bytes = 0;
        while (fgets(record, sizeof(record), f) && (bytes += strlen(record)) <= 1024 * 1024) {
            if (sscanf(record, "%32s %u", hash, &saved_id) != 2 || strcmp(hash, digest_text)) continue;
            for (i = 0; i < count; ++i)
                if (entries[i].id == saved_id) entries[i].earned = entries[i].written = 1;
        }
        fclose(f);
    }
    keep_earned();
    running = 1;
    SysPrintf("Local achievements: loaded %u for %s (%u earned)\n", count, game_id, achievements_earned());
    return (int)count;
fail:
    SysPrintf("Local achievements: invalid set/game at line %u\n", line_no);
    free(data);
    achievements_close();
    return -1;
fail_open:
    SysPrintf("Local achievements: cannot load bounded set/log path %s\n", achievementFile);
    return -1;
}

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
    if (in && size >= STATE_HEADER && size <= ACHIEVEMENTS_STATE_MAX &&
        !memcmp(in, "WAC1", 4) && !memcmp(in + 4, digest, 16)) {
        ok = rc_runtime_deserialize_progress_sized(&runtime, in + STATE_HEADER, size - STATE_HEADER, NULL) == RC_OK;
        if (ok)
            for (i = 0; i < count; ++i)
                if (in[20 + i / 8] & (1u << (i % 8))) entries[i].earned = 1;
    }
    if (!ok) rc_runtime_reset(&runtime);
    keep_earned(); /* loading an older state never revokes an earned local achievement */
    return ok;
}
