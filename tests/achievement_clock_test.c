#define _POSIX_C_SOURCE 200809L
#include "achievement_clock_test.h"
#include "achievement_clock.h"
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509.h>
#include <time.h>

static uint64_t (*system_ms)(void);
static mbedtls_time_t tls_time(mbedtls_time_t *out)
{
    int64_t utc = system_ms ? achievement_clock_now(system_ms()) : -1;
    mbedtls_time_t t = (mbedtls_time_t)utc;
    if ((int64_t)t != utc) t = (mbedtls_time_t)-1;
    if (out) *out = t;
    return t;
}
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *t, struct tm *out)
{
    /* Unknown UTC must fail both certificate date checks, rather than become
     * a seemingly valid 1969 timestamp when time() returns -1. */
    if (!t || (int64_t)*t < INT64_C(1577836800) || (int64_t)*t > INT64_C(4102444799)) return NULL;
    return gmtime_r(t, out);
}
int achievement_clock_tls_install(uint64_t (*milliseconds)(void))
{
    achievement_clock_clear();
    system_ms = milliseconds;
    return mbedtls_platform_set_time(tls_time);
}

static uint64_t fixture_ms;
static uint64_t fixture_counter(void) { return fixture_ms; }
int achievement_clock_test(FILE *f)
{
    int failures = 0;
#define CHECK(expr) do { if (!(expr)) { fprintf(f, "clock FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)
    mbedtls_x509_time boundary = {2026, 11, 1, 9, 0, 0}; /* US fall-back boundary, UTC */
    const int64_t utc = INT64_C(1793523600);
    fixture_ms = 1234;
    CHECK(!achievement_clock_tls_install(fixture_counter));
    CHECK(mbedtls_x509_time_is_future(&boundary));
    CHECK(mbedtls_x509_time_is_past(&boundary)); /* Unknown is fail closed. */
    const char *bad[] = {"", "-", "+", "841", "-841", "420x", " 420", "420\n", "9999"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        CHECK(achievement_clock_start(bad[i], utc, fixture_ms) < 0);
        CHECK(achievement_clock_now(fixture_ms) < 0);
    }
    CHECK(achievement_clock_start("0", utc, UINT64_MAX) < 0);
    CHECK(achievement_clock_start("0", INT64_MIN, fixture_ms) < 0);
    CHECK(achievement_clock_start("0", INT64_MAX, fixture_ms) < 0);
    CHECK(!achievement_clock_start("+420", utc - 25200 - 1, fixture_ms));
    CHECK(achievement_clock_now(fixture_ms) == utc - 1);
    CHECK(mbedtls_x509_time_is_future(&boundary));
    CHECK(!mbedtls_x509_time_is_past(&boundary));
    fixture_ms += 1000;
    CHECK(achievement_clock_now(fixture_ms) == utc);
    CHECK(!mbedtls_x509_time_is_future(&boundary));
    CHECK(!mbedtls_x509_time_is_past(&boundary));
    fixture_ms += 1000;
    CHECK(!mbedtls_x509_time_is_future(&boundary));
    CHECK(mbedtls_x509_time_is_past(&boundary));
    CHECK(!achievement_clock_start("480", utc - 28800, fixture_ms)); /* winter */
    CHECK(achievement_clock_now(fixture_ms) == utc);
    CHECK(!achievement_clock_start("-330", utc + 19800, fixture_ms)); /* India */
    CHECK(achievement_clock_now(fixture_ms) == utc);
    CHECK(!achievement_clock_start("0", utc, fixture_ms));
    CHECK(achievement_clock_now(--fixture_ms) < 0);
    CHECK(mbedtls_x509_time_is_past(&boundary) && mbedtls_x509_time_is_future(&boundary));
    CHECK(!achievement_clock_start("0", utc, fixture_ms));
    CHECK(achievement_clock_now(UINT64_MAX) < 0);
    achievement_clock_clear();
    fprintf(f, "clock: correction parsing, DST offsets, monotonic advancement, rollback/overflow and X509 +/-1s boundary: failures=%d\n", failures);
    return failures;
#undef CHECK
}
#ifdef ACHIEVEMENT_CLOCK_HOST_TEST
int main(void) { return achievement_clock_test(stdout) != 0; }
#endif
