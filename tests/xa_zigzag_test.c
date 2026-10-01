/* xa_zigzag_test.c - the XA 37800 -> 44100 Hz filter (dfsound/xa_zigzag.h) against the
 * 29-tap loop it replaced (2026-10-01), output word by output word.
 *
 *     cc -O2 -o xa_zigzag_test tests/xa_zigzag_test.c && ./xa_zigzag_test
 *
 * The header is included, so the code under test is the code the Wii runs. The input is a
 * random stream that switches between stereo and mono, with quiet and full-scale stretches
 * (saturation) and enough samples to wrap the ring many times.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../dfsound/xa_zigzag.h"

/* the old code, from dfsound/xa.c before 2026-10-01 */
static short ref_ring[2][32];
static unsigned int ref_p;

static short ref_tap(const short *ring, unsigned int table, unsigned int p)
{
	const short *coef = xa_zigzag_tables[table];
	int32_t sum = 0;
	unsigned int i;
	for (i = 0; i < 29; i++)
		sum += ((int32_t)ring[(p - i) & 31] * (int32_t)coef[i]) >> 15;
	if (sum < -32768) sum = -32768;
	else if (sum > 32767) sum = 32767;
	return (short)sum;
}

static short sample(unsigned int n)
{
	/* stretches of quiet noise and of full-scale noise */
	int loud = (n / 997) % 3 == 0;
	int v = rand() % 65536 - 32768;
	return (short)(loud ? v : v / 64);
}

int main(void)
{
	unsigned int n, j, six = 6, outs = 0, bad = 0;
	int stereo = 1;

	srand(12345);
	for (n = 0; n < 2000000; n++) {
		short l, r;
		if (n % 4099 == 0)
			stereo = rand() & 1;
		l = sample(n);
		r = stereo ? sample(n) : l;

		ref_ring[0][ref_p] = l;
		ref_ring[1][ref_p] = r;
		ref_p = (ref_p + 1) & 31;
		xa_zz_push(l, r);

		if (--six == 0) {
			six = 6;
			for (j = 0; j < 7; j++) {
				uint32_t want = (uint32_t)(unsigned short)ref_tap(ref_ring[0], j, ref_p) |
					((uint32_t)(unsigned short)ref_tap(ref_ring[1], j, ref_p) << 16);
				uint32_t got = xa_zz_out(j);
				outs++;
				if (got != want && bad++ < 5)
					printf("  FAIL  sample %u phase %u: %08x, want %08x\n", n, j, got, want);
			}
		}
	}
	printf("%s: %u outputs, %u different\n", bad ? "FAIL" : "PASS", outs, bad);
	return bad != 0;
}
