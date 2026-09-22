/* gte_divider_test.c - the GTE's divider against a true division.
 *
 *     cc -O2 -o gte_divider_test tests/gte_divider_test.c && ./gte_divider_test
 *
 * Runs on the host: gte_divider.c is plain C with no libogc in it, and is included below,
 * so the exact code the Wii runs is the code under test here.
 *
 * The coprocessor does not divide. It reads a first guess out of a 257-byte reciprocal
 * table and runs two Newton-Raphson steps on it, which is close to H/SZ but not equal to
 * it. Two things are checked:
 *
 *   1. every entry of the table is what its formula says it is. The entries are written
 *      out as constants, and Newton-Raphson converges so fast that a one-off typo in a
 *      single byte would still give an answer inside the tolerance below -- so the table
 *      has to be checked directly, or a typo would show up nowhere at all.
 *   2. the answer stays in range and within 3/65536 of a true division. 3 is the measured
 *      worst case over all 3,221,192,704 (n, d) pairs the coprocessor can be given; the
 *      denominators swept here reach it, so a change that loses a Newton-Raphson step
 *      fails here instead of on a television.
 */
#include <stdio.h>

#include "../gte_divider.c"

static int failures;

static void fail(const char *what, const char *why)
{
	printf("  FAIL  %-34s %s\n", what, why);
	failures++;
}

/* DIVIDE returns (n << 16) / d, so the error is in those units: 3 is 3/65536 of one screen
 * unit, before the caller multiplies by IR1 and shifts back down by 16. */
#define MAX_ERR 3

int main(void)
{
	unsigned d, i;
	unsigned worst = 0, worst_d = 0, worst_n = 0;

	/* 1. the table, rebuilt from the formula in psx-spx. */
	for (i = 0; i <= 0x100; i++) {
		unsigned want = (0x40000 / (i + 0x100) + 1) / 2;
		want = want > 0x101 ? want - 0x101 : 0;
		if (want != unr_table[i]) {
			char why[96];
			snprintf(why, sizeof why, "entry %u is 0x%02x, formula says 0x%02x",
				 i, unr_table[i], want);
			fail("reciprocal table", why);
			break;
		}
	}

	/* 2. every denominator, with the numerators that reach the corners of it: nothing,
	 * one, exactly 1.0, and just under the point where the result stops fitting. */
	for (d = 1; d <= 0xffff; d++) {
		unsigned hi = 2 * d - 1;
		unsigned ns[6], k;

		if (hi > 0xffff)
			hi = 0xffff;
		ns[0] = 0; ns[1] = 1; ns[2] = d; ns[3] = hi; ns[4] = hi / 2; ns[5] = d / 2;

		for (k = 0; k < 6; k++) {
			unsigned n = ns[k], got, err;
			unsigned long long exact;

			if (n > 0xffff)
				continue;
			got = DIVIDE((uint16_t)n, (uint16_t)d);
			if (got > 0x1ffff) {
				fail("range", "quotient above 0x1ffff");
				continue;
			}
			exact = (((unsigned long long)n << 16) + (d >> 1)) / d;
			err = (unsigned)(got > exact ? got - exact : exact - got);
			if (err > worst) {
				worst = err;
				worst_d = d;
				worst_n = n;
			}
		}
	}

	if (worst > MAX_ERR) {
		char why[96];
		snprintf(why, sizeof why, "worst error %u at %u/%u, over %u",
			 worst, worst_n, worst_d, (unsigned)MAX_ERR);
		fail("accuracy", why);
	}

	/* A numerator twice the denominator or more has no answer that fits. The coprocessor
	 * raises its divide-overflow flag, and the caller's limE() does that from this value,
	 * so it has to come back whole rather than clamped. A zero denominator is a real thing
	 * for a game to do -- a vertex on the camera plane -- and lands in the same place. */
	if (DIVIDE(0xffff, 0x7fff) != 0xffffffffu)
		fail("overflow", "did not report the overflow value");
	if (DIVIDE(0, 0) != 0xffffffffu || DIVIDE(1, 0) != 0xffffffffu)
		fail("zero denominator", "did not report the overflow value");

	printf("worst error %u/65536 at %u/%u\n", worst, worst_n, worst_d);
	printf(failures ? "%d FAILED\n" : "ok\n", failures);
	return failures != 0;
}
