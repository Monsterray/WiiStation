/* cover_art_test.c - decoding a cover (JPEG or PNG) into the explorer's RGB5A3 slot, on the host.
 *
 *     cc -std=c11 -O2 -o cover_art_test tests/cover_art_test.c Gamecube/cover_art.c
 *     ./cover_art_test [COVERS_DIR]        (the pack's covers folder: C:/wiistation-lib/cover_pack/covers)
 *
 * With a covers folder: the first JPEG and first PNG of box/ must decode, give a picture that is not
 * one flat colour, and a bigger output (96 x 96) must agree with the 48 x 48 one on the average colour;
 * every cover of a sample of the index decodes. Without it only the rejections are checked.
 * Exit status = the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Gamecube/cover_art.h"

static int failures, checks;

static void check(int ok, const char *what, const char *got)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL: %s (got %s)\n", what, got ? got : "(null)");
	}
}

static uint8_t *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b;
	long len;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)len);
	if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
	fclose(f);
	*n = (size_t)len;
	return b;
}

/* RGB5A3 pixel (opaque form) -> 8 bit channels */
static void px(uint16_t v, int *r, int *g, int *b)
{
	if (v & 0x8000) {
		*r = ((v >> 10) & 31) * 255 / 31; *g = ((v >> 5) & 31) * 255 / 31; *b = (v & 31) * 255 / 31;
	} else {
		*r = ((v >> 8) & 15) * 255 / 15; *g = ((v >> 4) & 15) * 255 / 15; *b = (v & 15) * 255 / 15;
	}
}

static void stats(const uint16_t *t, unsigned n, int mean[3], int *distinct)
{
	long sum[3] = { 0, 0, 0 };
	unsigned i;
	uint16_t first = t[0];
	*distinct = 0;
	for (i = 0; i < n; i++) {
		int r, g, b;
		px(t[i], &r, &g, &b);
		sum[0] += r; sum[1] += g; sum[2] += b;
		if (t[i] != first) (*distinct)++;
	}
	for (i = 0; i < 3; i++) mean[i] = (int)(sum[i] / (long)n);
}

static int decode_file(const char *path, unsigned ow, unsigned oh, uint16_t *out)
{
	size_t n;
	uint8_t *b = slurp(path, &n);
	int ok;
	if (!b) return -1;
	ok = cover_art_decode(b, n, out, ow, oh);
	free(b);
	return ok;
}

static void cover_checks(const char *path, const char *tag)
{
	static uint16_t a[48 * 48], b[96 * 96];
	int ma[3], mb[3], da, db, k, close = 1;
	check(decode_file(path, 48, 48, a) == 1, tag, path);
	check(decode_file(path, 96, 96, b) == 1, tag, path);
	stats(a, 48 * 48, ma, &da);
	stats(b, 96 * 96, mb, &db);
	check(da > 100, "the 48 x 48 picture is not one flat colour", path);
	for (k = 0; k < 3; k++) if (abs(ma[k] - mb[k]) > 6) close = 0;
	check(close, "48 and 96 agree on the average colour", path);
	printf("  %s %s mean %d %d %d, %d differing px\n", tag, path, ma[0], ma[1], ma[2], da);
}

int main(int argc, char **argv)
{
	static uint16_t out[48 * 48];
	uint8_t junk[64], png_head[16] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
	memset(junk, 0x5a, sizeof junk);

	out[0] = 0x1234;
	check(!cover_art_decode(NULL, 100, out, 48, 48), "no file", "");
	check(!cover_art_decode(junk, sizeof junk, out, 48, 48), "garbage", "");
	check(!cover_art_decode(png_head, sizeof png_head, out, 48, 48), "a PNG header and nothing else", "");
	check(!cover_art_decode(junk, sizeof junk, out, 47, 48), "width not a multiple of 4", "");
	check(!cover_art_decode(junk, sizeof junk, out, 48, 0), "no height", "");
	check(!cover_art_decode(junk, sizeof junk, out, 1024, 48), "output too big", "");
	check(out[0] == 0x1234, "a failed decode leaves the output alone", "");

	if (argc > 1) {
		char idx[512], line[512], path[600], jpg[600] = "", png[600] = "";
		FILE *f;
		int sampled = 0, ok = 0, rows = 0;
		snprintf(idx, sizeof idx, "%s/index.txt", argv[1]);
		f = fopen(idx, "rb");
		check(f != NULL, "the pack's index.txt is there", idx);
		while (f && fgets(line, sizeof line, f)) {
			char *tab = strchr(line, '\t'), *end;
			size_t l;
			if (!tab) continue;
			end = strchr(tab + 1, '\t');
			if (!end) continue;
			*end = 0;
			l = strlen(tab + 1);
			if (!strcmp(tab + 1, "-") || l < 5) continue;
			rows++;
			snprintf(path, sizeof path, "%s/%s", argv[1], tab + 1);
			if (!strcmp(tab + 1 + l - 4, ".jpg")) {
				if (!jpg[0]) strcpy(jpg, path);
				if ((++sampled % 40) == 0) {	/* about 1 in 40 of the covers */
					int r = decode_file(path, 48, 48, out);
					ok += r == 1;
					if (r != 1) check(0, "a sampled JPEG decodes", path);
				}
			} else if (!strcmp(tab + 1 + l - 4, ".png") && !png[0]) {
				strcpy(png, path);
			}
		}
		if (f) fclose(f);
		(void)rows;
		if (jpg[0]) cover_checks(jpg, "jpg");
		if (png[0]) cover_checks(png, "png");
		check(jpg[0] && png[0], "the pack has both a JPEG and a PNG cover", "");
		printf("  sampled %d JPEG covers, %d decoded\n", sampled / 40, ok);
		check(sampled / 40 > 100 && ok == sampled / 40, "every sampled cover decoded", "");
	}

	printf("cover_art: %d checks, %d failed\n", checks, failures);
	return failures;
}
