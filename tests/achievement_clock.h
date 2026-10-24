#ifndef WIISTATION_ACHIEVEMENT_CLOCK_H
#define WIISTATION_ACHIEVEMENT_CLOCK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Signed minutes ADDED to the console's wall-clock epoch, not a TZ identifier.
 * One serialized TLS owner. This never writes the RTC or system timebase. */
int achievement_clock_start(const char *correction, int64_t rtc, uint64_t system_ms);
int64_t achievement_clock_now(uint64_t system_ms); /* -1 = refuse TLS */
void achievement_clock_clear(void);
/* An HTTP Date value ("Sat, 24 Oct 2026 08:12:33 GMT", the IMF-fixdate of RFC 9110) as UTC
 * seconds; -1 when it is not one. */
int64_t achievement_clock_http_date(const char *s);
/* The correction that turns the console's clock (rtc) into the server's UTC: the difference
 * in whole quarter hours (time zones are), as "+420" / "-300" / "0" in out. -1 when the
 * difference is over 14 hours: then the console's date is wrong, and no time zone explains it. */
int achievement_clock_learn(int64_t server_utc, int64_t rtc, char out[8]);
#ifdef __cplusplus
}
#endif
#endif
