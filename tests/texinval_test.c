/* texinval_test.c - GlesGpu/texInval.h against a brute-force "shares a halfword" check.
 *
 *     cc -O2 -o texinval_test tests/texinval_test.c && ./texinval_test
 *
 * For every depth, several pages, every write range over a window around the page and a
 * grid of cached-entry texel ranges: an entry is dropped exactly when one of its texels
 * lives in a written halfword. Also counts how often the old formula (the last texel
 * taken as the first texel of the last halfword) keeps an entry it must drop. */
#include <assert.h>
#include <stdio.h>
#include "../GlesGpu/texInval.h"

/* the texel range [e0, e1] of the page at px (depth k) lives in a halfword of [x0, x1] */
static int brute(int px, int k, int x0, int x1, int e0, int e1)
{
    int t;
    for (t = e0; t <= e1; t++) {
        int hw = px + (t >> (2 - k));
        if (hw >= x0 && hw <= x1)
            return 1;
    }
    return 0;
}

static int old_rule(int px, int k, int x0, int x1, int e0, int e1)
{
    int end = px + (64 << k) - 1, t0, t1;
    if (x1 < px || x0 > end) return 0;
    if (x0 < px) x0 = px;
    if (x1 > end) x1 = end;
    t0 = (x0 - px) << (2 - k);
    t1 = (x1 - px) << (2 - k);
    return e1 >= t0 && e0 <= t1;
}

int main(void)
{
    static const int pages[] = { 0, 64, 320, 640, 768, 960 };
    unsigned long cases = 0, old_missed = 0;
    int k, p, x0, x1, e0, e1;

    for (k = 0; k < 3; k++)
        for (p = 0; p < 6; p++) {
            int px = pages[p];
            for (x0 = px - 70; x0 <= px + (64 << k) + 4; x0++)
                for (x1 = x0; x1 <= x0 + 70 && x1 < 1024; x1++)
                    for (e0 = 0; e0 < 256; e0 += 5)
                        for (e1 = e0; e1 < 256; e1 += 7) {
                            int t0, t1, got, want = brute(px, k, x0, x1, e0, e1);
                            got = texinval_texels(px, k, x0, x1, &t0, &t1) && e1 >= t0 && e0 <= t1;
                            assert(got == want);
                            assert(texinval_page_hit(px, 0, k, x0, 0, x1, 0) ==
                                   (x1 >= px && x0 <= px + (64 << k) - 1));
                            if (want && !old_rule(px, k, x0, x1, e0, e1))
                                old_missed++;
                            cases++;
                        }
        }
    printf("texinval_test: %lu cases match; the old rule kept %lu entries it had to drop\n",
           cases, old_missed);
    return old_missed ? 0 : 1;   /* the old rule's bug must show, or this test tests nothing */
}
