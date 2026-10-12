/* buttonfiles.c - names and paths of the button mapping files, and the read-device choice.
 *
 * Plain C, no Wii calls. tests/button_files_test.c builds it on the host.
 */
#include <stdio.h>
#include <string.h>

#include "buttonfiles.h"

/* One row per controller type. id is the letter in each driver's controller_t. */
static const struct {
	char id;
	const char *name;   /* in wiistation/buttons/ */
	const char *old;    /* in wiistation/, the name before this change */
} TYPES[] = {
	{ 'G', "gamecube.cfg",        "controlG.cfg" },   /* GameCube pad */
	{ 'H', "usb-hid.cfg",         "controlH.cfg" },   /* USB HID gamepad */
	{ 'C', "classic.cfg",         "controlC.cfg" },   /* Classic Controller */
	{ 'N', "wiimote-nunchuk.cfg", "controlN.cfg" },   /* Wiimote with Nunchuk */
	{ 'W', "wiimote.cfg",         "controlW.cfg" },   /* Wiimote alone */
	{ 'P', "wiiu-pro.cfg",        "controlP.cfg" },   /* Wii U Pro Controller */
	{ 'D', "wiiu-gamepad.cfg",    "controlD.cfg" },   /* Wii U GamePad */
};

/* Writes "<dev>:/wiistation/<rest>" into out. */
static int put(char *out, size_t size, const char *dev, const char *rest)
{
	return snprintf(out, size, "%s:/wiistation/%s", dev, rest) < (int)size;
}

int button_slot_path(char *out, size_t size, const char *dev, char id, int old)
{
	char rest[64];
	size_t i;
	for (i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) {
		if (TYPES[i].id != id)
			continue;
		snprintf(rest, sizeof rest, "%s%s", old ? "" : "buttons/", old ? TYPES[i].old : TYPES[i].name);
		return put(out, size, dev, rest);
	}
	return 0;
}

int button_game_path(char *out, size_t size, const char *dev, const char *cdrom_id, int old)
{
	char rest[96];
	if (old)
		snprintf(rest, sizeof rest, "%s.btn", cdrom_id);
	else
		snprintf(rest, sizeof rest, "buttons/games/%s.cfg", cdrom_id);
	return put(out, size, dev, rest);
}

int button_slot_read(char *out, size_t size, const char *dev, char id, int (*file_exists)(const char *path))
{
	if (!button_slot_path(out, size, dev, id, 0))
		return 0;
	if (file_exists(out))
		return 1;
	if (!button_slot_path(out, size, dev, id, 1))
		return 0;
	return file_exists(out);
}

int button_game_read(char *out, size_t size, const char *dev, const char *cdrom_id,
                     int (*file_exists)(const char *path))
{
	if (!button_game_path(out, size, dev, cdrom_id, 0))
		return 0;
	if (file_exists(out))
		return 1;
	if (!button_game_path(out, size, dev, cdrom_id, 1))
		return 0;
	return file_exists(out);
}

const char *button_other_device(const char *dev)
{
	return strcmp(dev, "usb") == 0 ? "sd" : "usb";
}

/* How many of the types in ids have a slot file on dev, under either name. */
static int count_found(const char *dev, const char *ids, int (*file_exists)(const char *path))
{
	char path[256];
	int found = 0;
	for (; *ids; ids++)
		found += button_slot_read(path, sizeof path, dev, *ids, file_exists);
	return found;
}

const char *button_read_device(const char *preferred, const char *ids, int (*file_exists)(const char *path))
{
	const char *other = button_other_device(preferred);
	int n = (int)strlen(ids);
	int p = count_found(preferred, ids, file_exists);
	int o = count_found(other, ids, file_exists);
	if (p == n)
		return preferred;
	if (o == n)
		return other;
	return o > p ? other : preferred;
}
