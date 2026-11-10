/* ach_account.h -- which account file a game load logs in with (Docs/ACHIEVEMENTS.md, Players and Miis step 4a), and
 * how the RetroAchievements set's Local journal is named (4c). Plain C, host-tested in
 * tests/ach_account_test.c. Only names: nothing here opens a file. */
#ifndef ACH_ACCOUNT_H
#define ACH_ACCOUNT_H

#include <stdio.h>
#include <string.h>

#define ACH_ACCOUNT_DIR "sd:/wiistation/achievements/"
#define ACH_ACCOUNT_WII ACH_ACCOUNT_DIR "ra_account.ini"   /* This Wii's account */
#define ACH_ACCOUNT_NAME_MAX 32                            /* with the NUL: players.h PLAYERS_ACCOUNT */

enum { ACH_ACCOUNT_THIS_WII = 0, ACH_ACCOUNT_NAMED = 1, ACH_ACCOUNT_BAD = -1 };

/* A link's name is a file name in achievements/ra/ without ".ini": no path, no drive, no
 * character a FAT card cannot have, not hidden */
static inline int ach_account_name_ok(const char *name)
{
	size_t i, n = name ? strlen(name) : 0;
	if (!n || n >= ACH_ACCOUNT_NAME_MAX || name[0] == '.')
		return 0;
	for (i = 0; i < n; i++)
		if ((unsigned char)name[i] < 0x20 || strchr("/\\:*?\"<>|", name[i]))
			return 0;
	return 1;
}

/* The account file of a Mii's link (players.h players_account): "" (no link, the Official seat
 * without one) and "*" are this Wii's ra_account.ini, a name is achievements/ra/<name>.ini. A
 * name that is no file name is ACH_ACCOUNT_BAD, and the path is empty. */
static inline int ach_account_path(const char *link, char *path, size_t size)
{
	int n;
	if (!link || !*link || !strcmp(link, "*")) {
		n = snprintf(path, size, "%s", ACH_ACCOUNT_WII);
		return n > 0 && (size_t)n < size ? ACH_ACCOUNT_THIS_WII : ACH_ACCOUNT_BAD;
	}
	if (ach_account_name_ok(link)) {
		n = snprintf(path, size, ACH_ACCOUNT_DIR "ra/%s.ini", link);
		if (n > 0 && (size_t)n < size)
			return ACH_ACCOUNT_NAMED;
	}
	if (size)
		path[0] = 0;
	return ACH_ACCOUNT_BAD;
}

/* The journal digest of a RetroAchievements set (the field a definition file's MD5 has in the
 * Local journal): "5241" (ASCII "RA"), zeros, the game's id as 8 hex digits. 32 characters, so
 * an old reader takes it for a digest and never mistakes it for an MD5 (no file hashes so). */
static inline void ach_ra_digest(unsigned game_id, char out[33])
{
	snprintf(out, 33, "5241%020x%08x", 0u, game_id);
}

/* One journal file per game: achievements/ra_local/<game id>.unlocks (a game's file stays
 * far below the journal's 1 MiB cap, whatever the other games earned) */
static inline int ach_ra_journal_path(unsigned game_id, char *path, size_t size)
{
	int n = snprintf(path, size, ACH_ACCOUNT_DIR "ra_local/%u.unlocks", game_id);
	return n > 0 && (size_t)n < size;
}

/* One leaderboard file per game: achievements/boards/<game id>.lb (Gamecube/ach_boards.h) */
static inline int ach_boards_path(unsigned game_id, char *path, size_t size)
{
	int n = snprintf(path, size, ACH_ACCOUNT_DIR "boards/%u.lb", game_id);
	return n > 0 && (size_t)n < size;
}

#endif
