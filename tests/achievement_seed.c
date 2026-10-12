#define _POSIX_C_SOURCE 200809L
#include "achievement_seed.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static char seed_path[256], busy_path[262], next_path[267], old_path[267];
static int owned, failed;

int achievement_seed_begin(const char *path)
{
    size_t n;
    if (owned || !path || !(n = strlen(path)) || n >= sizeof seed_path) return -1;
    memcpy(seed_path, path, n + 1);
    snprintf(busy_path, sizeof busy_path, "%s.busy", path);
    snprintf(next_path, sizeof next_path, "%s/next", busy_path);
    snprintf(old_path, sizeof old_path, "%s/old", busy_path);
    /* Exclusive filesystem ownership also rejects a previous interrupted run. */
    if (mkdir(busy_path, 0700)) return -1;
    owned = 1;
    failed = 0;
    return 0;
}

static int read_exact(unsigned char *out)
{
    FILE *f = fopen(seed_path, "rb");
    int ok;
    if (!f) return -1;
    ok = fread(out, 1, ACHIEVEMENT_SEED_SIZE, f) == ACHIEVEMENT_SEED_SIZE;
    if (fgetc(f) != EOF || ferror(f)) ok = 0;
    if (fclose(f)) ok = 0;
    return ok ? ACHIEVEMENT_SEED_SIZE : -1;
}

int achievement_seed_read(unsigned char *out, size_t length)
{
    unsigned char bytes[ACHIEVEMENT_SEED_SIZE];
    int result = -1;
    if (owned && !failed && out && length == sizeof bytes && read_exact(bytes) > 0) {
        memcpy(out, bytes, sizeof bytes);
        result = sizeof bytes;
    } else if (owned) failed = 1;
    /* Volatile stores prevent the compiler discarding secret cleanup. */
    for (size_t i = 0; i < sizeof bytes; ++i) ((volatile unsigned char *)bytes)[i] = 0;
    return result;
}

int achievement_seed_write(unsigned char *in, size_t length)
{
    FILE *f;
    int fd, ok, moved;
    unsigned char check[ACHIEVEMENT_SEED_SIZE];
    if (!owned || failed || !in || length != sizeof check) goto fail;
    fd = open(next_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) goto fail;
    f = fdopen(fd, "wb");
    if (!f) { close(fd); goto fail; }
    ok = fwrite(in, 1, length, f) == length;
    if (fflush(f) || fsync(fd)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok) goto fail;
    /* FAT cannot overwrite by rename. Move the old seed aside, never delete it
     * first: a failed commit puts it back. Any interruption leaves .busy and
     * fails closed. */
    if (remove(old_path) && errno != ENOENT) goto fail;
    moved = rename(seed_path, old_path) == 0;
    if (!moved && errno != ENOENT) goto fail;
    if (rename(next_path, seed_path)) {
        if (moved) rename(old_path, seed_path);
        goto fail;
    }
    if (moved && remove(old_path)) goto fail;
    ok = read_exact(check) > 0 && !memcmp(in, check, sizeof check);
    for (size_t i = 0; i < sizeof check; ++i) ((volatile unsigned char *)check)[i] = 0;
    if (!ok) goto fail;
    return (int)length;
fail:
    if (owned) failed = 1;
    return -1;
}

int achievement_seed_end(void)
{
    int result;
    if (!owned) return -1;
    /* Keep the marker after any error. Recovery requires fresh private entropy. */
    result = failed ? -1 : rmdir(busy_path);
    owned = 0;
    memset(seed_path, 0, sizeof seed_path);
    return result;
}
