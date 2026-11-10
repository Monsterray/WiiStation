/* ach_boards_test.c -- the local leaderboard file (Gamecube/ach_boards.h, Docs/ACHIEVEMENTS.md,
 * "Local leaderboards"): the exact bytes, create, append, improve in place, LowerIsBetter, reload,
 * a cut-short last record, a file of another version, the sort, and many Miis at once.
 * scripts/test-achievements.sh builds it with a scratch directory as its argument. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../Gamecube/ach_boards.h"
#include <time.h>

static const char *dir;
static char path[256];

static void setup(void)
{
	snprintf(path, sizeof path, "%s/boards_test.lb", dir);
	remove(path);
}

static long file_size(void)
{
	FILE *f = fopen(path, "rb");
	long n = -1;
	if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
	return n;
}

static void key_of(char *out, unsigned n) { snprintf(out, 17, "%016x", 0x82620000u + n); }

static void test_exact_bytes(void)
{
	ach_boards_t b;
	unsigned char got[ACH_LB_HEADER + ACH_LB_REC];
	static const unsigned char want[ACH_LB_HEADER + ACH_LB_REC] = {
		'W', 'L', 'B', '1', 0, 1, 0, 28, 0, 0, 0, 1,
		0x00, 0x01, 0x02, 0x03,
		'8', '2', '6', '2', '5', 'a', '0', '0', 'c', '2', '1', '2', '3', '4', '5', '6',
		0xff, 0xff, 0xfc, 0x18,   /* -1000 */
		0x65, 0x4a, 0xbc, 0xde,
	};
	FILE *f;
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 0, "a new file opens empty");
	CHECK(ach_boards_record(&b, 0x00010203, "82625a00c2123456", -1000, 0, 0x654abcdeu) == 1, "first score");
	CHECK(file_size() < 0, "nothing is written before the flush");
	CHECK(ach_boards_flush(&b) == 1, "flush");
	CHECK(file_size() == ACH_LB_HEADER + ACH_LB_REC, "12 + 28 bytes: %ld", file_size());
	f = fopen(path, "rb");
	CHECK(f && fread(got, 1, sizeof got, f) == sizeof got, "read it back");
	fclose(f);
	CHECK(!memcmp(got, want, sizeof got), "the bytes are big endian, field by field");
	ach_boards_close(&b);
}

static void test_improve_in_place_and_append(void)
{
	ach_boards_t b;
	CHECK(ach_boards_open(&b, path) == 1, "open");
	CHECK(ach_boards_record(&b, 1, "aaaaaaaaaaaaaaaa", 100, 0, 10) == 1, "new pair");
	CHECK(ach_boards_record(&b, 1, "bbbbbbbbbbbbbbbb", 50, 0, 11) == 1, "second Mii");
	CHECK(ach_boards_record(&b, 2, "aaaaaaaaaaaaaaaa", 7, 0, 12) == 1, "second board");
	CHECK(ach_boards_flush(&b) && file_size() == ACH_LB_HEADER + 3 * ACH_LB_REC, "three records");
	CHECK(ach_boards_record(&b, 1, "aaaaaaaaaaaaaaaa", 100, 0, 20) == 0, "equal is not better");
	CHECK(ach_boards_record(&b, 1, "aaaaaaaaaaaaaaaa", 90, 0, 21) == 0, "lower is worse here");
	CHECK(b.ndirty == 0, "a worse score changes nothing");
	CHECK(ach_boards_record(&b, 1, "aaaaaaaaaaaaaaaa", 150, 0, 22) == 1, "a new best");
	CHECK(b.n == 3 && b.ndirty == 1, "overwritten in place, no new record");
	CHECK(ach_boards_flush(&b) && file_size() == ACH_LB_HEADER + 3 * ACH_LB_REC, "the file did not grow");
	ach_boards_close(&b);
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 3, "reload: %u", b.n);
	{
		ach_board_score_t top[4];
		unsigned total = ach_boards_top(&b, 1, 0, top, 4);
		CHECK(total == 2 && top[0].value == 150 && top[0].date == 22 && top[1].value == 50, "board 1 after reload");
		CHECK(ach_boards_top(&b, 2, 0, top, 4) == 1 && top[0].value == 7, "board 2");
		CHECK(ach_boards_top(&b, 3, 0, top, 4) == 0, "an empty board");
	}
	ach_boards_close(&b);
}

static void test_lower_is_better_and_guest(void)
{
	ach_boards_t b;
	ach_board_score_t top[3];
	ach_boards_open(&b, path);
	CHECK(ach_boards_record(&b, 9, "", 9000, 1, 1) == 1, "Guest's first time");
	CHECK(ach_boards_record(&b, 9, "", 9500, 1, 2) == 0, "slower is not better");
	CHECK(ach_boards_record(&b, 9, "", 8700, 1, 3) == 1, "faster is");
	CHECK(ach_boards_record(&b, 9, "cccccccccccccccc", 8000, 1, 4) == 1, "a Mii");
	CHECK(ach_boards_top(&b, 9, 1, top, 3) == 2 && top[0].value == 8000 && top[1].value == 8700, "the lowest first");
	CHECK(top[1].key[0] == 0 && top[1].key[15] == 0, "Guest's key is all zero");
	CHECK(ach_boards_rank(&b, &top[1], 1) == 2, "rank");
	/* a tie: the earlier date ranks first */
	ach_boards_record(&b, 9, "dddddddddddddddd", 8700, 1, 2);
	CHECK(ach_boards_top(&b, 9, 1, top, 3) == 3 && top[1].date == 2 && top[2].date == 3, "ties by date");
	ach_boards_close(&b);
}

static void test_cut_short_last_record(void)
{
	ach_boards_t b;
	char k[17];
	unsigned i;
	FILE *f;
	long size;
	ach_boards_open(&b, path);
	for (i = 0; i < 5; i++) { key_of(k, i); ach_boards_record(&b, 1, k, (int32_t)i, 0, i); }
	ach_boards_close(&b);
	size = file_size();
	/* a power cut while the sixth record went in: half a record past the count */
	f = fopen(path, "ab");
	fwrite("0123456789abcd", 1, 14, f);
	fclose(f);
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 5, "five whole records: %u", b.n);
	key_of(k, 5);
	CHECK(ach_boards_record(&b, 1, k, 5, 0, 5) == 1 && ach_boards_flush(&b), "the next append writes over the fragment");
	CHECK(file_size() == size + ACH_LB_REC, "size %ld", file_size());
	ach_boards_close(&b);
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 6, "six now");
	ach_boards_close(&b);
	/* the count says more than the file holds */
	f = fopen(path, "r+b");
	fseek(f, 8, SEEK_SET);
	fwrite("\0\0\0\x63", 1, 4, f);
	fclose(f);
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 6, "the count is clamped to the whole records");
	ach_boards_close(&b);
	/* a header that never finished */
	f = fopen(path, "wb");
	fwrite("WLB1", 1, 4, f);
	fclose(f);
	CHECK(ach_boards_open(&b, path) == 1 && b.n == 0, "a torn header is an empty file");
	ach_boards_record(&b, 1, "eeeeeeeeeeeeeeee", 1, 0, 1);
	CHECK(ach_boards_flush(&b) && file_size() == ACH_LB_HEADER + ACH_LB_REC, "and is made whole");
	ach_boards_close(&b);
}

static void test_another_version_is_left_alone(void)
{
	ach_boards_t b;
	FILE *f = fopen(path, "wb");
	fwrite("WLB2\0\2\0\x20\0\0\0\0", 1, 12, f);
	fclose(f);
	CHECK(ach_boards_open(&b, path) == 0, "not ours");
	CHECK(ach_boards_record(&b, 1, "ffffffffffffffff", 1, 0, 1) == -1, "nothing is kept");
	ach_boards_close(&b);
	CHECK(file_size() == 12, "never written over");
}

static void test_many_miis(void)
{
	enum { MIIS = 300, BOARDS = 100 };
	ach_boards_t b;
	ach_board_score_t top[8];
	char k[17];
	unsigned i, j, total;
	clock_t t0;
	double fill_ms, flush_ms, load_ms, top_ms, rec_ms;
	long size;
	ach_boards_open(&b, path);
	t0 = clock();
	for (j = 0; j < BOARDS; j++)
		for (i = 0; i < MIIS; i++) {
			key_of(k, i);
			CHECK(ach_boards_record(&b, 1000 + j, k, (int32_t)((i * 7919u + j * 104729u) % 100000u), j & 1, 1700000000u + i) == 1,
			      "record %u/%u", j, i);
		}
	fill_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
	t0 = clock();
	CHECK(ach_boards_flush(&b), "flush");
	flush_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
	size = file_size();
	CHECK(size == ACH_LB_HEADER + (long)MIIS * BOARDS * ACH_LB_REC, "30000 records = 840012 bytes: %ld", size);
	ach_boards_close(&b);
	t0 = clock();
	CHECK(ach_boards_open(&b, path) == 1 && b.n == MIIS * BOARDS, "reload %u", b.n);
	load_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
	t0 = clock();
	total = ach_boards_top(&b, 1050, 0, top, 8);
	top_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
	CHECK(total == MIIS, "300 Miis on a board");
	for (i = 1; i < 8; i++) CHECK(top[i - 1].value >= top[i].value, "sorted");
	/* one improvement in the middle of 30000 records: the cost of a new best during play */
	key_of(k, 150);
	t0 = clock();
	CHECK(ach_boards_record(&b, 1050, k, 100001, 0, 1800000000u) == 1, "improve");
	rec_ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC;
	CHECK(ach_boards_flush(&b) && file_size() == size, "written in place");
	ach_boards_close(&b);
	printf("  30000 records (100 boards x 300 Miis), %ld bytes: fill %.1f ms, one write of all %.1f ms, "
	       "load %.2f ms, top 8 of a board %.3f ms, one new best %.3f ms (host PC; the Wii is far slower)\n",
	       size, fill_ms, flush_ms, load_ms, top_ms, rec_ms);
}

static const struct hc_test tests[] = {
	{ "exact_bytes", test_exact_bytes, false },
	{ "improve_in_place_and_append", test_improve_in_place_and_append, false },
	{ "lower_is_better_and_guest", test_lower_is_better_and_guest, false },
	{ "cut_short_last_record", test_cut_short_last_record, false },
	{ "another_version_left_alone", test_another_version_is_left_alone, false },
	{ "many_miis", test_many_miis, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	dir = argv[1] ? argv[1] : ".";
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
