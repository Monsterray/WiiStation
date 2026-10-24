#include "achievement_clock.h"
#include <stdio.h>
#include <string.h>
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
/* days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil) */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    int64_t era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int two(const char *s) { return s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' ? (s[0] - '0') * 10 + s[1] - '0' : -1; }

int64_t achievement_clock_http_date(const char *s)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    unsigned m;
    int day, hh, mm, ss, y1, y2;
    int64_t utc;
    /* "Www, DD Mon YYYY HH:MM:SS GMT": fixed positions */
    if (!s) return -1;
    while (*s == ' ') ++s;
    if (strlen(s) < 29) return -1;
    if (s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' || s[19] != ':' || s[22] != ':' ||
        s[25] != ' ' || s[26] != 'G' || s[27] != 'M' || s[28] != 'T')
        return -1;
    for (m = 0; m < 12 && (months[m * 3] != s[8] || months[m * 3 + 1] != s[9] || months[m * 3 + 2] != s[10]); m++)
        ;
    day = two(s + 5); y1 = two(s + 12); y2 = two(s + 14); hh = two(s + 17); mm = two(s + 20); ss = two(s + 23);
    if (m == 12 || day < 1 || day > 31 || y1 < 0 || y2 < 0 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60)
        return -1;
    utc = days_from_civil(y1 * 100 + y2, m + 1, (unsigned)day) * 86400 + hh * 3600 + mm * 60 + (ss > 59 ? 59 : ss);
    return utc < UTC_MIN || utc > UTC_MAX ? -1 : utc;
}

int achievement_clock_learn(int64_t server_utc, int64_t rtc, char out[8])
{
    int64_t diff = server_utc - rtc, quarters;
    if (server_utc < UTC_MIN || server_utc > UTC_MAX || diff > 50400 + 450 || diff < -50400 - 450) return -1;
    quarters = (diff >= 0 ? diff + 450 : diff - 450) / 900;   /* to the nearest quarter hour */
    if (quarters > 56) quarters = 56;
    if (quarters < -56) quarters = -56;
    if (quarters > 0) snprintf(out, 8, "+%d", (int)(quarters * 15));
    else snprintf(out, 8, "%d", (int)(quarters * 15));
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
