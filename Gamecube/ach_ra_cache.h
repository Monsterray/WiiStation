/* ach_ra_cache.h -- Local mode's rules for what rc_client may ask the network, and the card's
 * copy of the RetroAchievements set (Docs/ACHIEVEMENTS.md, "Local mode"). Plain C, host-tested in
 * tests/ach_ra_cache_test.c. Only names and text: nothing here opens a file or a socket.
 *
 * Local mode earns the official set and sends nothing. Of every request rc_client makes, only
 * three may reach the server, and only to download: the login (the patch API needs one), the
 * game's id for a disc's hash and the game's set. Anything else (an award, a leaderboard entry,
 * a ping with rich presence, a session start) is answered here with a refusal and never leaves
 * the console. The two downloads are kept on the card, so a game played once loads again with no
 * network (and with no account: the login is then made up here). */
#ifndef ACH_RA_CACHE_H
#define ACH_RA_CACHE_H

#include <stdio.h>
#include <string.h>
#include <time.h>

#define ACH_CACHE_DIR "sd:/wiistation/achievements/ra_cache/"
#define ACH_CACHE_PATH 96
#define ACH_CACHE_MAX (1024 * 1024)       /* a response bigger than this is not kept */
#define ACH_CACHE_REFRESH_DAYS 30         /* older than this, Local asks the server first (the card's copy is the fallback) */
#define ACH_OFFLINE_LINK "*offline"       /* the "account" of a game load served from the card: no file, no login on the wire */

enum { ACH_REQ_OTHER = 0, ACH_REQ_LOGIN, ACH_REQ_GAMEID, ACH_REQ_SETS };

/* The value of a parameter of a form-urlencoded body ("r=gameid&m=abc"), copied into out
 * (NUL-terminated, cut to size - 1). 0 when the name is not there. Never logs, never keeps. */
static inline int ach_req_param(const char *post, const char *name, char *out, size_t size)
{
	size_t n = strlen(name), i;
	const char *p = post;
	if (!post || !size) return 0;
	for (;;) {
		if (!strncmp(p, name, n) && p[n] == '=') {
			p += n + 1;
			for (i = 0; p[i] && p[i] != '&' && i + 1 < size; i++) out[i] = p[i];
			out[i] = 0;
			return 1;
		}
		p = strchr(p, '&');
		if (!p) return 0;
		p++;
	}
}

/* What a request is, from its body's r= parameter */
static inline int ach_req_kind(const char *post)
{
	char r[24];
	if (!ach_req_param(post, "r", r, sizeof r)) return ACH_REQ_OTHER;
	if (!strcmp(r, "login2")) return ACH_REQ_LOGIN;
	if (!strcmp(r, "gameid")) return ACH_REQ_GAMEID;
	if (!strcmp(r, "achievementsets")) return ACH_REQ_SETS;
	return ACH_REQ_OTHER;
}

/* Local mode may let this reach the server (only to read) */
static inline int ach_local_may_send(int kind) { return kind == ACH_REQ_LOGIN || kind == ACH_REQ_GAMEID || kind == ACH_REQ_SETS; }

static inline int ach_hex_ok(const char *s, size_t want)
{
	size_t i, n = strlen(s);
	if (n != want) return 0;
	for (i = 0; i < n; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return 0;
	return 1;
}

static inline int ach_digits_ok(const char *s)
{
	size_t i, n = strlen(s);
	if (!n || n > 10) return 0;
	for (i = 0; i < n; i++)
		if (s[i] < '0' || s[i] > '9') return 0;
	return 1;
}

/* The card file of the answer to a game's hash, and the card file of a game's set, by the
 * hash (the m= of the request: what begin_load_game asks with). Hash: 32 hex digits, lower
 * case in the name. Returns 0 for a name that is no hash (never a path from a stray value). */
static inline int ach_cache_hash_path(int kind, const char *hash, char *path, size_t size)
{
	char h[33];
	int i, n;
	if (!hash || !ach_hex_ok(hash, 32) || (kind != ACH_REQ_GAMEID && kind != ACH_REQ_SETS)) return 0;
	for (i = 0; i < 32; i++) h[i] = (char)(hash[i] >= 'A' && hash[i] <= 'F' ? hash[i] + 32 : hash[i]);
	h[32] = 0;
	n = snprintf(path, size, ACH_CACHE_DIR "%s-%s.json", kind == ACH_REQ_GAMEID ? "gameid" : "sets", h);
	return n > 0 && (size_t)n < size;
}

/* A set asked for by game id (a hash the server did not know, with a mapping the client gave) */
static inline int ach_cache_id_path(const char *id, char *path, size_t size)
{
	int n;
	if (!id || !ach_digits_ok(id)) return 0;
	n = snprintf(path, size, ACH_CACHE_DIR "sets-g%s.json", id);
	return n > 0 && (size_t)n < size;
}

/* The card file that answers (or would keep the answer to) this request; 0: not cached */
static inline int ach_cache_path(int kind, const char *post, char *path, size_t size)
{
	char v[40];
	if (kind == ACH_REQ_GAMEID) return ach_req_param(post, "m", v, sizeof v) && ach_cache_hash_path(kind, v, path, size);
	if (kind == ACH_REQ_SETS) {
		if (ach_req_param(post, "m", v, sizeof v)) return ach_cache_hash_path(kind, v, path, size);
		return ach_req_param(post, "g", v, sizeof v) && ach_cache_id_path(v, path, size);
	}
	return 0;
}

/* The text after a JSON key and its colon (spaces skipped), or NULL when the key is not there */
static inline const char *ach_json_value(const char *body, const char *key)
{
	char pat[24];
	const char *p;
	if (snprintf(pat, sizeof pat, "\"%s\"", key) >= (int)sizeof pat) return NULL;
	if (!(p = strstr(body, pat))) return NULL;
	p += strlen(pat);
	while (*p == ' ') p++;
	if (*p != ':') return NULL;
	p++;
	while (*p == ' ') p++;
	return p;
}

/* The server's answer is worth keeping: a success, and for a hash a game that exists */
static inline int ach_cache_keeps(int kind, const char *body, size_t len)
{
	const char *p;
	if (!body || !len || len > ACH_CACHE_MAX || (kind != ACH_REQ_GAMEID && kind != ACH_REQ_SETS)) return 0;
	if (!(p = ach_json_value(body, "Success")) || strncmp(p, "true", 4)) return 0;
	if (kind == ACH_REQ_GAMEID) {
		if (!(p = ach_json_value(body, "GameID"))) return 0;
		return *p >= '1' && *p <= '9';   /* GameID 0: the disc is unknown, ask again next time */
	}
	return 1;
}

/* The card's copy is recent enough to use without asking the server. A clock that is wrong (a
 * file from the future, or no clock) counts as recent: the card's copy is the safe choice. */
static inline int ach_cache_fresh(time_t mtime, time_t now)
{
	if (mtime <= 0 || now < mtime) return 1;
	return (now - mtime) <= (time_t)ACH_CACHE_REFRESH_DAYS * 24 * 3600;
}

/* The answer to a login in a game load served from the card: an account that exists only here.
 * The token is the word "offline"; the real token (if there is one) never comes in. user is
 * cut to letters, digits and . _ - */
static inline int ach_synth_login(const char *user, char *out, size_t size)
{
	char u[40];
	size_t i, n = 0;
	for (i = 0; user && user[i] && n < 32; i++)
		if ((user[i] >= '0' && user[i] <= '9') || (user[i] >= 'a' && user[i] <= 'z') || (user[i] >= 'A' && user[i] <= 'Z') ||
		    user[i] == '.' || user[i] == '_' || user[i] == '-')
			u[n++] = user[i];
	if (!n) { memcpy(u, "Local", 5); n = 5; }
	u[n] = 0;
	i = (size_t)snprintf(out, size, "{\"Success\":true,\"User\":\"%s\",\"Token\":\"offline\",\"Score\":0,\"SoftcoreScore\":0,\"Messages\":0}", u);
	return i < size;
}

/* The answer to a request Local mode will not send */
#define ACH_REFUSED_BODY "{\"Success\":false,\"Error\":\"Local mode sends nothing\"}"

/* A name for the log: never the body, never a parameter */
static inline const char *ach_req_name(int kind)
{
	return kind == ACH_REQ_LOGIN ? "login" : kind == ACH_REQ_GAMEID ? "gameid" : kind == ACH_REQ_SETS ? "set" : "other";
}

#endif
