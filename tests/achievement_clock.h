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
#ifdef __cplusplus
}
#endif
#endif
