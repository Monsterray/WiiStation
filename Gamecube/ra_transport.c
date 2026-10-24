/* The HTTPS transport for Official mode (ra_transport.h).
 *
 * This build has no HTTPS stack yet: the Mbed TLS + curl build proven on the bench Wii
 * (scripts/build-achievements-native-https-test.sh) still needs a secret TLS seed for each
 * Wii before it may carry a password or a token (Docs/ACHIEVEMENTS_RESEARCH.md, "TLS
 * entropy"). Until then every request completes with a client error, once, at the next poll,
 * so rc_client reports "no connection" and the game runs on. */
#include <string.h>
#include "ra_transport.h"

#define MAX_PENDING 8

static struct {
	rc_client_server_callback_t done;
	void *data;
} pending[MAX_PENDING];
static int npending;

static void fail(rc_client_server_callback_t done, void *data)
{
	rc_api_server_response_t r;
	memset(&r, 0, sizeof r);
	r.body = "";
	r.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
	done(&r, data);
}

void ra_transport_post(const char *url, const char *post, const char *content_type,
                       rc_client_server_callback_t done, void *done_data)
{
	(void)url; (void)post; (void)content_type;
	if (npending == MAX_PENDING) {   /* never dropped: completed now instead */
		fail(done, done_data);
		return;
	}
	pending[npending].done = done;
	pending[npending++].data = done_data;
}

void ra_transport_poll(void)
{
	/* a callback may post again: deliver a copy, so each request completes exactly once */
	int i, n = npending;
	struct { rc_client_server_callback_t done; void *data; } now[MAX_PENDING];
	memcpy(now, pending, sizeof now[0] * n);
	npending = 0;
	for (i = 0; i < n; i++)
		fail(now[i].done, now[i].data);
}

int ra_transport_available(void) { return 0; }
