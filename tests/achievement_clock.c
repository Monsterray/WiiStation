#include "achievement_clock.h"
#define UTC_MIN INT64_C(1577836800) /* 2020-01-01 */
#define UTC_MAX INT64_C(4102444799) /* 2099-12-31, also check time_t in TLS adapter */
static int ready;
static int64_t anchor;
static uint64_t started, last;
void achievement_clock_clear(void) { ready = 0; }
int achievement_clock_start(const char *s, int64_t rtc, uint64_t system_ms)
{
    int sign = 1, minutes = 0;
    unsigned digits = 0;
    achievement_clock_clear();
    if (!s || !*s || system_ms == UINT64_MAX) return -1;
    if (*s == '-' || *s == '+') { if (*s == '-') sign = -1; ++s; }
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || ++digits > 3) return -1;
        minutes = minutes * 10 + *s - '0';
    }
    if (!digits || minutes > 840 || rtc < UTC_MIN - 50400 || rtc > UTC_MAX + 50400) return -1;
    anchor = rtc + sign * minutes * 60;
    if (anchor < UTC_MIN || anchor > UTC_MAX) return -1;
    started = last = system_ms;
    ready = 1;
    return 0;
}
int64_t achievement_clock_now(uint64_t system_ms)
{
    uint64_t seconds;
    if (!ready) return -1;
    if (system_ms < last) { achievement_clock_clear(); return -1; }
    last = system_ms;
    seconds = (system_ms - started) / 1000;
    if (seconds > (uint64_t)(UTC_MAX - anchor)) { achievement_clock_clear(); return -1; }
    return anchor + (int64_t)seconds;
}
