/* The real menu font and its Metrics for the host tests (shared by menu_engine_test and menu_pages_test).
 * Needs MenuEngine.h and <stdio.h>/<stdlib.h>/<string.h> included first. */

/*  The font: fonts/En.dat, the way IplFont loads it  */

static unsigned char glyphWidth[256];

static bool loadFont(const char *path)
{
	const size_t REC = 1152 + 4;	/* [u16 code, big-endian][u8][u8 advance][1152-byte tile] */
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long len;
	bool have[256];
	size_t i;

	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
	if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(buf);
		return false;
	}
	fclose(f);

	memset(have, 0, sizeof have);
	for (i = 0; i + REC <= (size_t)len; i += REC) {
		unsigned code = ((unsigned)buf[i] << 8) | buf[i + 1];
		if (code < 256) {
			glyphWidth[code] = buf[i + 3];
			have[code] = true;
		}
	}
	free(buf);
	/* IplFont falls back to the blank character for anything it does not have. */
	for (i = 0; i < 256; i++)
		if (!have[i])
			glyphWidth[i] = have[' '] ? glyphWidth[' '] : 0;
	return have[' '];
}

/* IplFont::getStringWidth: sum of (advance + 1), plus 5, times the scale, in whole pixels. */
static int fontWidth(const char *s, float scale, void *)
{
	int sum = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		sum += glyphWidth[*p] + 1;
	return (int)((sum + 5) * scale);
}

/* IplFont::getStringHeight: CH_FONT_HEIGHT (24) times the scale. */
static int fontHeight(const char *, float scale, void *)
{
	return (int)(24 * scale);
}

static const menu::engine::Metrics kFont = { fontWidth, fontHeight, NULL };

