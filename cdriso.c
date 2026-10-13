/***************************************************************************
 *   Copyright (C) 2007 PCSX-df Team                                       *
 *   Copyright (C) 2009 Wei Mingzhi                                        *
 *   Copyright (C) 2012 notaz                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#include <assert.h>
#include "psxcommon.h"
#include "plugins.h"
#include "cdrom.h"
#include "cdriso.h"
#include "ppf.h"

#include <errno.h>
#include <zlib.h>
#ifdef USE_LIBCHDR
#include "deps/libchdr/include/libchdr/chd.h"
#ifndef CD_TRACK_PADDING
#define CD_TRACK_PADDING 4   /* libchdr cdrom.h: each CHD track is stored padded to 4 frames */
#endif
#endif // USE_LIBCHDR

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include <sys/time.h>
#include <unistd.h>

#include "Gamecube/DEBUG.h"
#include "Gamecube/perf_prof.h"
#include <ogc/semaphore.h>
#include "mem2_manager.h"
#include <strings.h>   /* strncasecmp: no read-ahead for smb: */
#include "Gamecube/wiiSXconfig.h"     /* cdBuffer, cdPrefetch, cdChdHunks */

#define OFF_T_MSB ((off_t)1 << (sizeof(off_t) * 8 - 1))

unsigned int cdrIsoMultidiskCount;
unsigned int cdrIsoMultidiskSelect;

static FILE *cdHandle = NULL;
static FILE *subHandle = NULL;

static bool subChanMixed = FALSE;
static bool subChanRaw = FALSE;

static bool multifile = FALSE;

static unsigned char cdbuffer[CD_FRAMESIZE_RAW];
static unsigned char subbuffer[SUB_FRAMESIZE];

static bool cddaBigEndian = TRUE;
/* Frame offset into CD image where pregap data would be found if it was there.
 * If a game seeks there we must *not* return subchannel data since it's
 * not in the CD image, so that cdrom code can fake subchannel data instead.
 * XXX: there could be multiple pregaps but PSX dumps only have one? */
static unsigned int pregapOffset;

// compressed image stuff
static struct {
	unsigned char buff_raw[16][CD_FRAMESIZE_RAW];
	unsigned char buff_compressed[CD_FRAMESIZE_RAW * 16 + 100];
	off_t *index_table;
	unsigned int index_len;
	unsigned int block_shift;
	unsigned int current_block;
	unsigned int sector_in_blk;
} *compr_img;

#ifdef USE_LIBCHDR
static struct {
	unsigned char *buffer;
	chd_file* chd;
	const chd_header* header;
	unsigned int sectors_per_hunk;
	/* An N-way hunk cache (N = the CdChdHunks setting, at most CHD_MAX_WAYS). The first
	 * version had two ways and, on a miss, decoded into whichever way had been hit last,
	 * so it kept evicting the most recently used hunk; hunk_stamp[] makes it a real LRU. */
#define CHD_MAX_WAYS 8
	unsigned int current_hunk[CHD_MAX_WAYS];
	unsigned int hunk_stamp[CHD_MAX_WAYS];
	unsigned int ways;
	unsigned int stamp;
	unsigned int current_buffer;
	unsigned int sector_in_hunk;
} *chd_img;
#endif // USE_LIBCHDR

static int (*cdimg_read_func)(FILE *f, unsigned int base, void *dest, int sector);
static int (*cdimg_read_sub_func)(FILE *f, int sector);

char* CALLBACK CDR__getDriveLetter(void);
long CALLBACK CDR__configure(void);
long CALLBACK CDR__test(void);
void CALLBACK CDR__about(void);
long CALLBACK CDR__setfilename(char *filename);

static void DecodeRawSubData(void);

struct trackinfo {
	enum {DATA=1, CDDA} type;
	char start[3];		// MSF-format
	char length[3];		// MSF-format
	FILE *handle;		// for multi-track images CDDA
	unsigned int start_offset; // byte offset from start of above file (chd: sector offset)
};

#define MAXTRACKS 100 /* How many tracks can a CD hold? */

static int numtracks = 0;
static struct trackinfo ti[MAXTRACKS];

// get a sector from a msf-array
static inline unsigned int msf2sec(char *msf) {
	//return ((msf[0] * 60 + msf[1]) * 75) + msf[2];
	/* 256-entry tables indexed by a whole byte; msf comes from disc data, where a
	 * minute count of 0x80 or more is negative in a signed char. */
	return msf2SectMNoItob[(unsigned char)msf[0]] + msf2SectSNoItob[(unsigned char)msf[1]] + msf[2];
}

static inline void sec2msf(unsigned int s, char *msf) {
	msf[0] = s / 75 / 60;
	//s = s - msf[0] * 75 * 60;
	s = s - msf2SectMNoItob[(unsigned char)msf[0]];
	msf[1] = s / 75;
	//s = s - msf[1] * 75;
	s = s - msf2SectSNoItob[(unsigned char)msf[1]];
	msf[2] = s;
}

// divide a string of xx:yy:zz into m, s, f
static inline void tok2msf(char *time, char *msf) {
	char *token;

	token = strtok(time, ":");
	if (token) {
		msf[0] = atoi(token);
	}
	else {
		msf[0] = 0;
	}

	token = strtok(NULL, ":");
	if (token) {
		msf[1] = atoi(token);
	}
	else {
		msf[1] = 0;
	}

	token = strtok(NULL, ":");
	if (token) {
		msf[2] = atoi(token);
	}
	else {
		msf[2] = 0;
	}
}

// cdread_normal/cdread_sub_mixed/cdread_2048 each fseek() to the target byte
// offset before every single sector read, even when consecutive reads are
// sequential (the common case: level loading, FMV playback) and the stream
// is already positioned exactly there after the previous fread(). newlib's
// fseek() doesn't special-case a same-position seek -- it unconditionally
// discards the stdio buffer cd_attach_stdio_buffer() below deliberately
// widens to 16KB, forcing a real underlying read (FAT/DVD I/O on real Wii
// hardware) on every sector regardless. Skipping the redundant seek lets
// that buffering actually do its job for sequential access.
//
// One position per open handle (CDIMG_SEEK_FILES of them): a CloneCD game reads the
// .img and the .sub in turn, and with a single tracked file every sub-channel read
// and every data read after it was a real fseek, i.e. a 16 KB buffer refill from the
// card for 96 or 2352 bytes. Gex 2 on the bench Wii (5.9.3): 3656 seeks, 27% of the
// run in CD reads, 0.77x.
#define CDIMG_SEEK_FILES 4
static struct { FILE *f; long pos; } cdimg_seek_t[CDIMG_SEEK_FILES];
static unsigned cdimg_seek_next;

static void cdimg_seek_forget(void)
{
	memset(cdimg_seek_t, 0, sizeof cdimg_seek_t);
}

static int cdimg_seek_slot(FILE *f)
{
	int i;
	for (i = 0; i < CDIMG_SEEK_FILES; i++)
		if (cdimg_seek_t[i].f == f)
			return i;
	return -1;
}

// Some c libs like newlib default buffering to just 1k which is less than
// cd sector size which is bad for performance.
// Note that NULL setvbuf() is implemented differently by different libs
// (newlib mallocs a buffer of given size and glibc ignores size and uses it's own).
//
// Every image handle gets its own buffer: the main image, the sub-channel file, the
// second handle the CDDA reader keeps on a single-file image, and each file of a
// multi-file .cue. The first version had ONE static 16 KB buffer and attached it to the
// main handle only, so every CDDA track read went through newlib's 1 KB default: three
// underlying reads per 2352-byte sector. The size follows the CdBuffer setting (16, 64
// or 256 KB; a bigger buffer means fewer, longer SD/USB transactions on a sequential
// stream, and more wasted read-ahead on a random one) and the memory comes from MEM2,
// which a stdio buffer does not need MEM1's speed for. Sized at open, so a change of the
// setting takes effect when the next game is loaded.
#define CD_MAX_STDIO_BUFS (MAXTRACKS + 3)
static void *cd_stdio_bufs[CD_MAX_STDIO_BUFS];
static int cd_stdio_buf_count;

static size_t cd_stdio_buffer_bytes(void)
{
	switch (cdBuffer) {
	case CD_BUFFER_64K:  return 64 * 1024;
	case CD_BUFFER_256K: return 256 * 1024;
	default:             return 16 * 1024;
	}
}

static void cd_attach_stdio_buffer(FILE *f)
{
#if !defined(fopen) // no stdio redirect
	if (f && cd_stdio_buf_count < CD_MAX_STDIO_BUFS) {
		size_t bytes = cd_stdio_buffer_bytes();
		void *buf = _mem2_memalign(32, bytes);
		if (buf) {
			int r;
			errno = 0;
			r = setvbuf(f, buf, _IOFBF, bytes);
			if (r) {
				SysPrintf("cdriso: setvbuf %d %d\n", r, errno);
				_mem2_free(buf);
			} else
				cd_stdio_bufs[cd_stdio_buf_count++] = buf;
		}
	}
#endif
	// A freshly (re)opened stream invalidates whatever position the seek
	// tracking above remembered -- a stale match against a reused FILE*
	// address would otherwise skip a seek that's actually needed.
	cdimg_seek_forget();
}

/* Only after every handle that used one has been fclose()d. */
static void cd_release_stdio_buffers(void)
{
	int i;
	for (i = 0; i < cd_stdio_buf_count; i++)
		_mem2_free(cd_stdio_bufs[i]);
	cd_stdio_buf_count = 0;
}

static int cdimg_seek(FILE *f, long pos)
{
	int k = cdimg_seek_slot(f);
	if (k >= 0 && pos == cdimg_seek_t[k].pos) {
		PERF_INC(cd_seq);
		return 0;
	}
	// fseek() is also relied on to clear the stream's EOF/error indicator;
	// only skip it once we know this exact seek is redundant.
	if (fseek(f, pos, SEEK_SET)) {
		if (k >= 0)
			cdimg_seek_t[k].f = NULL;
		return -1;
	}
	PERF_INC(cd_rand);
	if (k < 0)
		k = cdimg_seek_next++ % CDIMG_SEEK_FILES;
	cdimg_seek_t[k].f = f;
	cdimg_seek_t[k].pos = pos;
	return 0;
}

static void pf_kick_late(void);

/* Phase 1 profiling: fold one sector-read result into the counters. */
#ifdef PERF_PROF
static int pf_missed;                        /* debug: 1 = this read missed the ring, 2 = missed while the thread was mid-fetch */
#endif

static void perf_cd_done(unsigned long long t0, int ret)
{
	unsigned long long dt = perf_now_us() - t0;
	pf_kick_late();
#ifdef PERF_PROF
	if (pf_missed == 1) {
		PERF_ADD(cd_pf_miss_us, dt);
		PERF_MAX(cd_pf_miss_worst_us, (uint32_t)dt);
	} else if (pf_missed == 2) {
		PERF_ADD(cd_pf_bmiss_us, dt);
		PERF_MAX(cd_pf_bmiss_worst_us, (uint32_t)dt);
	}
	pf_missed = 0;
	if (dt > 1000)
		PERF_INC(cd_slow1);
	if (dt > 4000)
		PERF_INC(cd_slow4);
#endif
	PERF_ADD(io_total_us, dt);
	if (ret > 0) {
		PERF_INC(cd_reads);
		PERF_ADD(cd_bytes, (unsigned long long)ret);
	}
#ifdef PERF_PROF
	if ((uint32_t)dt > g_perf.io_worst_us)
		g_perf.io_worst_us = (uint32_t)dt;
#else
	(void)dt;
#endif
}

// Track the position a successful read actually left the stream at (not
// the nominal sector arithmetic), so a short read or error correctly
// forces a real seek next time instead of a wrong skip.
static void cdimg_seek_advance(FILE *f, long pos, int nread)
{
	int k = cdimg_seek_slot(f);
	if (k < 0)
		return;
	if (nread > 0)
		cdimg_seek_t[k].pos = pos + nread;
	else
		cdimg_seek_t[k].f = NULL;   /* a short read or an error: seek next time */
}

/* Read-ahead for raw images (bin/cue and mode-1 .iso).
 *
 * The emulated CD-ROM asks for one sector at a time, and on real hardware that read is a
 * synchronous SD or USB transaction on the emulator thread: the stdio buffer turns it into
 * one transaction every 7 (16 KB) to 108 (256 KB) sectors, but that transaction still
 * stalls the emulated CPU for as long as the card takes. Games stream from disc during
 * exactly the moments that are already expensive (level loads, FMV, XA music), so the
 * stall lands on top of the work.
 *
 * With CdPrefetch on, a second thread owns a second FILE* on the same image and keeps a
 * ring of the PF_SLOTS sectors after the emulator's last read filled. The emulator's read
 * becomes a memcpy from the ring when the sector is there, and the thread is kicked after
 * every read so it stays ahead. It runs at a HIGHER priority than the emulator thread
 * (PF_PRIO, above the default 64): the frame limiter busy-waits, so a lower-priority
 * thread would never run; a higher-priority one preempts just long enough to issue the
 * read, then blocks in the driver's I/O wait and gives the CPU back. libfat serialises
 * the two handles with its partition mutex and newlib's FILE locking keeps the streams
 * apart, so no lock of our own is needed. Single producer, single consumer, one core:
 * each slot carries its byte position as a tag, set to -1 while the thread writes it and
 * checked again after the emulator copies it out, so a slot overwritten mid-copy is
 * simply treated as a miss.
 *
 * Dolphin backs the SD card with a host file, so its reads never stall; this thread can
 * only be judged on hardware (perf.log `cd:` line: pf_hit/pf_miss/pf_reads). Off by
 * default until that has been done.
 *
 * Decode-ahead for compressed images (.pbp, .cbn, .chd) is the same thread, ring and
 * handoff with a block (PBP: 16 sectors, CBN: 1, CHD: one hunk) in each slot and the block
 * number as the tag (pf_secbytes = 1). There the cost is the decode, not the card: on the
 * bench Wii Frogger's 3920 sectors cost the emulator 2.8 s (.cbn) to 4.0 s (.chd) against
 * 0.35 s from a .bin. So it runs BELOW the emulator (DA_PRIO): it gets the CPU only while
 * the emulator sleeps in the frame limiter (LimiterWait = 1) or waits on the card, which is
 * about half of each frame; ahead of the emulator, never instead of it. It owns everything
 * it touches -- its own FILE*, z_stream and chd_file (none of them is thread-safe) -- and
 * only reads compr_img's index. If it never gets the CPU, every block is a miss and the
 * emulator decodes it itself, as before. */
#define PF_SLOTS 32
#define PF_PRIO  66
#define PF_MAX_SLOTS 64
#define DA_PRIO  48
#define DA_BYTES (16 * CD_FRAMESIZE_RAW * 4)   /* the decode-ahead ring: 4 PBP blocks, 64 CBN, 4 CHD hunks */
static volatile long pf_tag[PF_MAX_SLOTS];   /* per slot: its position (bytes, or block number), -1 = empty/being written */
static unsigned char *pf_data;               /* pf_nslots slots of pf_slotbytes, MEM2 */
static int pf_nslots, pf_slotbytes;
static int (*pf_fetch)(long pos, unsigned char *dst);   /* fills one slot; runs on the thread */
static FILE *pf_handle;
static lwp_t pf_thread = LWP_THREAD_NULL;
static sem_t pf_sem = LWP_SEM_NULL;
static volatile int pf_active, pf_quit;
#ifdef PERF_PROF
static volatile int pf_busy;                 /* the thread is inside pf_fetch (debug probe: cd_pf_bmiss) */
#define PF_BUSY(v) (pf_busy = (v))
#else
#define PF_BUSY(v) ((void)0)
#endif
static volatile long pf_want;                /* the position after the emulator's last read */
static int pf_secbytes;                      /* bytes per sector in this image: 2352 or 2048; 1 for blocks */
static long pf_filepos = -1;                 /* where pf_handle's stream is, to skip redundant seeks */
static char cd_main_path[MAXPATHLEN];        /* the file cdHandle is open on (the .bin may differ from the .cue) */

static int pf_read_at(long pos, unsigned char *dst)
{
	if (pos != pf_filepos && fseek(pf_handle, pos, SEEK_SET)) {
		pf_filepos = -1;
		return 0;
	}
	if (fread(dst, 1, pf_secbytes, pf_handle) != (size_t)pf_secbytes) {
		pf_filepos = -1;
		return 0;
	}
	pf_filepos = pos + pf_secbytes;
	return 1;
}

#define DA_CBUF_BYTES (CD_FRAMESIZE_RAW * 16 + 100)   /* = sizeof compr_img->buff_compressed */
static unsigned char *pf_cbuf;               /* decode-ahead: one compressed block, after the slots */
static z_stream pf_z;                        /* decode-ahead: the thread's own inflate state */
#ifdef USE_LIBCHDR
static chd_file *pf_chd;                     /* decode-ahead: the thread's own CHD, on pf_handle */
#endif
static int uncompress2_pcsx(z_stream *zp, void *out, unsigned long *out_size, void *in, unsigned long in_size);
static void uncompress2_pcsx_end(z_stream *zp);

/* Decode-ahead, PBP/CBN: what cdread_compressed does for a block, on the thread's own handle. */
static int pf_compr_fetch(long block, unsigned char *dst)
{
	unsigned long n = (unsigned long)pf_slotbytes;
	unsigned int size;
	off_t start;
	int plain;
	if ((unsigned long)block >= compr_img->index_len)
		return 0;
	start = compr_img->index_table[block] & ~OFF_T_MSB;
	size = (compr_img->index_table[block + 1] & ~OFF_T_MSB) - start;
	plain = (compr_img->index_table[block] & OFF_T_MSB) != 0;
	if (size > (plain ? n : DA_CBUF_BYTES))
		return 0;
	if (start != pf_filepos && fseeko(pf_handle, start, SEEK_SET)) {
		pf_filepos = -1;
		return 0;
	}
	if (fread(plain ? dst : pf_cbuf, 1, size, pf_handle) != size) {
		pf_filepos = -1;
		return 0;
	}
	pf_filepos = start + size;
	return plain || uncompress2_pcsx(&pf_z, dst, &n, pf_cbuf, size) == 0;
}

#ifdef USE_LIBCHDR
/* Decode-ahead, CHD: a second chd_file on the thread's own handle, opened on first use. */
static int pf_chd_fetch(long hunk, unsigned char *dst)
{
	if (pf_chd == NULL && chd_open_file(pf_handle, CHD_OPEN_READ, NULL, &pf_chd) != CHDERR_NONE) {
		pf_chd = NULL;
		return 0;
	}
	return chd_read(pf_chd, (uint32_t)hunk, dst) == CHDERR_NONE;
}
#endif

/* After the thread is joined (or was never started). */
static void pf_free(void)
{
#ifdef USE_LIBCHDR
	if (pf_chd) {
		chd_close(pf_chd);          /* before its FILE: chd_open_file does not own it */
		pf_chd = NULL;
	}
#endif
	uncompress2_pcsx_end(&pf_z);
	if (pf_handle) {
		fclose(pf_handle);          /* its stdio buffer is released with the others in ISOclose */
		pf_handle = NULL;
	}
	if (pf_data) {
		_mem2_free(pf_data);
		pf_data = NULL;
	}
}

static void *pf_main(void *arg)
{
	(void)arg;
	while (!pf_quit) {
		long next;
		LWP_SemWait(pf_sem);
		if (pf_quit)
			break;
		next = pf_want;
		while (!pf_quit) {
			long want = pf_want;
			unsigned long i;
			if (next < want)
				next = want;                                            /* the emulator jumped ahead: follow it */
			if (next - want >= (long)(pf_nslots - 1) * pf_secbytes)
				break;                                                  /* far enough ahead; sleep until kicked */
			i = (unsigned long)(next / pf_secbytes) & (pf_nslots - 1);
			if (pf_tag[i] != next) {
				pf_tag[i] = -1;
				PF_BUSY(1);
				if (!pf_fetch(next, pf_data + i * pf_slotbytes)) {
					PF_BUSY(0);
					break;                                              /* end of image or error: wait for the next kick */
				}
				PF_BUSY(0);
				pf_tag[i] = next;
				PERF_INC(cd_pf_reads);
			}
			next += pf_secbytes;
		}
	}
	return NULL;
}

/* fileBrowser-libfat.c's SD probe: is this card read the read-ahead thread's? */
int cd_prefetch_thread_is_self(void)
{
	return pf_thread != LWP_THREAD_NULL && LWP_GetSelf() == pf_thread;
}

static void pf_kick(long pos)
{
	pf_want = pos + pf_secbytes;
	LWP_SemPost(pf_sem);
}

/* A miss wakes the thread only once the emulator's own read is done (perf_cd_done, at the
 * end of every read). Woken first, the thread got the card ahead of the sector the game was
 * waiting for: read-ahead (PF_PRIO, above the emulator) at once; decode-ahead (DA_PRIO,
 * below) as soon as the emulator blocked in its own seek, after which libfat's partition
 * lock was handed to the waiting thread on unlock, and the emulator's fread then waited
 * for the thread's whole fread (priority inheritance only speeds that up). Dolphin,
 * MediEvil, the worst read of a game, CdPrefetch 0 / kick first / kick after: .cue
 * 3681/6400/3797 us, CHD 4432/5945/4400, PBP 3865/5896/3990, CBN 3024/5039/3071. */
static int pf_late;
static void pf_kick_late(void)
{
	if (pf_late) {
		pf_late = 0;
		LWP_SemPost(pf_sem);
	}
}

/* Copy the sector (or block) at pos out of the ring if it is there. Kicks the thread either
 * way: on a hit now, on a miss after the emulator's own read (pf_kick_late). */
static int pf_take(long pos, void *dest)
{
	unsigned long i = (unsigned long)(pos / pf_secbytes) & (pf_nslots - 1);
	if (pf_tag[i] == pos) {
		memcpy(dest, pf_data + i * pf_slotbytes, pf_slotbytes);
		if (pf_tag[i] == pos) {
			PERF_INC(cd_pf_hit);
			pf_kick(pos);
			return 1;
		}
	}
	PERF_INC(cd_pf_miss);
#ifdef PERF_PROF
	pf_missed = pf_busy ? 2 : 1;
	if (pf_busy)
		PERF_INC(cd_pf_bmiss);
#endif
	pf_want = pos + pf_secbytes;    /* pf_kick, after the emulator's own read (pf_kick_late) */
	pf_late = 1;
	return 0;
}

static void pf_stop(void)
{
	if (!pf_active)
		return;
	pf_active = 0;
	pf_late = 0;
	pf_quit = 1;
	LWP_SemPost(pf_sem);
	LWP_JoinThread(pf_thread, NULL);
	pf_thread = LWP_THREAD_NULL;
	LWP_SemDestroy(pf_sem);
	pf_sem = LWP_SEM_NULL;
	pf_free();
}

static void pf_start(const char *path, int secbytes, int slotbytes, int nslots, int (*fetch)(long, unsigned char *))
{
	int i, extra = fetch == pf_read_at ? 0 : DA_CBUF_BYTES;
	if (pf_active || !cdPrefetch)
		return;
	/* Not for a game on the network share: Gamecube/fileBrowser/smb2dev.c fetches the next
	 * 16 KB itself, and sectors read ahead and never used cost the game time over Wi-Fi. The
	 * bench Wii, Ape Escape over SMB: 0.77x without, 0.64x with (5.7.0, 2026-10-08). */
	if (!strncasecmp(path, "smb:", 4))
		return;
	/* Nor for a game on a DVD: libiso9660 has no lock, and this thread would read the disc
	 * beside the emulator's own handle (Gamecube/fileBrowser/fileBrowser-DVD.c). */
	if (!strncasecmp(path, "dvd:", 4))
		return;
	if (nslots > PF_MAX_SLOTS)
		nslots = PF_MAX_SLOTS;
	while (nslots & (nslots - 1))
		nslots &= nslots - 1;       /* a power of two: the thread's scan indexes a slot per step, and a
		                             * divide there cost every raw-image read 2 us in Dolphin */
	pf_data = (unsigned char *)_mem2_memalign(32, nslots * slotbytes + extra);
	pf_handle = fopen(path, "rb");
	if (pf_data == NULL || pf_handle == NULL || nslots < 2) {
		SysPrintf("cdriso: prefetch not started\n");
		goto fail;
	}
	cd_attach_stdio_buffer(pf_handle);
	for (i = 0; i < nslots; i++)
		pf_tag[i] = -1;
	pf_cbuf = pf_data + nslots * slotbytes;
	pf_nslots = nslots;
	pf_slotbytes = slotbytes;
	pf_fetch = fetch;
	pf_secbytes = secbytes;
	pf_filepos = 0;
	pf_want = 0;
	pf_quit = 0;
	if (LWP_SemInit(&pf_sem, 0, 1) != 0)
		goto fail;
	/* 64 KB for the decoders (libchdr's LZMA/zstd/FLAC run on this stack) */
	if (LWP_CreateThread(&pf_thread, pf_main, NULL, NULL, extra ? 64 * 1024 : 16 * 1024,
			fetch == pf_read_at ? PF_PRIO : DA_PRIO) != 0) {
		LWP_SemDestroy(pf_sem);
		pf_sem = LWP_SEM_NULL;
		goto fail;
	}
	pf_active = 1;
	return;
fail:
	pf_free();
}

// this function tries to get the .toc file of the given .bin
// the necessary data is put into the ti (trackinformation)-array
static int parsetoc(const char *isofile) {
	char			tocname[MAXPATHLEN];
	FILE			*fi;
	char			linebuf[256], tmp[256], name[256];
	char			*token;
	char			time[20], time2[20];
	unsigned int	t, sector_offs, sector_size;
	unsigned int	current_zero_gap = 0;

	numtracks = 0;

	// copy name of the iso and change extension from .bin to .toc
	strncpy(tocname, isofile, sizeof(tocname));
	tocname[MAXPATHLEN - 1] = '\0';
	if (strlen(tocname) >= 4) {
		strcpy(tocname + strlen(tocname) - 4, ".toc");
	}
	else {
		return -1;
	}

	if ((fi = fopen(tocname, "r")) == NULL) {
		// try changing extension to .cue (to satisfy some stupid tutorials)
		strcpy(tocname + strlen(tocname) - 4, ".cue");
		if ((fi = fopen(tocname, "r")) == NULL) {
			// if filename is image.toc.bin, try removing .bin (for Brasero)
			strcpy(tocname, isofile);
			t = strlen(tocname);
			if (t >= 8 && strcmp(tocname + t - 8, ".toc.bin") == 0) {
				tocname[t - 4] = '\0';
				if ((fi = fopen(tocname, "r")) == NULL) {
					return -1;
				}
			}
			else {
				return -1;
			}
		}
		// check if it's really a TOC named as a .cue
		if (fgets(linebuf, sizeof(linebuf), fi) != NULL) {
			token = strtok(linebuf, " ");
			if (token && strncmp(token, "CD", 2) != 0) {
				// && strcmp(token, "CATALOG") != 0) - valid for a real .cue
				fclose(fi);
				return -1;
			}
		}
		fseek(fi, 0, SEEK_SET);
	}

	memset(&ti, 0, sizeof(ti));
	cddaBigEndian = FALSE; // cdrdao uses big-endian for CD Audio

	sector_size = CD_FRAMESIZE_RAW;
	sector_offs = 2 * 75;

	// parse the .toc file
	while (fgets(linebuf, sizeof(linebuf), fi) != NULL) {
		// search for tracks
		strncpy(tmp, linebuf, sizeof(linebuf));
		token = strtok(tmp, " ");

		if (token == NULL) continue;

		if (!strcmp(token, "TRACK")) {
			sector_offs += current_zero_gap;
			current_zero_gap = 0;

			// get type of track
			token = strtok(NULL, " ");
			numtracks++;

			if (!strncmp(token, "MODE2_RAW", 9)) {
				ti[numtracks].type = DATA;
				sec2msf(2 * 75, ti[numtracks].start); // assume data track on 0:2:0

				// check if this image contains mixed subchannel data
				token = strtok(NULL, " ");
				if (token != NULL && !strncmp(token, "RW", 2)) {
					sector_size = CD_FRAMESIZE_RAW + SUB_FRAMESIZE;
					subChanMixed = TRUE;
					if (!strncmp(token, "RW_RAW", 6))
						subChanRaw = TRUE;
				}
			}
			else if (!strncmp(token, "AUDIO", 5)) {
				ti[numtracks].type = CDDA;
			}
		}
		else if (!strcmp(token, "DATAFILE")) {
			if (ti[numtracks].type == CDDA) {
				sscanf(linebuf, "DATAFILE \"%[^\"]\" #%d %8s", name, &t, time2);
				ti[numtracks].start_offset = t;
				t = t / sector_size + sector_offs;
				sec2msf(t, (char *)&ti[numtracks].start);
				tok2msf((char *)&time2, (char *)&ti[numtracks].length);
			}
			else {
				sscanf(linebuf, "DATAFILE \"%[^\"]\" %8s", name, time);
				tok2msf((char *)&time, (char *)&ti[numtracks].length);
			}
		}
		else if (!strcmp(token, "FILE")) {
			sscanf(linebuf, "FILE \"%[^\"]\" #%d %8s %8s", name, &t, time, time2);
			tok2msf((char *)&time, (char *)&ti[numtracks].start);
			t += msf2sec(ti[numtracks].start) * sector_size;
			ti[numtracks].start_offset = t;
			t = t / sector_size + sector_offs;
			sec2msf(t, (char *)&ti[numtracks].start);
			tok2msf((char *)&time2, (char *)&ti[numtracks].length);
		}
		else if (!strcmp(token, "ZERO") || !strcmp(token, "SILENCE")) {
			// skip unneeded optional fields
			while (token != NULL) {
				token = strtok(NULL, " ");
				if (strchr(token, ':') != NULL)
					break;
			}
			if (token != NULL) {
				tok2msf(token, tmp);
				current_zero_gap = msf2sec(tmp);
			}
			if (numtracks > 1) {
				t = ti[numtracks - 1].start_offset;
				t /= sector_size;
				pregapOffset = t + msf2sec(ti[numtracks - 1].length);
			}
		}
		else if (!strcmp(token, "START")) {
			token = strtok(NULL, " ");
			if (token != NULL && strchr(token, ':')) {
				tok2msf(token, tmp);
				t = msf2sec(tmp);
				ti[numtracks].start_offset += (t - current_zero_gap) * sector_size;
				t = msf2sec(ti[numtracks].start) + t;
				sec2msf(t, (char *)&ti[numtracks].start);
			}
		}
	}

	fclose(fi);

	return 0;
}

// this function tries to get the .cue file of the given .bin
// the necessary data is put into the ti (trackinformation)-array
static int parsecue(const char *isofile) {
	char			cuename[MAXPATHLEN];
	char			filepath[MAXPATHLEN];
	char			*incue_fname;
	FILE			*fi;
	char			*token;
	char			time[20];
	char			*tmp;
	char			linebuf[256], tmpb[256], dummy[256];
	unsigned int	incue_max_len;
	unsigned int	t, file_len, mode, sector_offs;
	unsigned int	sector_size = 2352;

	numtracks = 0;

	// copy name of the iso and change extension from .bin to .cue
	strncpy(cuename, isofile, sizeof(cuename));
	cuename[MAXPATHLEN - 1] = '\0';
	if (strlen(cuename) >= 4) {
		// If 'isofile' is a '.cd<X>' file, use it as a .cue file
		//  and don't try to search the additional .cue file
		if (strncasecmp(cuename + strlen(cuename) - 4, ".cd", 3) != 0 )
			strcpy(cuename + strlen(cuename) - 4, ".cue");
	}
	else {
		return -1;
	}

	if ((fi = fopen(cuename, "r")) == NULL) {
		return -1;
	}

	// Some stupid tutorials wrongly tell users to use cdrdao to rip a
	// "bin/cue" image, which is in fact a "bin/toc" image. So let's check
	// that...
	if (fgets(linebuf, sizeof(linebuf), fi) != NULL) {
		if (!strncmp(linebuf, "CD_ROM_XA", 9)) {
			// Don't proceed further, as this is actually a .toc file rather
			// than a .cue file.
			fclose(fi);
			return parsetoc(isofile);
		}
		fseek(fi, 0, SEEK_SET);
	}

	// build a path for files referenced in .cue
	strncpy(filepath, cuename, sizeof(filepath));
	tmp = strrchr(filepath, '/');
	if (tmp == NULL)
		tmp = strrchr(filepath, '\\');
	if (tmp != NULL)
		tmp++;
	else
		tmp = filepath;
	*tmp = 0;
	filepath[sizeof(filepath) - 1] = 0;
	incue_fname = tmp;
	incue_max_len = sizeof(filepath) - (tmp - filepath) - 1;

	memset(&ti, 0, sizeof(ti));

	file_len = 0;
	sector_offs = 2 * 75;
	isoFile.size = 0;

	while (fgets(linebuf, sizeof(linebuf), fi) != NULL) {
		strncpy(dummy, linebuf, sizeof(linebuf));
		token = strtok(dummy, " ");

		if (token == NULL) {
			continue;
		}

		if (!strcmp(token, "TRACK")) {
			numtracks++;

			sector_size = 0;
			if (strstr(linebuf, "AUDIO") != NULL) {
				ti[numtracks].type = CDDA;
				sector_size = 2352;
			}
			else if (sscanf(linebuf, " TRACK %u MODE%u/%u", &t, &mode, &sector_size) == 3)
				ti[numtracks].type = DATA;
			else {
				SysPrintf(".cue: failed to parse TRACK\n");
				ti[numtracks].type = numtracks == 1 ? DATA : CDDA;
			}
			if (sector_size == 0)
				sector_size = 2352;
		}
		else if (!strcmp(token, "INDEX")) {
			if (sscanf(linebuf, " INDEX %02d %8s", &t, time) != 2)
				SysPrintf(".cue: failed to parse INDEX\n");
			tok2msf(time, (char *)&ti[numtracks].start);

			t = msf2sec(ti[numtracks].start);
			ti[numtracks].start_offset = t * sector_size;
			t += sector_offs;
			sec2msf(t, ti[numtracks].start);

			// default track length to file length
			t = file_len - ti[numtracks].start_offset / sector_size;
			sec2msf(t, ti[numtracks].length);

			if (numtracks > 1 && ti[numtracks].handle == NULL) {
				// this track uses the same file as the last,
				// start of this track is last track's end
				t = msf2sec(ti[numtracks].start) - msf2sec(ti[numtracks - 1].start);
				sec2msf(t, ti[numtracks - 1].length);
			}
			if (numtracks > 1 && pregapOffset == -1)
				pregapOffset = ti[numtracks].start_offset / sector_size;
		}
		else if (!strcmp(token, "PREGAP")) {
			if (sscanf(linebuf, " PREGAP %8s", time) == 1) {
				tok2msf(time, dummy);
				sector_offs += msf2sec(dummy);
			}
			pregapOffset = -1; // mark to fill track start_offset
		}
		else if (!strcmp(token, "FILE")) {
			t = sscanf(linebuf, " FILE \"%255[^\"]\"", tmpb);
			if (t != 1)
				sscanf(linebuf, " FILE %255s", tmpb);

			tmp = strrchr(tmpb, '\\');
			if (tmp == NULL)
				tmp = strrchr(tmpb, '/');
			if (tmp != NULL)
				tmp++;
			else
				tmp = tmpb;
			strncpy(incue_fname, tmp, incue_max_len);
			ti[numtracks + 1].handle = fopen(filepath, "rb");
			cd_attach_stdio_buffer(ti[numtracks + 1].handle);

			// update global offset if this is not first file in this .cue
			if (numtracks + 1 > 1) {
				multifile = 1;
				sector_offs += file_len;
			}

			file_len = 0;
			if (ti[numtracks + 1].handle == NULL) {
				SysPrintf(_("\ncould not open: %s\n"), filepath);
				continue;
			}
			fseek(ti[numtracks + 1].handle, 0, SEEK_END);
			int trackFileSize = ftell(ti[numtracks + 1].handle);
			isoFile.size += trackFileSize;
			file_len = trackFileSize / 2352;

			if (numtracks == 0 && strlen(isofile) >= 4 &&
				(strcmp(isofile + strlen(isofile) - 4, ".cue") == 0 ||
				strncasecmp(isofile + strlen(isofile) - 4, ".cd", 3) == 0)) {
				// user selected .cue/.cdX as image file, use it's data track instead
				fclose(cdHandle);
				cdHandle = fopen(filepath, "rb");
				cd_attach_stdio_buffer(cdHandle);
				/* the read-ahead thread opens its own handle on this file, not the .cue */
				strncpy(cd_main_path, filepath, sizeof(cd_main_path) - 1);
			}
		}
	}

	fclose(fi);

	// if there are no tracks detected, then it's not a cue file
	if (!numtracks)
		return -1;

	return 0;
}

// this function tries to get the .ccd file of the given .img
// the necessary data is put into the ti (trackinformation)-array
static int parseccd(const char *isofile) {
	char			ccdname[MAXPATHLEN];
	FILE			*fi;
	char			linebuf[256];
	unsigned int	t;

	numtracks = 0;

	// copy name of the iso and change extension from .img to .ccd
	strncpy(ccdname, isofile, sizeof(ccdname));
	ccdname[MAXPATHLEN - 1] = '\0';
	if (strlen(ccdname) >= 4) {
		strcpy(ccdname + strlen(ccdname) - 4, ".ccd");
	}
	else {
		return -1;
	}

	if ((fi = fopen(ccdname, "r")) == NULL) {
		return -1;
	}

	memset(&ti, 0, sizeof(ti));

	while (fgets(linebuf, sizeof(linebuf), fi) != NULL) {
		if (!strncmp(linebuf, "[TRACK", 6)){
			numtracks++;
		}
		else if (!strncmp(linebuf, "MODE=", 5)) {
			sscanf(linebuf, "MODE=%d", &t);
			ti[numtracks].type = ((t == 0) ? CDDA : DATA);
		}
		else if (!strncmp(linebuf, "INDEX 1=", 8)) {
			sscanf(linebuf, "INDEX 1=%d", &t);
			sec2msf(t + 2 * 75, ti[numtracks].start);
			ti[numtracks].start_offset = t * 2352;

			// If we've already seen another track, this is its end
			if (numtracks > 1) {
				t = msf2sec(ti[numtracks].start) - msf2sec(ti[numtracks - 1].start);
				sec2msf(t, ti[numtracks - 1].length);
			}
		}
	}

	fclose(fi);

	// Fill out the last track's end based on size
	if (numtracks >= 1) {
		/* WiiStation: the size of the IMAGE. cdHandle is the file the browser
		 * picked, and it shows the .ccd and hides its .img, so cdHandle was the
		 * 789-byte sheet here (ISOopen swaps to the .img only later): every CCD
		 * game had a last track 150 sectors long and a lead-out at 00:04:00. */
		static const char *exts[] = { ".img", ".IMG", ".bin", ".BIN" };
		long size = -1;
		size_t i;

		for (i = 0; size < 0 && i < sizeof(exts) / sizeof(exts[0]); i++) {
			FILE *img;
			strcpy(ccdname + strlen(ccdname) - 4, exts[i]);
			if ((img = fopen(ccdname, "rb")) != NULL) {
				fseek(img, 0, SEEK_END);
				size = ftell(img);
				fclose(img);
			}
		}
		if (size < 0) {
			fseek(cdHandle, 0, SEEK_END);
			size = ftell(cdHandle);
		}
		t = size / 2352 - msf2sec(ti[numtracks].start) + 2 * 75;
		sec2msf(t, ti[numtracks].length);
	}

	return 0;
}

// this function tries to get the .mds file of the given .mdf
// the necessary data is put into the ti (trackinformation)-array
static int parsemds(const char *isofile) {
	char			mdsname[MAXPATHLEN];
	FILE			*fi;
	unsigned int	offset, extra_offset, l, i;
	unsigned short	s;

	numtracks = 0;

	// copy name of the iso and change extension from .mdf to .mds
	strncpy(mdsname, isofile, sizeof(mdsname));
	mdsname[MAXPATHLEN - 1] = '\0';
	if (strlen(mdsname) >= 4) {
		strcpy(mdsname + strlen(mdsname) - 4, ".mds");
	}
	else {
		return -1;
	}

	if ((fi = fopen(mdsname, "rb")) == NULL) {
		return -1;
	}

	memset(&ti, 0, sizeof(ti));

	// check if it's a valid mds file
	if (fread(&i, 1, sizeof(i), fi) != sizeof(i))
		goto fail_io;
	i = SWAP32(i);
	if (i != 0x4944454D) {
		// not an valid mds file
		fclose(fi);
		return -1;
	}

	// get offset to session block
	fseek(fi, 0x50, SEEK_SET);
	if (fread(&offset, 1, sizeof(offset), fi) != sizeof(offset))
		goto fail_io;
	offset = SWAP32(offset);

	// get total number of tracks
	offset += 14;
	fseek(fi, offset, SEEK_SET);
	if (fread(&s, 1, sizeof(s), fi) != sizeof(s))
		goto fail_io;
	s = SWAP16(s);
	numtracks = s;

	// get offset to track blocks
	fseek(fi, 4, SEEK_CUR);
	if (fread(&offset, 1, sizeof(offset), fi) != sizeof(offset))
		goto fail_io;
	offset = SWAP32(offset);

	// skip lead-in data
	while (1) {
		fseek(fi, offset + 4, SEEK_SET);
		if (fgetc(fi) < 0xA0) {
			break;
		}
		offset += 0x50;
	}

	// check if the image contains mixed subchannel data
	fseek(fi, offset + 1, SEEK_SET);
	subChanMixed = subChanRaw = (fgetc(fi) ? TRUE : FALSE);

	// read track data
	for (i = 1; i <= numtracks; i++) {
		fseek(fi, offset, SEEK_SET);

		// get the track type
		ti[i].type = ((fgetc(fi) == 0xA9) ? CDDA : DATA);
		fseek(fi, 8, SEEK_CUR);

		// get the track starting point
		ti[i].start[0] = fgetc(fi);
		ti[i].start[1] = fgetc(fi);
		ti[i].start[2] = fgetc(fi);

		if (fread(&extra_offset, 1, sizeof(extra_offset), fi) != sizeof(extra_offset))
			goto fail_io;
		extra_offset = SWAP32(extra_offset);

		// get track start offset (in .mdf)
		fseek(fi, offset + 0x28, SEEK_SET);
		if (fread(&l, 1, sizeof(l), fi) != sizeof(l))
			goto fail_io;
		l = SWAP32(l);
		ti[i].start_offset = l;

		// get pregap
		fseek(fi, extra_offset, SEEK_SET);
		if (fread(&l, 1, sizeof(l), fi) != sizeof(l))
			goto fail_io;
		l = SWAP32(l);
		if (l != 0 && i > 1)
			pregapOffset = msf2sec(ti[i].start);

		// get the track length
		if (fread(&l, 1, sizeof(l), fi) != sizeof(l))
			goto fail_io;
		l = SWAP32(l);
		sec2msf(l, ti[i].length);

		offset += 0x50;
	}
	fclose(fi);
	return 0;
fail_io:
#ifndef NDEBUG
	SysPrintf(_("File IO error in <%s:%s>.\n"), __FILE__, __func__);
#endif
	fclose(fi);
	return -1;
}

static int handlepbp(const char *isofile) {
	struct {
		unsigned int sig;
		unsigned int dontcare[8];
		unsigned int psar_offs;
	} pbp_hdr;
	struct {
		unsigned char type;
		unsigned char pad0;
		unsigned char track;
		char index0[3];
		char pad1;
		char index1[3];
	} toc_entry;
	struct {
		unsigned int offset;
		unsigned int size;
		unsigned int dontcare[6];
	} index_entry;
	char psar_sig[11];
	off_t psisoimg_offs, cdimg_base;
	unsigned int t, cd_length;
	unsigned int offsettab[8];
	unsigned int psar_offs, index_entry_size, index_entry_offset;
	const char *ext = NULL;
	int i, ret;

	if (strlen(isofile) >= 4)
		ext = isofile + strlen(isofile) - 4;
	if (ext == NULL || (strcmp(ext, ".pbp") != 0 && strcmp(ext, ".PBP") != 0))
		return -1;

	fseeko(cdHandle, 0, SEEK_SET);

	numtracks = 0;

	ret = fread(&pbp_hdr, 1, sizeof(pbp_hdr), cdHandle);
	if (ret != sizeof(pbp_hdr)) {
		SysPrintf("failed to read pbp\n");
		goto fail_io;
	}

	psar_offs = SWAP32(pbp_hdr.psar_offs);

	ret = fseeko(cdHandle, psar_offs, SEEK_SET);
	if (ret != 0) {
		SysPrintf("failed to seek to %x\n", psar_offs);
		goto fail_io;
	}

	psisoimg_offs = psar_offs;
	if (fread(psar_sig, 1, sizeof(psar_sig), cdHandle) != sizeof(psar_sig))
		goto fail_io;
	psar_sig[10] = 0;
	if (strcmp(psar_sig, "PSTITLEIMG") == 0) {
		// multidisk image?
		ret = fseeko(cdHandle, psar_offs + 0x200, SEEK_SET);
		if (ret != 0) {
			SysPrintf("failed to seek to %x\n", psar_offs + 0x200);
			goto fail_io;
		}

		if (fread(&offsettab, 1, sizeof(offsettab), cdHandle) != sizeof(offsettab)) {
			SysPrintf("failed to read offsettab\n");
			goto fail_io;
		}

		for (i = 0; i < sizeof(offsettab) / sizeof(offsettab[0]); i++) {
			if (offsettab[i] == 0)
				break;
		}
		cdrIsoMultidiskCount = i;
		if (cdrIsoMultidiskCount == 0) {
			SysPrintf("multidisk eboot has 0 images?\n");
			goto fail_io;
		}

		if (cdrIsoMultidiskSelect >= cdrIsoMultidiskCount)
			cdrIsoMultidiskSelect = 0;

		psisoimg_offs += SWAP32(offsettab[cdrIsoMultidiskSelect]);

		ret = fseeko(cdHandle, psisoimg_offs, SEEK_SET);
		if (ret != 0) {
			SysPrintf("failed to seek to %llx\n", (long long)psisoimg_offs);
			goto fail_io;
		}

		if (fread(psar_sig, 1, sizeof(psar_sig), cdHandle) != sizeof(psar_sig))
			goto fail_io;
		psar_sig[10] = 0;
	}

	if (strcmp(psar_sig, "PSISOIMG00") != 0) {
		SysPrintf("bad psar_sig: %s\n", psar_sig);
		goto fail_io;
	}

	// seek to TOC
	ret = fseeko(cdHandle, psisoimg_offs + 0x800, SEEK_SET);
	if (ret != 0) {
		SysPrintf("failed to seek to %llx\n", (long long)psisoimg_offs + 0x800);
		goto fail_io;
	}

	// first 3 entries are special
	fseek(cdHandle, sizeof(toc_entry), SEEK_CUR);
	if (fread(&toc_entry, 1, sizeof(toc_entry), cdHandle) != sizeof(toc_entry))
		goto fail_io;
	numtracks = btoi(toc_entry.index1[0]);

	if (fread(&toc_entry, 1, sizeof(toc_entry), cdHandle) != sizeof(toc_entry))
		goto fail_io;
	cd_length = btoi(toc_entry.index1[0]) * 60 * 75 +
		btoi(toc_entry.index1[1]) * 75 + btoi(toc_entry.index1[2]);

	for (i = 1; i <= numtracks; i++) {
		if (fread(&toc_entry, 1, sizeof(toc_entry), cdHandle) != sizeof(toc_entry))
			goto fail_io;

		ti[i].type = (toc_entry.type == 1) ? CDDA : DATA;

		ti[i].start_offset = btoi(toc_entry.index0[0]) * 60 * 75 +
			btoi(toc_entry.index0[1]) * 75 + btoi(toc_entry.index0[2]);
		ti[i].start_offset *= 2352;
		ti[i].start[0] = btoi(toc_entry.index1[0]);
		ti[i].start[1] = btoi(toc_entry.index1[1]);
		ti[i].start[2] = btoi(toc_entry.index1[2]);

		if (i > 1) {
			t = msf2sec(ti[i].start) - msf2sec(ti[i - 1].start);
			sec2msf(t, ti[i - 1].length);
		}
	}
	t = cd_length - ti[numtracks].start_offset / 2352;
	sec2msf(t, ti[numtracks].length);

	// seek to ISO index
	ret = fseeko(cdHandle, psisoimg_offs + 0x4000, SEEK_SET);
	if (ret != 0) {
		SysPrintf("failed to seek to ISO index\n");
		goto fail_io;
	}

	compr_img = calloc(1, sizeof(*compr_img));
	if (compr_img == NULL)
		goto fail_io;

	compr_img->block_shift = 4;
	compr_img->current_block = (unsigned int)-1;

	compr_img->index_len = (0x100000 - 0x4000) / sizeof(index_entry);
	compr_img->index_table = _mem2_malloc((compr_img->index_len + 1) * sizeof(compr_img->index_table[0]));
	if (compr_img->index_table == NULL)
		goto fail_io;

	cdimg_base = psisoimg_offs + 0x100000;
	for (i = 0; i < compr_img->index_len; i++) {
		ret = fread(&index_entry, 1, sizeof(index_entry), cdHandle);
		if (ret != sizeof(index_entry)) {
			SysPrintf("failed to read index_entry #%d\n", i);
			goto fail_index;
		}

		index_entry_size = SWAP32(index_entry.size);
		index_entry_offset = SWAP32(index_entry.offset);

		if (index_entry_size == 0)
			break;

		compr_img->index_table[i] = cdimg_base + index_entry_offset;
	}
	compr_img->index_table[i] = cdimg_base + index_entry_offset + index_entry_size;

	return 0;

fail_index:
	_mem2_free(compr_img->index_table);
	compr_img->index_table = NULL;
	goto done;

fail_io:
#ifndef NDEBUG
	SysPrintf(_("File IO error in <%s:%s>.\n"), __FILE__, __func__);
#endif

done:
	if (compr_img != NULL) {
		free(compr_img);
		compr_img = NULL;
	}
	return -1;
}

static int handlecbin(const char *isofile) {
	struct
	{
		char magic[4];
		unsigned int header_size;
		unsigned long long total_bytes;
		unsigned int block_size;
		unsigned char ver;		// 1
		unsigned char align;
		unsigned char rsv_06[2];
	} ciso_hdr;
	const char *ext = NULL;
	unsigned int *index_table = NULL;
	unsigned int index = 0, plain;
	int i, ret;

	if (strlen(isofile) >= 5)
		ext = isofile + strlen(isofile) - 5;
	if (ext == NULL || (strcasecmp(ext + 1, ".cbn") != 0 && strcasecmp(ext, ".cbin") != 0))
		return -1;

	fseek(cdHandle, 0, SEEK_SET);

	ret = fread(&ciso_hdr, 1, sizeof(ciso_hdr), cdHandle);
	if (ret != sizeof(ciso_hdr)) {
		SysPrintf("failed to read ciso header\n");
		return -1;
	}
	/* CISO is little-endian (written on PCs); the Wii is big-endian. Upstream runs on
	 * little-endian hosts and reads it raw, so on the Wii a .cbn never opened: the sizes
	 * and every index entry came out byte-swapped (and the image was read as raw sectors). */
	ciso_hdr.header_size = SWAP32(ciso_hdr.header_size);
	ciso_hdr.block_size = SWAP32(ciso_hdr.block_size);
	ciso_hdr.total_bytes = ((unsigned long long)SWAP32((unsigned int)ciso_hdr.total_bytes) << 32) |
	                       SWAP32((unsigned int)(ciso_hdr.total_bytes >> 32));

	if (strncmp(ciso_hdr.magic, "CISO", 4) != 0 || ciso_hdr.total_bytes <= 0 || ciso_hdr.block_size <= 0) {
		SysPrintf("bad ciso header\n");
		return -1;
	}
	if (ciso_hdr.header_size != 0 && ciso_hdr.header_size != sizeof(ciso_hdr)) {
		ret = fseeko(cdHandle, ciso_hdr.header_size, SEEK_SET);
		if (ret != 0) {
			SysPrintf("failed to seek to %x\n", ciso_hdr.header_size);
			return -1;
		}
	}

	compr_img = calloc(1, sizeof(*compr_img));
	if (compr_img == NULL)
		goto fail_io;

	compr_img->block_shift = 0;
	compr_img->current_block = (unsigned int)-1;

	compr_img->index_len = ciso_hdr.total_bytes / ciso_hdr.block_size;
	/* MEM2: on the Wii the MEM1 heap had no 864 KB block left after one game (a chain's second
	 * game, Frogger .cbn: the image opened as raw sectors and the game never started) */
	index_table = _mem2_malloc((compr_img->index_len + 1) * sizeof(index_table[0]));
	if (index_table == NULL) {
		SysPrintf("cbn: no memory for the index (%u bytes)\n", (compr_img->index_len + 1) * 4);
		goto fail_io;
	}

	/* CISO stores blocks + 1 index entries: the last marks where the last block ends. Only
	 * index_len were read and index_len + 1 used, so the last block's end was whatever the
	 * allocation held. An image with only index_len entries gets the end of the file. */
	ret = fread(index_table, sizeof(index_table[0]), compr_img->index_len + 1, cdHandle);
	if (ret == compr_img->index_len) {
		off_t end;
		fseeko(cdHandle, 0, SEEK_END);
		end = ftello(cdHandle);
		index_table[compr_img->index_len] = SWAP32((unsigned int)(end >> ciso_hdr.align));   /* stored as the file stores it */
	}
	else if (ret != compr_img->index_len + 1) {
		SysPrintf("failed to read index table\n");
		goto fail_index;
	}

	compr_img->index_table = _mem2_malloc((compr_img->index_len + 1) * sizeof(compr_img->index_table[0]));
	if (compr_img->index_table == NULL) {
		SysPrintf("cbn: no memory for the index table (%u bytes)\n", (compr_img->index_len + 1) * (unsigned)sizeof(compr_img->index_table[0]));
		goto fail_index;
	}

	for (i = 0; i < compr_img->index_len + 1; i++) {
		index = SWAP32(index_table[i]);
		plain = index & 0x80000000;
		index &= 0x7fffffff;
		compr_img->index_table[i] = (off_t)index << ciso_hdr.align;
		if (plain)
			compr_img->index_table[i] |= OFF_T_MSB;
	}

	/* the TOC (as upstream): one data track when no sheet named the tracks, and the last
	 * track's length from the uncompressed size -- a .cue beside the .cbn measured it from
	 * the compressed file (Frogger: a lead-out of 72397 for 108094) */
	if (numtracks == 0) {
		numtracks = 1;
		ti[1].type = DATA;
		ti[1].start_offset = 0;
		sec2msf(2 * 75, ti[1].start);
	}
	sec2msf((unsigned int)((ciso_hdr.total_bytes - ti[numtracks].start_offset) / 2352), ti[numtracks].length);
	_mem2_free(index_table);
	return 0;

fail_index:
	_mem2_free(index_table);
fail_io:
	if (compr_img != NULL) {
		free(compr_img);
		compr_img = NULL;
	}
	return -1;
}

#ifdef USE_LIBCHDR
static int handlechd(const char *isofile) {
	int frame_offset = 150;
	int file_offset = 0;
	int is_chd_ext = 0;
	chd_error err;

	if (strlen(isofile) >= 3) {
		const char *ext = isofile + strlen(isofile) - 3;
		is_chd_ext = !strcasecmp(ext, "chd");
	}
	chd_img = calloc(1, sizeof(*chd_img));
	if (chd_img == NULL)
		goto fail_io;

	err = chd_open_file(cdHandle, CHD_OPEN_READ, NULL, &chd_img->chd);
	if (err != CHDERR_NONE) {
		if (is_chd_ext)
			SysPrintf("chd_open: %d\n", err);
		goto fail_io;
	}

	chd_img->header = chd_get_header(chd_img->chd);

	chd_img->ways = cdChdHunks == CD_CHD_HUNKS_8 ? 8 : cdChdHunks == CD_CHD_HUNKS_4 ? 4 : 2;
	chd_img->buffer = _mem2_malloc(chd_img->header->hunkbytes * chd_img->ways);
	if (chd_img->buffer == NULL)
		goto fail_io;

	chd_img->sectors_per_hunk = chd_img->header->hunkbytes / (CD_FRAMESIZE_RAW + SUB_FRAMESIZE);
	{
		unsigned int w;
		for (w = 0; w < CHD_MAX_WAYS; w++) {
			chd_img->current_hunk[w] = (unsigned int)-1;
			chd_img->hunk_stamp[w] = 0;
		}
		chd_img->stamp = 0;
	}

	cddaBigEndian = FALSE;

	numtracks = 0;
	memset(ti, 0, sizeof(ti));

   while (1)
   {
      struct {
         char type[64];
         char subtype[32];
         char pgtype[32];
         char pgsub[32];
         uint32_t track;
         uint32_t frames;
         uint32_t pregap;
         uint32_t postgap;
      } md = {};
      char meta[256];
      uint32_t meta_size = 0;

      if (chd_get_metadata(chd_img->chd, CDROM_TRACK_METADATA2_TAG, numtracks, meta, sizeof(meta), &meta_size, NULL, NULL) == CHDERR_NONE)
         sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &md.track, md.type, md.subtype, &md.frames, &md.pregap, md.pgtype, md.pgsub, &md.postgap);
      else if (chd_get_metadata(chd_img->chd, CDROM_TRACK_METADATA_TAG, numtracks, meta, sizeof(meta), &meta_size, NULL, NULL) == CHDERR_NONE)
         sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &md.track, md.type, md.subtype, &md.frames);
      else
         break;

		SysPrintf("chd: %s\n", meta);

		if (md.track == 1) {
			if (!strncmp(md.subtype, "RW", 2)) {
				subChanMixed = TRUE;
				if (!strcmp(md.subtype, "RW_RAW"))
					subChanRaw = TRUE;
			}
		}

		ti[md.track].type = !strncmp(md.type, "AUDIO", 5) ? CDDA : DATA;

		/* A pregap of type V... is stored in the CHD and counted in md.frames; any other is
		 * silence the CHD leaves out. Every track is stored padded to CD_TRACK_PADDING (4)
		 * frames, so the next track starts in the file at the padded count -- without it a
		 * CHD's later tracks (CD audio) played from a few sectors early, and the lead-out
		 * came out a pregap late (scripts/disc_convert.py --check, Frogger: 2 and 150). */
		int pregap_in_file = md.pgtype[0] == 'V';
		sec2msf(frame_offset + md.pregap, ti[md.track].start);
		sec2msf(pregap_in_file ? md.frames - md.pregap : md.frames, ti[md.track].length);

		ti[md.track].start_offset = file_offset + (pregap_in_file ? md.pregap : 0);

		// XXX: what about postgap?
		frame_offset += pregap_in_file ? md.frames : md.frames + md.pregap;
		file_offset += (md.frames + CD_TRACK_PADDING - 1) & ~(CD_TRACK_PADDING - 1);
		numtracks++;
	}

	if (numtracks)
		return 0;

fail_io:
	if (chd_img != NULL) {
		free(chd_img->buffer);
		free(chd_img);
		chd_img = NULL;
	}
	return -1;
}
#endif // USE_LIBCHDR

// this function tries to get the .sub file of the given .img
static int opensubfile(const char *isoname) {
	char		subname[MAXPATHLEN];

	// copy name of the iso and change extension from .img to .sub
	strncpy(subname, isoname, sizeof(subname));
	subname[MAXPATHLEN - 1] = '\0';
	if (strlen(subname) >= 4) {
		strcpy(subname + strlen(subname) - 4, ".sub");
	}
	else {
		return -1;
	}

	subHandle = fopen(subname, "rb");
	if (subHandle == NULL)
		return -1;
	/* A small buffer, not the image's 16-256 KB one: the game reads sub-channel data
	 * only when it asks where the drive is (GetlocP), at scattered positions, so every
	 * read is a seek, and a seek empties the buffer. With 16 KB each 96-byte read cost
	 * a 16 KB card read (Gex 2 on the Wii: ~1750 of them, 28 MB, the game at 0.77x). */
	{
		static char sub_stdio_buf[2048] __attribute__((aligned(32)));
		setvbuf(subHandle, sub_stdio_buf, _IOFBF, sizeof sub_stdio_buf);
	}

	// A freshly opened handle could reuse a stale-tracked FILE* address --
	// see the identical comment on cd_attach_stdio_buffer().
	cdimg_seek_forget();

	return 0;
}

static int opensbifile(const char *isoname) {
	char		sbiname[MAXPATHLEN], disknum[MAXPATHLEN] = "0";
	int		s;

	strncpy(sbiname, isoname, sizeof(sbiname));
	sbiname[MAXPATHLEN - 1] = '\0';
	if (strlen(sbiname) >= 4) {
		if (cdrIsoMultidiskCount > 1) {
			sprintf(disknum, "_%i.sbi", cdrIsoMultidiskSelect + 1);
			strcpy(sbiname + strlen(sbiname) - 4, disknum);
		}
		else
			strcpy(sbiname + strlen(sbiname) - 4, ".sbi");
	}
	else {
		return -1;
	}

	fseek(cdHandle, 0, SEEK_END);
	s = ftell(cdHandle) / 2352;

	return LoadSBI(sbiname, s);
}

static int cdread_normal(FILE *f, unsigned int base, void *dest, int sector)
{
	int ret;
	long pos = base + sector * CD_FRAMESIZE_RAW;
	unsigned long long t0 = perf_now_us();
	int prefetched = pf_active && f == cdHandle;
	if (prefetched && pf_take(pos, dest)) {
		perf_cd_done(t0, CD_FRAMESIZE_RAW);
		return CD_FRAMESIZE_RAW;
	}
	if (cdimg_seek(f, pos))
		goto fail_io;
	ret = fread(dest, 1, CD_FRAMESIZE_RAW, f);
	cdimg_seek_advance(f, pos, ret);
	if (ret <= 0)
		goto fail_io;
	perf_cd_done(t0, ret);
	return ret;

fail_io:
	// often happens in cdda gaps of a split cue/bin, so not logged
	//SysPrintf("File IO error %d, base %u, sector %u\n", errno, base, sector);
	perf_cd_done(t0, -1);
	return -1;
}

static int cdread_sub_mixed(FILE *f, unsigned int base, void *dest, int sector)
{
	int ret;
	long pos = base + sector * (CD_FRAMESIZE_RAW + SUB_FRAMESIZE);
	unsigned long long t0 = perf_now_us();

	if (cdimg_seek(f, pos))
		goto fail_io;
	ret = fread(dest, 1, CD_FRAMESIZE_RAW, f);
	cdimg_seek_advance(f, pos, ret);
	if (ret <= 0)
		goto fail_io;
	perf_cd_done(t0, ret);
	return ret;

fail_io:
	//SysPrintf("File IO error %d, base %u, sector %u\n", errno, base, sector);
	perf_cd_done(t0, -1);
	return -1;
}

static int cdread_sub_sub_mixed(FILE *f, int sector)
{
	int ret;
	long pos = sector * (CD_FRAMESIZE_RAW + SUB_FRAMESIZE) + CD_FRAMESIZE_RAW;

	// Interleaved with cdread_sub_mixed on the same FILE*: must go through
	// cdimg_seek/cdimg_seek_advance too, or its seek would move the real
	// stream position without updating the tracker, leaving a stale
	// cdimg_seek_pos that could wrongly match (and skip) a later caller's
	// seek to that same offset.
	{
	unsigned long long t0 = perf_now_us();
	if (cdimg_seek(f, pos))
		goto fail_io_sub;
	ret = fread(subbuffer, 1, SUB_FRAMESIZE, f);
	cdimg_seek_advance(f, pos, ret);
	if (ret != SUB_FRAMESIZE)
		goto fail_io_sub;

	perf_cd_done(t0, SUB_FRAMESIZE);
	return SUB_FRAMESIZE;

fail_io_sub:
	SysPrintf("subchannel: file IO error %d, sector %u\n", errno, sector);
	perf_cd_done(t0, -1);
	return -1;
	}
}

/* Kept between sectors: inflateInit2 allocates a 32 KB window and a reset costs nothing.
 * At file scope so that ISOclose can give it back -- it used to be a function static, and
 * so was held for the life of the process. */
static z_stream z_inflate;

static int uncompress2_pcsx(z_stream *zp, void *out, unsigned long *out_size, void *in, unsigned long in_size)
{
	int ret = 0;

	if (zp->zalloc == NULL) {
		zp->next_in = Z_NULL;
		zp->avail_in = 0;
		zp->zalloc = Z_NULL;
		zp->zfree = Z_NULL;
		zp->opaque = Z_NULL;
		ret = inflateInit2(zp, -15);
	}
	else
		ret = inflateReset(zp);
	if (ret != Z_OK)
		return ret;

	zp->next_in = in;
	zp->avail_in = in_size;
	zp->next_out = out;
	zp->avail_out = *out_size;

	ret = inflate(zp, Z_NO_FLUSH);

	*out_size -= zp->avail_out;
	return ret == 1 ? 0 : ret;
}

static void uncompress2_pcsx_end(z_stream *zp)
{
	if (zp->zalloc != NULL) {
		inflateEnd(zp);
		memset(zp, 0, sizeof(*zp));
	}
}

static int cdread_compressed(FILE *f, unsigned int base, void *dest, int sector)
{
	unsigned long cdbuffer_size, cdbuffer_size_expect;
	unsigned int size;
	int is_compressed;
	off_t start_byte;
	int ret, block;
	unsigned long long t0 = perf_now_us();

	if (base)
		sector += base / 2352;

	block = sector >> compr_img->block_shift;
	compr_img->sector_in_blk = sector & ((1 << compr_img->block_shift) - 1);

	if (block == compr_img->current_block) {
		//printf("hit sect %d\n", sector);
		goto finish;
	}

	if (sector >= compr_img->index_len * 16) {
		SysPrintf("sector %d is past img end\n", sector);
		perf_cd_done(t0, -1);
		return -1;
	}

	/* the decode-ahead thread may have it already (pf_start in ISOopen) */
	if (pf_active && pf_take(block, compr_img->buff_raw[0]))
		goto decoded;

	start_byte = compr_img->index_table[block] & ~OFF_T_MSB;
	if (fseeko(cdHandle, start_byte, SEEK_SET) != 0) {
		SysPrintf("seek error for block %d at %llx: ",
			block, (long long)start_byte);
		perror(NULL);
		perf_cd_done(t0, -1);
		return -1;
	}

	is_compressed = !(compr_img->index_table[block] & OFF_T_MSB);
	size = (compr_img->index_table[block + 1] & ~OFF_T_MSB) - start_byte;
	if (size > sizeof(compr_img->buff_compressed)) {
		SysPrintf("block %d is too large: %u\n", block, size);
		perf_cd_done(t0, -1);
		return -1;
	}

	if (fread(is_compressed ? compr_img->buff_compressed : compr_img->buff_raw[0],
				1, size, cdHandle) != size) {
		SysPrintf("read error for block %d at %x: ", block, start_byte);
		perror(NULL);
		perf_cd_done(t0, -1);
		return -1;
	}

	if (is_compressed) {
		cdbuffer_size_expect = sizeof(compr_img->buff_raw[0]) << compr_img->block_shift;
		cdbuffer_size = cdbuffer_size_expect;
		ret = uncompress2_pcsx(&z_inflate, compr_img->buff_raw[0], &cdbuffer_size, compr_img->buff_compressed, size);
		if (ret != 0) {
			SysPrintf("uncompress failed with %d for block %d, sector %d\n",
					ret, block, sector);
			perf_cd_done(t0, -1);
			return -1;
		}
		if (cdbuffer_size != cdbuffer_size_expect)
			SysPrintf("cdbuffer_size: %lu != %lu, sector %d\n", cdbuffer_size,
					cdbuffer_size_expect, sector);
	}

	// done at last!
decoded:
	compr_img->current_block = block;

finish:
	if (dest != cdbuffer) // copy avoid HACK
		memcpy(dest, compr_img->buff_raw[compr_img->sector_in_blk],
			CD_FRAMESIZE_RAW);
	perf_cd_done(t0, CD_FRAMESIZE_RAW);
	return CD_FRAMESIZE_RAW;
}

#ifdef USE_LIBCHDR
static unsigned char *chd_get_sector(unsigned int current_buffer, unsigned int sector_in_hunk)
{
	return chd_img->buffer
		+ current_buffer * chd_img->header->hunkbytes
		+ sector_in_hunk * (CD_FRAMESIZE_RAW + SUB_FRAMESIZE);
}

/* Find the hunk in the cache, or decode it into the least recently used way.
 * Returns the way, or -1 when chd_read failed. */
static int chd_hunk_way(unsigned int hunk)
{
	unsigned int w, lru = 0;
	unsigned long long ct0;

	for (w = 0; w < chd_img->ways; w++) {
		if (chd_img->current_hunk[w] == hunk) {
			PERF_INC(chd_hit);
			chd_img->hunk_stamp[w] = ++chd_img->stamp;
			return (int)w;
		}
	}
	for (w = 1; w < chd_img->ways; w++)
		if (chd_img->hunk_stamp[w] < chd_img->hunk_stamp[lru])
			lru = w;

	PERF_INC(chd_miss);
	/* the decode-ahead thread may have it already (pf_start in ISOopen); chd_us stays the
	 * emulator's own decoding */
	if (!(pf_active && pf_take(hunk, chd_img->buffer + lru * chd_img->header->hunkbytes))) {
		ct0 = perf_now_us();
		if (chd_read(chd_img->chd, hunk, chd_img->buffer +
			lru * chd_img->header->hunkbytes) != CHDERR_NONE) {
			PERF_INC(chd_err);
			chd_img->current_hunk[lru] = (unsigned int)-1;
			return -1;
		}
		PERF_ADD(chd_us, perf_now_us() - ct0);
	}
	chd_img->current_hunk[lru] = hunk;
	chd_img->hunk_stamp[lru] = ++chd_img->stamp;
	return (int)lru;
}

static int cdread_chd(FILE *f, unsigned int base, void *dest, int sector)
{
	int way;
	unsigned long long t0 = perf_now_us();

	sector += base;

	chd_img->sector_in_hunk = sector % chd_img->sectors_per_hunk;
	way = chd_hunk_way(sector / chd_img->sectors_per_hunk);
	if (way < 0) {
		perf_cd_done(t0, -1);
		return -1;
	}
	chd_img->current_buffer = (unsigned int)way;

	if (dest != cdbuffer) // copy avoid HACK
		memcpy(dest, chd_get_sector(chd_img->current_buffer, chd_img->sector_in_hunk),
			CD_FRAMESIZE_RAW);
	perf_cd_done(t0, CD_FRAMESIZE_RAW);
	return CD_FRAMESIZE_RAW;
}

static int cdread_sub_chd(FILE *f, int sector)
{
	unsigned int sector_in_hunk;
	int way;

	if (!subChanMixed)
		return -1;

	sector_in_hunk = sector % chd_img->sectors_per_hunk;
	way = chd_hunk_way(sector / chd_img->sectors_per_hunk);
	if (way < 0)
		return -1;

	memcpy(subbuffer, chd_get_sector((unsigned int)way, sector_in_hunk) + CD_FRAMESIZE_RAW, SUB_FRAMESIZE);
	return SUB_FRAMESIZE;
}
#endif // USE_LIBCHDR

static int cdread_2048(FILE *f, unsigned int base, void *dest, int sector)
{
	int ret;
	long pos = base + sector * 2048;
	unsigned long long t0 = perf_now_us();

	if (pf_active && f == cdHandle && pf_take(pos, (char *)dest + 12 * 2)) {
		ret = 2048;
	} else {
		cdimg_seek(f, pos);
		ret = fread((char *)dest + 12 * 2, 1, 2048, f);
		cdimg_seek_advance(f, pos, ret);
	}
	perf_cd_done(t0, 12*2 + ret);

	// not really necessary, fake mode 2 header
	memset(cdbuffer, 0, 12 * 2);
	sec2msf(sector + 2 * 75, (char *)&cdbuffer[12]);
	cdbuffer[12 + 3] = 1;

	return 12*2 + ret;
}

static unsigned char * CALLBACK ISOgetBuffer_compr(void) {
	return compr_img->buff_raw[compr_img->sector_in_blk] + 12;
}

#ifdef USE_LIBCHDR
static unsigned char * CALLBACK ISOgetBuffer_chd(void) {
	return chd_get_sector(chd_img->current_buffer, chd_img->sector_in_hunk) + 12;
}
#endif // USE_LIBCHDR

static unsigned char * CALLBACK ISOgetBuffer(void) {
	return cdbuffer + 12;
}

static void PrintTracks(void) {
	int i;

	for (i = 1; i <= numtracks; i++) {
		SysPrintf(_("Track %.2d (%s) - Start %.2d:%.2d:%.2d, Length %.2d:%.2d:%.2d\n"),
			i, (ti[i].type == DATA ? "DATA" :
			    (ti[i].type == CDDA ? "AUDIO" : "UNKNOWN")),
			ti[i].start[0], ti[i].start[1], ti[i].start[2],
			ti[i].length[0], ti[i].length[1], ti[i].length[2]);
	}
}

const char *GetIsoFile(void) {
	return &isoFile.name[0];
}

#include "Gamecube/fileBrowser/fileBrowser.h"
#include "Gamecube/fileBrowser/fileBrowser-libfat.h"

// This function is invoked by the front-end when opening an ISO
// file for playback
static long CALLBACK ISOopen(void) {
	bool isMode1ISO = FALSE;
	char alt_bin_filename[MAXPATHLEN];
	const char *bin_filename;
	char image_str[1024] = {0};

	if (cdHandle != NULL) {
		return 0; // it's already open
	}

	if (strlen(GetIsoFile()) == 0 || isoFile_open(GetIsoFile()) == FILE_BROWSER_ERROR_NO_FILE) {
        return 0;
    }

	cdHandle = fopen(GetIsoFile(), "rb");
	if (cdHandle == NULL) {
		SysPrintf(_("Could't open '%s' for reading: %s\n"),
			GetIsoFile(), strerror(errno));
		return -1;
	}

	cd_attach_stdio_buffer(cdHandle);
	strncpy(cd_main_path, GetIsoFile(), sizeof(cd_main_path) - 1);
	sprintf(image_str, "Loaded CD Image: %s", GetIsoFile());

	cddaBigEndian = TRUE;
	subChanMixed = FALSE;
	subChanRaw = FALSE;
	pregapOffset = 0;
	cdrIsoMultidiskCount = 1;
	multifile = 0;

	CDR_getBuffer = ISOgetBuffer;
	cdimg_read_func = cdread_normal;
	cdimg_read_sub_func = NULL;

	if (parsetoc(GetIsoFile()) == 0) {
		strcat(image_str, "[+toc]");
	}
	else if (parseccd(GetIsoFile()) == 0) {
		strcat(image_str, "[+ccd]");
	}
	else if (parsemds(GetIsoFile()) == 0) {
		strcat(image_str, "[+mds]");
	}
	else if (parsecue(GetIsoFile()) == 0) {
		strcat(image_str, "[+cue]");
	}
	if (handlepbp(GetIsoFile()) == 0) {
		strcat(image_str, "[+pbp]");
		CDR_getBuffer = ISOgetBuffer_compr;
		cdimg_read_func = cdread_compressed;
	}
	else if (handlecbin(GetIsoFile()) == 0) {
		strcat(image_str, "[+cbin]");
		CDR_getBuffer = ISOgetBuffer_compr;
		cdimg_read_func = cdread_compressed;
	}
#ifdef USE_LIBCHDR
	else if (handlechd(GetIsoFile()) == 0) {
		strcat(image_str, "[+chd]");
		CDR_getBuffer = ISOgetBuffer_chd;
		cdimg_read_func = cdread_chd;
		cdimg_read_sub_func = cdread_sub_chd;
	}
#endif // USE_LIBCHDR

	if (!subChanMixed && opensubfile(GetIsoFile()) == 0) {
		strcat(image_str, "[+sub]");
	}
	if (opensbifile(GetIsoFile()) == 0) {
		strcat(image_str, "[+sbi]");
	}

	fseeko(cdHandle, 0, SEEK_END);

	// maybe user selected metadata file instead of main .bin ..
	bin_filename = GetIsoFile();
	if (ftello(cdHandle) < 2352 * 0x10) {
		static const char *exts[] = { ".bin", ".BIN", ".img", ".IMG" };
		FILE *tmpf = NULL;
		size_t i;
		char *p;

		strncpy(alt_bin_filename, bin_filename, sizeof(alt_bin_filename));
		alt_bin_filename[MAXPATHLEN - 1] = '\0';
		if (strlen(alt_bin_filename) >= 4) {
			p = alt_bin_filename + strlen(alt_bin_filename) - 4;
			for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
				strcpy(p, exts[i]);
				tmpf = fopen(alt_bin_filename, "rb");
				if (tmpf != NULL)
					break;
			}
		}
		if (tmpf != NULL) {
			bin_filename = alt_bin_filename;
			fclose(cdHandle);
			cdHandle = tmpf;
			cd_attach_stdio_buffer(cdHandle);
			strncpy(cd_main_path, alt_bin_filename, sizeof(cd_main_path) - 1);
			fseeko(cdHandle, 0, SEEK_END);

			isoFile.size = ftello(cdHandle);
		}
	}

	// guess whether it is mode1/2048
	if (cdimg_read_func == cdread_normal && ftello(cdHandle) % 2048 == 0) {
		unsigned int modeTest = 0;
		fseek(cdHandle, 0, SEEK_SET);
		if (!fread(&modeTest, sizeof(modeTest), 1, cdHandle)) {
			//SysPrintf(_("File IO error in <%s:%s>.\n"), __FILE__, __func__);
		}
		if (SWAP32(modeTest) != 0xffffff00) {
			strcat(image_str, "[2048]");
			isMode1ISO = TRUE;
		}
	}
	fseek(cdHandle, 0, SEEK_SET);

	/* A metadata-less image (an .iso, a lone .bin or .img): one data track, as upstream.
	 * Without it ti[1].type stayed 0 and the drive answered CdlID with "no disc". */
	if (numtracks == 0 && cdimg_read_func == cdread_normal) {
		off_t size_main;
		fseeko(cdHandle, 0, SEEK_END);
		size_main = ftello(cdHandle);
		fseek(cdHandle, 0, SEEK_SET);
		numtracks = 1;
		ti[1].type = DATA;
		ti[1].start_offset = 0;
		sec2msf(2 * 75, ti[1].start);
		sec2msf((unsigned int)(isMode1ISO ? size_main / 2048 : size_main / 2352), ti[1].length);
	}

	//SysPrintf("%s (%lld bytes).\n", image_str, (long long)size_main);

#ifdef PERF_PROF
	SysPrintf("%s\n", image_str);   /* the image kind and its track table, in tty.log (debug) */
	PrintTracks();
#endif

	if (subChanMixed && cdimg_read_func == cdread_normal) {
		cdimg_read_func = cdread_sub_mixed;
		cdimg_read_sub_func = cdread_sub_sub_mixed;
	}
	else if (isMode1ISO) {
		cdimg_read_func = cdread_2048;
		cdimg_read_sub_func = NULL;
	}

	// make sure we have another handle open for cdda
	if (numtracks > 1 && ti[1].handle == NULL) {
		ti[1].handle = fopen(bin_filename, "rb");
		cd_attach_stdio_buffer(ti[1].handle);
	}

	/* Raw images read ahead, compressed ones decode ahead (a block per slot, numbered). */
	if (cdimg_read_func == cdread_normal)
		pf_start(cd_main_path, CD_FRAMESIZE_RAW, CD_FRAMESIZE_RAW, PF_SLOTS, pf_read_at);
	else if (cdimg_read_func == cdread_2048)
		pf_start(cd_main_path, 2048, 2048, PF_SLOTS, pf_read_at);
	else if (cdimg_read_func == cdread_compressed)
		pf_start(cd_main_path, 1, CD_FRAMESIZE_RAW << compr_img->block_shift,
			DA_BYTES / (CD_FRAMESIZE_RAW << compr_img->block_shift), pf_compr_fetch);
#ifdef USE_LIBCHDR
	else if (cdimg_read_func == cdread_chd)
		pf_start(cd_main_path, 1, chd_img->header->hunkbytes,
			DA_BYTES / chd_img->header->hunkbytes, pf_chd_fetch);
#endif

	return 0;
}

static long CALLBACK ISOclose(void) {
	int i;

	pf_stop();
	if (cdHandle != NULL) {
		fclose(cdHandle);
		cdHandle = NULL;
	}
	if (subHandle != NULL) {
		fclose(subHandle);
		subHandle = NULL;
	}
	// Either closed FILE* could be reused by a later fopen() -- don't let a
	// stale tracked position falsely match against it.
	cdimg_seek_forget();

	if (compr_img != NULL) {
		_mem2_free(compr_img->index_table);
		free(compr_img);
		compr_img = NULL;
	}
	uncompress2_pcsx_end(&z_inflate);

#ifdef USE_LIBCHDR
	if (chd_img != NULL) {
		chd_close(chd_img->chd);
		_mem2_free(chd_img->buffer);
		free(chd_img);
		chd_img = NULL;
	}
#endif // USE_LIBCHDR

	for (i = 1; i <= numtracks; i++) {
		if (ti[i].handle != NULL) {
			fclose(ti[i].handle);
			ti[i].handle = NULL;
		}
	}
	numtracks = 0;
	ti[1].type = 0;
	cd_release_stdio_buffers();
	UnloadSBI();

	memset(cdbuffer, 0, sizeof(cdbuffer));
	CDR_getBuffer = ISOgetBuffer;

	return 0;
}

static long CALLBACK ISOinit(void) {
	assert(cdHandle == NULL);
	assert(subHandle == NULL);

	return 0; // do nothing
}

static long CALLBACK ISOshutdown(void) {
	ISOclose();
	return 0;
}

// return Starting and Ending Track
// buffer:
//  byte 0 - start track
//  byte 1 - end track
long CALLBACK ISOgetTN(unsigned char *buffer) {
	buffer[0] = 1;

	if (numtracks > 0) {
		buffer[1] = numtracks;
	}
	else {
		buffer[1] = 1;
	}

	return 0;
}

// return Track Time
// buffer:
//  byte 0 - frame
//  byte 1 - second
//  byte 2 - minute
static long CALLBACK ISOgetTD(unsigned char track, unsigned char *buffer) {
	if (track == 0) {
		unsigned int sect;
		unsigned char time[3];
		sect = msf2sec(ti[numtracks].start) + msf2sec(ti[numtracks].length);
		sec2msf(sect, (char *)time);
		buffer[2] = time[0];
		buffer[1] = time[1];
		buffer[0] = time[2];
	}
	else if (numtracks > 0 && track <= numtracks) {
		buffer[2] = ti[track].start[0];
		buffer[1] = ti[track].start[1];
		buffer[0] = ti[track].start[2];
	}
	else {
		buffer[2] = 0;
		buffer[1] = 2;
		buffer[0] = 0;
	}

	return 0;
}

// decode 'raw' subchannel data ripped by cdrdao
static void DecodeRawSubData(void) {
	unsigned char subQData[12];
	int i;

	memset(subQData, 0, sizeof(subQData));

	for (i = 0; i < 8 * 12; i++) {
		if (subbuffer[i] & (1 << 6)) { // only subchannel Q is needed
			subQData[i >> 3] |= (1 << (7 - (i & 7)));
		}
	}

	memcpy(&subbuffer[12], subQData, 12);
}

// read track
// time: byte 0 - minute; byte 1 - second; byte 2 - frame
// uses bcd format
static long CALLBACK ISOreadTrack(unsigned char *time) {
	//int sector = MSF2SECT(btoi(time[0]), btoi(time[1]), btoi(time[2]));
	int sector = MSF2SECT((time[0]), (time[1]), btoi(time[2]));
	long ret;

	if (cdHandle == NULL) {
		return 0;
	}

	if (pregapOffset && sector >= pregapOffset)
		sector -= 2 * 75;

	ret = cdimg_read_func(cdHandle, 0, cdbuffer, sector);
	if (ret <= 0)
		return 0;
#ifdef PERF_PROF
	{	/* "cdlog": who reads the image (perf.log "rw: reader" lines at the report) */
		extern void perf_cd_reader(void *ra, int sector);
		perf_cd_reader(__builtin_return_address(0), sector);
	}
	{	/* the per-vblank signature's CD part (perf_prof.c vsig): every sector the game got */
		extern u32 vsig_cd, vsig_cdn;
		u32 h = vsig_cd ^ (u32)sector;
		int k;
		for (k = 0; k < CD_FRAMESIZE_RAW; k++)
			h = (h ^ cdbuffer[k]) * 16777619u;
		vsig_cd = h;
		vsig_cdn++;
	}
#endif

	return 1;
}

// plays cdda audio
// sector: byte 0 - minute; byte 1 - second; byte 2 - frame
// does NOT uses bcd format
static long CALLBACK ISOplay(unsigned char *time) {
	return 0;
}

// stops cdda audio
static long CALLBACK ISOstop(void) {
	return 0;
}

// gets subchannel data
static unsigned char* CALLBACK ISOgetBufferSub(int sector) {
	if (pregapOffset && sector >= pregapOffset) {
		sector -= 2 * 75;
		if (sector < pregapOffset) // ?
			return NULL;
	}

	if (cdimg_read_sub_func != NULL) {
		if (cdimg_read_sub_func(cdHandle, sector) != SUB_FRAMESIZE)
			return NULL;
	}
	else if (subHandle != NULL) {
		int ret;
		long pos = sector * SUB_FRAMESIZE;
		if (cdimg_seek(subHandle, pos))
			return NULL;
		ret = fread(subbuffer, 1, SUB_FRAMESIZE, subHandle);
		cdimg_seek_advance(subHandle, pos, ret);
		if (ret != SUB_FRAMESIZE)
			return NULL;
	}
	else {
		return NULL;
	}

	if (subChanRaw) DecodeRawSubData();
	return subbuffer;
}

static long CALLBACK ISOgetStatus(struct CdrStat *stat) {
	CDR__getStatus(stat);

	// BIOS - boot ID (CD type)
	stat->Type = ti[1].type;

	return 0;
}

// read CDDA sector into buffer
long CALLBACK ISOreadCDDA(unsigned char m, unsigned char s, unsigned char f, unsigned char *buffer) {
	unsigned char msf[3] = {m, s, f};
	unsigned int track, track_start = 0;
	FILE *handle = cdHandle;
	unsigned int cddaCurPos;
	int ret;

	cddaCurPos = msf2sec((char *)msf);

	// find current track index
	for (track = numtracks; ; track--) {
		track_start = msf2sec(ti[track].start);
		if (track_start <= cddaCurPos)
			break;
		if (track == 1)
			break;
	}

	// data tracks play silent
	if (ti[track].type != CDDA) {
		memset(buffer, 0, CD_FRAMESIZE_RAW);
		return 0;
	}

	if (multifile) {
		// find the file that contains this track
		unsigned int file;
		for (file = track; file > 1; file--) {
			if (ti[file].handle != NULL) {
				handle = ti[file].handle;
				break;
			}
		}
	}
	if (!handle) {
		memset(buffer, 0, CD_FRAMESIZE_RAW);
		return -1;
	}

	ret = cdimg_read_func(handle, ti[track].start_offset,
		buffer, cddaCurPos - track_start);
	if (ret <= 0) {
		memset(buffer, 0, CD_FRAMESIZE_RAW);
		return -1;
	}

	if (cddaBigEndian) {
		int i;
		unsigned char tmp;

		for (i = 0; i < CD_FRAMESIZE_RAW / 2; i++) {
			tmp = buffer[i * 2];
			buffer[i * 2] = buffer[i * 2 + 1];
			buffer[i * 2 + 1] = tmp;
		}
	}

	return 0;
}

void cdrIsoInit(void) {
	CDR_init = ISOinit;
	CDR_shutdown = ISOshutdown;
	CDR_open = ISOopen;
	CDR_close = ISOclose;
	CDR_getTN = ISOgetTN;
	CDR_getTD = ISOgetTD;
	CDR_readTrack = ISOreadTrack;
	CDR_getBuffer = ISOgetBuffer;
	CDR_play = ISOplay;
	CDR_stop = ISOstop;
	CDR_getBufferSub = ISOgetBufferSub;
	CDR_getStatus = ISOgetStatus;
	CDR_readCDDA = ISOreadCDDA;

	CDR_getDriveLetter = CDR__getDriveLetter;
	CDR_configure = CDR__configure;
	CDR_test = CDR__test;
	CDR_about = CDR__about;
	CDR_setfilename = CDR__setfilename;

	numtracks = 0;
}

int cdrIsoActive(void) {
	return (cdHandle != NULL);
}
