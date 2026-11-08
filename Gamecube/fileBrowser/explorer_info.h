/* What the explorer shows about a file without opening the game: the format tag (by extension),
 * the region (from the tags in the name) and the disc count (from a header or a playlist).
 * Plain C, no libogc: the host test (tests/explorer_info_test.c) builds this file alone. */
#ifndef EXPLORER_INFO_H
#define EXPLORER_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* "CHD", "CUE", "PBP", ... for the extension of `name` (a path is fine), or NULL for any other file.
 * The string is static. */
const char *ei_format(const char *name);

/* Region from the tags of a file name: "(USA)", "(Europe)", "(Japan)", "(USA, Canada)", "[U]",
 * "[NTSC-J]", "[SLUS-01296]". Countries are joined as written ("USA, Europe"). Language lists such
 * as "(En,Fr,De)" are not regions. False (and out[0] = 0) when the name has no region tag. */
int ei_region(const char *name, char *out, unsigned outSize);

/* True for the formats whose disc count needs the file read: PBP (header) and M3U (playlist).
 * Every other format is one disc (a CUE with many tracks is still one disc). */
int ei_wants_disc_count(const char *name);

/* Reads `n` bytes at `off`; returns the bytes read, or < 0. */
typedef int (*ei_read_fn)(void *ctx, unsigned long off, void *buf, unsigned n);

/* Discs in a PBP: 1 for a single image, 2..8 for a multi-disc EBOOT (the same table cdriso.c
 * reads); 0 when the file is not a readable PBP. At most three small reads. */
int ei_pbp_discs(ei_read_fn rd, void *ctx);

/* Discs in an M3U playlist: its lines that are not empty and not "#" comments. `text` need not
 * end with NUL; `n` bytes are used. */
int ei_m3u_discs(const char *text, unsigned n);

/* ---- The facts strip's texts (all pure: the frame reads the files and passes what it learned) ---- */

/* "CHD", or "PBP, 4 discs" for 2 or more discs; "-" for a file ei_format does not know. */
void ei_fact_format(const char *name, int discs, char *out, unsigned outSize);

/* A size as "512 B", "640 KB", "242 MB", "1.1 GB" (binary units, whole numbers up to MB, one
 * decimal for GB); "-" for 0 (not known). */
void ei_fact_size(unsigned size, char *out, unsigned outSize);

/* The region from the name's tags (ei_region), or "Unknown". */
void ei_fact_region(const char *name, char *out, unsigned outSize);

/* Save states: "None", "1 state", "5 states"; "-" when n < 0 (the game's serial is not known). */
void ei_fact_saves(int n, char *out, unsigned outSize);

/* Achievements: set 1 = "Set found", 2 = "None", anything else "-" (the game was never loaded, so
 * nothing is known; slice 6 fills 1 and 2 from the Recent record). */
void ei_fact_ach(int set, char *out, unsigned outSize);

/* ---- Edge scroll: the list moves by one row instead of wrapping ---- */

/* The new first row of a paged list when the focus presses `dir` (+1 Down, -1 Up) on window slot
 * `slotIdx` (0..slots-1) of a window starting at item `top`, over `count` items; -1 when the press is
 * not an edge scroll (the engine's own link applies: a wrap at the true end, Up to the tab row at
 * the true top). Down on the last slot scrolls while items remain below; Up on slot 0 while items
 * remain above. */
int ei_edge_top(int top, int slots, int count, int slotIdx, int dir);

/* ---- The first game inside a folder (the facts strip of a folder row) ---- */

#define EI_PATH_MAX 256

/* One entry of a folder as the search sees it. `path` is the whole path; it stays valid until the
 * next call of the list function. "." and ".." are left out by the list function. */
typedef struct { const char *path; int isDir; unsigned size; } ei_ent;

/* Lists `dir` into out[0..max-1], in the order the explorer shows, and returns the count (more
 * than max entries are cut), or < 0 when the folder cannot be read. Called with dir == NULL at the
 * end of a search so the caller can free what it kept. */
typedef int (*ei_list_fn)(void *ctx, const char *dir, ei_ent *out, int max);

/* Looks for the first game inside `dir` (a file ei_format knows), breadth first: the folder's own
 * files, then its sub-folders in order, to `maxDepth` levels below it. At most `budget` entries are
 * looked at and `maxReads` folders are listed in all, so the search ends quickly even on a slow
 * source. In one folder a BIN is taken only if no other game format is there (a CUE's tracks are
 * not the game). On success returns 1 with the game's path in `path` and its listed size in
 * *size; 0 when none was found (or a limit was reached). */
int ei_find_game(ei_list_fn ls, void *ctx, const char *dir, int maxDepth, int budget, int maxReads,
                 char *path, unsigned pathSize, unsigned *size);

#ifdef __cplusplus
}
#endif

#endif
