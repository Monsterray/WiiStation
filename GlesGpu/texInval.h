/* texInval.h -- which cached texels a VRAM write reaches (Docs/GPU_CPU_PLAN.md G5).
 *
 * Pure arithmetic, no GPU state: tests/texinval_test.c runs this same code on the host.
 *
 * VRAM x is in halfwords. A texture page starts every 64 halfwords (x) and 256 lines (y).
 * A page of depth k (0 4-bit, 1 8-bit, 2 15-bit) is 256 texels wide and spans 64 << k
 * halfwords, 1 << (2 - k) texels to a halfword. All ranges are inclusive. */
#ifndef TEX_INVAL_H
#define TEX_INVAL_H

/* The halfwords a page of depth k starting at halfword px covers: [px, texinval_end(px, k)] */
static inline int texinval_end(int px, int k)
{
    return px + (64 << k) - 1;
}

/* The texels [*t0, *t1] (0..255) of that page that halfwords [x0, x1] reach. 0 if none.
 * The last written halfword holds 4 (4-bit) or 2 (8-bit) texels: all of them are reached. */
static inline int texinval_texels(int px, int k, int x0, int x1, int *t0, int *t1)
{
    int end = texinval_end(px, k);
    if (x1 < px || x0 > end)
        return 0;
    if (x0 < px) x0 = px;
    if (x1 > end) x1 = end;
    *t0 = (x0 - px) << (2 - k);
    *t1 = ((x1 - px + 1) << (2 - k)) - 1;
    return 1;
}

/* Does a page of depth k starting at (px, py) share a halfword with [x0, x1] x [y0, y1]? */
static inline int texinval_page_hit(int px, int py, int k, int x0, int y0, int x1, int y1)
{
    return x1 >= px && x0 <= texinval_end(px, k) && y1 >= py && y0 <= py + 255;
}

#endif /* TEX_INVAL_H */
