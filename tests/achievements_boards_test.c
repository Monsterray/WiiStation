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
	else if (!strncmp(key, "000000000000200", 15) && key[15] >= '0' && key[15] <= '7') snprintf(name, size, "P%d", key[15] - '0' + 1);
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
	achievement_local_score_t rows[ACH_LOCAL_MAX];
	unsigned n, i;
	int compact;
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
	CHECK(achievements_board_local(1, rows, &n, &compact) == 14 && n == ACH_LOCAL_ROWS + 1 && !compact, "14 Miis, 8 rows and Kat: %u", n);
	CHECK(rows[0].rank == 1 && rows[0].score[0] && !rows[0].seated && !strncmp(rows[0].name, "Mii ", 4), "best first, a Mii no longer here: %s", rows[0].name);
	CHECK(rows[6].rank == 7 && !rows[6].seated, "the top shrank to 7 for two Miis below");
	CHECK(rows[7].rank == 13 && rows[7].seated && !strcmp(rows[7].name, "Kat"), "Kat below the table, rank %u %s", rows[7].rank, rows[7].name);
	CHECK(rows[8].rank == 14 && rows[8].seated && !strcmp(rows[8].name, "Ollie"), "then Ollie, rank %u %s", rows[8].rank, rows[8].name);
	/* Kat alone climbs into the table: no extra row for her; Ollie is still below */
	seat(KEY_A, NULL);
	achievements_boards_players_changed();
	achievements_boards_score(8, 5000, 0, RC_FORMAT_SCORE, "High Score");
	seat(KEY_A, KEY_B);
	achievements_boards_players_changed();
	CHECK(achievements_board_local(1, rows, &n, &compact) == 14 && n == 9, "9 rows now");
	CHECK(rows[0].rank == 1 && rows[0].seated && !strcmp(rows[0].name, "Kat"), "Kat first");
	CHECK(rows[8].rank == 14 && rows[8].seated && !strcmp(rows[8].name, "Ollie"), "Ollie below the table now: %u %s", rows[8].rank, rows[8].name);
	CHECK(achievements_board_local(2, rows, &n, &compact) == -1 && n == 0, "no such board");
	CHECK(achievements_board_local(0, rows, &n, &compact) == 0 && n == 0, "a board without scores");
	achievements_close();
}

/* up to 8 players at once (two multitaps): k seated Miis, the first `better` of them in the top part */
static void seat_many(unsigned k)
{
	unsigned i;
	memset(seated, 0, sizeof seated);
	for (i = 0; i < k; i++) {
		snprintf(seated[i].key, sizeof seated[0].key, "%016x", 0x2000u + i);
		snprintf(seated[i].name, sizeof seated[0].name, "P%u", i + 1);
	}
	seated_n = k;
	achievements_boards_players_changed();
}

static void test_eight_seated(void)
{
	achievement_local_score_t rows[ACH_LOCAL_MAX];
	unsigned n, i, mine = 0;
	int compact;
	char key[17];
	FILE *f;
	long size;
	achievements_boards_open(path);
	/* 20 other Miis on board 8 (a score: higher is better) at 1000 + 10 i, then 8 seated Miis below them all */
	for (i = 0; i < 20; i++) {
		snprintf(key, sizeof key, "%016x", 0x1000u + i);
		memset(seated, 0, sizeof seated);
		snprintf(seated[0].key, sizeof seated[0].key, "%s", key);
		seated_n = 1;
		achievements_boards_players_changed();
		achievements_boards_score(8, 1000 + (int)i * 10, 0, RC_FORMAT_SCORE, "High Score");
	}
	achievements_flush();
	f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fclose(f);
	seat_many(8);
	CHECK(achievements_boards_score(8, 100, 0, RC_FORMAT_SCORE, "High Score") == 8, "a new best for each of the 8");
	CHECK(popup.who[0] && !strcmp(popup.who, "P1, P2 +6"), "the pop-up names two and counts six: '%s'", popup.who);
	f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	CHECK(ftell(f) == size, "8 records, none written in the frame");
	fclose(f);
	CHECK(achievements_board_local(1, rows, &n, &compact) == 28, "28 Miis");
	for (i = 0; i < n; i++) mine += rows[i].seated;
	CHECK(mine == 8 && compact && n <= ACH_LOCAL_MAX, "all 8 seated shown, compact: %u seated of %u rows", mine, n);
	CHECK(rows[0].rank == 1 && !rows[0].seated, "the top is there");
	CHECK(rows[n - 1].rank == 28 && !strcmp(rows[n - 1].name, "P8") && rows[n - 8].rank == 21, "the ties by key, the worst last with its real rank: %u %s", rows[n - 1].rank, rows[n - 1].name);
	/* two of them ranked in the top part: 6 below fit in normal rows by shrinking the top */
	seat_many(8);
	for (i = 2; i < 8; i++) {   /* P3..P8 better than everything: they are the top */
		seat_many(0); seated_n = 1;
		snprintf(seated[0].key, sizeof seated[0].key, "%016x", 0x2000u + i);
		snprintf(seated[0].name, sizeof seated[0].name, "P%u", i + 1);
		achievements_boards_players_changed();
		achievements_boards_score(8, 5000 + (int)i, 0, RC_FORMAT_SCORE, "High Score");
	}
	seat_many(8);
	CHECK(achievements_board_local(1, rows, &n, &compact) == 28, "still 28");
	for (mine = i = 0; i < n; i++) mine += rows[i].seated;
	CHECK(mine == 8 && !compact && n == ACH_LOCAL_FIT, "6 on top, 2 below: normal rows, %u rows, %u seated, compact %d", n, mine, compact);
	CHECK(rows[0].rank == 1 && rows[0].seated, "P8 first");
	CHECK(rows[n - 1].rank == 28 && rows[n - 2].rank == 27, "P1 and P2 last with ranks %u %u", rows[n - 2].rank, rows[n - 1].rank);
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
	{ "eight_seated", test_eight_seated, false },
	{ "no_boards_outside_the_ra_modes", test_no_boards_outside_the_ra_modes, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
