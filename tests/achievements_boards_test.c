/* achievements_boards_test.c -- the local leaderboards in the engine (achievements.c, Docs/ACHIEVEMENTS.md,
 * "Local leaderboards"): every seated Mii (Guest when none) keeps its best of a finished attempt in the
 * game's file, LowerIsBetter and rcheevos' value formats, the pop-up, the table the book reads (the
 * seated Miis marked, a seated Mii below the table last with its real rank), and the modes with no
 * boards. rc_client is not here: the test stands in for achievements_official.c's board list. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../achievements.h"
#include "../Gamecube/ach_boards.h"
#include "../deps/rcheevos/include/rc_runtime.h"
#include "../deps/rcheevos/include/rc_runtime_types.h"
#include <stdarg.h>
#include <stdlib.h>

static unsigned char ram[0x200000], scratch[0x400];
unsigned char *psxM = ram, *psxH = scratch;
void SysPrintf(const char *fmt, ...) { (void)fmt; }

/* the board list: 7 (a time, lower is better) and 8 (a score) */
int achievements_official_board_info(unsigned i, unsigned *id, int *lower, int *format)
{
	if (i > 1) return -1;
	*id = i ? 8 : 7;
	*lower = i ? 0 : 1;
	*format = i ? RC_FORMAT_SCORE : RC_FORMAT_FRAMES;
	return 0;
}

#define KEY_A "82625a00c2123456"   /* Kat */
#define KEY_B "82625dd1c2123456"   /* Ollie */

static const char *dir;
static char path[256];
static achievement_player_t seated[ACH_PLAYERS_MAX];
static unsigned seated_n;
static unsigned hook(achievement_player_t *out)
{
	memcpy(out, seated, sizeof seated);
	return seated_n;
}
static int lookup(const char *key, char *name, unsigned size)
{
	if (!strcmp(key, KEY_A)) snprintf(name, size, "Kat");
	else if (!strcmp(key, KEY_B)) snprintf(name, size, "Ollie");
	else return 0;
	return 1;
}

static void seat(const char *a, const char *b)
{
	const char *k[2] = { a, b };
	int i;
	memset(seated, 0, sizeof seated);
	seated_n = 0;
	for (i = 0; i < 2; i++)
		if (k[i]) {
			snprintf(seated[seated_n].key, sizeof seated[0].key, "%s", k[i]);
			snprintf(seated[seated_n].name, sizeof seated[0].name, "%s", !strcmp(k[i], KEY_A) ? "Kat" : "Ollie");
			seated_n++;
		}
}

static struct { unsigned n; char title[96], desc[24], who[96]; } popup;
static void on_notice(const achievement_notice_t *n)
{
	if (n->kind != ACH_NOTICE_BOARD) return;
	popup.n++;
	snprintf(popup.title, sizeof popup.title, "%s", n->title);
	snprintf(popup.desc, sizeof popup.desc, "%s", n->desc);
	snprintf(popup.who, sizeof popup.who, "%s", n->who);
}

static void setup(void)
{
	snprintf(path, sizeof path, "%s/boards_engine.lb", dir);
	achievements_close();   /* first: it writes what a failed test left open */
	remove(path);
	memset(&popup, 0, sizeof popup);
	seat(NULL, NULL);
	achievementsEnabled = ACH_LOCAL;
	achievements_notify = on_notice;
	achievements_players = hook;
	achievements_player_lookup = lookup;
}

static void test_every_seated_mii_keeps_its_best(void)
{
	ach_boards_t f;
	char text[24];
	seat(KEY_A, KEY_B);
	achievements_boards_open(path);
	CHECK(achievements_boards_score(7, 3600, 1, RC_FORMAT_FRAMES, "Best Lap") == 2, "both Miis");
	CHECK(popup.n == 1 && !strcmp(popup.title, "New best: Best Lap") && !strcmp(popup.who, "Kat, Ollie"), "pop-up: %s / %s", popup.title, popup.who);
	rc_format_value(text, sizeof text, 3600, RC_FORMAT_FRAMES);
	CHECK(!strcmp(popup.desc, text) && strchr(popup.desc, ':'), "the value as rcheevos formats it: %s", popup.desc);
	CHECK(achievements_boards_score(7, 3700, 1, RC_FORMAT_FRAMES, "Best Lap") == 0 && popup.n == 1, "slower: no new best, no pop-up");
	CHECK(achievements_boards_score(7, 3000, 1, RC_FORMAT_FRAMES, "Best Lap") == 2 && popup.n == 2, "faster");
	seat(KEY_B, NULL);
	achievements_boards_players_changed();
	CHECK(achievements_boards_score(7, 2900, 1, RC_FORMAT_FRAMES, "Best Lap") == 1 && !strcmp(popup.who, "Ollie"), "only Ollie sits");
	CHECK(achievements_boards_score(8, 500, 0, RC_FORMAT_SCORE, "High Score") == 1, "another board");
	achievements_close();   /* writes */
	CHECK(ach_boards_open(&f, path) == 1 && f.n == 3, "3 records: %u", f.n);
	{
		ach_board_score_t top[3];
		CHECK(ach_boards_top(&f, 7, 1, top, 3) == 2 && top[0].value == 2900 && top[1].value == 3000, "board 7 on disk");
		CHECK(top[0].date > 1000000000u, "a date");
	}
	ach_boards_close(&f);
}

static void test_guest_when_nobody_sits(void)
{
	ach_boards_t f;
	achievements_boards_open(path);
	CHECK(achievements_boards_score(8, 10, 0, RC_FORMAT_SCORE, "High Score") == 1, "Guest");
	CHECK(popup.n == 1 && popup.who[0] == 0, "Guest has no name on the pop-up");
	achievements_close();
	CHECK(ach_boards_open(&f, path) == 1 && f.n == 1 && f.rec[4] == 0 && f.rec[19] == 0, "the zero key");
	ach_boards_close(&f);
}

static void test_the_table(void)
{
	achievement_local_score_t rows[ACH_LOCAL_ROWS + 1];
	unsigned n = ACH_LOCAL_ROWS, i;
	char key[17];
	/* twelve other Miis, then Kat last */
	achievements_boards_open(path);
	for (i = 0; i < 12; i++) {
		snprintf(key, sizeof key, "%016x", 0x1000u + i);
		memset(seated, 0, sizeof seated);
		snprintf(seated[0].key, sizeof seated[0].key, "%s", key);
		seated_n = 1;
		achievements_boards_players_changed();
		achievements_boards_score(8, 1000 + (int)i * 10, 0, RC_FORMAT_SCORE, "High Score");
	}
	seat(KEY_A, KEY_B);
	achievements_boards_players_changed();
	achievements_boards_score(8, 50, 0, RC_FORMAT_SCORE, "High Score");
	CHECK(achievements_board_local(1, rows, &n) == 14 && n == ACH_LOCAL_ROWS + 1, "14 Miis, 8 rows and Kat: %u", n);
	CHECK(rows[0].rank == 1 && rows[0].score[0] && !rows[0].seated && !strncmp(rows[0].name, "Mii ", 4), "best first, a Mii no longer here: %s", rows[0].name);
	CHECK(rows[7].rank == 8 && !rows[7].seated, "eight rows");
	CHECK(rows[8].rank == 13 && rows[8].seated && !strcmp(rows[8].name, "Kat"), "Kat below the table, rank %u %s", rows[8].rank, rows[8].name);
	/* Kat alone climbs into the table: no extra row for her; Ollie is still below */
	seat(KEY_A, NULL);
	achievements_boards_players_changed();
	achievements_boards_score(8, 5000, 0, RC_FORMAT_SCORE, "High Score");
	seat(KEY_A, KEY_B);
	achievements_boards_players_changed();
	n = ACH_LOCAL_ROWS;
	CHECK(achievements_board_local(1, rows, &n) == 14 && n == 9, "9 rows now");
	CHECK(rows[0].rank == 1 && rows[0].seated && !strcmp(rows[0].name, "Kat"), "Kat first");
	CHECK(rows[8].rank == 14 && rows[8].seated && !strcmp(rows[8].name, "Ollie"), "Ollie below the table now: %u %s", rows[8].rank, rows[8].name);
	n = 3;
	CHECK(achievements_board_local(1, rows, &n) == 14 && n == 4, "asked for 3, and Ollie below");
	n = ACH_LOCAL_ROWS;
	CHECK(achievements_board_local(2, rows, &n) == -1 && n == 0, "no such board");
	n = ACH_LOCAL_ROWS;
	CHECK(achievements_board_local(0, rows, &n) == 0 && n == 0, "a board without scores");
	achievements_close();
}

static void test_no_boards_outside_the_ra_modes(void)
{
	FILE *f;
	achievementsEnabled = ACH_CUSTOM;
	seat(KEY_A, NULL);
	achievements_boards_open(path);
	CHECK(achievements_boards_score(7, 1, 1, RC_FORMAT_VALUE, "x") == 0, "Custom keeps no boards");
	achievements_close();
	f = fopen(path, "rb");
	CHECK(!f, "no file");
	if (f) fclose(f);
}

static const struct hc_test tests[] = {
	{ "every_seated_mii_keeps_its_best", test_every_seated_mii_keeps_its_best, false },
	{ "guest_when_nobody_sits", test_guest_when_nobody_sits, false },
	{ "the_table", test_the_table, false },
	{ "no_boards_outside_the_ra_modes", test_no_boards_outside_the_ra_modes, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
