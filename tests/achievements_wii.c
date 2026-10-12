/* Hardware wrapper for the same regression suite used by test-achievements.sh. */
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "../achievements.h"

int achievements_test_main(int argc, char **argv);
extern unsigned char *psxM;

static int compare(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    GXRModeObj *mode;
    void *fb;
    FILE *result, *set;
    char path[256];
    unsigned samples[1000], i, j;
    u64 start;
    hbc_agent_config cfg = {0};
    VIDEO_Init();
    mode = VIDEO_GetPreferredMode(NULL);
    fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (argc != 2 || strncmp(argv[1], "sd:/wiistation/bench-", 21) || strlen(argv[1]) > 180)
        return 1;
    if (!fatInitDefault()) return 1;
    cfg.name = "WiiStation tests";
    cfg.version = __DATE__ " " __TIME__;
    cfg.crash_reload_s = 3;
    if (hbc_agent_init(&cfg)) return 1;
    hbc_agent_net_wait(20000);
    printf("Achievement engine hardware regression suite\n");
    if (achievements_test_main(argc, argv)) return 1;
    /* Worst configured set, with active false conditions and repeated RAM reads.
     * Measure batches to reduce timer overhead; this is a runtime microbenchmark,
     * not a measurement of complete emulator throughput. */
    achievementsEnabled = 1;
    snprintf(achievementFile, sizeof achievementFile, "%s/benchmark.txt", argv[1]);
    snprintf(achievementLog, sizeof achievementLog, "%s/benchmark.unlocks", argv[1]);
    set = fopen(achievementFile, "w");
    if (!set) return 1;
    fprintf(set, "Game=TEST12345\n");
    for (i = 0; i < 128; ++i)
        fprintf(set, "%u|Bench %u|0xH%06x=123\n", i + 1, i, 0x1000 + i);
    fclose(set);
    memset(psxM + 0x1000, 0, 128);
    if (achievements_load("TEST12345") != 128) return 1;
    for (i = 0; i < 1000; ++i) {
        start = gettime();
        for (j = 0; j < 100; ++j) achievements_frame();
        samples[i] = ticks_to_microsecs(gettime() - start);
        hbc_agent_alive();
    }
    qsort(samples, 1000, sizeof(samples[0]), compare);
    achievements_close();
    snprintf(path, sizeof path, "%s/result.json", argv[1]);
    result = fopen(path, "w");
    if (!result) return 1;
    fprintf(result, "{\"pass\":true,\"suite\":\"achievements\",\"byte_order\":\"big\","
        "\"active_definitions\":128,\"batch_frames\":100,"
        "\"batch_us_p50\":%u,\"batch_us_p95\":%u,\"batch_us_p99\":%u}\n",
        samples[500], samples[950], samples[990]);
    if (fclose(result)) return 1;
    fatUnmount("sd");
    return 0;
}
