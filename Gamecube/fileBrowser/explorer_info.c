/* See explorer_info.h. */
#include "explorer_info.h"
#include <string.h>
#if defined(_WIN32) && !defined(__MINGW32__)	/* the host test under clang for Windows has no strings.h */
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* The extensions the disc loader opens (fileBrowser-libfat.c discExts), plus M3U for a playlist.
 * The tag is the extension in capitals. */
static const char *const FORMATS[] = {
	"CHD", "CUE", "BIN", "ISO", "PBP", "M3U", "IMG", "MDF", "MDS", "CCD", "TOC", "CBN", "EXE", 0
};

static const char *baseName(const char *path)
{
	const char *b = path, *p;
	for (p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			b = p + 1;
	return b;
}

const char *ei_format(const char *name)
{
	const char *dot = 0, *p;
	int i;
	if (!name)
		return 0;
	for (p = baseName(name); *p; p++)
		if (*p == '.')
			dot = p;
	if (!dot || dot[1] == 0)
		return 0;
	for (i = 0; FORMATS[i]; i++)
		if (strcasecmp(dot + 1, FORMATS[i]) == 0)
			return FORMATS[i];
	return 0;
}

int ei_wants_disc_count(const char *name)
{
	const char *f = ei_format(name);
	return f && (!strcmp(f, "PBP") || !strcmp(f, "M3U"));
}

/* ---- region ---- */

struct Tag { const char *token; const char *region; };

/* Whole tokens of a "(...)" or "[...]" group, case-insensitive. */
static const struct Tag TAGS[] = {
	{ "USA", "USA" }, { "U", "USA" }, { "NTSC-U", "USA" }, { "Canada", "Canada" },
	{ "Europe", "Europe" }, { "E", "Europe" }, { "PAL", "Europe" }, { "UK", "UK" },
	{ "France", "France" }, { "Germany", "Germany" }, { "Italy", "Italy" }, { "Spain", "Spain" },
	{ "Netherlands", "Netherlands" }, { "Sweden", "Sweden" }, { "Russia", "Russia" },
	{ "Scandinavia", "Scandinavia" }, { "Australia", "Australia" }, { "Brazil", "Brazil" },
	{ "Japan", "Japan" }, { "J", "Japan" }, { "NTSC-J", "Japan" },
	{ "Korea", "Korea" }, { "China", "China" }, { "Taiwan", "Taiwan" }, { "Asia", "Asia" },
	{ "World", "World" },
	{ 0, 0 }
};

/* The first four letters of a PlayStation serial ("SLUS-01296", "SCES_123.45") say where it was sold. */
static const struct Tag SERIALS[] = {
	{ "SLUS", "USA" }, { "SCUS", "USA" }, { "PAPX", "Japan" },
	{ "SLES", "Europe" }, { "SCES", "Europe" }, { "SLED", "Europe" }, { "SCED", "Europe" },
	{ "SLPS", "Japan" }, { "SLPM", "Japan" }, { "SCPS", "Japan" }, { "SCPM", "Japan" },
	{ "SLAJ", "Asia" }, { "SCAJ", "Asia" }, { "SLKA", "Korea" }, { "SCKA", "Korea" },
	{ 0, 0 }
};

static const char *lookupToken(const char *tok)
{
	int i;
	for (i = 0; TAGS[i].token; i++)
		if (strcasecmp(tok, TAGS[i].token) == 0)
			return TAGS[i].region;
	if (strlen(tok) >= 6 && (tok[4] == '-' || tok[4] == '_') && tok[5] >= '0' && tok[5] <= '9')
		for (i = 0; SERIALS[i].token; i++)
			if (strncasecmp(tok, SERIALS[i].token, 4) == 0)
				return SERIALS[i].region;
	return 0;
}

static int addRegion(char *out, unsigned outSize, const char *region)
{
	size_t len = strlen(out), add = strlen(region);
	const char *p = out;
	while (*p)		/* once only: "(Europe) [E]" is Europe */
	{
		const char *q = strchr(p, ',');
		size_t n = q ? (size_t)(q - p) : strlen(p);
		if (n == add && !strncmp(p, region, n))
			return 0;
		if (!q)
			break;
		p = q + 2;
	}
	if (len + (len ? 2 : 0) + add + 1 > outSize)
		return 0;
	if (len)
	{
		out[len++] = ',';
		out[len++] = ' ';
	}
	memcpy(out + len, region, add + 1);
	return 1;
}

int ei_region(const char *name, char *out, unsigned outSize)
{
	const char *p;
	if (!out || outSize == 0)
		return 0;
	out[0] = 0;
	if (!name)
		return 0;
	for (p = baseName(name); *p; p++)
	{
		char close = *p == '(' ? ')' : *p == '[' ? ']' : 0;
		const char *end, *s;
		char tok[48];
		if (!close || !(end = strchr(p + 1, close)))
			continue;
		for (s = p + 1; s <= end; s++)		/* tokens split at ',' (and the closing bracket) */
		{
			const char *t = s, *e, *r;
			while (s < end && *s != ',')
				s++;
			e = s;
			while (t < e && *t == ' ')
				t++;
			while (e > t && e[-1] == ' ')
				e--;
			if (e > t && (size_t)(e - t) < sizeof tok)
			{
				memcpy(tok, t, e - t);
				tok[e - t] = 0;
				if ((r = lookupToken(tok)) != 0)
					addRegion(out, outSize, r);
			}
		}
		p = end;
	}
	return out[0] != 0;
}

/* ---- disc counts ---- */

static unsigned le32(const unsigned char *b)
{
	return (unsigned)b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24;
}

int ei_pbp_discs(ei_read_fn rd, void *ctx)
{
	unsigned char hdr[0x28], sig[16], tab[32];
	unsigned long psar;
	int i, n;

	if (!rd || rd(ctx, 0, hdr, sizeof hdr) != (int)sizeof hdr)
		return 0;
	if (hdr[0] != 0 || hdr[1] != 'P' || hdr[2] != 'B' || hdr[3] != 'P')
		return 0;
	psar = le32(hdr + 0x24);		/* the eighth offset: DATA.PSAR */
	if (rd(ctx, psar, sig, sizeof sig) != (int)sizeof sig)
		return 0;
	if (!memcmp(sig, "PSISOIMG00", 10))
		return 1;
	if (memcmp(sig, "PSTITLEIMG", 10))
		return 0;
	if (rd(ctx, psar + 0x200, tab, sizeof tab) != (int)sizeof tab)
		return 0;
	for (n = 0, i = 0; i < 8 && le32(tab + 4 * i) != 0; i++)
		n++;
	return n;
}

int ei_m3u_discs(const char *text, unsigned n)
{
	unsigned i = 0;
	int lines = 0;
	if (n >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3))		/* a UTF-8 byte order mark */
		i = 3;
	while (i < n && text[i])
	{
		unsigned s = i, e;
		while (i < n && text[i] && text[i] != '\n' && text[i] != '\r')
			i++;
		e = i;
		while (i < n && (text[i] == '\n' || text[i] == '\r'))
			i++;
		while (s < e && (text[s] == ' ' || text[s] == '\t'))
			s++;
		while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t'))
			e--;
		if (e > s && text[s] != '#')
			lines++;
	}
	return lines;
}

/* ---- The facts strip's texts ---- */

#include <stdio.h>

static void putText(char *out, unsigned outSize, const char *s)
{
	if (!out || !outSize)
		return;
	snprintf(out, outSize, "%s", s);
}

void ei_fact_format(const char *name, int discs, char *out, unsigned outSize)
{
	const char *f = ei_format(name);
	if (!f)
		putText(out, outSize, "-");
	else if (discs >= 2 && out && outSize)
		snprintf(out, outSize, "%s, %d discs", f, discs);
	else
		putText(out, outSize, f);
}

void ei_fact_size(unsigned size, char *out, unsigned outSize)
{
	if (!out || !outSize)
		return;
	if (size == 0)
		putText(out, outSize, "-");
	else if (size < 1024u)
		snprintf(out, outSize, "%u B", size);
	else if (size < (1u << 20))
		snprintf(out, outSize, "%u KB", size >> 10);
	else if (size < (1u << 30))
		snprintf(out, outSize, "%u MB", size >> 20);
	else
	{
		/* tenths of a GB, rounded: no float on the console's hot path, and 64 bits for the multiply */
		unsigned long long t = ((unsigned long long)size * 10u + (1ull << 29)) >> 30;
		snprintf(out, outSize, "%u.%u GB", (unsigned)(t / 10u), (unsigned)(t % 10u));
	}
}

void ei_fact_region(const char *name, char *out, unsigned outSize)
{
	char r[48];
	if (ei_region(name, r, sizeof r))
		putText(out, outSize, r);
	else
		putText(out, outSize, "Unknown");
}

void ei_fact_saves(int n, char *out, unsigned outSize)
{
	if (!out || !outSize)
		return;
	if (n < 0)
		putText(out, outSize, "-");
	else if (n == 0)
		putText(out, outSize, "None");
	else if (n == 1)
		putText(out, outSize, "1 state");
	else
		snprintf(out, outSize, "%d states", n);
}

void ei_fact_ach(int set, char *out, unsigned outSize)
{
	putText(out, outSize, set == 1 ? "Set found" : set == 2 ? "None" : "-");
}

int ei_edge_top(int top, int slots, int count, int slotIdx, int dir)
{
	if (slots <= 0 || slotIdx < 0 || slotIdx >= slots)
		return -1;
	if (dir > 0)
		return (slotIdx == slots - 1 && top + slots < count) ? top + 1 : -1;
	if (dir < 0)
		return (slotIdx == 0 && top > 0) ? top - 1 : -1;
	return -1;
}

#define EI_QUEUE 16			/* folders waiting to be listed */
#define EI_LISTMAX 64		/* entries taken from one folder */

int ei_find_game(ei_list_fn ls, void *ctx, const char *dir, int maxDepth, int budget, int maxReads,
                 char *path, unsigned pathSize, unsigned *size)
{
	/* static: the queue is 4 KB, the menu thread's stack is not the place for it */
	static char queue[EI_QUEUE][EI_PATH_MAX];
	static int  depthOf[EI_QUEUE];
	ei_ent ents[EI_LISTMAX];
	int head = 0, tail = 1, reads = 0, found = 0;

	if (!ls || !dir || !path || pathSize == 0)
		return 0;
	path[0] = 0;
	if (size)
		*size = 0;
	strncpy(queue[0], dir, EI_PATH_MAX - 1);
	queue[0][EI_PATH_MAX - 1] = 0;
	depthOf[0] = 0;

	while (head < tail && reads < maxReads && budget > 0)
	{
		const int cur = head++;
		const int cap = budget < EI_LISTMAX ? budget : EI_LISTMAX;
		int n = ls(ctx, queue[cur], ents, cap), i, best = -1, bin = -1;
		reads++;
		if (n < 0)
			continue;
		if (n > cap)
			n = cap;
		budget -= n;
		for (i = 0; i < n && best < 0; i++)
		{
			const char *f;
			if (ents[i].isDir || !(f = ei_format(ents[i].path)))
				continue;
			if (strcmp(f, "BIN"))
				best = i;
			else if (bin < 0)
				bin = i;
		}
		if (best < 0)
			best = bin;
		if (best >= 0)
		{
			strncpy(path, ents[best].path, pathSize - 1);
			path[pathSize - 1] = 0;
			if (size)
				*size = ents[best].size;
			found = 1;
			break;
		}
		if (depthOf[cur] < maxDepth)
			for (i = 0; i < n && tail < EI_QUEUE; i++)
				if (ents[i].isDir)
				{
					strncpy(queue[tail], ents[i].path, EI_PATH_MAX - 1);
					queue[tail][EI_PATH_MAX - 1] = 0;
					depthOf[tail++] = depthOf[cur] + 1;
				}
	}
	ls(ctx, NULL, NULL, 0);
	return found;
}
