/* The HTTPS transport for Official mode (ra_transport.h).
 *
 * With RA_HTTPS (Makefile_Wii links Mbed TLS 3.6.4 + curl 8.16.0 from build_ogc3/deps/ra_https,
 * built by scripts/build-ra-https.sh), a worker thread does each request with curl: TLS 1.2+,
 * certificates checked against sd:/wiistation/achievements/cacert.pem, HTTPS only, no
 * redirects, bounded bodies and timeouts. The emulator thread only queues and, in
 * ra_transport_poll(), hands finished responses to rc_client.
 *
 * Mbed TLS takes all its entropy from a secret seed file for this Wii,
 * sd:/wiistation/achievements/tls_seed.bin (64 bytes from a PC's secure generator), and
 * rewrites it before each use (Docs/ACHIEVEMENTS_RESEARCH.md, "TLS entropy"). No seed, no
 * connection. Certificate dates use the Wii's clock plus AchievementUTCOffset (minutes).
 *
 * Without RA_HTTPS every request fails once at the next poll, and the game runs on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ra_transport.h"
#include <stdbool.h>
#include "../mem2_manager.h"   /* the requests, curl and Mbed TLS live in MEM2: MEM1 is the emulator's */

extern void SysPrintf(const char *fmt, ...);
char achievementUTCOffset[256];  /* the setting (CONFIG_STRING_SIZE): minutes to add to the Wii's clock for UTC, "+420" */

typedef struct {
	char *url, *post, *ctype;              /* copies: rc_client frees its request after the call */
	rc_client_server_callback_t done;
	void *data;
	char *body;                            /* the response, filled by the worker */
	size_t len;
	int status;                            /* HTTP status, or RC_API_SERVER_RESPONSE_* */
	long long server_utc;                  /* the response's Date header, UTC seconds; -1 none */
	volatile int state;                    /* 0 free, 1 queued, 2 done */
} Request;

#define MAX_REQUESTS 8
static Request req[MAX_REQUESTS];

static void finish(Request *r)
{
	rc_api_server_response_t resp;
	rc_client_server_callback_t done = r->done;
	void *data = r->data;
	memset(&resp, 0, sizeof resp);
	resp.body = r->body ? r->body : "";
	resp.body_length = r->body ? r->len : 0;
	resp.http_status_code = r->status;
	done(&resp, data);
	_mem2_free(r->url); _mem2_free(r->post); _mem2_free(r->ctype); _mem2_free(r->body);
	memset(r, 0, sizeof *r);   /* state 0: free again */
}

/* Answers that never went out (Local mode: the card's copy, a refusal): a small queue of its own,
 * on the emulator thread only, delivered by ra_transport_poll like any response */
static Request canned[MAX_REQUESTS];

void ra_transport_complete(rc_client_server_callback_t done, void *done_data, int status, const char *body, size_t len)
{
	int i;
	Request r;
	for (i = 0; i < MAX_REQUESTS && canned[i].state; i++) ;
	memset(&r, 0, sizeof r);
	r.done = done; r.data = done_data; r.status = status;
	if (body && len && (r.body = _mem2_malloc((uint32_t)len + 1))) {
		memcpy(r.body, body, len);
		r.body[len] = 0;
		r.len = len;
	}
	r.state = 2;
	if (i == MAX_REQUESTS) {   /* never dropped: completed now instead */
		finish(&r);
		return;
	}
	canned[i] = r;
}

static void poll_canned(void)
{
	int i;
	for (i = 0; i < MAX_REQUESTS; i++)
		if (canned[i].state == 2) finish(&canned[i]);
}

#ifndef RA_HTTPS

void ra_transport_post(const char *url, const char *post, const char *content_type,
                       rc_client_server_callback_t done, void *done_data)
{
	int i;
	(void)url; (void)post; (void)content_type;
	for (i = 0; i < MAX_REQUESTS && req[i].state; i++) ;
	if (i == MAX_REQUESTS) {   /* never dropped: completed now instead */
		Request r;
		memset(&r, 0, sizeof r);
		r.done = done; r.data = done_data; r.status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
		finish(&r);
		return;
	}
	req[i].done = done;
	req[i].data = done_data;
	req[i].status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
	req[i].state = 2;
}

void ra_transport_poll(void)
{
	int i;
	poll_canned();
	for (i = 0; i < MAX_REQUESTS; i++)
		if (req[i].state == 2) finish(&req[i]);
}

int ra_transport_available(void) { return 0; }

#else /* RA_HTTPS */

#include <gccore.h>
#include <network.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/cond.h>
#include <ogc/mutex.h>
#include <curl/curl.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_time.h>
#include "ogc_compat.h"   /* ws_if_config() */
#include "hbc_home.h"     /* hbc_home_net_wait(): the HBC agent's network start comes first */
#include "version.h"

/* The seed storage and the UTC clock proven on the bench Wii (tests/, 2026-10-11 and
 * 2026-10-23): one copy of the code, built into the product here. */
#include "../tests/achievement_seed.c"
#include "../tests/achievement_clock.c"

#define ACH_DIR "sd:/wiistation/achievements/"
#define SEED_FILE ACH_DIR "tls_seed.bin"
#define CA_FILE ACH_DIR "cacert.pem"
#define ZONE_FILE ACH_DIR "utc_offset.txt"   /* the learned correction: this Wii's clock to UTC */
#define MAX_BODY (2 * 1024 * 1024)

static lwp_t worker = LWP_THREAD_NULL;
static mutex_t lock = LWP_MUTEX_NULL;
static cond_t wake = LWP_COND_NULL;
static int ready = -1;   /* -1 not started, 0 failed (no seed, clock or network), 1 ready */
static char user_agent[128];
/* Marker scripts/make_release.py looks for in the DOL: present only in the real (curl + Mbed TLS) transport. */
static const char ws_ra_transport_marker[] = "WS-RA-TRANSPORT:curl+mbedtls";

/* ---- libogc v3 socket gaps that curl meets (bench jobs 20261023-13xxxx, Docs/ACHIEVEMENTS_NOTES.md) ---- */

/* v3 has poll() but no select(); curl, built with poll, calls select() only to sleep */
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
	if (n || r || w || e || !tv || tv->tv_sec < 0 || tv->tv_usec < 0) { errno = ENOSYS; return -1; }
	if (tv->tv_sec) sleep(tv->tv_sec);
	usleep(tv->tv_usec);
	return 0;
}

/* v3's getsockopt() always fails; curl asks only SO_ERROR, after poll() saw the socket
 * writable with no error. ponytail: a connect error poll misses shows at the TLS handshake. */
int getsockopt(int s, int level, int name, void *value, socklen_t *size)
{
	(void)s;
	if (level != SOL_SOCKET || name != SO_ERROR || !value || !size || *size < sizeof(int)) { errno = ENOPROTOOPT; return -1; }
	*(int *)value = 0;
	*size = sizeof(int);
	return 0;
}

/* ---- the clock: certificate dates ---- */

static uint64_t system_ms(void)
{
	s64 t = SYS_Time();
	return t < 0 ? UINT64_MAX : ticks_to_millisecs(t);
}

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)system_ms(); }

static mbedtls_time_t tls_time(mbedtls_time_t *out)
{
	int64_t utc = achievement_clock_now(system_ms());
	mbedtls_time_t t = (mbedtls_time_t)utc;
	if ((int64_t)t != utc) t = (mbedtls_time_t)-1;
	if (out) *out = t;
	return t;
}

/* An unknown UTC fails both certificate date checks, never passes as 1969 */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *t, struct tm *out)
{
	if (!t || (int64_t)*t < INT64_C(1577836800) || (int64_t)*t > INT64_C(4102444799)) return NULL;
	return gmtime_r(t, out);
}

/* ---- the worker ---- */

/* A power cut leaves the seed's ownership folder behind. Empty, no write was in flight and it
 * is removed; holding a half-written seed, the transport stays off until a fresh seed comes. */
static int seed_open(void)
{
	char path[300];
	if (achievement_seed_begin(SEED_FILE) == 0) return 0;
	snprintf(path, sizeof path, "%s.busy/next", SEED_FILE);
	if (access(path, F_OK) == 0) return -1;
	snprintf(path, sizeof path, "%s.busy/old", SEED_FILE);
	if (access(path, F_OK) == 0) return -1;
	snprintf(path, sizeof path, "%s.busy", SEED_FILE);
	if (rmdir(path) != 0) return -1;
	return achievement_seed_begin(SEED_FILE);
}

/* The correction the clock starts with: the setting AchievementUTCOffset when it is set (it
 * wins, and nothing is learned), else the one learned last time (utc_offset.txt), else 0. A
 * wrong guess costs nothing but the hours just after a certificate is issued: certificate
 * dates are checked to the second, and a clock some hours slow sees a new one as not valid yet.
 * The first verified answer puts it right (learn_zone). */
static char zone[8];
static int zone_fixed;

static void zone_load(void)
{
	FILE *f;
	zone_fixed = achievementUTCOffset[0] != 0;
	snprintf(zone, sizeof zone, "%s", zone_fixed ? achievementUTCOffset : "0");
	if (zone_fixed || !(f = fopen(ZONE_FILE, "r"))) return;
	if (fgets(zone, sizeof zone, f)) zone[strcspn(zone, "\r\n")] = 0;
	fclose(f);
	if (!zone[0]) snprintf(zone, sizeof zone, "0");
}

/* A Date header that came over a verified TLS connection: the server's UTC. The Wii's clock
 * keeps local time, so the difference is the time zone (summer time included): learned to the
 * quarter hour, bounded to 14 hours, kept on the card for the next start. A server that lies
 * moves the clock by at most that, never a certificate's validity by years. */
static void learn_zone(int64_t server_utc)
{
	char got[8];
	FILE *f;
	if (zone_fixed || server_utc < 0) return;
	if (achievement_clock_learn(server_utc, (int64_t)time(NULL), got) != 0) {
		SysPrintf("Achievements: the Wii's clock is more than 14 hours from the server's; set the date\n");
		return;
	}
	if (!strcmp(got, zone)) return;
	if (achievement_clock_start(got, (int64_t)time(NULL), system_ms()) != 0) return;
	SysPrintf("Achievements: the Wii's clock is UTC%s minutes (learned from the server)\n", got);
	snprintf(zone, sizeof zone, "%s", got);
	if ((f = fopen(ZONE_FILE ".new", "w"))) {
		int ok = fprintf(f, "%s\n", zone) > 0;
		ok = fclose(f) == 0 && ok;
		remove(ok ? ZONE_FILE : ZONE_FILE ".new");
		if (ok) rename(ZONE_FILE ".new", ZONE_FILE);
	}
}

/* Memory: MEM1 is nearly full while a game runs (32 KB free in CTR, 2026-10-24), and Mbed TLS
 * failed to parse the CA bundle for a second host there ("PK - Memory allocation failed", curl
 * 77: no badge). curl takes these through curl_global_init_mem; Mbed TLS's calloc and free are
 * renamed to ws_tls_calloc/ws_tls_free in its archives (scripts/build-ra-https.sh). The MEM2
 * heap locks itself, so the worker thread may use it. */
static void *curl_m_malloc(size_t n) { return _mem2_malloc(n); }
static void *curl_m_realloc(void *p, size_t n) { return _mem2_realloc(p, n); }
static void *curl_m_calloc(size_t n, size_t size) { return _mem2_calloc(n, size); }
void *ws_tls_calloc(size_t n, size_t size) { return _mem2_calloc(n, size); }
void ws_tls_free(void *p) { _mem2_free(p); }

static int start(void)
{
	char ip[16];
	FILE *f = fopen(SEED_FILE, "rb");
	if (!f) {
		SysPrintf("Achievements: no TLS seed (%s); see Docs/ACHIEVEMENTS.md\n", SEED_FILE);
		return 0;
	}
	fclose(f);
	if (seed_open() != 0 || mbedtls_platform_set_nv_seed(achievement_seed_read, achievement_seed_write)) {
		SysPrintf("Achievements: the TLS seed was left half written; put a fresh one there\n");
		return 0;
	}
	zone_load();
	if (mbedtls_platform_set_time(tls_time) ||
	    achievement_clock_start(zone, (int64_t)time(NULL), system_ms())) {
		SysPrintf("Achievements: the clock or AchievementUTCOffset is out of range\n");
		return 0;
	}
	hbc_home_net_wait();
	if (ws_if_config(ip, NULL, NULL, true) < 0) {
		SysPrintf("Achievements: no network\n");
		return 0;
	}
	if (curl_global_init_mem(CURL_GLOBAL_DEFAULT, curl_m_malloc, _mem2_free, curl_m_realloc, _mem2_strdup,
	                         curl_m_calloc) != CURLE_OK) return 0;
	return 1;
}

/* The response's Date header (case-insensitive name), for learn_zone */
static size_t take_header(char *p, size_t size, size_t count, void *data)
{
	Request *r = data;
	size_t n = size * count;
	char value[40];
	if (n > 5 && n < 5 + sizeof value && !strncasecmp(p, "date:", 5)) {
		memcpy(value, p + 5, n - 5);
		value[n - 5] = 0;
		value[strcspn(value, "\r\n")] = 0;
		r->server_utc = achievement_clock_http_date(value);
	}
	return n;
}

static size_t take_body(char *p, size_t size, size_t count, void *data)
{
	Request *r = data;
	size_t n = size * count;
	char *grown;
	if (size && n / size != count) return 0;
	if (n > MAX_BODY - r->len) return 0;   /* too big: the transfer fails */
	grown = _mem2_realloc(r->body, r->len + n + 1);
	if (!grown) return 0;
	r->body = grown;
	memcpy(r->body + r->len, p, n);
	r->len += n;
	r->body[r->len] = 0;
	return n;
}

#ifdef PERF_PROF
/* Debug builds: curl's own account of the connection (tty.log). Only its informational
 * lines: never the headers or the bodies, which carry the password or the token. */
static int curl_note(CURL *c, curl_infotype type, char *text, size_t len, void *user)
{
	(void)c; (void)user;
	if (type == CURLINFO_TEXT && len)
		SysPrintf("Achievements: curl: %.*s", (int)(len > 160 ? 160 : len), text);
	return 0;
}
#endif

static void perform(Request *r)
{
	/* One handle for every request, reset each time: its connection to the server stays open
	 * between requests, so only the first one pays the TLS handshake (0.55-2.2 s on the bench
	 * Wii, 2026-10-23); a closed connection is opened again by curl. */
	static CURL *c;
	CURLcode res;
	long http = 0;
	struct curl_slist *headers = NULL;
	if (!c) c = curl_easy_init();
	else curl_easy_reset(c);
	if (!c) { r->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR; return; }
	curl_easy_setopt(c, CURLOPT_URL, r->url);
	curl_easy_setopt(c, CURLOPT_CAINFO, CA_FILE);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");   /* never credentials over plain HTTP */
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, take_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, take_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, r);
	r->server_utc = -1;
#ifdef PERF_PROF
	curl_easy_setopt(c, CURLOPT_DEBUGFUNCTION, curl_note);
	curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
#endif
	if (r->post) {
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, r->post);
		if (r->ctype) {
			char h[128];
			snprintf(h, sizeof h, "Content-Type: %s", r->ctype);
			headers = curl_slist_append(headers, h);
			curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
		}
	}
	res = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
	if (res == CURLE_OK) {
		r->status = (int)http;
		learn_zone(r->server_utc);   /* verified TLS: the peer and its Date are the server's */
	}
	else if (res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT ||
	         res == CURLE_SEND_ERROR || res == CURLE_RECV_ERROR || res == CURLE_GOT_NOTHING)
		r->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;   /* rc_client tries again */
	else
		r->status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
	if (res != CURLE_OK)   /* the operation and the code only: never the URL or the body */
		SysPrintf("Achievements: HTTPS request failed (curl %d)\n", (int)res);
#ifdef PERF_PROF
	{	/* the cost of a request: wall time, of which the TLS handshake; bytes back */
		double total = 0, tls = 0;
		curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &total);
		curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &tls);
		SysPrintf("Achievements: request http=%d total_ms=%d tls_ms=%d bytes=%u\n", r->status,
		          (int)(total * 1000), (int)(tls * 1000), (unsigned)r->len);
	}
#endif
	curl_slist_free_all(headers);
}

static void *work(void *arg)
{
	(void)arg;
	ready = start();
	for (;;) {
		Request *r = NULL;
		int i;
		LWP_MutexLock(lock);
		for (;;) {
			for (i = 0; i < MAX_REQUESTS && !r; i++)
				if (req[i].state == 1) r = &req[i];
			if (r) break;
			LWP_CondWait(wake, lock);
		}
		LWP_MutexUnlock(lock);
		if (ready == 1)
			perform(r);
		else
			r->status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
		r->state = 2;   /* ra_transport_poll takes it from here */
	}
	return NULL;
}

static char *copy(const char *s) { return s ? _mem2_strdup(s) : NULL; }

void ra_transport_post(const char *url, const char *post, const char *content_type,
                       rc_client_server_callback_t done, void *done_data)
{
	int i;
	int n;
	if (worker == LWP_THREAD_NULL) {
		static u8 stack[128 * 1024] __attribute__((aligned(32)));   /* a TLS handshake is stack hungry */
		LWP_MutexInit(&lock, false);
		LWP_CondInit(&wake);
		/* "<product>/<version> (<system>) <extensions>": rcheevos asks for its own clause; it needs no client */
		n = snprintf(user_agent, sizeof user_agent, "WiiStation/%s (Wii) ", WIISTATION_VERSION);
		if (n > 0 && (size_t)n < sizeof user_agent)
			rc_client_get_user_agent_clause(NULL, user_agent + n, sizeof user_agent - n);
		{   /* keep the marker in release builds, where SysPrintf compiles away */
			static const char *volatile keep = ws_ra_transport_marker;
			(void)*keep;
		}
		SysPrintf("Achievements: User-Agent: %s (%s)\n", user_agent, ws_ra_transport_marker);
		LWP_CreateThread(&worker, work, NULL, stack, sizeof stack, 48);
	}
	LWP_MutexLock(lock);
	for (i = 0; i < MAX_REQUESTS && req[i].state; i++) ;
	if (i < MAX_REQUESTS) {
		req[i].url = copy(url);
		req[i].post = copy(post);
		req[i].ctype = copy(content_type);
		req[i].done = done;
		req[i].data = done_data;
		req[i].state = 1;
		LWP_CondSignal(wake);
	}
	LWP_MutexUnlock(lock);
	if (i == MAX_REQUESTS) {   /* never dropped: completed now instead */
		Request r;
		memset(&r, 0, sizeof r);
		r.done = done; r.data = done_data; r.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
		finish(&r);
	}
}

void ra_transport_poll(void)
{
	int i;
	poll_canned();
	for (i = 0; i < MAX_REQUESTS; i++)
		if (req[i].state == 2) finish(&req[i]);
}

int ra_transport_available(void) { return ready != 0; }

#endif /* RA_HTTPS */
