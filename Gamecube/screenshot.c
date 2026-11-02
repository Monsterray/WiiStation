/**
 * WiiStation - screenshot.c
 *
 * Saves the picture the PlayStation shows as a PNG file. The file goes to
 * sd:/wiistation/screenshots/<CdromId>_<n>.png, where n is the lowest free number.
 *
 * Two ways to take one:
 *  - The Screenshot button on the Current ROM page calls screenshot_save() from the menu.
 *    The emulator is stopped there, and VRAM still holds the last picture drawn.
 *  - A button combination in a game only sets a request (screenshot_poll). The next vblank
 *    serves it (screenshot_vblank), when VRAM holds the picture on screen. With OpenGX it
 *    waits (up to SHOT_WAIT_VBLANKS) while the renderer cannot yet copy all of the picture
 *    into VRAM: without that the file is the game's fill (one colour).
 *
 * Each save pauses the emulator while it compresses and writes the file. The pixels and the
 * compressed copy share one temporary buffer in MEM2.
 **/

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <zlib.h>
#include "../gpulib/gpu.h"
#include "../mem2_manager.h"
#include "wiiSXconfig.h"   /* OPEN_GX */
#include "screenshot.h"
#include "perf_prof.h"   /* perf_log_open: perf.log, or the play build's play.log */

#define SHOT_DIR "sd:/wiistation/screenshots"

/* The display width for each GPUSTAT horizontal resolution (gpulib/gpulib.c update_width) */
static const unsigned short SHOT_HRES[8] = { 256, 368, 320, 368, 512, 368, 640, 368 };

extern unsigned short *psxVuw;   /* the VRAM, 1024 x 512 16-bit pixels */
extern char CdromId[10];
extern unsigned int frame_counter;   /* psxcounters.c: +1 per vblank -- the screenshot's place in a recording */
extern char gpuPlugin;           /* GamecubeMain.cpp: the renderer the settings chose */
int gpu_screenshot_sync(int x, int y, int w, int h);   /* GlesGpu/gpuPlugin.c */
void gpu_screenshot_stats(int out[4]);   /* GlesGpu/gpuPlugin.c: what that sync found */
void PSX_display_rect(int *x, int *y, int *w, int *h, int *rgb24);   /* gpulib/oldGpu.c */

/* Set by a button combination, cleared when the next vblank saves the picture. */
static volatile int shot_due;
static int shot_waited, shot_waited_last;   /* vblanks a request has waited for the picture to be complete */
static int shot_peeked, shot_peek[4];   /* the sync it made at the first of them (gpu_screenshot_stats) */
#define SHOT_WAIT_VBLANKS 6

/* A 5-bit colour channel as 8 bits. The top bits fill the bottom, so 31 is 255. */
static unsigned char c5(unsigned v)
{
	v &= 31;
	return (unsigned char)((v << 3) | (v >> 2));
}

/* The share, in percent, of the picture that one colour fills (a sample of the pixels): a
 * picture that is nearly all one colour is what the log line must say, since a screenshot the
 * renderer could not sync is the game's fill and nothing else. */
static unsigned shot_flat_share(const unsigned char *raw, unsigned w, unsigned h, unsigned long *color)
{
	unsigned long c[8];
	unsigned n[8], x, y, k, total = 0, best = 0;

	memset(n, 0, sizeof n);
	memset(c, 0, sizeof c);
	for (y = 0; y < h; y += 3)
		for (x = 0; x < w; x += 5) {
			const unsigned char *p = raw + (unsigned long)y * (1 + 3 * w) + 1 + 3 * x;
			unsigned long v = ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];

			total++;
			for (k = 0; k < 8; k++)
				if (n[k] && c[k] == v) { n[k]++; break; }
			if (k == 8)
				for (k = 0; k < 8; k++)
					if (!n[k]) { c[k] = v; n[k] = 1; break; }
		}
	for (k = 0; k < 8; k++)
		if (n[k] > n[best]) best = k;
	*color = c[best];
	return total ? 100 * n[best] / total : 0;
}

/* Writes the displayed picture as PNG scanlines: a filter byte, then RGB per pixel.
 * The area wraps at the edges of VRAM. 15-bit colour is read from little-endian words, and
 * 24-bit colour is read as raw bytes from the VRAM address, as drawGX.c displays them. */
static void shot_rows(unsigned char *raw, unsigned w, unsigned h, unsigned sx, unsigned sy,
                      int rgb24)
{
	const unsigned char *bytes = (const unsigned char *)psxVuw;
	unsigned x, y;

	for (y = 0; y < h; y++) {
		unsigned row = (sy + y) & 511;
		unsigned char *d = raw + (unsigned long)y * (1 + 3 * w);

		*d++ = 0;   /* filter type 0: none */
		if (rgb24) {
			const unsigned char *s = bytes + row * 2048;

			for (x = 0; x < w; x++, d += 3) {
				unsigned b = sx * 2 + x * 3;

				d[0] = s[b & 2047];
				d[1] = s[(b + 1) & 2047];
				d[2] = s[(b + 2) & 2047];
			}
		} else {
			const unsigned char *s = bytes + row * 2048;

			for (x = 0; x < w; x++, d += 3) {
				/* VRAM holds each 16-bit word little-endian, as the PSX does */
				unsigned b = ((sx + x) & 1023) * 2;
				unsigned v = s[b] | (s[b + 1] << 8);

				d[0] = c5(v);
				d[1] = c5(v >> 5);
				d[2] = c5(v >> 10);
			}
		}
	}
}

static void be32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

/* One PNG chunk: its length, its type, its data, then a CRC over the type and data. */
static int put_chunk(FILE *f, const char *type, const unsigned char *data, unsigned long len)
{
	unsigned char head[8], tail[4];
	uLong crc;

	be32(head, len);
	memcpy(head + 4, type, 4);
	crc = crc32(0L, head + 4, 4);
	if (len)
		crc = crc32(crc, data, (uInt)len);
	be32(tail, crc);
	return fwrite(head, 1, 8, f) == 8 &&
	       (len == 0 || fwrite(data, 1, len, f) == len) &&
	       fwrite(tail, 1, 4, f) == 4;
}

/* Where the picture is in VRAM, and its size. The new soft renderer keeps this in gpulib's
 * gpu struct. The old soft renderer and OpenGX keep it in PSXDisplay (oldGpu.c). */
static void shot_geometry(unsigned *sx, unsigned *sy, unsigned *w, unsigned *h, int *rgb24)
{
	if (gpuPlugin == NEW_SOFT) {
		*sx = (unsigned)gpu.screen.src_x;
		*sy = (unsigned)gpu.screen.src_y;
		*w = SHOT_HRES[(gpu.status >> 16) & 7];
		*h = (unsigned)gpu.screen.vres;
		*rgb24 = (gpu.status & PSX_GPU_STATUS_RGB24) != 0;
	} else {
		int x, y, mw, mh;

		PSX_display_rect(&x, &y, &mw, &mh, rgb24);
		*sx = (unsigned)x;
		*sy = (unsigned)y;
		*w = (unsigned)mw;
		*h = (unsigned)mh;
	}
	if (*w < 1 || *w > 640)
		*w = 320;
	if (*h < 1 || *h > 512)
		*h = 240;
	*sx &= 1023;
	*sy &= 511;
}

/* The picture as PNG scanlines for the unlock pictures (ach_shot.c): a MEM2 buffer the caller
 * frees with _mem2_free, rows of a filter byte and RGB per pixel, *w x *h. NULL: no picture or
 * no memory. Called from the emulator thread at a vblank, as screenshot_vblank. */
unsigned char *screenshot_grab(unsigned *w, unsigned *h)
{
	unsigned sx, sy;
	int rgb24;
	unsigned char *raw;

	if (!psxVuw)
		return NULL;
	shot_geometry(&sx, &sy, w, h, &rgb24);
	raw = _mem2_malloc((unsigned long)*h * (1 + 3 * *w));
	if (!raw)
		return NULL;
	if (gpuPlugin == OPEN_GX)   /* OpenGX keeps its newest pixels in the EFB until a read */
		gpu_screenshot_sync(sx, sy, rgb24 ? (3 * *w + 1) / 2 + 1 : *w, *h);
	shot_rows(raw, *w, *h, sx, sy, rgb24);
	return raw;
}

/* A PNG file made in one buffer around a deflate stream: buf holds the stream (idat_len bytes)
 * at buf + SHOT_PNG_HEAD; this writes the signature, IHDR and IDAT's header before it and the
 * IDAT CRC and IEND after it. Returns the file's length. */
unsigned long screenshot_png_seal(unsigned char *buf, unsigned long idat_len, unsigned w, unsigned h)
{
	static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char *p = buf, ihdr[13];
	uLong crc;

	memcpy(p, sig, 8);
	be32(ihdr, w);
	be32(ihdr + 4, h);
	ihdr[8] = 8;	/* bit depth */
	ihdr[9] = 2;	/* colour type: RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	be32(p + 8, 13);
	memcpy(p + 12, "IHDR", 4);
	memcpy(p + 16, ihdr, 13);
	crc = crc32(crc32(0L, p + 12, 4), ihdr, 13);
	be32(p + 29, crc);
	be32(p + 33, idat_len);
	memcpy(p + 37, "IDAT", 4);
	p = buf + SHOT_PNG_HEAD + idat_len;
	crc = crc32(crc32(0L, buf + 37, 4), buf + SHOT_PNG_HEAD, (uInt)idat_len);
	be32(p, crc);
	be32(p + 4, 0);
	memcpy(p + 8, "IEND", 4);
	be32(p + 12, crc32(0L, p + 8, 4));
	return SHOT_PNG_HEAD + idat_len + 16;
}

/* Saves the picture now. name (if not NULL) gets the file's name, such as "SCUS94228_2.png".
 * Returns 0 when the file is written, or -1 when it is not. */
int screenshot_save(char *name, unsigned size)
{
	static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned w, h, n, sx, sy;
	int rgb24;
	unsigned long rawlen, outlen;
	unsigned char *buf, *raw, *out;
	char file[40], path[96];
	const char *id;
	FILE *f;
	int ok = 0, sync[4] = { 0, 0, 0, 0 };
	unsigned flat = 0, waited = shot_waited_last;
	unsigned long flatcol = 0;
	u64 t0 = gettime();

	if (!psxVuw)
		return -1;
	shot_geometry(&sx, &sy, &w, &h, &rgb24);
	rawlen = (unsigned long)h * (1 + 3 * w);
	outlen = compressBound(rawlen);
	buf = _mem2_malloc(rawlen + outlen);
	if (!buf)
		return -1;
	raw = buf;
	out = buf + rawlen;
	if (shot_peeked) {   /* screenshot_vblank synced already: its numbers are the real ones */
		memcpy(sync, shot_peek, sizeof sync);
	} else if (gpuPlugin == OPEN_GX) {   /* OpenGX keeps its newest pixels in the EFB until a read */
		gpu_screenshot_sync(sx, sy, rgb24 ? (3 * w + 1) / 2 + 1 : w, h);
		gpu_screenshot_stats(sync);
	}
	shot_peeked = 0;
	shot_waited_last = 0;
	shot_rows(raw, w, h, sx, sy, rgb24);
	flat = shot_flat_share(raw, w, h, &flatcol);
	if (compress2(out, &outlen, raw, rawlen, Z_BEST_SPEED) != Z_OK)
		goto done;

	/* The lowest free number for this game, so no file is overwritten */
	id = CdromId[0] ? CdromId : "noid";
	mkdir("sd:/wiistation", 0777);
	mkdir(SHOT_DIR, 0777);
	for (n = 1; n < 10000; n++) {
		FILE *probe;

		sprintf(file, "%s_%u.png", id, n);
		sprintf(path, SHOT_DIR "/%s", file);
		probe = fopen(path, "rb");
		if (!probe)
			break;
		fclose(probe);
	}
	f = fopen(path, "wb");
	if (!f)
		goto done;
	be32(ihdr, w);
	be32(ihdr + 4, h);
	ihdr[8] = 8;	/* bit depth */
	ihdr[9] = 2;	/* colour type: RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	ok = fwrite(sig, 1, 8, f) == 8 &&
	     put_chunk(f, "IHDR", ihdr, 13) &&
	     put_chunk(f, "IDAT", out, outlen) &&
	     put_chunk(f, "IEND", NULL, 0);
	ok = (fclose(f) == 0) && ok;
	if (!ok)
		remove(path);
	if (ok && name && size)
		snprintf(name, size, "%s", file);

done:
	_mem2_free(buf);
	{
		/* One line in perf.log: the file and what the save cost in emulator time */
		FILE *lf = perf_log_open();

		if (lf) {
			/* vbl=: a screenshot taken while a movie is recorded marks a moment to look at
			 * (scripts/marks.py: the frame, a box drawn on it, a trace of that box) */
			fprintf(lf, "screenshot: %s %s %ux%u at %u,%u %llu ms vbl=%u\n", ok ? "saved" : "FAILED",
			        ok ? file : "-", w, h, sx, sy,
			        (unsigned long long)ticks_to_millisecs(diff_ticks(t0, gettime())), frame_counter);
			/* what the picture came from (OpenGX: tiles the renderer had drawn and not yet copied
			 * into VRAM, tiles it could not copy, whether it keeps snapshots) and how flat it is: a
			 * screenshot that is one colour with stale=0 or missed>0 was not synced */
			fprintf(lf, "screenshot: sync tiles=%d stale=%d missed=%d hist=%d waited=%u flat=%u%% color=%06lx\n", sync[0], sync[1], sync[2], sync[3], waited, flat, flatcol);
			perf_log_close(lf);
		}
	}
	return ok ? 0 : -1;
}

/* A button combination's state, from a controller driver each time it reads the pad. The
 * request is made when the combination goes down, not for as long as it is held. */
extern int pad_probing;   /* PlugPAD.c pad_menu_combo_held */

void screenshot_poll(int control, int held)
{
	static char was_held[4];
	int k = control & 3;

	if (pad_probing)   /* the menu asking about its own combination, not a game reading the pad */
		return;

	if (held && !was_held[k])
		shot_due = 1;
	was_held[k] = held != 0;
}

/* Called at each vblank (psxcounters.c). Does nothing unless a request is waiting. */
void screenshot_vblank(void)
{
	if (!shot_due)
		return;
	/* OpenGX: if the renderer could not copy some of the picture into VRAM (no snapshot of
	 * what it drew there), the file would show the game's fill where the picture is. The game
	 * draws again within a few frames and the renderer then keeps what it draws: wait for that,
	 * a few vblanks at most. */
	if (gpuPlugin == OPEN_GX && psxVuw && shot_waited < SHOT_WAIT_VBLANKS) {
		unsigned sx, sy, w, h;
		int rgb24, st[4];

		shot_geometry(&sx, &sy, &w, &h, &rgb24);
		gpu_screenshot_sync(sx, sy, rgb24 ? (3 * w + 1) / 2 + 1 : w, h);
		gpu_screenshot_stats(st);
		if (!shot_waited)
			memcpy(shot_peek, st, sizeof shot_peek);   /* the first look is what the log line shows */
		shot_peeked = 1;
		if (st[2] > 0) {
			shot_waited++;
			return;
		}
	}
	shot_waited_last = shot_waited;
	shot_waited = 0;
	shot_due = 0;
	screenshot_save(NULL, 0);
}
