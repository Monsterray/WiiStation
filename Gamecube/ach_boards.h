/* ach_boards.h -- this Wii's leaderboard scores of one game (Docs/ACHIEVEMENTS.md, "Local leaderboards").
 * Header only, no libogc: the host tests include it.
 *
 * One binary file per game, sd:/wiistation/achievements/boards/<game id>.lb. All numbers are
 * big endian, written and read field by field:
 *   header, 12 bytes:  "WLB1" | u16 version (1) | u16 record size (28) | u32 record count
 *   record, 28 bytes:  u32 board id | 16 bytes Mii key (ASCII hex; all zero = Guest) | s32 best value | u32 date
 * One record per (board id, Mii key): only a Mii's best. The records are in the order they were
 * first written: a new best overwrites its record in place, a new pair appends one. The whole file
 * is read once at the game load and kept as it is (a MEM2 block on the Wii); during play only the
 * records that changed are written (one seek + 28 bytes each, then the 4-byte count when the file
 * grew). The count is written after the records, so a write cut short leaves a file that reads:
 * a record the count does not cover is ignored, and the next append writes over it. Sorting is
 * done on request (ach_boards_top), never while playing. */
#ifndef WIISTATION_ACH_BOARDS_H
#define WIISTATION_ACH_BOARDS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HW_RVL
#include <stdbool.h>
#include "../mem2_manager.h"
#define ACH_LB_ALLOC(n) _mem2_malloc(n)
#define ACH_LB_REALLOC(p, n) _mem2_realloc(p, n)
#define ACH_LB_FREE(p) _mem2_free(p)
#else
#define ACH_LB_ALLOC(n) malloc(n)
#define ACH_LB_REALLOC(p, n) realloc(p, n)
#define ACH_LB_FREE(p) free(p)
#endif

#define ACH_LB_VERSION 1
#define ACH_LB_HEADER 12
#define ACH_LB_REC 28
#define ACH_LB_KEY 16
#define ACH_LB_MAX_RECORDS 200000   /* 5.6 MB: far beyond 100 boards x 300 Miis (30000, 840 KB) */
#define ACH_LB_DIRTY 32             /* records waiting to be written; a full list is written at once */

typedef struct {
    char path[128];
    unsigned char *rec;       /* n records, as in the file */
    unsigned n, cap;          /* in memory; room for */
    unsigned file_n;          /* records the file holds and counts */
    unsigned dirty[ACH_LB_DIRTY], ndirty;
    int bad;                  /* the file is not ours (another version or format): never written */
} ach_boards_t;

typedef struct {
    uint32_t board;
    char key[ACH_LB_KEY];     /* not terminated; all zero = Guest */
    int32_t value;
    uint32_t date;            /* seconds since 1970 */
} ach_board_score_t;

static inline void ach_lb_put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static inline uint32_t ach_lb_get32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline void ach_lb_header(unsigned char *h, uint32_t count)
{
    memcpy(h, "WLB1", 4);
    h[4] = 0;
    h[5] = ACH_LB_VERSION;
    h[6] = 0;
    h[7] = ACH_LB_REC;
    ach_lb_put32(h + 8, count);
}

static inline void ach_lb_get(const unsigned char *r, ach_board_score_t *s)
{
    s->board = ach_lb_get32(r);
    memcpy(s->key, r + 4, ACH_LB_KEY);
    s->value = (int32_t)ach_lb_get32(r + 20);
    s->date = ach_lb_get32(r + 24);
}

/* A Mii's key as the file has it: the hex text, or zeros for Guest ("") */
static inline void ach_lb_key(char out[ACH_LB_KEY], const char *key)
{
    unsigned n = 0;
    memset(out, 0, ACH_LB_KEY);
    while (key && n < ACH_LB_KEY && key[n]) n++;
    if (n) memcpy(out, key, n);
}

/* a ranks before b: the better value, then the earlier date, then the key */
static inline int ach_lb_before(const ach_board_score_t *a, const ach_board_score_t *b, int lower_is_better)
{
    if (a->value != b->value) return lower_is_better ? a->value < b->value : a->value > b->value;
    if (a->date != b->date) return a->date < b->date;
    return memcmp(a->key, b->key, ACH_LB_KEY) < 0;
}

/* Reads the file at path (one fread). No file, or a header cut short: an empty table, the file
 * is made at the first write. Returns 0 when the file is not ours: the table stays empty and
 * nothing is ever written over it. */
static inline int ach_boards_open(ach_boards_t *b, const char *path)
{
    unsigned char h[ACH_LB_HEADER];
    unsigned count, whole;
    long size;
    FILE *f;
    memset(b, 0, sizeof *b);
    snprintf(b->path, sizeof b->path, "%s", path);
    if (!(f = fopen(path, "rb"))) return 1;
    if (fread(h, 1, ACH_LB_HEADER, f) != ACH_LB_HEADER) {   /* made, but the header never finished */
        fclose(f);
        return 1;
    }
    if (memcmp(h, "WLB1", 4) || h[5] != ACH_LB_VERSION || h[4] || h[6] || h[7] != ACH_LB_REC) {
        fclose(f);
        b->bad = 1;
        return 0;
    }
    count = ach_lb_get32(h + 8);
    whole = fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= ACH_LB_HEADER ? (unsigned)((size - ACH_LB_HEADER) / ACH_LB_REC) : 0;
    if (count > whole) count = whole;    /* a record cut short at the end is not one */
    if (count > ACH_LB_MAX_RECORDS) count = ACH_LB_MAX_RECORDS;
    b->cap = count + 64;
    if (!(b->rec = ACH_LB_ALLOC((size_t)b->cap * ACH_LB_REC))) {
        fclose(f);
        b->cap = 0;
        b->bad = 1;
        return 0;
    }
    if (fseek(f, ACH_LB_HEADER, SEEK_SET) == 0)
        b->n = (unsigned)(fread(b->rec, ACH_LB_REC, count, f));
    fclose(f);
    b->file_n = b->n;
    return 1;
}

/* Writes what changed: the records first, then the count. 1 when nothing is left to write. */
static inline int ach_boards_flush(ach_boards_t *b)
{
    unsigned char h[ACH_LB_HEADER], num[4];
    unsigned i;
    FILE *f;
    if (!b->ndirty) return 1;
    if (b->bad) return 0;
    if (!(f = fopen(b->path, "r+b")) && !(f = fopen(b->path, "w+b"))) return 0;
    if (!b->file_n) {   /* a new file, or one whose header was cut short */
        ach_lb_header(h, 0);
        if (fseek(f, 0, SEEK_SET) || fwrite(h, 1, ACH_LB_HEADER, f) != ACH_LB_HEADER) goto fail;
    }
    for (i = 0; i < b->ndirty; i++)
        if (fseek(f, (long)(ACH_LB_HEADER + (size_t)b->dirty[i] * ACH_LB_REC), SEEK_SET) ||
            fwrite(b->rec + (size_t)b->dirty[i] * ACH_LB_REC, 1, ACH_LB_REC, f) != ACH_LB_REC)
            goto fail;
    if (b->n > b->file_n) {
        ach_lb_put32(num, b->n);
        if (fflush(f) || fseek(f, 8, SEEK_SET) || fwrite(num, 1, 4, f) != 4) goto fail;
    }
    if (fclose(f)) return 0;
    b->file_n = b->n;
    b->ndirty = 0;
    return 1;
fail:
    fclose(f);
    return 0;   /* the records stay in the list: the next flush writes them again */
}

static inline void ach_boards_close(ach_boards_t *b)
{
    ach_boards_flush(b);
    if (b->rec) ACH_LB_FREE(b->rec);
    memset(b, 0, sizeof *b);
}

/* The index of the record of (board, key), or -1 */
static inline int ach_boards_find(const ach_boards_t *b, uint32_t board, const char key[ACH_LB_KEY])
{
    unsigned i;
    for (i = 0; i < b->n; i++) {
        const unsigned char *r = b->rec + (size_t)i * ACH_LB_REC;
        if (ach_lb_get32(r) == board && !memcmp(r + 4, key, ACH_LB_KEY)) return (int)i;
    }
    return -1;
}

/* A score of key (a Mii's hex key, "" for Guest) on a board. 1: it is the Mii's new best (kept,
 * written at the next flush); 0: not better than the one it has; -1: not kept (no room, or the
 * file is not ours). Never writes the file, apart from a full list of changes. */
static inline int ach_boards_record(ach_boards_t *b, uint32_t board, const char *key, int32_t value,
                                    int lower_is_better, uint32_t date)
{
    char k[ACH_LB_KEY];
    unsigned char *r;
    unsigned i, idx;
    int found;
    if (b->bad || (b->ndirty == ACH_LB_DIRTY && !ach_boards_flush(b))) return -1;   /* a full list that cannot be written: no room */
    ach_lb_key(k, key);
    found = ach_boards_find(b, board, k);
    if (found >= 0) {
        int32_t old = (int32_t)ach_lb_get32(b->rec + (size_t)found * ACH_LB_REC + 20);
        if (lower_is_better ? value >= old : value <= old) return 0;
        idx = (unsigned)found;
    } else {
        if (b->n >= ACH_LB_MAX_RECORDS) return -1;
        if (b->n == b->cap) {
            unsigned cap = b->cap ? b->cap * 2 : 64;
            unsigned char *p = ACH_LB_REALLOC(b->rec, (size_t)cap * ACH_LB_REC);
            if (!p) return -1;
            b->rec = p;
            b->cap = cap;
        }
        idx = b->n++;
    }
    r = b->rec + (size_t)idx * ACH_LB_REC;
    ach_lb_put32(r, board);
    memcpy(r + 4, k, ACH_LB_KEY);
    ach_lb_put32(r + 20, (uint32_t)value);
    ach_lb_put32(r + 24, date);
    for (i = 0; i < b->ndirty && b->dirty[i] != idx; i++) ;
    if (i == b->ndirty) b->dirty[b->ndirty++] = idx;
    if (b->ndirty == ACH_LB_DIRTY) ach_boards_flush(b);
    return 1;
}

/* A board's best `max` scores into out, best first; returns how many Miis have a score on it.
 * One pass over the records and a short insertion: no allocation, no sort of the whole table. */
static inline unsigned ach_boards_top(const ach_boards_t *b, uint32_t board, int lower_is_better,
                                      ach_board_score_t *out, unsigned max)
{
    unsigned i, total = 0, kept = 0, j;
    for (i = 0; i < b->n; i++) {
        ach_board_score_t s;
        const unsigned char *r = b->rec + (size_t)i * ACH_LB_REC;
        if (ach_lb_get32(r) != board) continue;
        total++;
        ach_lb_get(r, &s);
        if (kept == max && !ach_lb_before(&s, &out[max - 1], lower_is_better)) continue;
        for (j = kept < max ? kept : max - 1; j > 0 && ach_lb_before(&s, &out[j - 1], lower_is_better); j--)
            out[j] = out[j - 1];
        out[j] = s;
        if (kept < max) kept++;
    }
    return total;
}

/* The rank (1 = best) of a score among its board's */
static inline unsigned ach_boards_rank(const ach_boards_t *b, const ach_board_score_t *s, int lower_is_better)
{
    unsigned i, rank = 1;
    for (i = 0; i < b->n; i++) {
        ach_board_score_t o;
        const unsigned char *r = b->rec + (size_t)i * ACH_LB_REC;
        if (ach_lb_get32(r) != s->board) continue;
        ach_lb_get(r, &o);
        if (ach_lb_before(&o, s, lower_is_better)) rank++;
    }
    return rank;
}

#endif
