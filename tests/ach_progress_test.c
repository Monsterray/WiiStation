/* ach_progress_test.c -- the progress pop-up rule (Gamecube/ach_progress.h): a pop-up only at a
 * new 10 % step, never for the first reading after a load, reset, disc swap or state load; and
 * the unlock pictures' file names (Gamecube/ach_shot.h). Plain C, no file is opened.
 * scripts/test-achievements.sh builds and runs it. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../Gamecube/ach_progress.h"
#include "../Gamecube/ach_shot.h"

static void test_steps_of_a_percent(void)
{
	CHECK(ach_step_of(0) == 0 && ach_step_of(9) == 0, "0-9 %% is step 0");
	CHECK(ach_step_of(10) == 1 && ach_step_of(19) == 1, "10-19 %% is step 1");
	CHECK(ach_step_of(99) == 9, "99 %% is step 9");
	CHECK(ach_step_of(100) == 10 && ach_step_of(250) == 10, "100 %% and more is step 10");
}

static void test_first_reading_is_the_baseline(void)
{
	unsigned char last = ACH_STEP_NONE;
	CHECK(!ach_step_update(&last, 47), "the first reading shows nothing, whatever it says");
	CHECK(last == 4, "it is the baseline: step %u", last);
	CHECK(!ach_step_update(&last, 49), "the same step: nothing");
	CHECK(ach_step_update(&last, 50), "crossing 50 %%: a pop-up");
	CHECK(!ach_step_update(&last, 55), "inside the step: nothing");
	CHECK(ach_step_update(&last, 71), "a jump over steps shows once");
	CHECK(last == 7, "and keeps the step it reached: %u", last);
}

static void test_each_step_once_and_falling_back(void)
{
	unsigned char last = ACH_STEP_NONE;
	unsigned p, shown = 0;
	ach_step_update(&last, 0);
	for (p = 1; p <= 99; p++) shown += ach_step_update(&last, p);
	CHECK(shown == 9, "from 0 to 99 %% the pop-ups are at 10, 20 ... 90: %u", shown);
	CHECK(!ach_step_update(&last, 95), "inside the last step: nothing");
	CHECK(!ach_step_update(&last, 20), "falling back shows nothing");
	CHECK(last == 2, "and lowers the step: %u", last);
	CHECK(ach_step_update(&last, 30), "climbing again crosses 30 %%: shown again");
	CHECK(!ach_step_update(&last, 9) && !ach_step_update(&last, 9), "a value near zero is no news");
}

static void test_a_reset_starts_a_new_baseline(void)
{
	unsigned char last = ACH_STEP_NONE;
	ach_step_update(&last, 60);
	ach_step_update(&last, 80);
	last = ACH_STEP_NONE;   /* a reset, a state load, a disc swap */
	CHECK(!ach_step_update(&last, 30), "the first reading after it is the baseline, not a pop-up");
	CHECK(ach_step_update(&last, 40), "the next step shows");
}

static void test_table_of_achievements(void)
{
	ach_steps_t t;
	unsigned i;
	ach_steps_reset(&t);
	CHECK(!ach_steps_update(&t, 101, 33), "101: the first reading");
	CHECK(!ach_steps_update(&t, 202, 80), "202: its own first reading, not 101's");
	CHECK(ach_steps_update(&t, 101, 40), "101 crosses 40 %%");
	CHECK(!ach_steps_update(&t, 202, 85), "202 stays in its step");
	CHECK(ach_steps_update(&t, 202, 90), "202 crosses 90 %%");
	ach_steps_reset(&t);
	CHECK(!ach_steps_update(&t, 101, 90), "after a reset 101 starts again");
	/* a ring: more achievements than ACH_STEPS forget the oldest, which starts again (no pop-up shown by mistake) */
	for (i = 0; i < ACH_STEPS + 5; i++) ach_steps_update(&t, 1000 + i, 10);
	CHECK(t.n == ACH_STEPS, "the table is full: %u", t.n);
	CHECK(!ach_steps_update(&t, 101, 95), "101 was forgotten: a new baseline, no pop-up");
	CHECK(!ach_steps_update(&t, 1000 + ACH_STEPS + 4, 10), "the newest is still known");
	CHECK(ach_steps_update(&t, 1000 + ACH_STEPS + 4, 20), "and crosses its step");
}

static void test_shot_paths(void)
{
	char path[128];
	CHECK(ach_shot_path("1234", 56, path, sizeof path), "an Official set");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/shots/1234/56.png"), "path: %s", path);
	CHECK(ach_shot_path("0123456789abcdef", 7, path, sizeof path), "a Local digest's first 16 characters");
	CHECK(!strcmp(path, "sd:/wiistation/achievements/shots/0123456789abcdef/7.png"), "path: %s", path);
	CHECK(!ach_shot_path("", 1, path, sizeof path) && !path[0], "no set (Test mode, nothing loaded): no path");
	CHECK(!ach_shot_path(NULL, 1, path, sizeof path), "NULL: no path");
	CHECK(!ach_shot_path("../x", 1, path, sizeof path), "a set id is never a path");
	CHECK(!ach_shot_path("12/34", 1, path, sizeof path), "no slash");
	CHECK(!ach_shot_path("ABCD", 1, path, sizeof path), "lowercase hex only");
	CHECK(!ach_shot_path("1234", 1, path, 20) && !path[0], "a path that does not fit is refused");
	CHECK(!ach_shot_set_ok("0123456789abcdef01234567"), "24 characters are too long");
	CHECK(ach_shot_set_ok("0123456789abcdef0123456"), "23 fit");
}

static const struct hc_test tests[] = {
	{ "steps_of_a_percent", test_steps_of_a_percent, false },
	{ "first_reading_is_the_baseline", test_first_reading_is_the_baseline, false },
	{ "each_step_once_and_falling_back", test_each_step_once_and_falling_back, false },
	{ "a_reset_starts_a_new_baseline", test_a_reset_starts_a_new_baseline, false },
	{ "table_of_achievements", test_table_of_achievements, false },
	{ "shot_paths", test_shot_paths, false },
};

int main(int argc, char **argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, NULL);
}
