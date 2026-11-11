/* disc_serial_test.c - the explorer's serial reader (names, BOOT lines, every image layout), on the host.
 *
 *     cc -std=c11 -O2 -D_CRT_SECURE_NO_WARNINGS -o disc_serial_test tests/disc_serial_test.c Gamecube/fileBrowser/disc_serial.c
 *     ./disc_serial_test SCRATCH_DIR
 *
 * Builds a tiny ISO 9660 volume (SYSTEM.CNF in the root) in the sector layouts the explorer meets
 * (2048, raw 2352 with the data at 16 or 24, 2336), wraps it as iso/bin/img/mdf and behind a cue,
 * toc, ccd, mds and m3u, writes PBPs with a PARAM.SFO, and reads each one's serial.
 * Exit status = the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Gamecube/fileBrowser/disc_serial.h"

static int failures, checks;

static void check(int ok, const char *what, const char *got)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL: %s (got %s)\n", what, got ? got : "(none)");
	}
}

static void want(const char *path, const char *serial, const char *what)
{
	char out[DS_SERIAL_MAX] = "";
	const int rc = ds_read_serial(path, out);
	char msg[256];
	snprintf(msg, sizeof msg, "%s: %s", what, serial ? serial : "no serial");
	check(serial ? rc == 1 && !strcmp(out, serial) : rc == 0, msg, rc ? out : NULL);
}

static void put_le32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* sector n of the volume: 2048 bytes */
static void make_volume(unsigned char (*sec)[2048], int count, const char *cnf)
{
	unsigned char *pvd = sec[16], *dir = sec[20], *r;
	memset(sec, 0, (size_t)count * 2048);
	pvd[0] = 1;
	memcpy(pvd + 1, "CD001", 5);
	pvd[156] = 34;
	put_le32(pvd + 156 + 2, 20);
	put_le32(pvd + 156 + 10, 2048);
	r = dir;
	r[0] = 34; r[32] = 1; r[33] = 0;  put_le32(r + 2, 20); put_le32(r + 10, 2048);	/* "." */
	r += 34;
	r[0] = 34; r[32] = 1; r[33] = 1;  put_le32(r + 2, 20); put_le32(r + 10, 2048);	/* ".." */
	r += 34;
	r[0] = 40; r[32] = 7; memcpy(r + 33, "OTHER.X", 7); put_le32(r + 2, 25); put_le32(r + 10, 10);
	r += 40;
	r[0] = 46; r[32] = 12; memcpy(r + 33, "SYSTEM.CNF;1", 12); put_le32(r + 2, 22); put_le32(r + 10, (unsigned)strlen(cnf));
	memcpy(sec[22], cnf, strlen(cnf));
}

static void write_image(const char *path, unsigned char (*sec)[2048], int count, unsigned stride, unsigned off)
{
	FILE *f = fopen(path, "wb");
	unsigned char frame[2448];
	int i;
	if (!f) { printf("cannot write %s\n", path); exit(2); }
	for (i = 0; i < count; i++) {
		memset(frame, 0xAA, sizeof frame);
		if (stride == 2352 || stride == 2448 || stride == 2336) {
			memset(frame, 0, 12);
			frame[0] = 0xFF;
		}
		memcpy(frame + off, sec[i], 2048);
		fwrite(frame, 1, stride, f);
	}
	fclose(f);
}

static void write_text(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f) { printf("cannot write %s\n", path); exit(2); }
	fputs(text, f);
	fclose(f);
}

static void write_pbp(const char *path, const char *discId)
{
	unsigned char b[0x200];
	unsigned char *sfo;
	memset(b, 0, sizeof b);
	memcpy(b, "\0PBP", 4);
	put_le32(b + 8, 0x28);		/* PARAM.SFO */
	put_le32(b + 12, 0x28 + 0x80);	/* the next section */
	put_le32(b + 0x24, 0x1000);
	sfo = b + 0x28;
	memcpy(sfo, "\0PSF", 4);
	put_le32(sfo + 8, 0x30);		/* key table */
	put_le32(sfo + 12, 0x40);		/* data table */
	put_le32(sfo + 16, 1);
	sfo[20] = 0; sfo[21] = 0;		/* key offset 0 */
	put_le32(sfo + 20 + 12, 0);		/* data offset 0 */
	memcpy(sfo + 0x30, "DISC_ID", 8);
	memcpy(sfo + 0x40, discId, strlen(discId));
	{
		FILE *f = fopen(path, "wb");
		if (!f) { printf("cannot write %s\n", path); exit(2); }
		fwrite(b, 1, 0x28 + 0x80, f);
		fclose(f);
	}
}

static void names(void)
{
	char o[DS_SERIAL_MAX];
	check(ds_name_serial("sd:/wiistation/isos/Ape Escape [U] [SCUS-94423].cue", o) && !strcmp(o, "SCUS94423"), "bracket dash", o);
	check(ds_name_serial("sd:/isos/Gex/slus-00042.bin", o) && !strcmp(o, "SLUS00042"), "lower case file", o);
	check(ds_name_serial("sd:/isos/Final Fantasy VIII [NTSC-U] [Disc2of4] [SLUS-00908]/disc.cue", o) && !strcmp(o, "SLUS00908"), "the folder's name", o);
	check(ds_name_serial("sd:/isos/SCES 12345 Game.chd", o) && !strcmp(o, "SCES12345"), "space", o);
	check(ds_name_serial("sd:/isos/SLUS_00892.cue", o) && !strcmp(o, "SLUS00892"), "underscore", o);
	check(!ds_name_serial("sd:/isos/Frogger (USA)/Frogger (USA).cue", o), "no serial in the names", o);
	check(!ds_name_serial("sd:/isos/Game 1234567/x.bin", o), "digits only", o);
	check(!ds_name_serial("sd:/isos/XSLUS-00892.cue", o), "glued to a letter", o);
	check(!ds_name_serial("sd:/isos/SLUS-008921.cue", o), "six digits", o);
	check(!ds_name_serial("x.cue", o), "no folder", o);
}

static void boot(void)
{
	char o[DS_SERIAL_MAX];
	const char *a = "BOOT = cdrom:\\SLUS_008.92;1\r\nTCB = 4\r\n";
	const char *b = "BOOT=cdrom:SCES_123.45;1\nTCB=4";
	const char *c = "BOOT = cdrom:\\EXE\\SLES_001.23;1\r\n";
	const char *d = "BOOT = cdrom:\\PSX.EXE;1\r\n";
	check(ds_parse_boot(a, (unsigned)strlen(a), o) && !strcmp(o, "SLUS00892"), "boot backslash", o);
	check(ds_parse_boot(b, (unsigned)strlen(b), o) && !strcmp(o, "SCES12345"), "boot no backslash", o);
	check(ds_parse_boot(c, (unsigned)strlen(c), o) && !strcmp(o, "SLES00123"), "boot in a subfolder", o);
	check(!ds_parse_boot(d, (unsigned)strlen(d), o), "boot PSX.EXE", o);
	check(!ds_parse_boot("TCB = 4", 7, o), "no boot line", o);
}

int main(int argc, char **argv)
{
	static unsigned char sec[32][2048];
	char p[600], q[600];
	const char *dir = argc > 1 ? argv[1] : ".";
	static const struct { unsigned stride, off; const char *ext; } L[] = {
		{ 2048, 0, "iso" }, { 2352, 24, "bin" }, { 2352, 16, "bin" }, { 2448, 24, "img" }, { 2336, 8, "mdf" },
	};
	unsigned i;

	names();
	boot();

	make_volume(sec, 32, "BOOT = cdrom:\\SCUS_944.23;1\r\nTCB = 4\r\n");
	for (i = 0; i < sizeof L / sizeof *L; i++) {
		char what[64];
		snprintf(p, sizeof p, "%s/v%u.%s", dir, i, L[i].ext);
		write_image(p, sec, 32, L[i].stride, L[i].off);
		snprintf(what, sizeof what, "layout %u/%u", L[i].stride, L[i].off);
		want(p, "SCUS94423", what);
	}
	/* a cue, its file in the same folder (a quoted name with spaces, upper-case keywords, CRLF) */
	snprintf(p, sizeof p, "%s/Game One.bin", dir);
	write_image(p, sec, 32, 2352, 24);
	snprintf(q, sizeof q, "%s/Game One.cue", dir);
	write_text(q, "FILE \"Game One.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\nFILE \"Other.bin\" BINARY\r\n");
	want(q, "SCUS94423", "cue");
	snprintf(q, sizeof q, "%s/Game One.CUE", dir);
	write_text(q, "  file \"Game One.bin\" binary\n");
	want(q, "SCUS94423", "cue, lower case");
	snprintf(q, sizeof q, "%s/Game One.toc", dir);
	write_text(q, "CD_ROM_XA\nTRACK MODE2_RAW\nDATAFILE \"Game One.bin\" 0\n");
	want(q, "SCUS94423", "toc");
	snprintf(q, sizeof q, "%s/Game One.m3u", dir);
	write_text(q, "# playlist\n\nGame One.cue\n");
	snprintf(q, sizeof q, "%s/Game One.cue", dir);
	write_text(q, "FILE \"Game One.bin\" BINARY\n");
	snprintf(q, sizeof q, "%s/Game One.m3u", dir);
	want(q, "SCUS94423", "m3u");
	/* ccd -> .img, mds -> .mdf */
	snprintf(p, sizeof p, "%s/Twin.img", dir);
	write_image(p, sec, 32, 2352, 16);
	snprintf(q, sizeof q, "%s/Twin.ccd", dir);
	write_text(q, "[CloneCD]\nVersion=3\n");
	want(q, "SCUS94423", "ccd");
	snprintf(p, sizeof p, "%s/Twin2.mdf", dir);
	write_image(p, sec, 32, 2352, 16);
	snprintf(q, sizeof q, "%s/Twin2.mds", dir);
	write_text(q, "MEDIA DESCRIPTOR");
	want(q, "SCUS94423", "mds");
	/* no SYSTEM.CNF, and a PS-X EXE boot */
	make_volume(sec, 32, "BOOT = cdrom:\\PSX.EXE;1\r\n");
	snprintf(p, sizeof p, "%s/exe.iso", dir);
	write_image(p, sec, 32, 2048, 0);
	want(p, NULL, "PSX.EXE disc");
	write_text(p, "not a disc at all");
	want(p, NULL, "not a disc");
	snprintf(p, sizeof p, "%s/missing.bin", dir);
	want(p, NULL, "no such file");
	snprintf(q, sizeof q, "%s/dangling.cue", dir);
	write_text(q, "FILE \"gone.bin\" BINARY\n");
	want(q, NULL, "cue without its file");
	/* PBP: PARAM.SFO's DISC_ID, with and without the dash */
	snprintf(p, sizeof p, "%s/a.pbp", dir);
	write_pbp(p, "SLUS00506");
	want(p, "SLUS00506", "pbp plain");
	write_pbp(p, "SCUS-94227");
	want(p, "SCUS94227", "pbp dash");
	write_pbp(p, "Frogger");
	want(p, NULL, "pbp title not a serial");

	printf("disc_serial: %d checks, %d failed\n", checks, failures);
	return failures;
}
