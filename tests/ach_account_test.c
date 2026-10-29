/* ach_account_test.c -- which account file a game load logs in with, and the names of the
 * RetroAchievements set's Local journal (Gamecube/ach_account.h, Docs/MII_PLAN.md steps 4a and
 * 4c). Plain C, no file is opened. scripts/test-achievements.sh builds and runs it. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../Gamecube/ach_account.h"

static void test_link_to_path(void)
{
	char path[96];
	CHECK(ach_account_path("", path, sizeof path) == ACH_ACCOUNT_THIS_WII, "no link: this Wii's");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/ra_account.ini"), "path: %s", path);
	CHECK(ach_account_path("*", path, sizeof path) == ACH_ACCOUNT_THIS_WII, "* is this Wii's");
	CHECK(!strcmp(path, ACH_ACCOUNT_WII), "path: %s", path);
	CHECK(ach_account_path(NULL, path, sizeof path) == ACH_ACCOUNT_THIS_WII, "NULL is no link");
	CHECK(ach_account_path("dev", path, sizeof path) == ACH_ACCOUNT_NAMED, "a name");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/ra/dev.ini"), "path: %s", path);
	CHECK(ach_account_path("Kat_2-b", path, sizeof path) == ACH_ACCOUNT_NAMED, "letters, digits, _ and -");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/ra/Kat_2-b.ini"), "path: %s", path);
	CHECK(ach_account_path("my account", path, sizeof path) == ACH_ACCOUNT_NAMED, "the browser lists a name with a space");
}

static void test_bad_names(void)
{
	char path[96] = "x";
	const char *bad[] = { "../x", "a/b", "a\\b", "C:x", ".hidden", "..", "a*b", "a?b", "a\"b", "a<b", "a|b", "a\tb",
	                      "0123456789012345678901234567890123" /* 34: too long */ };
	unsigned i;
	for (i = 0; i < sizeof bad / sizeof *bad; i++) {
		CHECK(ach_account_path(bad[i], path, sizeof path) == ACH_ACCOUNT_BAD, "bad name %u: %s", i, bad[i]);
		CHECK(!path[0], "a bad name leaves an empty path");
	}
	CHECK(ach_account_path("0123456789012345678901234567890", path, sizeof path) == ACH_ACCOUNT_NAMED, "31 characters fit");
	CHECK(ach_account_path("dev", path, 10) == ACH_ACCOUNT_BAD, "a path that does not fit is bad");
}

static void test_ra_digest_and_journal(void)
{
	char d[33], path[96];
	ach_ra_digest(1234, d);
	CHECK(strlen(d) == 32, "32 characters: %s", d);
	CHECK(!strcmp(d, "52410000000000000000" "0000" "000004d2"), "the digest: %s", d);
	ach_ra_digest(0xdeadbeef, d);
	CHECK(!strcmp(d, "52410000000000000000" "0000" "deadbeef"), "the digest: %s", d);
	CHECK(ach_ra_journal_path(1234, path, sizeof path), "the path fits");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/ra_local/1234.unlocks"), "path: %s", path);
	CHECK(!ach_ra_journal_path(1234, path, 20), "a path that does not fit is refused");
}

static const struct hc_test tests[] = {
	{ "link_to_path", test_link_to_path, false },
	{ "bad_names", test_bad_names, false },
	{ "ra_digest_and_journal", test_ra_digest_and_journal, false },
};

int main(int argc, char **argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
