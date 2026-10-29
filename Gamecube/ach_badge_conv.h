/* The libogc-free parts of ach_badge.c, so the host test (tests/ach_badge_test.c) can run them:
 * the badge name check and the RGBA8 -> RGB5A3 conversion into GX's 4 x 4 tile order. */
#ifndef ACH_BADGE_CONV_H
#define ACH_BADGE_CONV_H

#include <stddef.h>
#include <stdint.h>

#define ACH_BADGE_DIM 64                                   /* every badge texture is 64 x 64 */
#define ACH_BADGE_BYTES (ACH_BADGE_DIM * ACH_BADGE_DIM * 2)   /* RGB5A3: 2 bytes a pixel */
#define ACH_BADGE_NAME_MAX 32

/* 1: [0-9A-Za-z_], 1..32 characters. The name becomes part of a file name and a URL, so
 * nothing else gets through: no dot, slash, colon. */
static inline int ach_badge_name_ok(const char *s)
{
	unsigned n = 0;
	if (!s) return 0;
	for (; s[n]; n++) {
		char c = s[n];
		if (n >= ACH_BADGE_NAME_MAX) return 0;
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
			return 0;
	}
	return n > 0;
}

/* One pixel as GX_TF_RGB5A3: opaque is 1 rrrrr ggggg bbbbb, anything less than opaque is
 * 0 aaa rrrr gggg bbbb. */
static inline uint16_t ach_badge_px(unsigned r, unsigned g, unsigned b, unsigned a)
{
	if (a == 255) return (uint16_t)(0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
	return (uint16_t)((a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4));
}

/* rgba: w x h pixels, 4 bytes each (R, G, B, A), w and h from 1 to 4096. out: 64 x 64 RGB5A3
 * pixels in GX's order (4 x 4 tiles, left to right and top to bottom, the pixels of a tile
 * the same way: 32 bytes a tile). Another size than 64 x 64 is scaled with nearest-neighbour. */
static inline void ach_badge_rgba_to_rgb5a3(const uint8_t *rgba, unsigned w, unsigned h, uint16_t *out)
{
	unsigned x, y;
	for (y = 0; y < ACH_BADGE_DIM; y++) {
		unsigned sy = (unsigned)((uint32_t)y * h / ACH_BADGE_DIM);
		for (x = 0; x < ACH_BADGE_DIM; x++) {
			unsigned sx = (unsigned)((uint32_t)x * w / ACH_BADGE_DIM);
			const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
			out[((y >> 2) * (ACH_BADGE_DIM / 4) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] =
				ach_badge_px(p[0], p[1], p[2], p[3]);
		}
	}
}

/* A picture of any size (w and h from 1 to 4096) as ow x oh GX_TF_RGB5A3, in GX's order too (ow and
 * oh multiples of 4): each output pixel is the average of the source pixels its box covers, so a
 * picture shown smaller keeps its detail (the unlock pictures: 320 x 240 and up -> 256 x 192). */
static inline void ach_rgba_to_rgb5a3_box(const uint8_t *rgba, unsigned w, unsigned h, uint16_t *out, unsigned ow, unsigned oh)
{
	unsigned x, y;
	for (y = 0; y < oh; y++) {
		unsigned y0 = (unsigned)((uint32_t)y * h / oh), y1 = (unsigned)((uint32_t)(y + 1) * h / oh);
		if (y1 <= y0) y1 = y0 + 1;
		for (x = 0; x < ow; x++) {
			unsigned x0 = (unsigned)((uint32_t)x * w / ow), x1 = (unsigned)((uint32_t)(x + 1) * w / ow), sx, sy;
			uint32_t r = 0, g = 0, b = 0, a = 0, n;
			if (x1 <= x0) x1 = x0 + 1;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++) {
					const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
					r += p[0]; g += p[1]; b += p[2]; a += p[3];
				}
			n = (uint32_t)(y1 - y0) * (x1 - x0);
			out[((y >> 2) * (ow / 4) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] =
				ach_badge_px(r / n, g / n, b / n, a / n);
		}
	}
}

#endif
