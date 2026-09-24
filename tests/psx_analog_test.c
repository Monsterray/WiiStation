/* psx_analog_test.c - exhaustive check of WiiStation's analog-stick maths.
 *
 *     cc -O2 -o psx_analog_test tests/psx_analog_test.c -lm && ./psx_analog_test
 *
 * Runs on the host: psx_analog.h is plain C with no libogc in it, so the exact code the
 * Wii runs is the code under test here. This covers the maths; scripts/padtest.py covers
 * the live path through Dolphin, which this cannot see.
 *
 * What it asserts, for every driver's full input range:
 *   - rest reads exactly 128, so nothing drifts when the stick is let go;
 *   - the curve never goes backwards;
 *   - both ends are reached, so no part of the stick's travel is unreachable;
 *   - no long plateau, so no part of the stick's travel does nothing. This is the one
 *     that caught the real bug: a Classic Controller used to hit full deflection at about
 *     half its throw, because two separate gains were applied one after the other.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../Gamecube/gc_input/psx_analog.h"

/* Each driver's own conversion, copied from its source. Keeping them here rather than
 * including the drivers (which need libogc) means a driver that changes its conversion
 * without changing this file will not be caught -- so the numbers are named after the
 * files they come from and the test prints them. */
#define GC_STICK_FULL  96.0f     /* controller-GC.c, controller-HidGC.c */
#define DRC_DEADZONE   0.078f    /* controller-WiiDRC.c */

static uint8_t gc(int raw)       { return psx_analog((float)raw / GC_STICK_FULL); }
static uint8_t classic(int raw)  { return psx_analog((float)raw / 127.0f); }
static uint8_t nunchuk(int raw)  { return psx_analog((float)raw / 127.0f); }
static uint8_t wupc(int raw)     { return psx_analog((float)raw / 1024.0f); }
static uint8_t wiidrc(int raw)
{
	float c = (float)raw / 75.0f;
	if (c > DRC_DEADZONE)       c = (c - DRC_DEADZONE) / (1.0f - DRC_DEADZONE);
	else if (c < -DRC_DEADZONE) c = (c + DRC_DEADZONE) / (1.0f - DRC_DEADZONE);
	else                        c = 0.0f;
	return psx_analog(c);
}

static int failures;

static void fail(const char *what, const char *detail)
{
	printf("  FAIL %s: %s\n", what, detail);
	failures++;
}

/* `lo`..`hi` is the driver's own full-travel range; `dead` is how much of it the driver
 * deliberately ignores around the centre, as a fraction (0 for everything but the GamePad). */
static void check(const char *name, uint8_t (*f)(int), int lo, int hi, float dead)
{
	char msg[160];
	int i, plateau = 0, worst = 0, worst_at = 0, span = hi - lo;
	/* A plateau is only a defect outside the dead zone, and a stick with many more raw
	 * steps than the PlayStation's 256 must repeat values -- allow for both. */
	int allowed = (span / 256) + 2 + (int)(span * dead);
	uint8_t prev = f(lo), v;

	if (f(0) != 128) {
		snprintf(msg, sizeof msg, "rest reads %u, not 128", f(0));
		fail(name, msg);
	}
	if (f(lo) != 0) {
		snprintf(msg, sizeof msg, "full one way reads %u, not 0", f(lo));
		fail(name, msg);
	}
	if (f(hi) != 255) {
		snprintf(msg, sizeof msg, "full the other way reads %u, not 255", f(hi));
		fail(name, msg);
	}
	for (i = lo + 1; i <= hi; i++) {
		v = f(i);
		if (v < prev) {
			snprintf(msg, sizeof msg, "curve goes backwards at raw %d (%u then %u)", i, prev, v);
			fail(name, msg);
			break;
		}
		plateau = (v == prev) ? plateau + 1 : 0;
		if (plateau > worst) { worst = plateau; worst_at = i; }
		prev = v;
	}
	if (worst > allowed) {
		snprintf(msg, sizeof msg, "%d raw steps around %d change nothing (allowed %d)",
			worst + 1, worst_at, allowed + 1);
		fail(name, msg);
	}
	printf("  %-22s raw %5d..%-5d  out %3u..%-3u  rest %3u  longest flat %d/%d\n",
		name, lo, hi, f(lo), f(hi), f(0), worst + 1, allowed + 1);
}

int main(void)
{
	uint8_t v;
	int i;

	printf("stick curves\n");
	check("GameCube",        gc,      -96,   96,   0.0f);
	check("Classic",         classic, -127,  127,  0.0f);
	check("Wiimote+Nunchuk", nunchuk, -127,  127,  0.0f);
	check("Wii U Pro",       wupc,    -1024, 1024, 0.0f);
	check("Wii U GamePad",   wiidrc,  -75,   75,   DRC_DEADZONE);

	printf("gate to square\n");
	{
		/* The octagon's corners -- a full push in each of the 8 directions -- must reach the
		 * square's: 0 or 255 on a moved axis, 128 on an unmoved one. */
		int d;
		float x, y, prev, r;
		char msg[80];
		for (d = 0; d < 8; d++) {
			x = cosf(d * 3.14159265f / 4.0f);
			y = sinf(d * 3.14159265f / 4.0f);
			psx_square(&x, &y);
			if ((psx_analog(x) % 255 && psx_analog(x) != 128) || (psx_analog(y) % 255 && psx_analog(y) != 128)) {
				snprintf(msg, sizeof msg, "corner %d reads (%u, %u)", d, psx_analog(x), psx_analog(y));
				fail("gate to square", msg);
			}
		}
		x = y = 0.0f;
		psx_square(&x, &y);
		if (psx_analog(x) != 128 || psx_analog(y) != 128) fail("gate to square", "rest moved");
		/* Pushing further along any direction never comes back in. */
		for (d = 0; d < 64; d++) {
			prev = 0.0f;
			for (i = 1; i <= 100; i++) {
				x = cosf(d * 3.14159265f / 32.0f) * i / 100.0f;
				y = sinf(d * 3.14159265f / 32.0f) * i / 100.0f;
				psx_square(&x, &y);
				r = x * x + y * y;
				if (r < prev) { fail("gate to square", "goes backwards along a direction"); break; }
				prev = r;
			}
		}
		printf("  8 gate corners reach the square's; rest stays; outward stays outward\n");
	}

	printf("sensitivity\n");
	/* 1.0 must be the identity, or the default setting is not a 1:1 stick. */
	for (i = 0; i <= 255; i++)
		if (apply_sensitivity((uint8_t)i, 1.0f) != (uint8_t)i) {
			char msg[80];
			snprintf(msg, sizeof msg, "1.0 changed %d to %u", i, apply_sensitivity((uint8_t)i, 1.0f));
			fail("sensitivity", msg);
			break;
		}
	printf("  1.0 leaves all 256 values alone\n");

	/* Rest stays at rest at any setting. Below 1.0 the ends pull in -- that is what a
	 * lower sensitivity means -- so only 1.0 and above still have to reach them. */
	for (i = 1; i <= 40; i++) {
		float s = i / 10.0f;
		if (apply_sensitivity(128, s) != 128) fail("sensitivity", "rest moved");
		if (s >= 1.0f && (apply_sensitivity(0, s) != 0 || apply_sensitivity(255, s) != 255))
			fail("sensitivity", "an end stopped being reachable at or above 1.0");
	}
	printf("  0.1 .. 4.0 keep rest at 128; 1.0 and up still reach both ends\n");

	/* Above 1.0 the outer travel saturates -- that is the documented trade, so check it
	 * is a saturation and not a wrap. */
	v = apply_sensitivity(255, 2.0f);
	if (v != 255) fail("sensitivity", "2.0 did not saturate at 255");
	v = apply_sensitivity(0, 2.0f);
	if (v != 0) fail("sensitivity", "2.0 did not saturate at 0");
	printf("  2.0 saturates instead of wrapping\n");

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
