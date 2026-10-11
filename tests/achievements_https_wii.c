/* Credential-free HTTPS probe. The default binary port uses fake entropy; the
 * source-built NV variant uses public dummy seeds. Neither accepts credentials. */
#include <gccore.h>
#include <fat.h>
#include <wiisocket.h>
#include <curl/curl.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#ifdef ACHIEVEMENTS_NV_PROBE
#include "../achievement_seed.h"
#include <mbedtls/platform.h>
#include <mbedtls/platform_time.h>
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}
#endif
static void checkpoint(FILE *f) { fflush(f); fsync(fileno(f)); }
static size_t bounded_body(char *p, size_t size, size_t count, void *data) {
    size_t *used = data, n = size * count;
    (void)p;
    if (size && n / size != count) return 0;
    if (n > 65536 - *used) return 0;
    *used += n;
    return n;
}
static int probe(FILE *f, const char *name, const char *url, const char *ca, int expected_failure) {
    CURL *c = curl_easy_init();
    CURLcode r;
    char error[CURL_ERROR_SIZE] = {0};
    long http = 0, verify = 0;
    double total = 0, handshake = 0;
    size_t bytes = 0;
    int ok;
    if (!c) return 0;
#define SET(option, value) do { if (curl_easy_setopt(c, option, value) != CURLE_OK) { curl_easy_cleanup(c); return 0; } } while (0)
    SET(CURLOPT_URL, url);
    SET(CURLOPT_ERRORBUFFER, error);
    SET(CURLOPT_CAINFO, ca);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_PROTOCOLS_STR, "https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_CONNECTTIMEOUT, 10L);
    SET(CURLOPT_TIMEOUT, 20L);
    SET(CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    SET(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    SET(CURLOPT_USERAGENT, "WiiStation/2026.10.11 HTTPS-probe");
    SET(CURLOPT_WRITEFUNCTION, bounded_body);
    SET(CURLOPT_WRITEDATA, &bytes);
    r = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_getinfo(c, CURLINFO_SSL_VERIFYRESULT, &verify);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &total);
    curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &handshake);
    ok = expected_failure ? (r == CURLE_PEER_FAILED_VERIFICATION && http == 0)
                          : (r == CURLE_OK && verify == 0 && http > 0);
    fprintf(f, "%s: curl=%d http=%ld verify=%ld bytes=%u total_ms=%.2f tls_ms=%.2f pass=%d\n",
            name, (int)r, http, verify, (unsigned)bytes, total * 1000, handshake * 1000, ok);
    fprintf(f, "diagnostic: %s\n", error);
    checkpoint(f);
    curl_easy_cleanup(c);
    hbc_agent_alive();
    return ok;
}
int main(int argc, char **argv) {
    GXRModeObj *m;
    char ca[256], path[256];
    FILE *f;
    hbc_agent_config cfg = {0};
    int pass = 1;
    VIDEO_Init(); m = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
    console_init(fb, 20, 20, m->fbWidth, m->xfbHeight, m->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(m); VIDEO_SetNextFramebuffer(fb); VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    if (argc != 2 || strncmp(argv[1], "sd:/wiistation/bench-", 21) || strlen(argv[1]) > 180 || !fatInitDefault()) return 1;
    cfg.name = "WiiStation HTTPS probe"; cfg.version = __DATE__ " " __TIME__; cfg.crash_reload_s = 3;
    if (hbc_agent_init(&cfg)) return 1;
    hbc_agent_net_wait(20000);
    snprintf(ca, sizeof ca, "%s/cacert.pem", argv[1]);
    snprintf(path, sizeof path, "%s/probe.log", argv[1]);
    f = fopen(path, "w"); if (!f) return 1;
#ifdef ACHIEVEMENTS_NV_PROBE
    char seed[256];
    FILE *fixture;
    snprintf(seed, sizeof seed, "%s/public-seed", argv[1]);
    fixture = fopen(seed, "wb"); if (!fixture) return 1;
    for (unsigned i = 0; i < ACHIEVEMENT_SEED_SIZE; ++i)
        if (fputc((unsigned char)i, fixture) == EOF) return 1;
    if (fclose(fixture) || achievement_seed_begin(seed) ||
        mbedtls_platform_set_nv_seed(achievement_seed_read, achievement_seed_write)) return 1;
    fprintf(f, "clock=%lld entropy=NV-only-public-dummy-source-build\n", (long long)time(NULL));
#else
    fprintf(f, "clock=%lld entropy=fake-public-probe-only\n", (long long)time(NULL));
#endif
    checkpoint(f);
    if (wiisocket_init() < 0) return 1;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    pass &= probe(f, "ra", "https://retroachievements.org/robots.txt", ca, 0);
    pass &= probe(f, "valid-control", "https://sha256.badssl.com/", ca, 0);
    pass &= probe(f, "wrong-host", "https://wrong.host.badssl.com/", ca, 1);
    pass &= probe(f, "untrusted", "https://untrusted-root.badssl.com/", ca, 1);
    pass &= probe(f, "expired", "https://expired.badssl.com/", ca, 1);
    curl_global_cleanup();
#ifdef ACHIEVEMENTS_NV_PROBE
    pass &= achievement_seed_end() == 0;
    pass &= remove(seed) == 0;
#endif
    fclose(f);
    snprintf(path, sizeof path, "%s/result.json", argv[1]);
    f = fopen(path, "w"); if (!f) return 1;
    fprintf(f, "{\"pass\":%s,\"suite\":\"https-public\",\"authenticated_transport_ready\":false}\n", pass ? "true" : "false"); fclose(f);
    /* Finish seed and result I/O before removing the adapter's devoptab entry. */
    wiisocket_deinit();
    fatUnmount("sd");
    return 0;
}
