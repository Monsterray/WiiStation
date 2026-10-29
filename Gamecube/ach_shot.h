/* The unlock pictures (ach_shot.c): when an achievement unlocks, the game's frame is saved as
 * sd:/wiistation/achievements/shots/<set id>/<achievement id>.png, once per achievement on this
 * console (an existing file is never replaced). The book (AchievementsFrame) shows them. The
 * naming helpers are plain C, host-tested in tests/ach_progress_test.c. */
#ifndef ACH_SHOT_H
#define ACH_SHOT_H

#include <stdio.h>
#include <string.h>

#define ACH_SHOT_DIR "sd:/wiistation/achievements/shots/"

/* A set id names a folder: 1-23 characters of 0-9 a-f (a game's decimal id, or a digest's hex) */
static inline int ach_shot_set_ok(const char *set)
{
	size_t i, n = set ? strlen(set) : 0;
	if (!n || n > 23)
		return 0;
	for (i = 0; i < n; i++)
		if (!((set[i] >= '0' && set[i] <= '9') || (set[i] >= 'a' && set[i] <= 'f')))
			return 0;
	return 1;
}

/* The picture's file; 0 when the set id is no folder name (and path is empty) */
static inline int ach_shot_path(const char *set, unsigned id, char *path, size_t size)
{
	int n;
	if (size)
		path[0] = 0;
	if (!ach_shot_set_ok(set))
		return 0;
	n = snprintf(path, size, ACH_SHOT_DIR "%s/%u.png", set, id);
	if (n <= 0 || (size_t)n >= size) {
		if (size)
			path[0] = 0;
		return 0;
	}
	return 1;
}

#ifdef __cplusplus
extern "C" {
#endif

/* An achievement unlocked: take the picture now (emulator thread, at a vblank). The file is
 * made later, off the emulated frame. Does nothing for an empty set id (Test mode), a
 * picture the console has already, or when the memory for it is not there. */
void ach_shot_request(const char *set, unsigned id);

/* Once a vblank (psxcounters.c): a slice of the compression of the oldest waiting picture */
void ach_shot_tick(void);

/* The game stopped (the menu opens): finishes every waiting picture now, so the book finds it */
void ach_shot_flush(void);

#ifdef __cplusplus
}
#endif
#endif
