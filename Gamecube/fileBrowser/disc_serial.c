/* See disc_serial.h. */
#include "disc_serial.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) && !defined(__MINGW32__)
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#if defined(HW_RVL)
#ifndef DS_ZLIB
#define DS_ZLIB
#endif
#include <stdbool.h>
#include <stdint.h>
#include "../../mem2_manager.h"
#define DS_ALLOC(n) _mem2_malloc((uint32_t)(n))
#define DS_FREE(p)  _mem2_free(p)
#else
#define DS_ALLOC(n) malloc(n)
#define DS_FREE(p)  free(p)
#endif

static unsigned le32(const unsigned char *b)
{
	return (unsigned)b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24;
}

/* ---- from the names ---- */

/* Four letters, an optional '-', '_' or ' ', five digits ("SLUS-00892", "scus94423"), standing alone
 * (no letter or digit just before or after). A serial starts with S or P. */
static int serial_in(const char *s, char out[DS_SERIAL_MAX])
{
	const char *p;
	for (p = s; *p; p++)
	{
		const char *q = p;
		int i;
		if ((p != s && isalnum((unsigned char)p[-1])) || (toupper((unsigned char)*p) != 'S' && toupper((unsigned char)*p) != 'P'))
			continue;
		for (i = 0; i < 4; i++)
			if (!isalpha((unsigned char)q[i]))
				break;
		if (i < 4)
			continue;
		q += 4;
		if (*q == '-' || *q == '_' || *q == ' ')
			q++;
		for (i = 0; i < 5; i++)
			if (!isdigit((unsigned char)q[i]))
				break;
		if (i < 5 || isalnum((unsigned char)q[5]))
			continue;
		for (i = 0; i < 4; i++)
			out[i] = (char)toupper((unsigned char)p[i]);
		memcpy(out + 4, q, 5);
		out[9] = 0;
		return 1;
	}
	return 0;
}

int ds_name_serial(const char *path, char out[DS_SERIAL_MAX])
{
	const char *base = path, *p, *dirStart = path, *dirEnd = path;
	char tmp[256];
	size_t n;
	if (!path || !out)
		return 0;
	for (p = path; *p; p++)
		if (*p == '/' || *p == '\\')
		{
			dirStart = base;		/* the component before the last separator starts where the last file name did */
			dirEnd = p;
			base = p + 1;
		}
	if (serial_in(base, out))
		return 1;
	/* the folder holding the file: between the last two separators */
	n = (size_t)(dirEnd - dirStart);
	if (dirEnd > dirStart && n > 0 && n < sizeof tmp)
	{
		memcpy(tmp, dirStart, n);
		tmp[n] = 0;
		if (serial_in(tmp, out))
			return 1;
	}
	return 0;
}

/* ---- the BOOT line ---- */

int ds_parse_boot(const char *text, unsigned n, char out[DS_SERIAL_MAX])
{
	unsigned i, k = 0;
	char name[64];
	for (i = 0; i + 5 < n; i++)
		if (!strncasecmp(text + i, "cdrom", 5))
			break;
	if (i + 5 >= n)
		return 0;
	while (i < n && text[i] != ':')
		i++;
	/* the file name: after the last '\' or '/' of the path, up to ';' or the end of the line */
	{
		unsigned s = ++i, e;
		for (e = s; e < n && text[e] && text[e] != ';' && text[e] != '\r' && text[e] != '\n' && text[e] != ' ' && text[e] != '\t'; e++)
			if (text[e] == '\\' || text[e] == '/')
				s = e + 1;
		for (i = s; i < e && k < sizeof name - 1; i++)
			if (isalnum((unsigned char)text[i]))
				name[k++] = (char)toupper((unsigned char)text[i]);
	}
	name[k] = 0;
	/* four letters then five digits: the PlayStation's own naming (SLUS_008.92 -> SLUS00892) */
	if (k < 9)
		return 0;
	for (i = 0; i < 4; i++)
		if (!isalpha((unsigned char)name[i]))
			return 0;
	for (i = 4; i < 9; i++)
		if (!isdigit((unsigned char)name[i]))
			return 0;
	memcpy(out, name, 9);
	out[9] = 0;
	return 1;
}

/* ---- the volume ---- */

static const unsigned OFFS[] = { 0, 16, 24, 8 };		/* where the 2048 data bytes sit in a frame */

int ds_serial_from_frames(ds_frame_fn rd, void *ctx, char out[DS_SERIAL_MAX])
{
	unsigned char frame[DS_FRAME], pvd[2048], dir[2048];
	unsigned off = 0, i, rootLba, rootSize, pos, sector;
	int n;
	if (!rd || !out)
		return -1;
	n = rd(ctx, 16, frame);
	if (n < 2048)
		return -1;
	for (i = 0; i < sizeof OFFS / sizeof *OFFS; i++)
	{
		off = OFFS[i];
		if ((int)(off + 2048) <= n && frame[off] == 1 && !memcmp(frame + off + 1, "CD001", 5))
			break;
	}
	if (i == sizeof OFFS / sizeof *OFFS)
		return -1;
	memcpy(pvd, frame + off, 2048);
	rootLba = le32(pvd + 156 + 2);
	rootSize = le32(pvd + 156 + 10);
	if (rootSize > 4 * 2048)
		rootSize = 4 * 2048;
	for (sector = 0; sector * 2048 < rootSize; sector++)
	{
		if (rd(ctx, rootLba + sector, frame) < (int)(off + 2048))
			return 0;
		memcpy(dir, frame + off, 2048);
		for (pos = 0; pos < 2048;)
		{
			const unsigned len = dir[pos];
			if (len == 0)
				break;			/* records do not cross a sector: on to the next one */
			if (pos + 33 <= 2048 && dir[pos + 32] >= 10 && !strncasecmp((const char *)dir + pos + 33, "SYSTEM.CNF", 10))
			{
				char text[2048];
				const unsigned lba = le32(dir + pos + 2), size = le32(dir + pos + 10);
				if (rd(ctx, lba, frame) < (int)(off + 2048))
					return 0;
				memcpy(text, frame + off, 2048);
				return ds_parse_boot(text, size < 2048 ? size : 2048, out);
			}
			pos += len;
		}
	}
	return 0;
}

/* ---- images in a plain file ---- */

struct Raw { FILE *f; unsigned stride; };

static int raw_frame(void *ctx, unsigned long lba, unsigned char *frame)
{
	struct Raw *r = (struct Raw *)ctx;
	const unsigned want = r->stride < 2352 ? r->stride : 2352;
	if (fseek(r->f, (long)(lba * r->stride), SEEK_SET))
		return 0;
	return (int)fread(frame, 1, want, r->f);
}

/* 1 / 0 / -1 as ds_serial_from_frames, trying the sector sizes of the formats in turn */
static int raw_serial(FILE *f, char out[DS_SERIAL_MAX])
{
	static const unsigned STRIDES[] = { 2352, 2048, 2448, 2336 };
	struct Raw r;
	unsigned i;
	r.f = f;
	for (i = 0; i < sizeof STRIDES / sizeof *STRIDES; i++)
	{
		int rc;
		r.stride = STRIDES[i];
		rc = ds_serial_from_frames(raw_frame, &r, out);
		if (rc >= 0)
			return rc;
	}
	return -1;
}

static const char *base_of(const char *path)
{
	const char *b = path, *p;
	for (p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			b = p + 1;
	return b;
}

static const char *ext_of(const char *path)
{
	const char *b = base_of(path), *dot = 0;
	for (; *b; b++)
		if (*b == '.')
			dot = b;
	return dot ? dot + 1 : "";
}

/* dir of path + name, or name alone when it is a whole path ("sd:/..." or starts with a slash) */
static int sibling(char *out, unsigned size, const char *path, const char *name)
{
	const unsigned dirLen = (unsigned)(base_of(path) - path);
	if (!strchr(name, ':') && name[0] != '/' && name[0] != '\\')
	{
		if (dirLen + strlen(name) + 1 > size)
			return 0;
		memcpy(out, path, dirLen);
		strcpy(out + dirLen, name);
	}
	else if (strlen(name) + 1 > size)
		return 0;
	else
		strcpy(out, name);
	return 1;
}

/* The first file a cue or toc names: FILE "x" / DATAFILE "x" */
static int sheet_file(const char *path, char *out, unsigned size)
{
	char text[2048];
	size_t n;
	char *p, *q;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[n] = 0;
	for (p = text; *p;)		/* the first line that starts with FILE or DATAFILE */
	{
		char *line = p;
		while (*line == ' ' || *line == '\t')
			line++;
		if ((!strncasecmp(line, "FILE", 4) && (line[4] == ' ' || line[4] == '\t')) ||
		    (!strncasecmp(line, "DATAFILE", 8) && (line[8] == ' ' || line[8] == '\t')))
		{
			p = line;
			break;
		}
		p += strcspn(p, "\n");
		if (*p)
			p++;
	}
	if (!*p || !(p = strchr(p, '"')) || !(q = strchr(p + 1, '"')))
		return 0;
	*q = 0;
	return sibling(out, size, path, p + 1);
}

static FILE *open_with_ext(const char *path, const char *e1, const char *e2)
{
	char p[256];
	const char *dot = strrchr(path, '.');
	unsigned head = dot ? (unsigned)(dot - path) : (unsigned)strlen(path);
	FILE *f;
	if (head + 5 > sizeof p)
		return NULL;
	memcpy(p, path, head);
	strcpy(p + head, e1);
	if ((f = fopen(p, "rb")) != NULL)
		return f;
	strcpy(p + head, e2);
	return fopen(p, "rb");
}

/* ---- PBP ---- */

#ifdef DS_ZLIB
#include <zlib.h>
struct Pbp { FILE *f; unsigned long img; long block; };
static unsigned char pbpBuf[16 * 2352], pbpIn[16 * 2352 + 128];

static int pbp_frame(void *ctx, unsigned long lba, unsigned char *frame)
{
	struct Pbp *p = (struct Pbp *)ctx;
	const long block = (long)(lba >> 4);
	if (block != p->block)
	{
		unsigned char e[8];
		unsigned long off, size;
		if (fseek(p->f, (long)(p->img + 0x4000 + 32u * (unsigned)block), SEEK_SET) || fread(e, 1, 8, p->f) != 8)
			return 0;
		off = le32(e);
		size = le32(e + 4);
		if (size == 0 || size > sizeof pbpIn || fseek(p->f, (long)(p->img + 0x100000 + off), SEEK_SET) ||
		    fread(pbpIn, 1, size, p->f) != size)
			return 0;
		if (size == sizeof pbpBuf)
			memcpy(pbpBuf, pbpIn, size);			/* a block that did not shrink is stored as it is */
		else
		{
			z_stream z;
			int rc;
			memset(&z, 0, sizeof z);
			if (inflateInit2(&z, -15) != Z_OK)
				return 0;
			z.next_in = pbpIn;
			z.avail_in = (uInt)size;
			z.next_out = pbpBuf;
			z.avail_out = sizeof pbpBuf;
			rc = inflate(&z, Z_NO_FLUSH);
			inflateEnd(&z);
			if (rc != Z_STREAM_END && rc != Z_OK)
				return 0;
		}
		p->block = block;
	}
	memcpy(frame, pbpBuf + (lba & 15) * 2352, 2352);
	return 2352;
}
#endif

/* DISC_ID in PARAM.SFO: "SLUS00506" or "SCUS-94227" */
static int pbp_disc_id(FILE *f, const unsigned char *hdr, char out[DS_SERIAL_MAX])
{
	unsigned char sfo[1024];
	const unsigned at = le32(hdr + 8), icon = le32(hdr + 12);
	unsigned keys, data, count, k, len;
	if (icon <= at || fseek(f, (long)at, SEEK_SET))
		return 0;
	len = icon - at < sizeof sfo ? icon - at : (unsigned)sizeof sfo;
	if (fread(sfo, 1, len, f) != len || len < 20 || memcmp(sfo, "\0PSF", 4))
		return 0;
	keys = le32(sfo + 8);
	data = le32(sfo + 12);
	count = le32(sfo + 16);
	for (k = 0; k < count && 20 + 16 * (k + 1) <= len; k++)
	{
		const unsigned char *e = sfo + 20 + 16 * k;
		const unsigned koff = e[0] | e[1] << 8, doff = le32(e + 12);
		if (keys + koff + 8 <= len && !memcmp(sfo + keys + koff, "DISC_ID", 8) && data + doff + 9 <= len)
		{
			char v[24];
			unsigned i, m = 0;
			for (i = 0; i < 20 && data + doff + i < len && sfo[data + doff + i]; i++)
				if (isalnum(sfo[data + doff + i]))
					v[m++] = (char)toupper(sfo[data + doff + i]);
			v[m] = 0;
			if (m < 9 || !isalpha((unsigned char)v[0]) || !isdigit((unsigned char)v[4]))
				return 0;
			memcpy(out, v, 9);
			out[9] = 0;
			return 1;
		}
	}
	return 0;
}

static int pbp_serial(const char *path, char out[DS_SERIAL_MAX])
{
	unsigned char hdr[0x28];
	int rc = 0;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "\0PBP", 4))
	{
		rc = pbp_disc_id(f, hdr, out);
#ifdef DS_ZLIB
		if (!rc)
		{
			struct Pbp p;
			unsigned char sig[16];
			p.f = f;
			p.img = le32(hdr + 0x24);
			p.block = -1;
			if (!fseek(f, (long)p.img, SEEK_SET) && fread(sig, 1, 16, f) == 16)
			{
				if (!memcmp(sig, "PSTITLEIMG", 10))		/* several discs: the first one's image */
				{
					unsigned char tab[4];
					if (!fseek(f, (long)(p.img + 0x200), SEEK_SET) && fread(tab, 1, 4, f) == 4 && le32(tab))
						p.img += le32(tab);
					fseek(f, (long)p.img, SEEK_SET);
					if (fread(sig, 1, 16, f) != 16)
						sig[0] = 0;
				}
				if (!memcmp(sig, "PSISOIMG00", 10))
					rc = ds_serial_from_frames(pbp_frame, &p, out) == 1;
			}
		}
#endif
	}
	fclose(f);
	return rc;
}

/* ---- CHD (the Wii build links libchdr) ---- */

#ifdef USE_LIBCHDR
#include "../../deps/libchdr/include/libchdr/chd.h"
struct Chd { chd_file *c; unsigned fps, base; unsigned char *hunk; long cur; };

static int chd_frame(void *ctx, unsigned long lba, unsigned char *frame)
{
	struct Chd *h = (struct Chd *)ctx;
	const unsigned long fr = h->base + lba;
	const long hunk = (long)(fr / h->fps);
	if (hunk != h->cur)
	{
		if (chd_read(h->c, (uint32_t)hunk, h->hunk) != CHDERR_NONE)
			return 0;
		h->cur = hunk;
	}
	memcpy(frame, h->hunk + (fr % h->fps) * 2448, 2352);
	return 2352;
}

static int chd_serial(const char *path, char out[DS_SERIAL_MAX])
{
	struct Chd h;
	const chd_header *hd;
	char meta[256], pgtype[32] = "";
	unsigned track = 0, frames = 0, pregap = 0;
	uint32_t msize = 0;
	int rc = 0;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	memset(&h, 0, sizeof h);
	if (chd_open_file(f, CHD_OPEN_READ, NULL, &h.c) != CHDERR_NONE)
	{
		fclose(f);
		return 0;
	}
	hd = chd_get_header(h.c);
	h.fps = hd->hunkbytes / 2448;
	h.cur = -1;
	if (h.fps && (h.hunk = (unsigned char *)DS_ALLOC(hd->hunkbytes)) != NULL)
	{
		char type[64], sub[32], pgsub[32];
		unsigned postgap;
		if (chd_get_metadata(h.c, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof meta, &msize, NULL, NULL) == CHDERR_NONE)
			sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, sub, &frames, &pregap, pgtype, pgsub, &postgap);
		h.base = pgtype[0] == 'V' ? pregap : 0;		/* a pregap of type V... is stored in the file */
		rc = ds_serial_from_frames(chd_frame, &h, out) == 1;
		DS_FREE(h.hunk);
	}
	chd_close(h.c);		/* before its FILE: chd_open_file does not own it */
	fclose(f);
	return rc;
}
#endif

/* ---- by the file's extension ---- */

static int open_serial(const char *path, int depth, char out[DS_SERIAL_MAX])
{
	const char *e = ext_of(path);
	char other[256];
	FILE *f = NULL;
	int rc = 0;
	if (!strcasecmp(e, "pbp"))
		return pbp_serial(path, out);
#ifdef USE_LIBCHDR
	if (!strcasecmp(e, "chd"))
		return chd_serial(path, out);
#endif
	if (!strcasecmp(e, "m3u"))
	{
		char text[512], *s;
		size_t n;
		if (depth || !(f = fopen(path, "rb")))
			return 0;
		n = fread(text, 1, sizeof text - 1, f);
		fclose(f);
		text[n] = 0;
		for (s = text; *s;)
		{
			char *end = s + strcspn(s, "\r\n");
			const char c = *end;
			*end = 0;
			while (*s == ' ' || *s == '\t')
				s++;
			if (*s && *s != '#' && sibling(other, sizeof other, path, s))
				return open_serial(other, depth + 1, out);
			if (!c)
				break;
			s = end + 1;
		}
		return 0;
	}
	if (!strcasecmp(e, "cue") || !strcasecmp(e, "toc"))
	{
		if (!sheet_file(path, other, sizeof other))
			return 0;
		if (depth > 1)		/* a sheet naming a sheet naming a sheet */
			return 0;
		return open_serial(other, depth + 1, out);
	}
	if (!strcasecmp(e, "ccd"))
		f = open_with_ext(path, ".img", ".IMG");
	else if (!strcasecmp(e, "mds"))
		f = open_with_ext(path, ".mdf", ".MDF");
	else if (!strcasecmp(e, "bin") || !strcasecmp(e, "iso") || !strcasecmp(e, "img") || !strcasecmp(e, "mdf"))
		f = fopen(path, "rb");
	if (!f)
		return 0;
	rc = raw_serial(f, out) == 1;
	fclose(f);
	return rc;
}

int ds_read_serial(const char *path, char out[DS_SERIAL_MAX])
{
	if (!path || !out)
		return 0;
	out[0] = 0;
	return open_serial(path, 0, out);
}
