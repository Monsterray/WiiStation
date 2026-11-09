/* The Recent tab's record: the last 20 games loaded, newest first, one shared list in
 * <card>:/wiistation/recent.ini. Each entry keeps what the explorer shows without opening the
 * game: where it was loaded from, when, the disc, the CD serial (CdromId: the save states'
 * name), the RetroAchievements hash and whether a set was found. Plain C, no libogc:
 * tests/recent_list_test.c builds recent_list.c alone.
 *
 * A missing or damaged file is an empty list; a write goes through a temp file and a rename. */
#ifndef RECENT_LIST_H
#define RECENT_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#define RECENT_MAX 20
#define RECENT_PATH 256

enum { RECENT_SET_UNKNOWN = 0, RECENT_SET_FOUND = 1, RECENT_SET_NONE = 2 };

typedef struct {
	char path[RECENT_PATH];	/* the path as it was loaded: "sd:/wiistation/isos/Frogger.cue" */
	char src[8];			/* "sd", "usb", "dvd", "smb" (the tab it came from) */
	int  disc;				/* 1-based disc of a multi-disc file; 0 = a single disc */
	long long time;			/* seconds since 1970 (the console's clock) */
	char serial[12];		/* CdromId ("SLUS00506"); "" for a file without one */
	char hash[33];			/* the RetroAchievements PS1 hash (32 hex); "" not known */
	int  set;				/* RECENT_SET_* */
} recent_entry;

typedef struct {
	recent_entry e[RECENT_MAX];
	int n;
} recent_list;

void recent_clear(recent_list *l);

/* Reads the file into l (cleared first). A missing file gives 0; a damaged one keeps the entries
 * that are whole and drops the rest. Returns the entries read. */
int recent_load(recent_list *l, const char *file);

/* Writes the list (temp file, then rename). 0 on success, -1 when the card cannot be written. */
int recent_save(const recent_list *l, const char *file);

/* Puts e first. The same path (compared without regard to case) is replaced, so a game is in the
 * list once, at the front; beyond RECENT_MAX the oldest falls off. An entry without a path is
 * refused (returns -1); else 0. */
int recent_add(recent_list *l, const recent_entry *e);

/* The index of the entry with this path, or -1. */
int recent_find(const recent_list *l, const char *path);

/* The Recent tab's letter filter. A cell is the letter strip's: 0 = "#", 1..26 = A..Z (the first
 * character of the file name after the last / or \, any case); -1 for a path without a name. */
int recent_cell(const char *path);
/* Bit k set when some entry's cell is k (the strip's lit letters; always of the FULL list). */
unsigned recent_cells(const recent_list *l);
/* The indexes (list order, newest first) of the entries of cell `cell` (all when cell < 0), up to
 * max; returns how many. */
int recent_filter_list(const recent_list *l, int cell, int *idx, int max);
/* The filter after a press on a strip letter: the same letter clears (-1), another letter with
 * games switches to it, a letter with no game (or an invalid cell) leaves `cur` as it was. */
int recent_filter_press(const recent_list *l, int cur, int cell);

/* "today", "yesterday" or "2026-10-28" for time t, by the calendar days of t and now (UTC days:
 * the console keeps no time zone here). A time in the future or 0 is "-". */
void recent_when(long long t, long long now, char *out, unsigned size);

/* The device prefix of a path ("sd:/x" gives "sd") as the entry's src; "" when there is none. */
void recent_src_of(const char *path, char *out, unsigned size);

/* The set result for the disc just loaded: FOUND, NONE or UNKNOWN (see recent_list.c). */
int recent_set_result(int ra_on, const char *hash, unsigned count, int loading);

/* An entry whose set is UNKNOWN takes hash and set from the older entry for the same path. */
void recent_keep_known(recent_entry *e, const recent_list *l);

/* "<dev><statespath><serial>.st<slot>" into out; 0 ok, -1 empty serial / bad slot / too long. */
int recent_state_path(char *out, unsigned size, const char *dev, const char *statespath,
                      const char *serial, int slot);


/* The set result for the disc just loaded: FOUND, NONE or UNKNOWN (see recent_list.c). */
int recent_set_result(int ra_on, const char *hash, unsigned count, int loading);

/* An entry whose set is UNKNOWN takes hash and set from the older entry for the same path. */
void recent_keep_known(recent_entry *e, const recent_list *l);

/* "<dev><statespath><serial>.st<slot>" into out; 0 ok, -1 empty serial / bad slot / too long. */
int recent_state_path(char *out, unsigned size, const char *dev, const char *statespath,
                      const char *serial, int slot);

#ifdef __cplusplus
}
#endif

#endif
