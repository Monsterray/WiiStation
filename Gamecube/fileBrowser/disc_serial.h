/* disc_serial.h -- a game file's PlayStation serial ("SCUS94423", the CdromId form the cover pack is
 * keyed by), for the explorer's cover. Two ways, cheapest first:
 *   ds_name_serial: from the names ("Game [SCUS-94423].cue", or its folder's name); no read.
 *   ds_read_serial: from the disc itself: SYSTEM.CNF's BOOT line, read through the one image reader
 *     that fits the file (cue/bin/iso/img with the sector layout found by looking, ccd, mds, toc,
 *     m3u -> its first line, PBP: PARAM.SFO's DISC_ID, else the first disc's sectors; CHD on the Wii).
 *     It opens its own handle and keeps nothing: the game being played is not touched.
 * Plain C: the host test (tests/disc_serial_test.c) runs the same code as the Wii. */
#ifndef DISC_SERIAL_H
#define DISC_SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SERIAL_MAX 12

/* 1 and *out = the serial, or 0 when no name carries one */
int ds_name_serial(const char *path, char out[DS_SERIAL_MAX]);

/* 1 and *out = the serial read from the disc, or 0 (no such file, a format not read here, no
 * SYSTEM.CNF, a PS-X EXE disc) */
int ds_read_serial(const char *path, char out[DS_SERIAL_MAX]);

/* The image-independent part: a reader of one raw sector frame (the 2048 data bytes sit at some
 * offset in it) and the walk from the volume descriptor to the BOOT line. rd returns the bytes it
 * put in frame (at most DS_FRAME), 0 or less for a failed read. Returns 1 and the serial, 0 for a
 * volume without a serial, -1 when lba 16 is no ISO 9660 volume descriptor. */
#define DS_FRAME 2448
typedef int (*ds_frame_fn)(void *ctx, unsigned long lba, unsigned char *frame);
int ds_serial_from_frames(ds_frame_fn rd, void *ctx, char out[DS_SERIAL_MAX]);

/* A "BOOT = cdrom:\SLUS_008.92;1" line (SYSTEM.CNF's text) as the serial; 1 or 0 */
int ds_parse_boot(const char *text, unsigned n, char out[DS_SERIAL_MAX]);

#ifdef __cplusplus
}
#endif
#endif
