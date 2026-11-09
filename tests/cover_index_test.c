/* cover_index_test.c - the cover pack's index lookup, on the host.
 *
 *     cc -std=c11 -O2 -o cover_index_test tests/cover_index_test.c Gamecube/fileBrowser/cover_index.c
 *     ./cover_index_test SCRATCH_DIR [REAL_INDEX_TXT]
 *
 * Builds small indexes in SCRATCH_DIR (sorted, CRLF, no final newline, damaged, unsorted) and checks
 * every row is found and every absent serial is not. With a second argument (the real pack's
 * index.txt, C:/wiistation-lib/cover_pack/covers/index.txt) every one of its rows is looked up.
 * Exit status = the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Gamecube/fileBrowser/cover_index.h"

static int failures, checks;

static void check(int ok, const char *what, const char *got)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL: %s (got %s)\n", what, got ? got : "(null)");
	}
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("cannot write %s\n", path);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	char path[512], buf[64], big[64 * 1024];
	cover_row r;
	int i, n = 0;
	const char *dir = argc > 1 ? argv[1] : ".";

	/* a sorted index of 300 rows with rows of different lengths (3d column present or '-') */
	big[0] = 0;
	for (i = 0; i < 300; i++) {
		char line[160];
		snprintf(line, sizeof line, "SLUS%05d\tbox/SLUS%05d.jpg\t%s\tpsxcovers\t%s\n", i * 3, i * 3,
		         i % 4 == 0 ? "disc/SLUS00000.png" : "-", i % 5 == 0 ? "3d/x.png" : "-");
		strcat(big, line);
	}
	snprintf(path, sizeof path, "%s/index_a.txt", dir);
	write_file(path, big);
	for (i = 0; i < 300; i++) {
		snprintf(buf, sizeof buf, "SLUS%05d", i * 3);
		check(cover_index_find(path, buf, &r) && !strcmp(r.serial, buf), buf, r.serial);
		snprintf(buf, sizeof buf, "box/SLUS%05d.jpg", i * 3);
		check(!strcmp(r.box, buf), "box path", r.box);
		check((i % 4 == 0) == (r.disc[0] != 0), "disc column", r.disc);
		check((i % 5 == 0) == (r.art3d[0] != 0), "3d column", r.art3d);
		snprintf(buf, sizeof buf, "SLUS%05d", i * 3 + 1);
		check(!cover_index_find(path, buf, &r), "absent between rows", buf);
	}
	check(!cover_index_find(path, "AAAA00001", &r), "absent before the first", "");
	check(!cover_index_find(path, "SLUS99999", &r), "absent after the last", "");
	check(cover_index_find(path, "slus-00003", &r) && !strcmp(r.serial, "SLUS00003"), "lower case and dash", r.serial);
	check(!cover_index_find(path, "", &r), "empty serial", "");
	check(!cover_index_find(path, "SLUS-0000300000", &r), "too long", "");
	check(!cover_index_find(path, "SLUS00003", NULL), "no output", "");
	check(!cover_index_find("no/such/dir/index.txt", "SLUS00003", &r), "no file", "");

	/* CRLF lines and no newline after the last row */
	snprintf(path, sizeof path, "%s/index_b.txt", dir);
	write_file(path, "SCES00001\tbox/a.jpg\t-\tx\t-\r\nSCUS00002\tbox/b.jpg\tdisc/b.png\tx\t3d/b.png\r\nSLUS00003\tbox/c.jpg\t-\tx\t-");
	check(cover_index_find(path, "SCES00001", &r) && !strcmp(r.box, "box/a.jpg") && !r.disc[0], "crlf first", r.box);
	check(cover_index_find(path, "SCUS00002", &r) && !strcmp(r.art3d, "3d/b.png"), "crlf middle", r.art3d);
	check(cover_index_find(path, "SLUS00003", &r) && !strcmp(r.box, "box/c.jpg"), "last row, no newline", r.box);

	/* one row */
	snprintf(path, sizeof path, "%s/index_c.txt", dir);
	write_file(path, "SLUS00506\tbox/SLUS00506.jpg\t-\tpsxcovers\t-\n");
	check(cover_index_find(path, "SLUS00506", &r), "single row", "");
	check(!cover_index_find(path, "SLUS00507", &r), "single row, other serial", "");

	/* empty file */
	snprintf(path, sizeof path, "%s/index_d.txt", dir);
	write_file(path, "");
	check(!cover_index_find(path, "SLUS00506", &r), "empty file", "");

	/* damaged: a row without a serial in the middle must not find a wrong game */
	snprintf(path, sizeof path, "%s/index_e.txt", dir);
	write_file(path, "SCES00001\tbox/a.jpg\t-\tx\t-\n\t\t\t\t\n\t\n!!!\n");
	check(!cover_index_find(path, "SLUS00003", &r), "damaged index", "");

	/* paths */
	check(!cover_index_path(buf, sizeof buf, "sd:/wiistation/covers", "box/A.jpg") && !strcmp(buf, "sd:/wiistation/covers/box/A.jpg"), "join", buf);
	check(!cover_index_path(buf, sizeof buf, "sd:/c/", "box/A.jpg") && !strcmp(buf, "sd:/c/box/A.jpg"), "join with slash", buf);
	check(cover_index_path(buf, sizeof buf, "sd:/c", "") == -1, "join empty", "");
	check(cover_index_path(buf, 10, "sd:/wiistation/covers", "box/A.jpg") == -1, "join too long", "");

	if (argc > 2) {	/* the whole real index: every row found, in one pass */
		FILE *f = fopen(argv[2], "rb");
		char line[256];
		if (f) {
			while (fgets(line, sizeof line, f)) {
				char s[16];
				size_t k = strcspn(line, "\t");
				if (k == 0 || k > 11) continue;
				memcpy(s, line, k);
				s[k] = 0;
				n++;
				if (!cover_index_find(argv[2], s, &r) || strcmp(r.serial, s)) {
					check(0, "real index row", s);
					if (failures > 10) break;
				}
			}
			fclose(f);
			printf("real index: %d rows looked up\n", n);
			check(n > 0, "real index had rows", "");
		}
	}

	printf("cover_index: %d checks, %d failed\n", checks, failures);
	return failures;
}
