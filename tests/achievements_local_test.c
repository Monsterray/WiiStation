/* achievements_local_test.c -- Local mode (setting Achievements = 4): the official RetroAchievements
 * set, nothing sent, every player earns into the Local journal (achievements.c achievements_ra_*,
 * Docs/ACHIEVEMENTS.md "Local mode"). rc_client is not here (its answers come from the Wii
 * build): the test hands over the entries the way achievements_official.c does, with no Official
 * side. scripts/test-achievements.sh builds it with the rcheevos runtime and runs it with a
 * scratch directory as its argument. */
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

#define KEY_A "82625a00c2123456"   /* Kat */
#define KEY_B "82625dd1c2123456"   /* Ollie */
#define GAME 4242u

static const char *dir;
static achievement_player_t seated[ACH_PLAYERS_MAX];
static unsigned seated_n;
static unsigned hook(achievement_player_t *out)
{
	memcpy(out, seated, sizeof seated);
	return seated_n;
}

static void seat(const char *a, const char *b)
{
	const char *k[2] = { a, b }, *n[2] = { "Kat", "Ollie" };
	int i;
	memset(seated, 0, sizeof seated);
	seated_n = 0;
	for (i = 0; i < 2; i++)
		if (k[i]) {
			snprintf(seated[seated_n].key, sizeof seated[0].key, "%s", k[i]);
			snprintf(seated[seated_n].name, sizeof seated[0].name, "%s", n[i]);
			seated_n++;
		}
}

static struct { char who[96]; unsigned char has_who; } last;
static void on_notice(const achievement_notice_t *n)
{
	if (n->kind != ACH_NOTICE_UNLOCK) return;
	last.has_who = n->who && n->who[0];
	snprintf(last.who, sizeof last.who, "%s", n->who ? n->who : "");
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

/* "<digest><tail>" */
static const char *dig(const char *tail)
{
	static char buf[8][128];
	static int i;
	i = (i + 1) % 8;
	snprintf(buf[i], sizeof buf[0], "%s%s", digest, tail);
	return buf[i];
}

static void setup(void)
{
	snprintf(journal, sizeof journal, "%s/local_%u.unlocks", dir, GAME);
	remove(journal);
	ach_ra_digest(GAME, digest);
	memset(logtext, 0, sizeof logtext);
	memset(&last, 0, sizeof last);
	seat(NULL, NULL);
	achievementsEnabled = ACH_LOCAL;
	achievements_notify = on_notice;
	achievements_players = hook;
	achievements_close();
}

static void set_in(void)
{
	achievements_ra_add(101, "Alpha", "first", "1001", 5, 4500, ACH_TYPE_PROGRESSION, 0);
	achievements_ra_add(103, "Omega", "last", "1003", 25, 250, ACH_TYPE_WIN, 0);
	achievements_ra_add(102, "Beta", "second", "1002", 10, 900, ACH_TYPE_STANDARD, 0);
}

/* The Local side as achievements_official.c builds it in Local mode: no Official side */
static unsigned begin(void)
{
	unsigned n = achievements_ra_begin(digest, journal, 0, NULL, "", "");
	if (n) set_in();
	return n;
}

static void test_the_numbers_are_the_settings_files(void)
{
	/* a card written before Local existed: 1 = own file, 2 = Test, 3 = Official keep their meaning */
	CHECK(ACH_OFF == 0 && ACH_CUSTOM == 1 && ACH_TEST == 2 && ACH_OFFICIAL == 3, "the old numbers");
	CHECK(ACH_LOCAL == 4, "Local is the next number");
}

static void test_guest_alone_earns(void)
{
	achievement_notice_t n;
	achievement_entry_t e;
	char *text;
	unsigned earned, total, points, total_points;
	CHECK(achievements_ra_plan_all() == 1, "Guest alone is one player");
	CHECK(achievements_ra_plan("") == 0, "(the Official plan still gives none)");
	CHECK(achievements_ra_plan_all() == 1, "planned again");
	CHECK(begin() == 1, "one player");
	CHECK(achievements_ra_end() == 1, "the journal reads (none yet)");
	CHECK(achievements_ra_active(), "active");
	CHECK(achievements_player_count() == 1 && !achievements_player_key(0)[0], "the page: Guest");
	CHECK(achievements_count() == 3, "the table has the set");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(102, &n) == 1, "Guest lacked Beta");
	CHECK(n.earned == 1 && n.count == 3, "progress 1 of 3");
	CHECK(!n.who || !n.who[0], "no names for Guest");
	text = read_file(journal);
	CHECK(count_in(text, dig(" 102 Beta\n")) == 1, "a line with no key, the RA digest: %s", text);
	CHECK(achievements_earned() == 1, "the union: %u", achievements_earned());
	CHECK(achievements_entry(1, &e) == 0 && !strcmp(e.title, "Beta") && e.earned && e.rarity_x100 == 900, "the book's entry: earned, rarity");
	achievements_ra_summary(&earned, &total, &points, &total_points);
	CHECK(earned == 1 && total == 3 && points == 10 && total_points == 40, "the card's numbers: %u of %u, %u of %u points", earned, total, points, total_points);
	/* the next load: Guest has it */
	achievements_close();
	CHECK(achievements_ra_plan_all() == 1 && begin() == 1 && achievements_ra_end(), "again");
	CHECK(achievements_earned() == 1, "restored from the journal");
	CHECK(achievements_ra_trigger(102, &n) == 0, "nothing new");
}

static void test_every_seated_mii_earns_and_guest_does_not(void)
{
	achievement_notice_t n;
	char *text;
	seat(KEY_A, KEY_B);
	CHECK(achievements_ra_plan_all() == 2, "both, no Official side");
	CHECK(begin() == 2 && achievements_ra_end(), "begin");
	CHECK(achievements_player_count() == 2 && !strcmp(achievements_player_key(0), KEY_A), "the page: Kat, Ollie");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(101, &n) == 3, "both lacked Alpha");
	CHECK(n.who && !strcmp(n.who, "Kat and Ollie"), "who: %s", n.who ? n.who : "(null)");
	text = read_file(journal);
	CHECK(count_in(text, " 101 " KEY_A " Alpha\n") == 1 && count_in(text, " 101 " KEY_B " Alpha\n") == 1, "two lines: %s", text);
	CHECK(!count_in(text, dig(" 101 Alpha")), "none for Guest");
	CHECK(strstr(logtext, KEY_A) == NULL, "no whole key in the log");
	CHECK(achievements_earned_for(KEY_B) == 1, "the Players menu: Ollie has 1: %d", achievements_earned_for(KEY_B));
	CHECK(achievements_earned_for("826261a2c2123456") == 0, "a Mii that is not playing: 0, from the journal");
}

static void test_the_players_change_in_play(void)
{
	achievement_notice_t n;
	/* Guest plays, then Kat is seated in the menu and the game goes on */
	CHECK(achievements_ra_plan_all() == 1 && begin() == 1 && achievements_ra_end(), "Guest alone");
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(101, &n) == 1, "Guest earns Alpha");
	CHECK(achievements_ra_replan("", 1) == 0, "nothing changed: no new Local side");
	seat(KEY_A, NULL);
	CHECK(achievements_ra_replan("", 1) == 1, "Kat is seated: the players differ");
	CHECK(begin() == 1 && achievements_ra_end(), "built again for Kat");
	CHECK(!strcmp(achievements_player_key(0), KEY_A), "the page: Kat");
	CHECK(achievements_earned() == 0, "Guest's Alpha is not Kat's: %u", achievements_earned());
	memset(&n, 0, sizeof n);
	CHECK(achievements_ra_trigger(101, &n) == 1, "Kat earns Alpha too");
	CHECK(n.who && !strcmp(n.who, "Kat"), "who: Kat");
	CHECK(count_in(read_file(journal), dig(" 101 Alpha\n")) == 1 && count_in(read_file(journal), " 101 " KEY_A " Alpha\n") == 1, "both lines are there");
	/* Ollie joins */
	seat(KEY_A, KEY_B);
	CHECK(achievements_ra_replan("", 1) == 1, "Ollie joins");
	CHECK(begin() == 2 && achievements_ra_end(), "both");
	CHECK(achievements_player_earned(0) == 1 && achievements_player_earned(1) == 0, "Kat 1, Ollie 0");
	/* the same seats again: the plan is the same */
	CHECK(achievements_ra_replan("", 1) == 0, "the same people");
	/* no Local side loaded: nothing to plan again */
	achievements_close();
	CHECK(achievements_ra_replan("", 1) == 0, "closed");
}

static void test_modes_with_no_rc_client_load_nothing(void)
{
	/* a build without achievements_official.c (the host): Local loads nothing, and never reads
	 * the Custom set file */
	snprintf(achievementFile, sizeof achievementFile, "%s/does_not_exist.txt", dir);
	CHECK(achievements_load("SLUS00000") == 0, "no set");
	CHECK(achievements_count() == 0, "empty");
	CHECK(strstr(logtext, "not in this build") != NULL, "says so: %s", logtext);
	CHECK(strstr(achievements_status(), "Official") != NULL || strstr(achievements_status(), "not in this build") != NULL,
	      "the status line: %s", achievements_status());
}

static void test_custom_mode_keeps_its_own_players(void)
{
	/* Custom mode (the old Local) still takes the players from the hook and needs its file */
	achievementsEnabled = ACH_CUSTOM;
	seat(KEY_A, NULL);
	CHECK(achievements_load("SLUS00000") <= 0, "no file: no set");
	CHECK(!achievements_ra_active(), "no RetroAchievements side in Custom mode");
}

static const struct hc_test tests[] = {
	{ "numbers_are_the_settings_files", test_the_numbers_are_the_settings_files, false },
	{ "guest_alone_earns", test_guest_alone_earns, false },
	{ "every_seated_mii_earns", test_every_seated_mii_earns_and_guest_does_not, false },
	{ "players_change_in_play", test_the_players_change_in_play, false },
	{ "no_rc_client_loads_nothing", test_modes_with_no_rc_client_load_nothing, false },
	{ "custom_mode_keeps_its_players", test_custom_mode_keeps_its_own_players, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
