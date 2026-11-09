/* Host test of the Recent record (Gamecube/fileBrowser/recent_list.c).
 *   recent_list_test <scratch dir>        exit status = failed checks */
#include "../Gamecube/fileBrowser/recent_list.h"

#include <stdio.h>
#include <string.h>

static int checks, failed;

#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static recent_entry mk(const char *path, long long t, const char *serial)
{
	recent_entry e;
	memset(&e, 0, sizeof e);
	snprintf(e.path, sizeof e.path, "%s", path);
	recent_src_of(path, e.src, sizeof e.src);
	e.time = t;
	snprintf(e.serial, sizeof e.serial, "%s", serial);
	return e;
}

static void writeFile(const char *file, const char *text)
{
	FILE *f = fopen(file, "wb");
	fputs(text, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	static recent_list l, r;
	char file[300], buf[64];
	recent_entry e;
	int i;
	const char *dir = argc > 1 ? argv[1] : ".";

	snprintf(file, sizeof file, "%s/recent_test.ini", dir);

	/* add: newest first, one entry per path */
	recent_clear(&l);
	e = mk("sd:/isos/A.cue", 100, "SLUS00001");   CHECK(recent_add(&l, &e) == 0);
	e = mk("usb:/isos/B.chd", 200, "SLUS00002");   CHECK(recent_add(&l, &e) == 0);
	e = mk("sd:/isos/C.pbp", 300, "SLUS00003");    CHECK(recent_add(&l, &e) == 0);
	CHECK(l.n == 3);
	CHECK(!strcmp(l.e[0].path, "sd:/isos/C.pbp") && !strcmp(l.e[2].path, "sd:/isos/A.cue"));
	CHECK(!strcmp(l.e[1].src, "usb") && !strcmp(l.e[0].src, "sd"));

	/* move to front: A again (other case, new time, a disc, a set) replaces its old entry */
	e = mk("SD:/ISOS/a.cue", 400, "SLUS00001");
	e.disc = 2;  e.set = RECENT_SET_FOUND;  snprintf(e.hash, sizeof e.hash, "0123456789abcdef0123456789abcdef");
	CHECK(recent_add(&l, &e) == 0);
	CHECK(l.n == 3);
	CHECK(l.e[0].time == 400 && l.e[0].disc == 2 && l.e[0].set == RECENT_SET_FOUND);
	CHECK(!strcmp(l.e[1].path, "sd:/isos/C.pbp") && !strcmp(l.e[2].path, "usb:/isos/B.chd"));
	CHECK(recent_find(&l, "sd:/isos/A.cue") == 0 && recent_find(&l, "sd:/isos/none") == -1);
	e.path[0] = 0;
	CHECK(recent_add(&l, &e) == -1 && l.n == 3);

	/* cap: 25 games leave the newest 20, the oldest gone */
	recent_clear(&l);
	for (i = 0; i < 25; i++) {
		char p[64];
		snprintf(p, sizeof p, "sd:/g%02d.cue", i);
		e = mk(p, 1000 + i, "S");
		recent_add(&l, &e);
	}
	CHECK(l.n == RECENT_MAX);
	CHECK(!strcmp(l.e[0].path, "sd:/g24.cue") && !strcmp(l.e[19].path, "sd:/g05.cue"));
	CHECK(recent_find(&l, "sd:/g04.cue") == -1);
	/* a game already in a full list moves to the front and nothing falls off */
	e = mk("sd:/g10.cue", 9999, "S");
	recent_add(&l, &e);
	CHECK(l.n == RECENT_MAX && !strcmp(l.e[0].path, "sd:/g10.cue") && recent_find(&l, "sd:/g05.cue") == 19);

	/* save and load round trip, every field */
	recent_clear(&l);
	e = mk("sd:/isos/A B (USA).cue", 1762400000LL, "SLUS00506");
	e.disc = 3;  e.set = RECENT_SET_NONE;
	recent_add(&l, &e);
	e = mk("smb:/share/Spyro.chd", 1762300000LL, "SCUS94228");
	e.set = RECENT_SET_FOUND;  snprintf(e.hash, sizeof e.hash, "fedcba9876543210fedcba9876543210");
	recent_add(&l, &e);
	CHECK(recent_save(&l, file) == 0);
	CHECK(recent_load(&r, file) == 2);
	CHECK(!memcmp(&r.e[0], &l.e[0], sizeof r.e[0]) && !memcmp(&r.e[1], &l.e[1], sizeof r.e[1]));
	CHECK(r.e[0].disc == 0 && !strcmp(r.e[0].src, "smb") && r.e[1].set == RECENT_SET_NONE && r.e[1].disc == 3);
	/* a second save over the first (the rename replaces the file) */
	CHECK(recent_save(&l, file) == 0 && recent_load(&r, file) == 2);

	/* a missing file and an unwritable place */
	snprintf(buf, sizeof buf, "%s/no_such_file.ini", dir);
	CHECK(recent_load(&r, buf) == 0 && r.n == 0);
	CHECK(recent_save(&l, "/no/such/folder/recent.ini") == -1);

	/* corrupt files start empty or keep the whole entries only */
	writeFile(file, "");
	CHECK(recent_load(&r, file) == 0);
	writeFile(file, "this is not an ini file\n\x01\x02\xff garbage = = =\n[Game\nPath\n");
	CHECK(recent_load(&r, file) == 0);
	writeFile(file, "[Game]\nSource = sd\nTime = 5\n[Game]\nPath = \"sd:/ok.cue\"\nTime = abc\nSet = banana\nHash = short\n"
	                "[Other]\nPath = \"sd:/not_a_game.cue\"\n[Game]\nPath = \"sd:/ctl\x01.cue\"\n");
	CHECK(recent_load(&r, file) == 1);	/* no path, wrong section and a control character: dropped */
	CHECK(!strcmp(r.e[0].path, "sd:/ok.cue") && r.e[0].time == 0 && r.e[0].set == RECENT_SET_UNKNOWN && !r.e[0].hash[0]);
	/* a very long path is cut, not overrun; a last line without a newline still counts */
	{
		static char big[2000];
		memset(big, 'x', sizeof big - 1);
		big[sizeof big - 1] = 0;
		strcpy(big, "[Game]\nPath = \"sd:/");
		memset(big + strlen(big), 'y', 1500);
		strcpy(big + 19 + 1500, ".cue\"\n[Game]\nPath = \"sd:/last.cue\"");
		writeFile(file, big);
		CHECK(recent_load(&r, file) == 2 && strlen(r.e[0].path) < RECENT_PATH && !strcmp(r.e[1].path, "sd:/last.cue"));
	}
	/* more than 20 entries in a file: the first 20 (newest) */
	{
		FILE *f = fopen(file, "wb");
		for (i = 0; i < 30; i++)
			fprintf(f, "[Game]\nPath = \"sd:/n%02d.cue\"\n", i);
		fclose(f);
		CHECK(recent_load(&r, file) == RECENT_MAX && !strcmp(r.e[0].path, "sd:/n00.cue"));
	}
	remove(file);

	/* an entry on a device that is not there is just an entry: the list neither knows nor drops it */
	recent_clear(&l);
	e = mk("usb:/gone/Game.chd", 50, "SLES00001");
	recent_add(&l, &e);
	CHECK(recent_save(&l, file) == 0 && recent_load(&r, file) == 1 && !strcmp(r.e[0].src, "usb"));
	remove(file);

	/* recent_when: today, yesterday, a date; the console's clock set back */
	{
		const long long now = 1762473600LL + 3600;	/* 2025-11-07 01:00 UTC */
		recent_when(now - 3000, now, buf, sizeof buf);			CHECK(!strcmp(buf, "today"));
		recent_when(now - 7200, now, buf, sizeof buf);			CHECK(!strcmp(buf, "yesterday"));
		recent_when(now - 86400 * 2, now, buf, sizeof buf);		CHECK(!strcmp(buf, "2025-11-05"));
		recent_when(1698796800LL, now, buf, sizeof buf);		CHECK(!strcmp(buf, "2023-11-01"));
		recent_when(951782400LL, now, buf, sizeof buf);			CHECK(!strcmp(buf, "2000-02-29"));
		recent_when(0, now, buf, sizeof buf);					CHECK(!strcmp(buf, "-"));
		recent_when(now + 86400 * 5, now, buf, sizeof buf);		CHECK(!strcmp(buf, "-"));
	}
	recent_src_of("dvd:/x", buf, sizeof buf);					CHECK(!strcmp(buf, "dvd"));
	recent_src_of("no colon", buf, sizeof buf);					CHECK(!buf[0]);
	recent_src_of("averylongdevice:/x", buf, sizeof buf);		CHECK(!buf[0]);

	/* set result, keep_known, state path */
	CHECK(recent_set_result(1, "abc", 5, 0) == RECENT_SET_FOUND);
	CHECK(recent_set_result(1, "abc", 5, 1) == RECENT_SET_FOUND);
	CHECK(recent_set_result(1, "abc", 0, 0) == RECENT_SET_NONE);
	CHECK(recent_set_result(1, "abc", 0, 1) == RECENT_SET_UNKNOWN);
	CHECK(recent_set_result(1, "", 5, 0) == RECENT_SET_UNKNOWN);
	CHECK(recent_set_result(0, "abc", 5, 0) == RECENT_SET_UNKNOWN);
	{
		recent_list kl;  recent_entry k, o;
		recent_clear(&kl);
		o = mk("sd:/k.cue", 10, "SLUS1");  o.set = RECENT_SET_FOUND;  strcpy(o.hash, "h1");
		recent_add(&kl, &o);
		k = mk("sd:/K.cue", 20, "SLUS1");  k.set = RECENT_SET_UNKNOWN;
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_FOUND && !strcmp(k.hash, "h1"));
		k = mk("sd:/other.cue", 20, "SLUS2");
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_UNKNOWN && !k.hash[0]);
		k = mk("sd:/k.cue", 20, "SLUS1");  k.set = RECENT_SET_NONE;
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_NONE);
	}
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/wiistation/savestates/", "SLUS00506", 3) == 0 &&
	      !strcmp(buf, "sd:/wiistation/savestates/SLUS00506.st3"));
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/a/", "", 0) == -1);
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/a/", "X", 10) == -1);
	CHECK(recent_state_path(buf, 8, "sd:", "/wiistation/savestates/", "X", 1) == -1);

	/* set result, keep_known, state path */
	CHECK(recent_set_result(1, "abc", 5, 0) == RECENT_SET_FOUND);
	CHECK(recent_set_result(1, "abc", 5, 1) == RECENT_SET_FOUND);
	CHECK(recent_set_result(1, "abc", 0, 0) == RECENT_SET_NONE);
	CHECK(recent_set_result(1, "abc", 0, 1) == RECENT_SET_UNKNOWN);
	CHECK(recent_set_result(1, "", 5, 0) == RECENT_SET_UNKNOWN);
	CHECK(recent_set_result(0, "abc", 5, 0) == RECENT_SET_UNKNOWN);
	{
		recent_list kl;  recent_entry k, o;
		recent_clear(&kl);
		o = mk("sd:/k.cue", 10, "SLUS1");  o.set = RECENT_SET_FOUND;  strcpy(o.hash, "h1");
		recent_add(&kl, &o);
		k = mk("sd:/K.cue", 20, "SLUS1");  k.set = RECENT_SET_UNKNOWN;
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_FOUND && !strcmp(k.hash, "h1"));
		k = mk("sd:/other.cue", 20, "SLUS2");
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_UNKNOWN && !k.hash[0]);
		k = mk("sd:/k.cue", 20, "SLUS1");  k.set = RECENT_SET_NONE;
		recent_keep_known(&k, &kl);
		CHECK(k.set == RECENT_SET_NONE);
	}
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/wiistation/savestates/", "SLUS00506", 3) == 0 &&
	      !strcmp(buf, "sd:/wiistation/savestates/SLUS00506.st3"));
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/a/", "", 0) == -1);
	CHECK(recent_state_path(buf, sizeof buf, "sd:", "/a/", "X", 10) == -1);
	CHECK(recent_state_path(buf, 8, "sd:", "/wiistation/savestates/", "X", 1) == -1);

	/* the letter filter: cells as the strip's, the list filtered in order, press = toggle */
	{
		static recent_list fl;
		int idx[RECENT_MAX], n;
		recent_clear(&fl);
		e = mk("sd:/isos/Crash Bandicoot 3.cue", 100, "S1");  recent_add(&fl, &e);
		e = mk("usb:/isos/final fantasy VIII.chd", 200, "S2"); recent_add(&fl, &e);
		e = mk("sd:/isos/castlevania.bin", 300, "S3");          recent_add(&fl, &e);
		e = mk("smb:/x/1Xtreme.cue", 400, "S4");                recent_add(&fl, &e);
		e = mk("sd:\\isos\\Croc.cue", 500, "S5");               recent_add(&fl, &e);
		/* list is now Croc, 1Xtreme, castlevania, FF8, Crash (newest first) */
		CHECK(recent_cell("sd:/isos/Crash.cue") == 3 && recent_cell("sd:/isos/crash.cue") == 3);
		CHECK(recent_cell("sd:\\isos\\Zed.cue") == 26 && recent_cell("1Xtreme.cue") == 0);
		CHECK(recent_cell("sd:/isos/") == -1 && recent_cell("") == -1 && recent_cell(NULL) == -1);
		CHECK(recent_cells(&fl) == ((1u << 0) | (1u << 3) | (1u << 6)));
		n = recent_filter_list(&fl, 3, idx, RECENT_MAX);
		CHECK(n == 3 && idx[0] == 0 && idx[1] == 2 && idx[2] == 4);	/* Croc, castlevania, Crash */
		n = recent_filter_list(&fl, 6, idx, RECENT_MAX);
		CHECK(n == 1 && idx[0] == 3);
		n = recent_filter_list(&fl, 5, idx, RECENT_MAX);
		CHECK(n == 0);
		n = recent_filter_list(&fl, -1, idx, RECENT_MAX);
		CHECK(n == 5 && idx[4] == 4);
		n = recent_filter_list(&fl, -1, idx, 2);
		CHECK(n == 2);
		CHECK(recent_filter_press(&fl, -1, 3) == 3);		/* on */
		CHECK(recent_filter_press(&fl, 3, 3) == -1);		/* the same letter again: off */
		CHECK(recent_filter_press(&fl, 3, 6) == 6);			/* another letter with games: switch */
		CHECK(recent_filter_press(&fl, 3, 5) == 3);			/* no game: nothing happens */
		CHECK(recent_filter_press(&fl, -1, 5) == -1);
		CHECK(recent_filter_press(&fl, -1, 0) == 0 && recent_filter_press(&fl, 0, 0) == -1);
		CHECK(recent_filter_press(&fl, 3, -1) == 3 && recent_filter_press(&fl, 3, 27) == 3);
		recent_clear(&fl);
		CHECK(recent_cells(&fl) == 0 && recent_filter_press(&fl, -1, 3) == -1);
	}

	printf("recent_list: %d checks, %d failed\n", checks, failed);
	return failed;
}
