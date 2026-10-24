/* The HTTPS transport for Official mode (achievements_official.c). The emulator thread posts a
 * request and goes on; a worker does DNS, TLS and the transfer; ra_transport_poll(), on the
 * emulator thread, hands each response to its callback exactly once. The worker never reads
 * emulated RAM. Docs/ACHIEVEMENTS.md, "Design rules for Official mode", rule 4. */
#ifndef RA_TRANSPORT_H
#define RA_TRANSPORT_H

#include "../deps/rcheevos/include/rc_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* url, post (NULL: GET) and content_type are copied before this returns */
void ra_transport_post(const char *url, const char *post, const char *content_type,
                       rc_client_server_callback_t done, void *done_data);
void ra_transport_poll(void);       /* deliver completed responses (emulator thread) */
int ra_transport_available(void);   /* 0: this build has no HTTPS stack, every request fails */

#ifdef __cplusplus
}
#endif
#endif
