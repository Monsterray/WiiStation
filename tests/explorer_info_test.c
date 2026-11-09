/* explorer_info_test.c - the explorer's format tag, region and disc count, on the host.
 *
 *     cc -std=c11 -O2 -o explorer_info_test tests/explorer_info_test.c Gamecube/fileBrowser/explorer_info.c
 *     ./explorer_info_test [path of the repository's tests/fixtures/explorer]
 *
 * The names are the real ones on the test card (C:/wiistation-lib/games: isos, Formats, Tests),
 * with a few made-up ones for the cases the card does not have. The two PBP fixtures are the first
 * 64 KB of real files: Final Fantasy VIII (four discs) and Frogger (one disc).
 * Exit status = the number of failed checks.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Gamecube/fileBrowser/explorer_info.h"

static int failures, checks;

static void check(int ok, const char *what, const char *got)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("FAIL: %s (got %s)\n", what, got ? got : "(null)");
	}
}

static void fmt(const char *name, const char *want)
{
	const char *got = ei_format(name);
	check((!got && !want) || (got && want && !strcmp(got, want)), name, got);
}

static void region(const char *name, const char *want)
{
	char out[32];
	int found = ei_region(name, out, sizeof out);
	check(found == (want != 0) && !strcmp(out, want ? want : ""), name, out);
}

struct Mem { const unsigned char *p; unsigned long n; };

static int memRead(void *ctx, unsigned long off, void *buf, unsigned n)
{
	const struct Mem *m = ctx;
	if (off >= m->n)
		return 0;
	if (off + n > m->n)
		n = (unsigned)(m->n - off);
	memcpy(buf, m->p + off, n);
	return (int)n;
}

static unsigned char *slurp(const char *dir, const char *name, unsigned long *n)
{
	char path[512];
	FILE *f;
	unsigned char *b;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (!f)
		return 0;
	b = malloc(1 << 20);
	*n = fread(b, 1, 1 << 20, f);
	fclose(f);
	return b;
}

static int discsOf(const unsigned char *p, unsigned long n)
{
	struct Mem m = { p, n };
	return ei_pbp_discs(memRead, &m);
}

static void m3u(const char *text, int want, const char *what)
{
	char got[16];
	snprintf(got, sizeof got, "%d", ei_m3u_discs(text, (unsigned)strlen(text)));
	check(ei_m3u_discs(text, (unsigned)strlen(text)) == want, what, got);
}

/* A fake tree for ei_find_game: "dir" -> "f:path|d:path|..." in listing order */
static const struct { const char *dir; const char *kids; } TREE[] = {
	{ "sd:/a",       "f:sd:/a/readme.txt|d:sd:/a/b|f:sd:/a/cover.png" },
	{ "sd:/a/b",     "d:sd:/a/b/c|f:sd:/a/b/Game (USA) (Track 1).bin|f:sd:/a/b/Game (USA).cue|f:sd:/a/b/Game (USA) (Track 2).bin" },
	{ "sd:/a/b/c",   "f:sd:/a/b/c/deep.chd" },
	{ "sd:/only",    "d:sd:/only/x|f:sd:/only/note.txt" },
	{ "sd:/only/x",  "d:sd:/only/x/y|f:sd:/only/x/z.nfo" },
	{ "sd:/only/x/y", "d:sd:/only/x/y/z" },
	{ "sd:/only/x/y/z", "d:sd:/only/x/y/z/w" },
	{ "sd:/only/x/y/z/w", "f:sd:/only/x/y/z/w/too_deep.chd" },
	{ "sd:/empty",   "" },
	{ "sd:/bins",    "f:sd:/bins/t1.bin|f:sd:/bins/t2.bin" },
	{ "sd:/wide",    "d:sd:/wide/d1|d:sd:/wide/d2|d:sd:/wide/d3|d:sd:/wide/d4|f:sd:/wide/late.iso" },
	{ "sd:/wide/d1", "f:sd:/wide/d1/one.pbp" },
	{ "sd:/mixed",   "f:sd:/mixed/a.txt|f:sd:/mixed/b.ISO|f:sd:/mixed/c.chd" },
	{ 0, 0 }
};
static char listKept[2048];
static int listCalls, listReleased;

static int fakeList(void *ctx, const char *dir, ei_ent *out, int max)
{
	int t, n = 0;
	char *p;
	(void)ctx;
	if (!dir) { listReleased++; return 0; }
	listCalls++;
	for (t = 0; TREE[t].dir; t++)
		if (!strcmp(TREE[t].dir, dir))
			break;
	if (!TREE[t].dir)
		return -1;							/* unreadable */
	snprintf(listKept, sizeof listKept, "%s", TREE[t].kids);
	for (p = listKept; *p && n < max; )
	{
		out[n].isDir = p[0] == 'd';
		out[n].path = p + 2;
		out[n].size = 1000u + (unsigned)n;
		n++;
		while (*p && *p != '|') p++;
		if (*p == '|') *p++ = 0;
	}
	return n;
}

static void findGame(const char *dir, int depth, int budget, int reads, const char *want, const char *what)
{
	char p[EI_PATH_MAX];
	unsigned sz = 7;
	const int ok = ei_find_game(fakeList, 0, dir, depth, budget, reads, p, sizeof p, &sz);
	check(want ? (ok && !strcmp(p, want)) : (!ok && p[0] == 0 && sz == 0), what, p);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "tests/fixtures/explorer";
	unsigned long n;
	unsigned char *b;

	/* the format tag: the extensions of the real files, any case, any folder */
	fmt("Frogger (USA).bin", "BIN");
	fmt("Frogger (USA).cue", "CUE");
	fmt("Frogger (USA).CUE", "CUE");
	fmt("Medievil [U] [SCUS-94227].chd", "CHD");
	fmt("Medievil [U] [SCUS-94227].pbp", "PBP");
	fmt("sd:/wiistation/isos/Formats/Frogger (USA) [ISO]/Frogger (USA).iso", "ISO");
	fmt("Frogger (USA).mds", "MDS");
	fmt("Frogger (USA).ccd", "CCD");
	fmt("Frogger (USA).toc", "TOC");
	fmt("Medievil.cbn", "CBN");
	fmt("Final Fantasy IX (USA).m3u", "M3U");
	fmt("Frogger (USA).img", "IMG");
	fmt("Frogger (USA).mdf", "MDF");
	fmt("Final Fantasy IX Official Strategy Guide.pdf", 0);
	fmt("sd:/wiistation/isos/Kids.Demos/readme", 0);		/* the dot belongs to the folder */
	fmt("Crash Bash", 0);
	fmt("Frogger (USA).", 0);
	fmt("", 0);
	fmt("Frogger (USA).cue.txt", 0);

	/* the region: the tags of the real names */
	region("Spyro the Dragon 3 - Year of the Dragon (USA) (Rev 1)", "USA");
	region("Resident Evil 3 - Nemesis (USA, Canada)", "USA, Canada");
	region("Tobal 2 (Japan) (Rev 1)", "Japan");
	region("PaRappa the Rapper (USA) (En,Fr,De,Es,It)", "USA");
	region("Legacy of Kain - Soul Reaver [U]", "USA");
	region("Final Fantasy IX [U] [Disc 1] [SLUS-01296]", "USA");
	region("Medievil [U] [SCUS-94227].pbp", "USA");
	region("Spyro the Dragon 2 - Ripto's Rage [NTSC-U] [SCUS-94425].pbp", "USA");
	region("Spyro - Year of the Dragon (USA) (Rev 1).pbp", "USA");
	region("sd:/wiistation/isos/Tobal 2 (Japan) (Rev 1)/Tobal 2 (Japan).cue", "Japan");
	region("Colin McRae Rally (Europe)", "Europe");
	region("Crash Bandicoot (Europe) [E]", "Europe");
	region("Game (Japan, USA)", "Japan, USA");
	region("Game [SLES-01234]", "Europe");
	region("Game [SLPS-00001]", "Japan");
	region("Game [NTSC-J]", "Japan");
	region("Gran Turismo 2 (Simulation Mode) (v1.2)", 0);
	region("Crash Bash", 0);
	region("Final Fantasy VIII.pbp", 0);
	region("Game (En,Fr,De)", 0);
	region("Game (USA", 0);							/* no closing bracket */
	region("sd:/USA (Europe)/Crash Bash.cue", 0);	/* the tags of a folder are not the file's */
	region("", 0);
	{
		char tiny[5];								/* too small for the answer: cut, never past the end */
		check(ei_region("Game (Europe)", tiny, sizeof tiny) == 0 && tiny[0] == 0, "region buffer too small", tiny);
	}

	/* which formats need a read */
	check(ei_wants_disc_count("a.pbp") && ei_wants_disc_count("a.M3U"), "pbp and m3u need a read", 0);
	check(!ei_wants_disc_count("a.chd") && !ei_wants_disc_count("a.cue") && !ei_wants_disc_count("a.iso")
	      && !ei_wants_disc_count("dir"), "a CUE (even with many tracks), CHD and ISO are one disc: no read", 0);

	/* PBP: the real headers, then bad ones */
	if ((b = slurp(dir, "ff8_4disc_head.pbp", &n)) != 0)
	{
		char got[16];
		snprintf(got, sizeof got, "%d", discsOf(b, n));
		check(discsOf(b, n) == 4, "Final Fantasy VIII PBP has 4 discs", got);
		check(discsOf(b, 100) == 0, "a PBP cut before its PSAR is unreadable", 0);
		check(discsOf(b, 0x10100) == 0, "a PBP cut before its disc table is unreadable", 0);
		free(b);
	}
	else
		check(0, "tests/fixtures/explorer/ff8_4disc_head.pbp missing", dir);
	if ((b = slurp(dir, "frogger_1disc_head.pbp", &n)) != 0)
	{
		char got[16];
		snprintf(got, sizeof got, "%d", discsOf(b, n));
		check(discsOf(b, n) == 1, "Frogger PBP has 1 disc", got);
		b[1] = 'X';
		check(discsOf(b, n) == 0, "a file that is not a PBP has no disc count", 0);
		free(b);
	}
	else
		check(0, "tests/fixtures/explorer/frogger_1disc_head.pbp missing", dir);
	{
		/* eight discs in the table, then a ninth the reader must not count */
		static unsigned char img[0x400];
		int i;
		memset(img, 0, sizeof img);
		memcpy(img + 1, "PBP", 3);
		img[0x24] = 0x00; img[0x25] = 0x01;			/* PSAR at 0x100 */
		memcpy(img + 0x100, "PSTITLEIMG000000", 16);
		for (i = 0; i < 8; i++)
			img[0x300 + 4 * i] = (unsigned char)(i + 1);
		check(discsOf(img, sizeof img) == 8, "an eight disc table counts 8", 0);
		memset(img + 0x300, 0, 32);
		check(discsOf(img, sizeof img) == 0, "an empty disc table is no game", 0);
		check(ei_pbp_discs(0, 0) == 0, "no reader", 0);
	}

	/* M3U */
	m3u("Final Fantasy IX (USA) (Disc 1).chd\r\nFinal Fantasy IX (USA) (Disc 2).chd\r\n"
	    "Final Fantasy IX (USA) (Disc 3).chd\r\nFinal Fantasy IX (USA) (Disc 4).chd\r\n", 4, "four lines, CRLF");
	m3u("#EXTM3U\n#a comment\nDisc 1.cue\n\n   \nDisc 2.cue", 2, "comments and blanks do not count; no final newline");
	m3u("\xEF\xBB\xBF#EXTM3U\nDisc 1.cue\n", 1, "a byte order mark before a comment");
	m3u("  Disc 1.cue  \n\tDisc 2.cue\t\n", 2, "indented lines");
	m3u("", 0, "an empty playlist");
	m3u("\n\n\r\n", 0, "only line breaks");
	m3u("Disc 1.cue", 1, "one line");

	/* The facts strip's texts */
	{
		char o[32];
		static const struct { unsigned size; const char *want; } sz[] = {
			{ 0, "-" }, { 1, "1 B" }, { 512, "512 B" }, { 1023, "1023 B" }, { 1024, "1 KB" },
			{ 655360, "640 KB" }, { 1048575, "1023 KB" }, { 1048576, "1 MB" }, { 654311424u, "624 MB" },
			{ 253755392u, "242 MB" }, { 1073741823u, "1023 MB" }, { 1073741824u, "1.0 GB" },
			{ 1181116006u, "1.1 GB" }, { 0xFFFFFFFFu, "4.0 GB" },
		};
		static const struct { int n; const char *want; } sv[] = {
			{ -1, "-" }, { 0, "None" }, { 1, "1 state" }, { 5, "5 states" }, { 10, "10 states" },
		};
		unsigned i;
		for (i = 0; i < sizeof sz / sizeof sz[0]; i++)
		{
			ei_fact_size(sz[i].size, o, sizeof o);
			check(!strcmp(o, sz[i].want), sz[i].want, o);
		}
		for (i = 0; i < sizeof sv / sizeof sv[0]; i++)
		{
			ei_fact_saves(sv[i].n, o, sizeof o);
			check(!strcmp(o, sv[i].want), sv[i].want, o);
		}
		ei_fact_ach(1, o, sizeof o);  check(!strcmp(o, "Set found"), "ach 1", o);
		ei_fact_ach(2, o, sizeof o);  check(!strcmp(o, "None"), "ach 2", o);
		ei_fact_ach(0, o, sizeof o);  check(!strcmp(o, "-"), "ach 0 (never loaded)", o);
		ei_fact_ach(-1, o, sizeof o); check(!strcmp(o, "-"), "ach -1", o);
		ei_fact_format("Game.chd", 1, o, sizeof o);       check(!strcmp(o, "CHD"), "format CHD", o);
		ei_fact_format("Game.PBP", 0, o, sizeof o);       check(!strcmp(o, "PBP"), "format PBP, discs unknown", o);
		ei_fact_format("FF8.pbp", 4, o, sizeof o);        check(!strcmp(o, "PBP, 4 discs"), "format PBP 4 discs", o);
		ei_fact_format("notes.txt", 1, o, sizeof o);      check(!strcmp(o, "-"), "format of an unknown file", o);
		ei_fact_region("Crash Bandicoot 3 (USA).chd", o, sizeof o);  check(!strcmp(o, "USA"), "region USA", o);
		ei_fact_region("Players.chd", o, sizeof o);       check(!strcmp(o, "Unknown"), "region none", o);
		ei_fact_size(654311424u, o, 4);                   check(!strcmp(o, "624"), "a small buffer cuts, never overruns", o);
		ei_fact_size(1, 0, 0);                            /* no buffer: must not crash */
	}

	/* Edge scroll: Down on the last slot with items below, Up on slot 0 with items above */
	{
		char o[16];
		static const struct { int top, slots, count, slot, dir, want; } es[] = {
			{ 0, 7, 20, 6, 1, 1 },		/* last slot, more below: scroll by one */
			{ 5, 7, 20, 6, 1, 6 },
			{ 13, 7, 20, 6, 1, -1 },	/* window at the true end: the engine wraps */
			{ 0, 7, 20, 5, 1, -1 },		/* a middle slot: the engine moves the focus */
			{ 3, 7, 20, 0, -1, 2 },		/* first slot, items above: scroll up */
			{ 1, 7, 20, 0, -1, 0 },
			{ 0, 7, 20, 0, -1, -1 },	/* true top: Up goes to the tab */
			{ 3, 7, 20, 1, -1, -1 },
			{ 0, 7, 7, 6, 1, -1 },		/* exactly one page: nothing to scroll */
			{ 0, 7, 8, 6, 1, 1 },		/* one item past the page */
			{ 0, 7, 3, 2, 1, -1 },		/* a short list */
			{ 0, 7, 20, 7, 1, -1 },		/* slot out of range */
			{ 0, 0, 20, 0, 1, -1 },
			{ 0, 7, 20, 6, 0, -1 },		/* no direction */
		};
		unsigned i;
		for (i = 0; i < sizeof es / sizeof es[0]; i++)
		{
			const int g = ei_edge_top(es[i].top, es[i].slots, es[i].count, es[i].slot, es[i].dir);
			snprintf(o, sizeof o, "%d", g);
			check(g == es[i].want, "edge top", o);
		}
	}

	/* The first game inside a folder */
	findGame("sd:/a", 3, 64, 8, "sd:/a/b/Game (USA).cue", "the CUE, not the BIN tracks, two levels down");
	findGame("sd:/a/b", 3, 64, 8, "sd:/a/b/Game (USA).cue", "CUE beats the BINs listed before it");
	findGame("sd:/a", 0, 64, 8, NULL, "depth 0 lists the folder alone: no game");
	findGame("sd:/a", 1, 64, 8, "sd:/a/b/Game (USA).cue", "depth 1 reaches the sub-folder");
	findGame("sd:/only", 3, 64, 8, NULL, "a game 4 levels down is not found (depth 3)");
	findGame("sd:/empty", 3, 64, 8, NULL, "an empty folder");
	findGame("sd:/bins", 3, 64, 8, "sd:/bins/t1.bin", "BINs only: the first one");
	findGame("sd:/nope", 3, 64, 8, NULL, "an unreadable folder");
	findGame("sd:/wide", 3, 64, 8, "sd:/wide/late.iso", "own files come before the sub-folders (breadth first)");
	findGame("sd:/mixed", 3, 64, 8, "sd:/mixed/b.ISO", "the first game in listing order");
	findGame("sd:/a", 3, 2, 8, NULL, "a budget of 2 entries stops inside the first folder");
	findGame("sd:/a", 3, 3, 8, NULL, "budget 3: the sub-folder is queued but not listed");
	findGame("sd:/a", 3, 64, 1, NULL, "one read: only the folder itself");
	findGame("sd:/a", 3, 64, 2, "sd:/a/b/Game (USA).cue", "two reads reach it");
	{
		char p[EI_PATH_MAX];
		unsigned sz = 0;
		listCalls = listReleased = 0;
		check(ei_find_game(fakeList, 0, "sd:/wide", 3, 64, 8, p, sizeof p, &sz) == 1 && sz == 1004,
		      "size of the game from the listing", "");
		check(listCalls == 1 && listReleased == 1, "found in the first folder: one read, then released", "");
		listCalls = listReleased = 0;
		ei_find_game(fakeList, 0, "sd:/only", 3, 64, 8, p, sizeof p, 0);
		check(listCalls == 4 && listReleased == 1, "no game: lists to depth 3 (4 folders), released once", "");
		check(ei_find_game(0, 0, "sd:/a", 3, 64, 8, p, sizeof p, 0) == 0, "no list function", "");
		check(ei_find_game(fakeList, 0, "sd:/a", 3, 64, 8, p, 4, 0) == 1 && !strcmp(p, "sd:"),
		      "a small buffer cuts, never overruns", p);
	}

	printf("explorer_info: %d checks, %d failed\n", checks, failures);
	return failures;
}
