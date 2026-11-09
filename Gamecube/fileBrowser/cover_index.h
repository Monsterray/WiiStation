/* The cover pack's index (scripts/cover_pack.py): <device>:/wiistation/covers/index.txt, tab separated,
 * sorted by serial, one row per serial that has a picture:
 *     SERIAL <TAB> box path <TAB> disc path <TAB> source <TAB> 3d path        ('-' = none)
 * Paths are relative to the covers folder ("box/SLUS00506.jpg"). Serials are CdromId form, upper
 * case, no dash ("SLUS00506"). Plain C, no Wii calls, so the host test builds it as it is. */
#ifndef COVER_INDEX_H
#define COVER_INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#define COVER_PATH_MAX 48

typedef struct {
	char serial[12];
	char box[COVER_PATH_MAX];	/* "" = none */
	char disc[COVER_PATH_MAX];
	char art3d[COVER_PATH_MAX];
} cover_row;

/* Finds serial (any case, a dash is ignored) in the index file at path by binary search on the
 * sorted lines: the file is read a few lines at a time, never loaded. 1: *out is filled; 0: no such
 * serial, or no file, or the file is not sorted (a damaged index finds nothing rather than the
 * wrong game). */
int cover_index_find(const char *index_path, const char *serial, cover_row *out);

/* Joins "<dir>/<rel>" into out. 0 ok; -1 when rel is empty or the result does not fit. */
int cover_index_path(char *out, int outsize, const char *dir, const char *rel);

#ifdef __cplusplus
}
#endif
#endif
