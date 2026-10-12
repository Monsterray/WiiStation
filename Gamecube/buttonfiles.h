/* buttonfiles.h - where the button mapping files are, and which device they are read from.
 *
 * Plain C, no Wii calls, so tests/button_files_test.c can build it on the host.
 * Layout and migration: Docs/BUTTON_CONFIGS.md.
 */
#ifndef BUTTONFILES_H
#define BUTTONFILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each function writes a path into out and returns 1, or returns 0 when the path does not fit
 * or the identifier is unknown. "dev" is "sd" or "usb". */

/* A type's slot file: "<dev>:/wiistation/buttons/<name>", or the old "<dev>:/wiistation/controlX.cfg"
 * when old is 1. id is the controller's identifier letter (G, H, C, N, W, P, D). */
int button_slot_path(char *out, size_t size, const char *dev, char id, int old);

/* A game's file: "<dev>:/wiistation/buttons/games/<cdrom_id>.cfg", or the old
 * "<dev>:/wiistation/<cdrom_id>.btn" when old is 1. */
int button_game_path(char *out, size_t size, const char *dev, const char *cdrom_id, int old);

/* Reading: out gets the new name when file_exists reports it, else the old name. Returns 1 when
 * the file is there under either name, 0 when neither is. */
int button_slot_read(char *out, size_t size, const char *dev, char id, int (*file_exists)(const char *path));
int button_game_read(char *out, size_t size, const char *dev, const char *cdrom_id,
                     int (*file_exists)(const char *path));

/* The device the other one: "usb" for "sd", "sd" for "usb". */
const char *button_other_device(const char *dev);

/* The device to read a set of slot files from. ids holds one identifier per type on this build.
 * The preferred device when it has every file, else the other one when it has every file, else
 * the one with more files (the preferred device on a tie). One set is never a mix of devices. */
const char *button_read_device(const char *preferred, const char *ids, int (*file_exists)(const char *path));

#ifdef __cplusplus
}
#endif

#endif
