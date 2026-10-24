/* scripts/test-achievements.sh: the HTTP Date parser and the time zone learnt from it
 * (tests/achievement_clock.c, built into Gamecube/ra_transport.c). */
#include "achievement_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char z[8];
    const int64_t t = INT64_C(1793523600);   /* 2026-11-01 09:00:00 UTC */
    assert(achievement_clock_http_date("Sun, 01 Nov 2026 09:00:00 GMT") == t);
    assert(achievement_clock_http_date(" Sun, 01 Nov 2026 09:00:00 GMT") == t);
    assert(achievement_clock_http_date("Thu, 01 Jan 1970 00:00:00 GMT") == -1);   /* before 2020: refused */
    assert(achievement_clock_http_date("Sat, 29 Feb 2028 23:59:60 GMT") == INT64_C(1835481599));
    assert(achievement_clock_http_date("Sun, 01 Nov 2026 09:00:00 UTC") == -1);
    assert(achievement_clock_http_date("Sun, 01 Foo 2026 09:00:00 GMT") == -1);
    assert(achievement_clock_http_date("Sun, 1 Nov 2026 09:00:00 GMT") == -1);
    assert(achievement_clock_http_date("") == -1 && achievement_clock_http_date(NULL) == -1);
    /* a Wii in California keeps PDT, 7 h behind UTC, plus 40 s of drift */
    assert(achievement_clock_learn(t, t - 7 * 3600 + 40, z) == 0 && !strcmp(z, "+420"));
    assert(achievement_clock_learn(t, t - 8 * 3600, z) == 0 && !strcmp(z, "+480"));
    assert(achievement_clock_learn(t, t + 5 * 3600 + 1800, z) == 0 && !strcmp(z, "-330"));   /* India */
    assert(achievement_clock_learn(t, t, z) == 0 && !strcmp(z, "0"));
    assert(achievement_clock_learn(t, t - 14 * 3600, z) == 0 && !strcmp(z, "+840"));
    assert(achievement_clock_learn(t, t - 15 * 3600, z) == -1);   /* no time zone: the date is wrong */
    assert(achievement_clock_learn(t, t + 3 * 86400, z) == -1);
    assert(achievement_clock_learn(-1, t, z) == -1);
    /* the learnt correction starts the clock */
    assert(achievement_clock_learn(t, t - 7 * 3600, z) == 0 && achievement_clock_start(z, t - 7 * 3600, 1000) == 0);
    assert(achievement_clock_now(1000) == t);
    puts("achievement_clock_learn_test: ok");
    return 0;
}
