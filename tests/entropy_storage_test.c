/* Public deterministic fixture only; never provision a real seed over HBC. */
#define _POSIX_C_SOURCE 200809L
#include "../achievement_seed.h"
#include <mbedtls/entropy.h>
#include <mbedtls/platform.h>
#include <mbedtls/ctr_drbg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef GEKKO
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <mbedtls/platform_time.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}
#endif

static FILE *report;
static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(report, "FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)

static void fixture(const char *path, unsigned length)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        for (unsigned i = 0; i < length; ++i) CHECK(fputc((unsigned char)i, f) != EOF);
        CHECK(!fclose(f));
    }
}

static int extract(unsigned char *out)
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int result;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    CHECK(entropy.MBEDTLS_PRIVATE(source_count) == 1);
    result = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                  (const unsigned char *)"public storage test", 19);
    if (!result) result = mbedtls_ctr_drbg_random(&drbg, out, 32);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return result;
}

static void run(const char *root)
{
    char path[256], busy[262], next[267];
    unsigned char a[32], b[32], before[64], after[64];
    FILE *f;
    snprintf(path, sizeof path, "%s/public-seed", root);
    snprintf(busy, sizeof busy, "%s.busy", path);
    snprintf(next, sizeof next, "%s/next", busy);
    CHECK(MBEDTLS_ENTROPY_BLOCK_SIZE == ACHIEVEMENT_SEED_SIZE);
    CHECK(!mbedtls_platform_set_nv_seed(achievement_seed_read, achievement_seed_write));
    fixture(path, 64);
    CHECK(!achievement_seed_begin(path));
    CHECK(achievement_seed_begin(path) < 0); /* Another owner cannot enter. */
    CHECK(achievement_seed_read(before, sizeof before) == 64);
    CHECK(!extract(a));
    CHECK(achievement_seed_read(after, sizeof after) == 64);
    CHECK(memcmp(before, after, sizeof before) != 0);
    CHECK(!achievement_seed_end());
    CHECK(!achievement_seed_begin(path));
    CHECK(!extract(b));
    CHECK(memcmp(a, b, sizeof a) != 0);
    CHECK(!achievement_seed_end());
    fprintf(report, "PASS fsync, rotation, reopen, exclusive ownership, fresh context output\n");

    for (unsigned length = 0; length <= 65; ++length) {
        if (length != 0 && length != 63 && length != 65) continue;
        fixture(path, length);
        CHECK(!achievement_seed_begin(path));
        memset(b, 0xa5, sizeof b);
        CHECK(extract(b) != 0);
        for (unsigned i = 0; i < sizeof b; ++i) CHECK(b[i] == 0xa5);
        CHECK(achievement_seed_end() < 0);
        CHECK(achievement_seed_begin(path) < 0); /* Error marker survives. */
        CHECK(!rmdir(busy)); /* Only fixture recovery; real recovery needs a fresh secret. */
    }
    CHECK(!remove(path));
    CHECK(!achievement_seed_begin(path));
    CHECK(extract(b) != 0); /* Missing seed must not be created automatically. */
    CHECK(achievement_seed_end() < 0);
    CHECK(!rmdir(busy));
    fprintf(report, "PASS missing, empty, truncated and oversized rejection\n");

    fixture(path, 64);
    CHECK(!achievement_seed_begin(path));
    fixture(next, 1); /* Incomplete previous candidate / failed exclusive commit. */
    memset(b, 0xa5, sizeof b);
    CHECK(extract(b) != 0);
    for (unsigned i = 0; i < sizeof b; ++i) CHECK(b[i] == 0xa5);
    CHECK(achievement_seed_end() < 0);
    CHECK(achievement_seed_begin(path) < 0);
    CHECK(!remove(next));
    CHECK(!rmdir(busy));

    /* Simulate loss of the old seed between removal and rename. No promotion
     * of a leftover candidate is allowed on restart. */
    CHECK(!mkdir(busy, 0700));
    fixture(next, 64);
    CHECK(!remove(path));
    CHECK(achievement_seed_begin(path) < 0);
    CHECK(!remove(next));
    CHECK(!rmdir(busy));
    fprintf(report, "PASS failed commit and interrupted rotation refuse restart\n");
    f = fopen(path, "rb");
    CHECK(f == NULL);
    if (f) fclose(f);
}

int main(int argc, char **argv)
{
    char path[256];
#ifdef GEKKO
    GXRModeObj *mode;
    hbc_agent_config cfg = {0};
    VIDEO_Init(); mode = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode); VIDEO_SetNextFramebuffer(fb); VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    if (argc != 2 || strncmp(argv[1], "sd:/wiistation/bench-", 21) ||
        strlen(argv[1]) > 180 || strstr(argv[1], "..") || !fatInitDefault()) return 1;
    cfg.name = "WiiStation tests"; cfg.version = __DATE__ " " __TIME__; cfg.crash_reload_s = 3;
    if (hbc_agent_init(&cfg)) return 1;
    hbc_agent_net_wait(20000);
#else
    if (argc != 2 || strlen(argv[1]) > 180) return 1;
#endif
    snprintf(path, sizeof path, "%s/storage.log", argv[1]);
    report = fopen(path, "w");
    if (!report) return 1;
    run(argv[1]);
    fprintf(report, "failures=%d fixture=public authenticated_transport_ready=false\n", failures);
    if (fclose(report)) ++failures;
    snprintf(path, sizeof path, "%s/result.json", argv[1]);
    report = fopen(path, "w");
    if (!report) return 1;
    fprintf(report, "{\"suite\":\"entropy-storage\",\"pass\":%s,\"failures\":%d,\"authenticated_transport_ready\":false}\n",
            failures ? "false" : "true", failures);
    if (fclose(report)) ++failures;
#ifdef GEKKO
    fatUnmount("sd");
#endif
    puts(failures ? "entropy storage FAIL" : "entropy storage PASS (public fixture)");
    return failures != 0;
}
