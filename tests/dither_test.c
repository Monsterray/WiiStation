/* dither_test.c - SoftGPU/dither5.h against the soft GPU's original dither rule.
 *
 *     cc -O2 -o dither_test tests/dither_test.c && ./dither_test
 */
#include <stdio.h>
#include "../SoftGPU/dither5.h"

int main(void)
{
	unsigned c, v, bad = 0;

	for (c = 0; c < 8; c++)
		for (v = 0; v < 256; v++) {
			/* the rule Dither16 applied per pixel before the table */
			unsigned low = v & 7, out = v >> 3;
			if (out < 0x1F && low > c) out++;
			/* counted, not assert(): an NDEBUG build must still fail loudly */
			if (dith5[c][v] != out) {
				if (bad < 8)
					printf("dither_test: MISMATCH c=%u v=%u table=%u rule=%u\n",
					       c, v, dith5[c][v], out);
				bad++;
			}
		}
	if (bad)
		printf("dither_test: %u/2048 entries mismatch\n", bad);
	else
		printf("dither_test: all 2048 entries match\n");
	return bad != 0;
}
