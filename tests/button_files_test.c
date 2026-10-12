/* button_files_test.c - the button mapping file names, the migration from the old names, and
 * the choice of the device a set is read from. Runs on the host with a fake file system.
 *
 *     cc -o button_files_test tests/button_files_test.c Gamecube/buttonfiles.c && ./button_files_test
 *
 * Exit 0 when every check passes. The Wii's own file calls are in GamecubeMain.cpp, not here.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../Gamecube/buttonfiles.h"

/* The files that exist, as full paths. */
static char fake[64][256];
static int nfake;

static int fake_exists(const char *path)
{
	int i;
	for (i = 0; i < nfake; i++)
		if (strcmp(fake[i], path) == 0)
			return 1;
	return 0;
}

static void add(const char *path)
{
	assert(nfake < 64);
	snprintf(fake[nfake++], sizeof fake[0], "%s", path);
}

static void add_slot(const char *dev, char id, int old)
{
	char path[256];
	assert(button_slot_path(path, sizeof path, dev, id, old));
	add(path);
}

static void check_names(void)
{
	char p[256];
	assert(button_slot_path(p, sizeof p, "sd", 'G', 0));
	assert(strcmp(p, "sd:/wiistation/buttons/gamecube.cfg") == 0);
	assert(button_slot_path(p, sizeof p, "usb", 'N', 0));
	assert(strcmp(p, "usb:/wiistation/buttons/wiimote-nunchuk.cfg") == 0);
	assert(button_slot_path(p, sizeof p, "sd", 'P', 1));
	assert(strcmp(p, "sd:/wiistation/controlP.cfg") == 0);
	assert(!button_slot_path(p, sizeof p, "sd", 'X', 0));   /* unknown identifier */

	assert(button_game_path(p, sizeof p, "usb", "SLUS-00001", 0));
	assert(strcmp(p, "usb:/wiistation/buttons/games/SLUS-00001.cfg") == 0);
	assert(button_game_path(p, sizeof p, "sd", "SLUS-00001", 1));
	assert(strcmp(p, "sd:/wiistation/SLUS-00001.btn") == 0);

	assert(strcmp(button_other_device("sd"), "usb") == 0);
	assert(strcmp(button_other_device("usb"), "sd") == 0);
}

static void check_migration(void)
{
	char p[256];
	/* Only the old name exists: it is read. */
	nfake = 0;
	add("sd:/wiistation/controlG.cfg");
	assert(button_slot_read(p, sizeof p, "sd", 'G', fake_exists) == 1);
	assert(strcmp(p, "sd:/wiistation/controlG.cfg") == 0);

	/* Both names exist: the new one wins. */
	add("sd:/wiistation/buttons/gamecube.cfg");
	assert(button_slot_read(p, sizeof p, "sd", 'G', fake_exists) == 1);
	assert(strcmp(p, "sd:/wiistation/buttons/gamecube.cfg") == 0);

	/* Neither exists: 0. */
	nfake = 0;
	assert(button_slot_read(p, sizeof p, "sd", 'H', fake_exists) == 0);

	/* Game file: same rule. */
	nfake = 0;
	add("usb:/wiistation/SLUS-00001.btn");
	assert(button_game_read(p, sizeof p, "usb", "SLUS-00001", fake_exists) == 1);
	assert(strcmp(p, "usb:/wiistation/SLUS-00001.btn") == 0);
}

static void check_device(void)
{
	int i;
	const char *all = "GHCNWPD";
	/* Preferred device has every file: it is used. */
	nfake = 0;
	for (i = 0; all[i]; i++) add_slot("sd", all[i], 0);
	assert(strcmp(button_read_device("sd", all, fake_exists), "sd") == 0);

	/* Preferred device has none, the other has every file (old names): the other is used whole. */
	nfake = 0;
	for (i = 0; all[i]; i++) add_slot("usb", all[i], 1);
	assert(strcmp(button_read_device("sd", all, fake_exists), "usb") == 0);

	/* Both partial: the one with more files. */
	nfake = 0;
	for (i = 0; i < 3; i++) add_slot("sd", all[i], 0);
	for (i = 0; i < 5; i++) add_slot("usb", all[i], 0);
	assert(strcmp(button_read_device("sd", all, fake_exists), "usb") == 0);
	assert(strcmp(button_read_device("usb", all, fake_exists), "usb") == 0);

	/* Tie: the preferred device. */
	nfake = 0;
	for (i = 0; i < 4; i++) add_slot("sd", all[i], 0);
	for (i = 0; i < 4; i++) add_slot("usb", all[i], 0);
	assert(strcmp(button_read_device("usb", all, fake_exists), "usb") == 0);

	/* Nothing anywhere: the preferred device, so a first save goes where the user chose. */
	nfake = 0;
	assert(strcmp(button_read_device("usb", all, fake_exists), "usb") == 0);

	/* A GameCube-only build reads one type. */
	nfake = 0;
	add_slot("usb", 'G', 0);
	assert(strcmp(button_read_device("sd", "G", fake_exists), "usb") == 0);
}

int main(void)
{
	check_names();
	check_migration();
	check_device();
	printf("button_files_test: all checks pass\n");
	return 0;
}
