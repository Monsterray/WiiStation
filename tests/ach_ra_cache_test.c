/* ach_ra_cache_test.c -- Local mode's request rules and the card's copy of the set
 * (Gamecube/ach_ra_cache.h): which requests may leave the console, the file names, what is worth
 * keeping, when the copy is recent, the login that is made up. Plain C; no network, no file. */
#include "../Gamecube/ach_ra_cache.h"
#include <stdlib.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

#define HASH "0123456789abcdef0123456789ABCDEF"

static void test_request_kinds(void)
{
	CHECK(ach_req_kind("r=login2&u=Kat&p=secret") == ACH_REQ_LOGIN, "login");
	CHECK(ach_req_kind("r=login2&u=Kat&t=token") == ACH_REQ_LOGIN, "login with a token");
	CHECK(ach_req_kind("r=gameid&m=" HASH) == ACH_REQ_GAMEID, "gameid");
	CHECK(ach_req_kind("r=achievementsets&u=Kat&t=tok&m=" HASH "&v=2") == ACH_REQ_SETS, "sets");
	/* everything that would tell the site something: refused, in any order of the parameters */
	CHECK(ach_req_kind("r=awardachievement&u=Kat&t=tok&a=101&h=0&m=" HASH "&v=abc") == ACH_REQ_OTHER, "award");
	CHECK(ach_req_kind("r=submitlbentry&u=Kat&t=tok&i=5&s=100") == ACH_REQ_OTHER, "leaderboard entry");
	CHECK(ach_req_kind("r=ping&u=Kat&t=tok&g=4242&m=Level%201") == ACH_REQ_OTHER, "ping with rich presence");
	CHECK(ach_req_kind("r=startsession&u=Kat&t=tok&g=4242") == ACH_REQ_OTHER, "start session");
	CHECK(ach_req_kind("r=postactivity&u=Kat") == ACH_REQ_OTHER, "activity");
	CHECK(ach_req_kind("u=Kat&r=login2") == ACH_REQ_LOGIN, "r= need not be first");
	CHECK(ach_req_kind("xr=login2") == ACH_REQ_OTHER, "a name that only ends in r is not r");
	CHECK(ach_req_kind("r=login2x") == ACH_REQ_OTHER, "no prefix match on the value");
	CHECK(ach_req_kind("") == ACH_REQ_OTHER && ach_req_kind(NULL) == ACH_REQ_OTHER, "empty, none");
	CHECK(ach_local_may_send(ACH_REQ_LOGIN) && ach_local_may_send(ACH_REQ_GAMEID) && ach_local_may_send(ACH_REQ_SETS), "downloads go");
	CHECK(!ach_local_may_send(ACH_REQ_OTHER), "the rest never");
}

static void test_file_names(void)
{
	char path[ACH_CACHE_PATH];
	CHECK(ach_cache_path(ACH_REQ_SETS, "r=achievementsets&u=Kat&t=tok&m=" HASH "&v=2", path, sizeof path) &&
	      !strcmp(path, ACH_CACHE_DIR "sets-0123456789abcdef0123456789abcdef.json"), "sets by hash, lower case: %s", path);
	CHECK(ach_cache_path(ACH_REQ_GAMEID, "r=gameid&m=" HASH, path, sizeof path) &&
	      !strcmp(path, ACH_CACHE_DIR "gameid-0123456789abcdef0123456789abcdef.json"), "gameid: %s", path);
	CHECK(ach_cache_path(ACH_REQ_SETS, "r=achievementsets&u=Kat&t=tok&g=4242&v=2", path, sizeof path) &&
	      !strcmp(path, ACH_CACHE_DIR "sets-g4242.json"), "sets by id: %s", path);
	CHECK(!ach_cache_path(ACH_REQ_SETS, "r=achievementsets&m=../../../x", path, sizeof path), "a hash that is a path: no file");
	CHECK(!ach_cache_path(ACH_REQ_SETS, "r=achievementsets&m=" HASH "0", path, sizeof path), "33 characters: no file");
	CHECK(!ach_cache_path(ACH_REQ_SETS, "r=achievementsets&g=12ab", path, sizeof path), "an id with letters: no file");
	CHECK(!ach_cache_path(ACH_REQ_SETS, "r=achievementsets&g=", path, sizeof path), "an empty id: no file");
	CHECK(!ach_cache_path(ACH_REQ_LOGIN, "r=login2&u=Kat&p=x", path, sizeof path), "a login has no file");
	CHECK(!ach_cache_path(ACH_REQ_GAMEID, "r=gameid", path, sizeof path), "no m=: no file");
	CHECK(!ach_cache_path(ACH_REQ_SETS, "r=achievementsets&m=" HASH, path, 20), "a path too long for its buffer: no file");
	CHECK(!strstr(ACH_CACHE_DIR, "ra_local"), "the cache is not the journal's folder");
}

static void test_what_is_kept(void)
{
	const char *ok_id = "{\"Success\":true,\"GameID\":4242}";
	const char *unknown = "{\"Success\":true,\"GameID\":0}";
	const char *failed = "{\"Success\":false,\"Error\":\"x\"}";
	const char *sets = "{\"Success\":true,\"GameId\":4242,\"Title\":\"Spyro\"}";
	CHECK(ach_cache_keeps(ACH_REQ_GAMEID, ok_id, strlen(ok_id)), "a known game");
	CHECK(!ach_cache_keeps(ACH_REQ_GAMEID, unknown, strlen(unknown)), "GameID 0 is asked for again next time");
	CHECK(!ach_cache_keeps(ACH_REQ_GAMEID, failed, strlen(failed)), "a failure is not kept");
	CHECK(ach_cache_keeps(ACH_REQ_SETS, sets, strlen(sets)), "a set");
	CHECK(!ach_cache_keeps(ACH_REQ_SETS, failed, strlen(failed)), "a failed set");
	CHECK(!ach_cache_keeps(ACH_REQ_LOGIN, ok_id, strlen(ok_id)), "a login is never kept (it holds a token)");
	CHECK(!ach_cache_keeps(ACH_REQ_SETS, sets, ACH_CACHE_MAX + 1), "too big");
	CHECK(!ach_cache_keeps(ACH_REQ_SETS, "", 0) && !ach_cache_keeps(ACH_REQ_SETS, NULL, 5), "nothing");
}

static void test_recent_enough(void)
{
	time_t now = 1900000000;
	CHECK(ach_cache_fresh(now - 3600, now), "an hour old");
	CHECK(ach_cache_fresh(now - 29 * 24 * 3600, now), "29 days old");
	CHECK(!ach_cache_fresh(now - 31 * 24 * 3600, now), "31 days old: ask the server first");
	CHECK(ach_cache_fresh(now + 5, now), "a file from the future (a wrong clock): use it");
	CHECK(ach_cache_fresh(0, now), "no time on the file: use it");
}

static void test_the_made_up_login(void)
{
	char body[160];
	CHECK(ach_synth_login("Local", body, sizeof body) && strstr(body, "\"Success\":true") && strstr(body, "\"User\":\"Local\""), "ok: %s", body);
	CHECK(strstr(body, "\"Token\":\"offline\""), "the token is the word offline: %s", body);
	CHECK(ach_synth_login("Ka\"t\\x", body, sizeof body) && strstr(body, "\"User\":\"Katx\""), "quotes and backslashes cut: %s", body);
	CHECK(ach_synth_login("", body, sizeof body) && strstr(body, "\"User\":\"Local\""), "no name: Local");
	CHECK(ach_synth_login(NULL, body, sizeof body), "NULL");
	CHECK(!ach_synth_login("Kat", body, 20), "too small a buffer says so");
}

static void test_params(void)
{
	char v[8];
	CHECK(ach_req_param("a=1&bb=22&c=333", "bb", v, sizeof v) && !strcmp(v, "22"), "middle");
	CHECK(ach_req_param("a=1&bb=22&c=333", "c", v, sizeof v) && !strcmp(v, "333"), "last");
	CHECK(ach_req_param("a=1234567890", "a", v, sizeof v) && !strcmp(v, "1234567"), "cut to the buffer: %s", v);
	CHECK(!ach_req_param("a=1&b=2", "ab", v, sizeof v), "not there");
	CHECK(!ach_req_param("a=1", "a", v, 0), "no room");
	CHECK(!strcmp(ach_req_name(ACH_REQ_OTHER), "other") && !strcmp(ach_req_name(ACH_REQ_SETS), "set"), "names for the log");
}

int main(void)
{
	test_request_kinds();
	test_file_names();
	test_what_is_kept();
	test_recent_enough();
	test_the_made_up_login();
	test_params();
	if (failures) { printf("%d failed\n", failures); return 1; }
	printf("ach_ra_cache: request kinds, file names, what is kept, freshness, the made-up login ok\n");
	return 0;
}
