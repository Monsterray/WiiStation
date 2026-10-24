/* Host test for Gamecube/ach_badge_conv.h: the badge name check and RGBA8 -> RGB5A3 (64 x 64,
 * GX 4 x 4 tile order). Expected values are worked out by hand. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Gamecube/ach_badge_conv.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define CHECK_PX(out, i, want) do { unsigned got_ = (out)[i]; if (got_ != (unsigned)(want)) { \
	printf("FAIL line %d: out[%d] = %04x, want %04x\n", __LINE__, (int)(i), got_, (unsigned)(want)); failures++; } } while (0)

static void put(uint8_t *rgba, unsigned w, unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned a)
{
	uint8_t *p = rgba + ((size_t)y * w + x) * 4;
	p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static void names(void)
{
	char long33[34], long32[33];
	memset(long33, 'a', 33); long33[33] = 0;
	memset(long32, '7', 32); long32[32] = 0;
	CHECK(ach_badge_name_ok("123456"));
	CHECK(ach_badge_name_ok("Ab_9"));
	CHECK(ach_badge_name_ok("0"));
	CHECK(ach_badge_name_ok(long32));
	CHECK(!ach_badge_name_ok(NULL));
	CHECK(!ach_badge_name_ok(""));
	CHECK(!ach_badge_name_ok(long33));
	CHECK(!ach_badge_name_ok("../x"));
	CHECK(!ach_badge_name_ok("a/b"));
	CHECK(!ach_badge_name_ok("a\b"));
	CHECK(!ach_badge_name_ok("a.png"));
	CHECK(!ach_badge_name_ok("a:b"));
	CHECK(!ach_badge_name_ok("a b"));
	CHECK(!ach_badge_name_ok("12\n34"));
}

static void pixels(void)
{
	static uint8_t src[64 * 64 * 4];
	static uint16_t out[64 * 64];
	memset(src, 0, sizeof src);
	memset(out, 0xEE, sizeof out);
	/* opaque: 0x8000 | r5 << 10 | g5 << 5 | b5 */
	put(src, 64, 0, 0, 255, 0, 0, 255);          /* tile 0, pixel 0 */
	put(src, 64, 5, 1, 8, 16, 24, 255);          /* tile 1, in-tile (1,1): 16 + 5 = 21 */
	put(src, 64, 0, 4, 0, 255, 0, 255);          /* tile row 1: tile 16, offset 256 */
	put(src, 64, 63, 63, 0, 0, 255, 255);        /* tile 255, in-tile (3,3): 4095 */
	put(src, 64, 3, 0, 255, 255, 255, 255);      /* in-tile (3,0): offset 3 */
	put(src, 64, 0, 3, 7, 7, 7, 255);            /* in-tile (0,3): offset 12; 7 >> 3 = 0 */
	/* less than opaque: 0 | a3 << 12 | r4 << 8 | g4 << 4 | b4 */
	put(src, 64, 1, 0, 255, 128, 64, 128);       /* a3 = 4 */
	put(src, 64, 2, 0, 255, 255, 255, 254);      /* a3 = 7 */
	put(src, 64, 4, 0, 16, 32, 48, 0);           /* tile 1 pixel 0 */
	ach_badge_rgba_to_rgb5a3(src, 64, 64, out);
	CHECK_PX(out, 0, 0xFC00);
	CHECK_PX(out, 16 + 4 + 1, 0x8443);
	CHECK_PX(out, 256, 0x83E0);
	CHECK_PX(out, 4095, 0x801F);
	CHECK_PX(out, 3, 0xFFFF);
	CHECK_PX(out, 12, 0x8000);
	CHECK_PX(out, 1, 0x4F84);
	CHECK_PX(out, 2, 0x7FFF);
	CHECK_PX(out, 16, 0x0123);
	CHECK_PX(out, 5, 0x0000);                    /* an untouched transparent black pixel */
	CHECK(sizeof out == ACH_BADGE_BYTES);
}

static void scaling(void)
{
	static uint8_t big[128 * 128 * 4], small[32 * 32 * 4];
	static uint16_t out[64 * 64];
	uint16_t red = ach_badge_px(255, 0, 0, 255), blue = ach_badge_px(0, 0, 255, 255);
	unsigned i, x, y;
	/* 32 x 32 up: every source pixel covers 2 x 2 */
	memset(small, 0, sizeof small);
	for (i = 0; i < 32 * 32; i++) small[i * 4 + 3] = 255;
	put(small, 32, 1, 0, 255, 0, 0, 255);
	put(small, 32, 0, 1, 0, 0, 255, 255);
	ach_badge_rgba_to_rgb5a3(small, 32, 32, out);
#define PX(x, y) out[(((y) >> 2) * 16 + ((x) >> 2)) * 16 + ((y) & 3) * 4 + ((x) & 3)]
	for (y = 0; y < 2; y++)
		for (x = 2; x < 4; x++) CHECK(PX(x, y) == red);
	for (y = 2; y < 4; y++)
		for (x = 0; x < 2; x++) CHECK(PX(x, y) == blue);
	CHECK(PX(0, 0) == 0x8000 && PX(4, 0) == 0x8000 && PX(4, 2) == 0x8000);
	/* 128 x 128 down: out(x, y) is source (2x, 2y); the odd rows and columns are skipped */
	memset(big, 0, sizeof big);
	for (i = 0; i < 128 * 128; i++) big[i * 4 + 3] = 255;
	put(big, 128, 6, 4, 255, 0, 0, 255);         /* out (3, 2) */
	put(big, 128, 7, 5, 0, 0, 255, 255);         /* skipped */
	ach_badge_rgba_to_rgb5a3(big, 128, 128, out);
	CHECK(PX(3, 2) == red);
	CHECK(PX(3, 3) == 0x8000 && PX(4, 2) == 0x8000);
	/* a non-square 64 x 16 source: 4 rows of the output per source row */
	{
		static uint8_t wide[64 * 16 * 4];
		memset(wide, 0, sizeof wide);
		put(wide, 64, 10, 1, 255, 0, 0, 255);
		ach_badge_rgba_to_rgb5a3(wide, 64, 16, out);
		for (y = 0; y < 8; y++) CHECK((PX(10, y) == red) == (y >= 4 && y < 8));
	}
#undef PX
}

int main(void)
{
	names();
	pixels();
	scaling();
	if (failures) { printf("ach_badge_test: %d FAILED\n", failures); return 1; }
	printf("ach_badge_test: ok\n");
	return 0;
}
