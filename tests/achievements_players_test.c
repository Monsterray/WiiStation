/* achievements_players_test.c -- the Local engine with Miis as players (achievements.c,
 * Docs/ACHIEVEMENTS.md, Players and Miis phase 3): who earns, the journal's key column, the union of the earned
 * state, a state made by other players, the 1 MB cap. scripts/test-achievements.sh builds it
 * with the rcheevos runtime and runs it with a scratch directory as its argument. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../achievements.h"
#include "../Gamecube/ach_players.h"
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

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
#define KEY_C "826261a2c2123456"   /* Rina */
#define KEY_D "82626573c2123456"   /* Ann */
#define KEY_GONE "826265ffc2123456"

static const char *dir;
static const char SET[] = "Game=TEST12345\nTitle=Players\n"
	"1|Alpha|0xH000010=1|10\n2|Beta|0xH000011=1|10\n3|Gamma|0xH000012=1|10\n";

/* who sits: set by each test; the hook gives these, as Gamecube/ach_players.c would */
static achievement_player_t seated[ACH_PLAYERS_MAX];
static unsigned seated_n;
static unsigned hook(achievement_player_t *out)
{
	memcpy(out, seated, sizeof seated);
	return seated_n;
}

static void seat(const char *a, const char *an, const char *b, const char *bn, const char *c, const char *cn)
{
	const char *k[3] = { a, b, c }, *n[3] = { an, bn, cn };
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

static struct { char who[96], title[80]; unsigned earned, beaten, mastered; } notices[16];
static int notice_n;
static void on_notice(const achievement_notice_t *n)
{
	if (n->kind != ACH_NOTICE_UNLOCK || notice_n >= 16) return;
	snprintf(notices[notice_n].who, sizeof notices[0].who, "%s", n->who ? n->who : "(null)");
	snprintf(notices[notice_n].title, sizeof notices[0].title, "%s", n->title);
	notices[notice_n].earned = n->earned;
	notices[notice_n].beaten = n->beaten;
	notices[notice_n].mastered = n->mastered;
	notice_n++;
}

static char path_of[2][300];
static const char *path(int which)
{
	snprintf(path_of[which], sizeof path_of[which], "%s/%s", dir, which ? "unlocks.txt" : "set.txt");
	return path_of[which];
}

static void write_file(const char *p, const char *text, const char *mode)
{
	FILE *f = fopen(p, mode);
	if (f) { fputs(text, f); fclose(f); }
}

static char *read_file(const char *p)
{
	static char buf[1 << 16];
	FILE *f = fopen(p, "rb");
	size_t n = 0;
	if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
	buf[n] = 0;
	return buf;
}

static int lines_with(const char *text, const char *needle)
{
	int n = 0;
	const char *p = text;
	while ((p = strstr(p, needle))) { n++; p += strlen(needle); }
	return n;
}

static void setup(void)
{
	remove(path(1));
	memset(ram, 0, sizeof ram);
	memset(logtext, 0, sizeof logtext);
	notice_n = 0;
	seated_n = 0;
	memset(seated, 0, sizeof seated);
	snprintf(achievementFile, sizeof achievementFile, "%s", path(0));
	snprintf(achievementLog, sizeof achievementLog, "%s", path(1));
	achievementsEnabled = ACH_CUSTOM;
	achievements_notify = on_notice;
	achievements_players = hook;
	write_file(path(0), SET, "wb");
}

/* Run the game until achievement id (1-3) has had its chance to trigger */
static void trigger(int id)
{
	ram[0x10 + id - 1] = 0;   /* a condition already true at load waits for a false first */
	achievements_frame();
	ram[0x10 + id - 1] = 1;
	achievements_frame();
	achievements_frame();
}

static int load(void) { return achievements_load("TEST12345"); }

/* ---- the tests ---- */

static void test_guest_alone_is_as_before(void)
{
	char *j;
	CHECK(load() == 3, "load");
	CHECK(achievements_player_count() == 1 && !achievements_player_key(0)[0], "Guest alone");
	trigger(1);
	CHECK(achievements_earned() == 1, "earned %u", achievements_earned());
	CHECK(notice_n == 1 && !notices[0].who[0], "a Guest toast names nobody: '%s'", notices[0].who);
	achievements_close();
	j = read_file(path(1));
	/* the line is the old one: "<digest> <id> <title>" */
	CHECK(strlen(j) > 33 && j[32] == ' ' && !strcmp(j + 33, "1 Alpha\n"), "old line format: '%s'", j);
}

static void test_old_lines_belong_to_guest(void)
{
	/* a journal from before the Players menu, made by Guest */
	CHECK(load() == 3, "load");
	trigger(1);
	achievements_close();

	/* Guest again: it has it */
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 1, "Guest keeps its unlock");
	achievements_close();

	/* Kat plays: her set is empty, and the old line is not hers */
	notice_n = 0;
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_player_count() == 1 && !strcmp(achievements_player_key(0), KEY_A), "Kat plays");
	CHECK(achievements_earned() == 0, "old unlocks stay with Guest: %u", achievements_earned());
	trigger(1);   /* she earns Alpha too: a new line with her key, Guest's line stays */
	CHECK(achievements_earned() == 1 && notice_n == 1 && !strcmp(notices[0].who, "Kat"), "Kat toast '%s'", notices[0].who);
	achievements_close();
	CHECK(lines_with(read_file(path(1)), "\n") == 2, "two lines: Guest's and Kat's");
	CHECK(lines_with(read_file(path(1)), " " KEY_A " Alpha") == 1, "Kat's line has her key");
	CHECK(achievements_earned_for("") == -1, "no set loaded: no count");

	/* Guest, once more: still its own one unlock */
	seat(NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 1 && achievements_earned_for(KEY_A) == 1, "Kat's count read from the journal");
	CHECK(achievements_earned_for(KEY_B) == 0, "Ollie has none");
}

static void test_two_miis_both_earn(void)
{
	char *j;
	seat(KEY_A, "Kat", KEY_B, "Ollie", NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_player_count() == 2, "two players");
	trigger(2);
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "Kat and Ollie"), "toast names both: '%s'", notices[0].who);
	CHECK(achievements_player_earned(0) == 1 && achievements_player_earned(1) == 1, "each has one");
	achievements_close();
	j = read_file(path(1));
	CHECK(lines_with(j, " " KEY_A " Beta") == 1 && lines_with(j, " " KEY_B " Beta") == 1, "a line per Mii: '%s'", j);
	/* the log has first characters only, never a name or a whole key */
	CHECK(!strstr(logtext, KEY_A) && !strstr(logtext, KEY_B), "no whole key in the log");
	CHECK(!strstr(logtext, "Kat") && !strstr(logtext, "Ollie"), "no name in the log");
	CHECK(strstr(logtext, "82625a00") && strstr(logtext, "82625dd1"), "the 8-character prefixes are logged: %s", logtext);
}

static void test_swap_and_union(void)
{
	char *j;
	/* Kat has Beta from before; Ollie plays with her and has nothing */
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	trigger(2);
	achievements_close();

	seat(KEY_B, "Ollie", KEY_A, "Kat", NULL, NULL);   /* swapped seats */
	notice_n = 0;
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 1, "the union has Beta: %u", achievements_earned());
	CHECK(achievements_player_earned(0) == 0 && achievements_player_earned(1) == 1, "Ollie none, Kat one");
	trigger(2);   /* Beta keeps firing for Ollie, who lacks it */
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "Ollie"), "only Ollie: '%s'", notices[0].who);
	CHECK(notices[0].earned == 1, "his count, not the union: %u", notices[0].earned);
	achievements_close();
	j = read_file(path(1));
	CHECK(lines_with(j, " " KEY_A " Beta") == 1 && lines_with(j, " " KEY_B " Beta") == 1, "one line each, none twice: '%s'", j);

	/* an achievement nobody has: both get it, in one toast */
	notice_n = 0;
	CHECK(load() == 3, "load");
	trigger(3);
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "Ollie and Kat"), "seat order: '%s'", notices[0].who);
	CHECK(achievements_earned() == 2, "union two");
}

static void test_who_text_sizes(void)
{
	seat(KEY_A, "Kat", KEY_B, "Ollie", KEY_C, "Rina");
	CHECK(load() == 3, "load");
	trigger(1);
	CHECK(!strcmp(notices[0].who, "Kat, Ollie and Rina"), "three: '%s'", notices[0].who);
	achievements_close();
	seat(KEY_A, "Kat", KEY_B, "Ollie", KEY_C, "Rina");
	snprintf(seated[3].key, sizeof seated[3].key, KEY_D);
	snprintf(seated[3].name, sizeof seated[3].name, "Ann");
	seated_n = 4;
	notice_n = 0;
	CHECK(load() == 3, "load");
	trigger(3);
	CHECK(!strcmp(notices[0].who, "4 players"), "four: '%s'", notices[0].who);
}

/* two multitaps: 8 Miis earn it, one journal line each, and the page can show each of them */
static void test_eight_players(void)
{
	unsigned i;
	memset(seated, 0, sizeof seated);
	for (i = 0; i < 8; i++) {
		snprintf(seated[i].key, sizeof seated[0].key, "826260%02xc2123456", i);
		snprintf(seated[i].name, sizeof seated[0].name, "P%u", i + 1);
	}
	seated_n = 8;
	CHECK(load() == 3, "load");
	CHECK(achievements_player_count() == 8, "eight players");
	trigger(1);
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "8 players"), "one toast, '%s'", notices[0].who);
	CHECK(lines_with(read_file(path(1)), "826260") == 8, "eight journal lines");
	for (i = 0; i < 8; i++) {
		achievements_view_player(i);
		CHECK(achievements_player_earned(i) == 1 && !strcmp(achievements_player_name(i), seated[i].name), "player %u", i);
	}
	achievements_close();
}

static void test_missing_mii_reads_as_guest(void)
{
	char *j;
	/* a Mii earned Alpha, then its Mii left the console: the seat reads as Guest (the hook
	 * gives nothing for it); the line stays in the journal and Guest earns on its own */
	seat(KEY_GONE, "Gone", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	trigger(1);
	achievements_close();

	seat(NULL, NULL, NULL, NULL, NULL, NULL);   /* the hook skips the missing Mii */
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 0, "Guest has nothing");
	trigger(1);
	CHECK(notice_n == 2 && !notices[1].who[0], "Guest's toast has no name");
	achievements_close();
	j = read_file(path(1));
	CHECK(lines_with(j, " " KEY_GONE " Alpha") == 1, "the missing Mii's line stays: '%s'", j);
	CHECK(lines_with(j, "\n") == 2, "and Guest's was added");
	CHECK(load() == 3 && achievements_earned_for(KEY_GONE) == 1, "its unlocks are still counted");
}

static void test_cap(void)
{
	size_t before;
	FILE *f;
	int i;
	CHECK(load() == 3, "load");
	trigger(1);
	achievements_close();
	/* fill the journal, with lines of another set, up to 20 bytes under the cap */
	f = fopen(path(1), "ab");
	CHECK(f != NULL, "journal");
	while (ftell(f) < 1024 * 1024 - 150)
		fputs("00000000000000000000000000000000 9 filler filler filler filler filler\n", f);
	for (i = (int)(1024 * 1024 - 20 - ftell(f)) - 1; i > 0; i--)
		fputc('x', f);
	fputc('\n', f);
	fclose(f);
	f = fopen(path(1), "rb");
	fseek(f, 0, SEEK_END);
	before = (size_t)ftell(f);
	fclose(f);
	CHECK(before < 1024 * 1024 && before > 1024 * 1024 - 100, "size %zu", before);

	seat(KEY_A, "Kat", KEY_B, "Ollie", NULL, NULL);
	CHECK(load() == 3, "a journal under the cap loads");
	trigger(2);   /* two lines of about 60 bytes: past the cap, refused, not written */
	CHECK(achievements_earned() == 1, "earned in the game");
	achievements_close();
	f = fopen(path(1), "rb");
	fseek(f, 0, SEEK_END);
	CHECK((size_t)ftell(f) <= 1024 * 1024, "the file never grows past the cap");
	fclose(f);
	/* a journal over the cap: the whole set is refused */
	f = fopen(path(1), "ab");
	for (i = 0; i < 100; i++)
		fputs("00000000000000000000000000000000 9 filler filler filler filler filler\n", f);
	fclose(f);
	CHECK(load() == -1, "a journal over the cap refuses the set");
}

static void test_state_from_other_players(void)
{
	void *state;
	int n;
	/* Kat plays and has Alpha; her state carries her key */
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	trigger(1);
	n = achievements_state_save(NULL, 0);
	CHECK(n >= 36 + 19, "state size %d", n);
	state = malloc((size_t)n);
	CHECK(achievements_state_save(state, (unsigned)n) == n, "save");
	CHECK(!memcmp((char *)state + n - 2, "WK", 2) && ((unsigned char *)state)[n - 3] == 1, "seat keys at the end");
	CHECK(!memcmp((char *)state + n - 3 - 16, KEY_A, 16), "Kat's key");
	achievements_close();

	/* Ollie plays: the state's game goes on, the earned flags are Ollie's own (none) */
	seat(KEY_B, "Ollie", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 0, "Ollie has nothing");
	CHECK(achievements_state_load(state, (unsigned)n) == 1, "the state loads for the game");
	CHECK(achievements_earned() == 0, "Kat's unlock did not move to Ollie: %u", achievements_earned());
	notice_n = 0;
	trigger(1);   /* Alpha fires for Ollie: only him */
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "Ollie"), "Ollie earns it: '%s'", notices[0].who);
	achievements_close();
	CHECK(lines_with(read_file(path(1)), " Alpha") == 2, "Kat's line and Ollie's");

	/* the same Kat again: her state's bits apply (a journal lost in between is not needed) */
	remove(path(1));
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 0, "journal gone");
	CHECK(achievements_state_load(state, (unsigned)n) == 1, "load");
	CHECK(achievements_earned() == 1, "Kat's own state gives it back");
	achievements_close();

	/* Guest loading a state of Kat's: nothing moves to Guest */
	remove(path(1));
	seat(NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	CHECK(achievements_state_load(state, (unsigned)n) == 1, "load");
	CHECK(achievements_earned() == 0, "Guest gets nothing from Kat's state");
	/* and a state Guest makes has no keys: the bytes are those of before the Players menu */
	trigger(2);
	n = achievements_state_save(NULL, 0);
	free(state);
	state = malloc((size_t)n);
	CHECK(achievements_state_save(state, (unsigned)n) == n, "save");
	CHECK(!(((unsigned char *)state)[n - 2] == 'W' && ((unsigned char *)state)[n - 1] == 'K'), "no keys in Guest's state");
	achievements_close();
	remove(path(1));
	CHECK(load() == 3, "load");
	CHECK(achievements_earned() == 0, "journal gone");
	CHECK(achievements_state_load(state, (unsigned)n) == 1 && achievements_earned() == 1, "Guest's own state: its bit");
	free(state);
}

static void test_test_mode_has_no_players(void)
{
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	achievementsEnabled = ACH_TEST;
	CHECK(load() == 3, "load");
	trigger(1);
	CHECK(notice_n == 1 && !notices[0].who[0], "Test mode: Guest alone, no name");
	achievements_close();
	CHECK(!fopen(path(1), "rb"), "Test mode writes no journal");
}

static void test_players_changed_loads_again(void)
{
	seat(KEY_A, "Kat", NULL, NULL, NULL, NULL);
	CHECK(load() == 3, "load");
	trigger(1);
	seat(KEY_A, "Kat", KEY_B, "Ollie", NULL, NULL);   /* Ollie sits down in the menu */
	achievements_players_changed();
	CHECK(achievements_player_count() == 2, "two players now");
	CHECK(achievements_player_earned(0) == 1 && achievements_player_earned(1) == 0, "Kat keeps hers, Ollie has none");
	notice_n = 0;
	trigger(1);   /* Alpha fires again, for Ollie only */
	CHECK(notice_n == 1 && !strcmp(notices[0].who, "Ollie"), "Ollie only: '%s'", notices[0].who);
	achievements_close();
	CHECK(lines_with(read_file(path(1)), " Alpha") == 2, "Kat's line and Ollie's, none twice");
}

/* ---- the seats: which Miis earn (Gamecube/ach_players.h) ---- */

static unsigned conn_mask;
static int seat_connected(int seat) { return (int)(conn_mask >> seat & 1); }
static int known(const char *key, char *name, unsigned size)
{
	if (!strcmp(key, KEY_GONE)) return 0;                  /* no longer on the console */
	if (!strcmp(key, KEY_C)) return -1;                    /* the database was not read */
	snprintf(name, size, "%s", !strcmp(key, KEY_A) ? "Kat" : "Ollie");
	return 1;
}

static void test_seats_pick_connected_miis(void)
{
	char s[26][256];
	achievement_player_t out[ACH_PLAYERS_MAX];
	unsigned n;
	memset(s, 0, sizeof s);
	strcpy(s[0], KEY_A);
	strcpy(s[1], KEY_B);
	strcpy(s[3], KEY_GONE);       /* multitap 1B: a Mii that left */
	strcpy(s[4], KEY_C);          /* 1C: name unknown */
	strcpy(s[5], "82625A00C2123456");   /* 1D: Kat again, upper case: once only */
	strcpy(s[6], "short");        /* not a key */
	strcpy(s[12], KEY_D);         /* Co-Op 1 P3, pad not connected */
	conn_mask = 1u << 0 | 1u << 3 | 1u << 4 | 1u << 5 | 1u << 6;   /* seat 1 (Ollie) has no pad */
	n = ach_players_pick((const char (*)[256])s, 26, seat_connected, known, out);
	CHECK(n == 2, "Kat and Rina: %u", n);
	CHECK(!strcmp(out[0].key, KEY_A) && !strcmp(out[0].name, "Kat"), "Kat first");
	CHECK(!strcmp(out[1].key, KEY_C) && !strcmp(out[1].name, "Mii"), "an unknown name reads Mii");
	conn_mask = 0;
	CHECK(ach_players_pick((const char (*)[256])s, 26, seat_connected, known, out) == 0, "no pads: Guest");
	conn_mask = ~0u;
	n = ach_players_pick((const char (*)[256])s, 26, seat_connected, known, out);
	CHECK(n == 4 && !strcmp(out[1].key, KEY_B), "all connected: Kat, Ollie, Rina, Ann in seat order: %u", n);
}

static const struct hc_test tests[] = {
	{ "guest_alone_is_as_before", test_guest_alone_is_as_before, false },
	{ "old_lines_belong_to_guest", test_old_lines_belong_to_guest, false },
	{ "two_miis_both_earn", test_two_miis_both_earn, false },
	{ "swap_and_union", test_swap_and_union, false },
	{ "who_text_sizes", test_who_text_sizes, false },
	{ "eight_players", test_eight_players, false },
	{ "missing_mii_reads_as_guest", test_missing_mii_reads_as_guest, false },
	{ "cap", test_cap, false },
	{ "state_from_other_players", test_state_from_other_players, false },
	{ "test_mode_has_no_players", test_test_mode_has_no_players, false },
	{ "players_changed_loads_again", test_players_changed_loads_again, false },
	{ "seats_pick_connected_miis", test_seats_pick_connected_miis, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
