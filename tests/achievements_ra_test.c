/* achievements_ra_test.c -- Official mode with Local players (achievements.c achievements_ra_*,
 * Docs/MII_PLAN.md step 4c): the Miis besides the Official one earn rc_client's set into the
 * Local journal with the set's RetroAchievements digest. rc_client itself is not here (its
 * answers come from the Wii build): the test hands over the entries the way
 * achievements_official.c does. scripts/test-achievements.sh builds it with the rcheevos runtime
 * and runs it with a scratch directory as its argument. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../achievements.h"
#include "../Gamecube/ach_account.h"
#include <stdarg.h>
#include <stdlib.h>

static unsigned char ram[0x200000], scratch[0x400];
unsigned char *psxM = ram, *psxH = scratch;

static char logtext[16384];
void SysPrintf(const char *fmt, ...)
{
	size_t n = strlen(logtext);
	va_list ap;
	va_start(ap, fmt);
	if (n < sizeof logtext - 256)
		vsnprintf(logtext + n, sizeof logtext - n, fmt, ap);
	va_end(ap);
}

#define KEY_OFF "82625a00c2123456"   /* Kat: plays Official */
#define KEY_B "82625dd1c2123456"     /* Ollie */
#define KEY_C "826261a2c2123456"     /* Rina */
#define GAME 4242u

static const char *dir;
static achievement_player_t seated[ACH_PLAYERS_MAX];
static unsigned seated_n;
static unsigned hook(achievement_player_t *out)
{
	memcpy(out, seated, sizeof seated);
	return seated_n;
}

static void seat(const char *a, const char *b, const char *c)
{
	const char *k[3] = { a, b, c }, *n[3] = { "Kat", "Ollie", "Rina" };
	int i;
	memset(seated, 0, sizeof seated);
	seated_n = 0;
	for (i = 0; i < 3; i++)
		if (k[i]) {
			snprintf(seated[seated_n].key, sizeof seated[0].key, "%s", k[i]);
			snprintf(seated[seated_n].name, sizeof seated[0].name, "%s", n[i]);
			seated_n++;
		}
}

static struct { char who[96], title[80]; unsigned earned, count; unsigned char beaten, mastered; } notices[16];
static int notice_n;
static void on_notice(const achievement_notice_t *n)
{
	if (n->kind != ACH_NOTICE_UNLOCK || notice_n >= 16) return;
	snprintf(notices[notice_n].who, sizeof notices[0].who, "%s", n->who ? n->who : "(null)");
	snprintf(notices[notice_n].title, sizeof notices[0].title, "%s", n->title);
	notices[notice_n].earned = n->earned;
	notices[notice_n].count = n->count;
	notices[notice_n].beaten = n->beaten;
	notices[notice_n].mastered = n->mastered;
	notice_n++;
}

static char journal[300], digest[33];

static char *read_file(const char *p)
{
	static char buf[1 << 16];
	FILE *f = fopen(p, "rb");
	size_t n = 0;
	if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
	buf[n] = 0;
	return buf;
}

static int count_in(const char *text, const char *needle)
{
	int n = 0;
	const char *p = text;
	while ((p = strstr(p, needle))) { n++; p += strlen(needle); }
	return n;
}

static void setup(void)
{
	snprintf(journal, sizeof journal, "%s/ra_local_%u.unlocks", dir, GAME);
	remove(journal);
	ach_ra_digest(GAME, digest);
	memset(logtext, 0, sizeof logtext);
	notice_n = 0;
	seat(NULL, NULL, NULL);
	achievementsEnabled = ACH_OFFICIAL;
	achievements_notify = on_notice;
	achievements_players = hook;
	achievements_close();
}

/* The set as achievements_official.c hands it over: Alpha (progression), Beta, Omega (win) */
static void set_in(void)
{
	achievements_ra_add(101, "Alpha", "first", "1001", 5, 4500, ACH_TYPE_PROGRESSION, 0);
	achievements_ra_add(103, "Omega", "last", "1003", 25, 250, ACH_TYPE_WIN, 0);
	achievements_ra_add(102, "Beta", "second", "1002", 10, 900, ACH_TYPE_STANDARD, 0);   /* out of order: sorted by id */
}

static unsigned begin(int official_plays, const char *note_key, const char *note)
{
	unsigned n = achievements_ra_begin(digest, journal, official_plays, NULL, note_key, note);
	if (n) set_in();
	return n;
}

static void test_plan_leaves_out_the_official_mii(void)
{
	seat(KEY_OFF, KEY_B, KEY_C);
	CHECK(achievements_ra_plan(KEY_OFF) == 2, "Ollie and Rina earn Local");
	CHECK(achievements_ra_plan("82625A00C2123456") == 2, "the key's case does not matter");
	CHECK(achievements_ra_plan("") == 3, "no Official Mii (this Wii's account): all three");
	seat(KEY_OFF, NULL, NULL);
	CHECK(achievements_ra_plan(KEY_OFF) == 0, "the Official Mii alone: nobody earns Local");
	seat(NULL, NULL, NULL);
	CHECK(achievements_ra_plan("") == 0, "Guest alone: nobody earns Local");
	CHECK(achievements_ra_begin(digest, journal, 1, NULL, "", "") == 0, "no players: no Local side");
	CHECK(!achievements_ra_active(), "inactive");
}

static void test_local_mii_earns_into_the_journal(void)
{
	achievement_notice_t n;
	char *text;
	seat(KEY_OFF, KEY_B, NULL);
	CHECK(achievements_ra_plan(KEY_OFF) == 1, "Ollie");
	CHECK(begin(1, "", "") == 1, "one Local player");
	CHECK(achievements_ra_end() == 1, "the journal reads (none yet)");
	CHECK(achievements_ra_active(), "active");
	CHECK(achievements_count() == 3, "the table has the set: %u", achievements_count());

	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(102, &n) == 1, "Ollie lacked Beta");
	CHECK(n.who && !strcmp(n.who, "Ollie"), "who: %s", n.who ? n.who : "(null)");
	CHECK(n.earned == 1 && n.count == 3, "progress 1 of 3: %u of %u", n.earned, n.count);
	text = read_file(journal);
	CHECK(count_in(text, digest) == 1, "one line, with the RA digest");
	CHECK(count_in(text, " 102 " KEY_B " Beta\n") == 1, "id, key and title: %s", text);
	CHECK(!count_in(text, KEY_OFF), "the Official Mii has no line");
	CHECK(achievements_ra_trigger(102, &n) == 0, "again: nobody lacks it");
	CHECK(achievements_ra_trigger(999, &n) == 0, "an id not in the set");
	CHECK(count_in(read_file(journal), digest) == 1, "no second line");
	CHECK(strstr(logtext, KEY_B) == NULL, "the log has no whole key: %s", logtext);

	/* the next game load: Ollie has it, Kat's trigger does nothing for the Local side */
	achievements_close();
	CHECK(!achievements_ra_active(), "closed");
	CHECK(achievements_ra_plan(KEY_OFF) == 1, "planned again");
	CHECK(begin(1, "", "") == 1 && achievements_ra_end() == 1, "loaded again");
	CHECK(achievements_player_earned(1) == 1, "Ollie's 1 comes back from the journal: %u", achievements_player_earned(1));
	CHECK(achievements_ra_trigger(102, &n) == 0, "Beta: Ollie has it");
	CHECK(achievements_ra_trigger(101, &n) == 1, "Alpha: new for Ollie");
	CHECK(achievements_player_earned(1) == 2, "two now");
}

static void test_two_local_miis_one_has_it(void)
{
	achievement_notice_t n;
	seat(KEY_OFF, KEY_B, KEY_C);
	CHECK(achievements_ra_plan(KEY_OFF) == 2, "Ollie and Rina");
	CHECK(begin(1, "", "") == 2 && achievements_ra_end(), "begin");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(101, &n) == 3, "both lacked it (mask 11)");
	CHECK(n.who && !strcmp(n.who, "Ollie and Rina"), "who: %s", n.who ? n.who : "(null)");
	/* Rina leaves, Ollie plays alone, earns Beta; then Rina is back */
	achievements_close();
	seat(KEY_OFF, KEY_B, NULL);
	CHECK(achievements_ra_plan(KEY_OFF) == 1 && begin(1, "", "") == 1 && achievements_ra_end(), "Ollie alone");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(102, &n) == 1, "Beta for Ollie");
	achievements_close();
	seat(KEY_OFF, KEY_B, KEY_C);
	CHECK(achievements_ra_plan(KEY_OFF) == 2 && begin(1, "", "") == 2 && achievements_ra_end(), "both again");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(102, &n) == 2, "only Rina lacked Beta (mask 10)");
	CHECK(n.who && !strcmp(n.who, "Rina"), "who: %s", n.who ? n.who : "(null)");
	CHECK(achievements_ra_trigger(103, &n) == 3, "Omega for both");
	CHECK(n.mastered && n.beaten, "Alpha and Omega earned by both: beaten and mastered");
}

static void test_the_page_lists_the_official_mii_first(void)
{
	achievement_entry_t e;
	seat(KEY_OFF, KEY_B, KEY_C);
	CHECK(achievements_ra_plan(KEY_OFF) == 2, "two");
	CHECK(achievements_ra_begin(digest, journal, 1, "Kat", "", "") == 2, "begin");
	set_in();
	CHECK(achievements_ra_end(), "end");
	CHECK(achievements_player_count() == 3, "Kat, Ollie, Rina: %u", achievements_player_count());
	CHECK(!strcmp(achievements_player_key(0), KEY_OFF) && !strcmp(achievements_player_name(0), "Kat"), "player 0 is Kat");
	CHECK(!strcmp(achievements_player_key(1), KEY_B) && !strcmp(achievements_player_name(1), "Ollie"), "player 1 is Ollie");
	CHECK(!strcmp(achievements_player_key(2), KEY_C), "player 2 is Rina");
	achievements_view_player(2);
	CHECK(achievements_viewed_player() == 2, "viewing Rina");
	{
		achievement_notice_t n;
		memset(&n, 0, sizeof n);
		achievements_ra_trigger(102, &n);   /* both get Beta */
	}
	CHECK(achievements_entry(1, &e) == 0 && !strcmp(e.title, "Beta") && e.earned, "Rina has Beta");
	CHECK(e.rarity_x100 == 900 && !strcmp(e.badge, "1002") && e.points == 10, "rarity, badge and points come along");
	CHECK(achievements_entry(0, &e) == 0 && !strcmp(e.title, "Alpha") && !e.earned, "Alpha: not yet");
	achievements_view_player(7);
	CHECK(achievements_viewed_player() == 0, "past the end: the first");
}

static void test_a_failed_account_earns_local(void)
{
	char *text;
	achievement_notice_t n;
	seat(KEY_OFF, KEY_B, NULL);
	CHECK(achievements_ra_plan("") == 2, "Kat is Local too");
	CHECK(achievements_ra_begin(digest, journal, 0, NULL, KEY_OFF, "Account file missing: earns Local") == 2, "begin");
	set_in();
	CHECK(achievements_ra_end(), "end");
	CHECK(achievements_player_count() == 2, "no Official side on the page: Kat and Ollie");
	CHECK(!strcmp(achievements_player_key(0), KEY_OFF), "Kat first");
	CHECK(!strcmp(achievements_player_note(0), "Account file missing: earns Local"), "Kat's reason");
	CHECK(!achievements_player_note(1)[0], "Ollie has none");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(103, &n) == 3, "Kat and Ollie");
	text = read_file(journal);
	CHECK(count_in(text, " 103 " KEY_OFF " Omega\n") == 1 && count_in(text, " 103 " KEY_B " Omega\n") == 1, "two lines: %s", text);
}

static void test_the_journal_cap_turns_the_local_side_off(void)
{
	FILE *f = fopen(journal, "wb");
	char line[200];
	long size = 0;
	CHECK(f != NULL, "journal opens");
	snprintf(line, sizeof line, "%s 999 %s filler filler filler filler filler filler filler filler filler\n", digest, KEY_B);
	while (size <= 1024 * 1024) { fputs(line, f); size += (long)strlen(line); }
	fclose(f);
	seat(KEY_OFF, KEY_B, NULL);
	CHECK(achievements_ra_plan(KEY_OFF) == 1 && begin(1, "", "") == 1, "begin");
	CHECK(achievements_ra_end() == 0, "too large: refused");
	CHECK(!achievements_ra_active(), "Local side off");
	CHECK(achievements_player_count() == 1, "the page is the Official one again");
}

static void test_close_gives_the_tables_back(void)
{
	achievement_notice_t n;
	seat(KEY_OFF, KEY_B, NULL);
	CHECK(achievements_ra_plan(KEY_OFF) == 1 && begin(1, "", "") == 1 && achievements_ra_end(), "begin");
	memset(&n, 0, sizeof n);
	achievements_ra_trigger(101, &n);
	achievements_close();
	CHECK(achievements_count() == 0 && achievements_player_count() == 1, "nothing is left");
	CHECK(!achievements_ra_active(), "inactive");
	CHECK(achievements_ra_trigger(101, &n) == 0, "no trigger after close");
}

static const struct hc_test tests[] = {
	{ "plan_leaves_out_the_official_mii", test_plan_leaves_out_the_official_mii, false },
	{ "local_mii_earns_into_the_journal", test_local_mii_earns_into_the_journal, false },
	{ "two_local_miis_one_has_it", test_two_local_miis_one_has_it, false },
	{ "page_lists_the_official_mii_first", test_the_page_lists_the_official_mii_first, false },
	{ "failed_account_earns_local", test_a_failed_account_earns_local, false },
	{ "journal_cap", test_the_journal_cap_turns_the_local_side_off, false },
	{ "close_gives_the_tables_back", test_close_gives_the_tables_back, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
