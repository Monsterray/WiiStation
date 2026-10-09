/**
 * WiiStation - smb2dev.c
 *
 * "smb:" as a newlib device over libsmb2 (SMB2/3): the file browser lists it with opendir(),
 * and the CD code reads games from it with fopen()/fread() as from the SD card (smb2dev.h).
 *
 * One libsmb2 context serves the whole device, and a context is not safe for two threads,
 * so every call takes one lock: the menu lists folders while the emulator reads the game.
 * A connection per open file was measured on the bench Wii (2026-10-08) and dropped: IOS
 * does not overlap socket calls, so two connections were no faster than one.
 *
 * Speed. The bench Wii's Wi-Fi carries about 1 MB/s of plain TCP to the Wii (hbc-reborn's
 * tests/netblock, 2026-10-08), but a reply to an SMB read comes in at about 650 KB/s, about
 * 1.3 KB per IOS call of about 2 ms. So the device sends as few bytes as it can, in small
 * requests, and takes the replies on a thread of its own:
 *  - Reads go into 16 KB blocks (one stdio buffer, CdBuffer), 16 of them, shared by every
 *    handle on the same file. A read larger than a block sends a request per block at once.
 *  - The block after the one a read ended in is requested at once, so it comes in while the
 *    game uses this one (cdriso.c starts no CD read-ahead of its own for a game here).
 *  - The pump thread keeps the socket serviced while requests are in flight
 *    (smb2_pread_async()), so replies come in while the game runs.
 * Measured on the bench Wii, Ape Escape's first 1200 vblanks: 5.7.0 (one request at a time,
 * on the game's thread, growing to 128 KB) made the game wait 7.8-8.8 s; this, 0.6-5 s with
 * Wi-Fi conditions, and as fast as the SD card (0.96x) on a good run. Fetching further ahead
 * was slower: 64-256 KB ahead waited 5.7-6.7 s, the data sent ahead of a jump being wasted
 * and in the way of what the game wanted next (2026-10-08, .runs/hw_pipe_ab*).
 *
 * The blocks are allocated at the first mount and never freed: blocks allocated per open
 * and freed per close fragmented the heap the next game's recompiler needs (5.7.1).
 *
 * A request that fails means the connection is gone (the server restarted, Wi-Fi dropped).
 * The device then connects again and opens the file again, once, before it gives up, so a
 * game keeps running across a short network drop.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <gccore.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <ogc/lwp_watchdog.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../deps/libsmb2/include/smb2/smb2.h"
#include "../../deps/libsmb2/include/smb2/libsmb2.h"
#include <network.h>
#include <time.h>
#include "smb2dev.h"

#define BLOCK_BITS     4
#define SMB_BLOCKS     (1 << BLOCK_BITS)   /* 16 blocks, 256 KB in all, never freed */
#define SMB_BLOCK      (16 * 1024)    /* one request: one stdio buffer */
#define SMB_OPEN_MAX   8
#define SMB_TIMEOUT    10             /* seconds before a request counts as lost */
#define PUMP_PRIO      67             /* above the emulator (64), whose frame limiter busy-waits */
#define PUMP_POLL_MS   20             /* the pump's longest wait: libsmb2's timeouts run then */
#define SEQ_MASK       (0xFFFFFFFFu >> BLOCK_BITS)

enum { B_FREE, B_PENDING, B_READY, B_FAILED };

typedef struct {
	u8 *buf;
	u32 key;            /* the file: key_of() */
	u64 off;            /* file offset of buf[0] */
	u32 len;            /* bytes asked for while pending, bytes got once ready */
	u32 used;           /* last use, for the replacement choice */
	u32 seq;            /* the request in flight: a callback for an older one is ignored */
	u8 state;
	struct smb2fh *fh;  /* the handle a pending request reads through */
	u64 t0;             /* when the request went out */
} smb_block;

typedef struct smb_file_s {
	struct smb2fh *fh;
	char path[256];
	u32 key;
	u64 size, pos;
	u64 stream;   /* where the next request starts */
} smb_file;

typedef struct {
	struct smb2dir *dir;
} smb_dir;

static struct smb2_context *ctx;
static mutex_t lock = LWP_MUTEX_NULL;
static cond_t work_cond = LWP_COND_NULL;   /* a request went out: the pump has work */
static cond_t done_cond = LWP_COND_NULL;   /* a request ended */
static lwp_t pump_thread = LWP_THREAD_NULL;
static int mounted;
static int pending;            /* requests in flight */
static u32 generation;         /* a new context: the pump's poll of the old socket is stale */
static u32 failures;           /* requests that failed, for a reader waiting on one */
static int broken;             /* smb2_service() failed: no request goes out until a reconnect,
                                * as a late reply to one given up on could land in its block */
static u32 use_clock, req_seq;
static char cfg_ip[64], cfg_share[128], cfg_user[64], cfg_pass[64];
static char last_error[128];
static smb2dev_stats_t stats;
static smb_block blocks[SMB_BLOCKS];
static smb_file *open_files[SMB_OPEN_MAX];

/* deps/libsmb2/lib/compat.c: what the sockets cost */
extern struct smb2_wii_counters {
	unsigned polls, recvs, sends;
	unsigned long long poll_us, recv_us, send_us, recv_bytes;
} smb2_wii_count;

/* "smb:/a/b" -> "a/b"; libsmb2 wants the path inside the share, and turns '/' into '\'. */
static const char *share_path(const char *path)
{
	const char *p = strchr(path, ':');
	p = p ? p + 1 : path;
	while (*p == '/')
		++p;
	return p;
}

/* Which file a block holds: the path, size and time, so a file changed on the server
 * does not match the blocks of the old one */
static u32 key_of(const char *path, u64 size, u64 mtime)
{
	u32 h = 2166136261u;
	const unsigned char *p;

	for (p = (const unsigned char *)path; *p; p++)
		h = (h ^ *p) * 16777619u;
	h = (h ^ (u32)size) * 16777619u;
	h = (h ^ (u32)(size >> 32)) * 16777619u;
	h = (h ^ (u32)mtime) * 16777619u;
	return h ? h : 1;
}

static void note_error(const char *what)
{
	snprintf(last_error, sizeof last_error, "%s: %s", what, ctx ? smb2_get_error(ctx) : "no context");
}

/* The connection is gone or replaced: every request in flight is lost. Their callbacks may
 * still come (libsmb2 cancels them when the context goes); block_cb() ignores them. */
static void fail_pending(void)
{
	int i;

	for (i = 0; i < SMB_BLOCKS; i++)
		if (blocks[i].state == B_PENDING) {
			blocks[i].state = B_FAILED;
			blocks[i].fh = NULL;
			failures++;
		}
	pending = 0;
	LWP_CondBroadcast(done_cond);
}

/* Make the context and connect it. Called with the lock held. */
static int connect_ctx(void)
{
	fail_pending();
	generation++;
	broken = 0;
	if (ctx)
		smb2_destroy_context(ctx);
	ctx = smb2_init_context();
	if (!ctx) {
		snprintf(last_error, sizeof last_error, "no memory for the SMB context");
		return -ENOMEM;
	}
	smb2_set_security_mode(ctx, SMB2_NEGOTIATE_SIGNING_ENABLED);
	smb2_set_version(ctx, SMB2_VERSION_ANY);
	smb2_set_timeout(ctx, SMB_TIMEOUT);
	if (cfg_user[0])
		smb2_set_user(ctx, cfg_user);
	if (cfg_pass[0])
		smb2_set_password(ctx, cfg_pass);
	if (smb2_connect_share(ctx, cfg_ip, cfg_share, cfg_user[0] ? cfg_user : NULL) < 0) {
		note_error("connect");
		smb2_destroy_context(ctx);
		ctx = NULL;
		return -EIO;
	}
	return 0;
}

/* The connection is gone: connect again and open every open file again. Lock held. */
static int reconnect_all(void)
{
	int i;

	stats.reconnects++;
	if (connect_ctx() < 0)
		return -1;
	for (i = 0; i < SMB_OPEN_MAX; i++) {
		smb_file *o = open_files[i];
		if (!o)
			continue;
		o->fh = smb2_open(ctx, o->path, O_RDONLY);
		if (!o->fh)
			note_error("open after reconnect");
	}
	return 0;
}

/* One read at an offset, whole, into buf, waiting for it: when no block can take it. Lock held. */
static int pread_full(smb_file *f, u8 *buf, u32 len, u64 off)
{
	u32 got = 0;
	int retried = 0;
	u64 t0 = gettime();

	if (broken) {   /* the connection failed under the pump: start on a new one */
		retried = 1;
		if (reconnect_all() < 0 || !f->fh)
			return -1;
	}
	while (got < len) {
		int n = f->fh && ctx ? smb2_pread(ctx, f->fh, buf + got, len - got, off + got) : -ENOTCONN;
		if (n < 0) {
			note_error("read");
			if (retried++ || reconnect_all() < 0 || !f->fh)
				return -1;
			continue;
		}
		if (n == 0)
			break;   /* end of file */
		got += n;
		stats.requests++;
	}
	stats.bytes += got;
	stats.us += ticks_to_microsecs(diff_ticks(t0, gettime()));
	return (int)got;
}

/* A request ended (in smb2_service(), on whichever thread called it, with the lock held) */
static void block_cb(struct smb2_context *c, int status, void *command_data, void *priv)
{
	u32 v = (u32)priv;
	smb_block *b = &blocks[v & (SMB_BLOCKS - 1)];

	(void)c;
	(void)command_data;
	if (b->state != B_PENDING || b->seq != v >> BLOCK_BITS)
		return;   /* given up on by fail_pending(): the block has moved on */
	if (status >= 0) {
		b->state = B_READY;
		b->len = (u32)status;
		stats.bytes += (u32)status;
	} else {
		b->state = B_FAILED;
		failures++;
		note_error("read");
	}
	b->fh = NULL;
	stats.req_us += ticks_to_microsecs(diff_ticks(b->t0, gettime()));
	pending--;
	LWP_CondBroadcast(done_cond);
}

/* The block of this file that holds offset at, ready or on its way */
static smb_block *find_block(u32 key, u64 at)
{
	int i;

	for (i = 0; i < SMB_BLOCKS; i++) {
		smb_block *b = &blocks[i];
		if ((b->state == B_PENDING || b->state == B_READY) && b->key == key
		    && at >= b->off && at < b->off + b->len)
			return b;
	}
	return NULL;
}

/* A block to reuse: a free or failed one, else the one least recently used */
static smb_block *victim(void)
{
	smb_block *v = NULL;
	int i;

	for (i = 0; i < SMB_BLOCKS; i++) {
		smb_block *b = &blocks[i];
		if (!b->buf || b->state == B_PENDING)
			continue;
		if (b->state != B_READY)
			return b;
		if (!v || b->used < v->used)
			v = b;
	}
	return v;
}

/* Send a read of len bytes at off into a block, without waiting for it. Lock held. */
static smb_block *issue(smb_file *f, u64 off, u32 len)
{
	smb_block *b;

	if (off >= f->size || !f->fh || !ctx || broken || pump_thread == LWP_THREAD_NULL)
		return NULL;   /* the caller reads synchronously; on a broken connection that fails
		                * and connects again */
	if (len > f->size - off)
		len = (u32)(f->size - off);
	b = victim();
	if (!b)
		return NULL;   /* every block is in flight */
	b->state = B_PENDING;
	b->key = f->key;
	b->off = off;
	b->len = len;
	b->fh = f->fh;
	b->used = ++use_clock;
	b->seq = ++req_seq & SEQ_MASK;
	b->t0 = gettime();
	if (smb2_pread_async(ctx, f->fh, b->buf, len, off, block_cb,
	                     (void *)((b->seq << BLOCK_BITS) | (u32)(b - blocks))) < 0) {
		b->state = B_FREE;
		note_error("read");
		return NULL;
	}
	pending++;
	if ((unsigned)pending > stats.inflight_max)
		stats.inflight_max = pending;
	stats.requests++;
	LWP_CondSignal(work_cond);
	return b;
}

/* Have pos to pos + span requested, a block per request. f->stream is where the requests
 * so far end; a read elsewhere (a jump) moves it. Lock held. */
static void top_up(smb_file *f, u64 pos, u32 span)
{
	u64 end = pos + span;

	if (f->stream < pos || f->stream > end)
		f->stream = pos;
	if (end > f->size)
		end = f->size;
	while (f->stream < end) {
		smb_block *b = find_block(f->key, f->stream);
		u32 len;

		if (b) {   /* fetched before: carry on past it */
			if (!b->len)
				break;
			f->stream = b->off + b->len;
			continue;
		}
		len = end - f->stream > SMB_BLOCK ? SMB_BLOCK : (u32)(end - f->stream);
		if (!issue(f, f->stream, len))
			break;
		f->stream += len;
	}
}

static int smb_open(struct _reent *r, void *fs, const char *path, int flags, int mode)
{
	smb_file *f = (smb_file *)fs;
	struct smb2_stat_64 st;
	int i;

	(void)mode;
	if ((flags & O_ACCMODE) != O_RDONLY) {
		r->_errno = EROFS;   /* games and folders are only read */
		return -1;
	}
	memset(f, 0, sizeof(*f));
	snprintf(f->path, sizeof f->path, "%s", share_path(path));
	LWP_MutexLock(lock);
	if (!mounted || !ctx || smb2_stat(ctx, f->path, &st) < 0 || st.smb2_type == SMB2_TYPE_DIRECTORY
	    || !(f->fh = smb2_open(ctx, f->path, O_RDONLY))) {
		LWP_MutexUnlock(lock);
		r->_errno = mounted ? ENOENT : ENODEV;
		return -1;
	}
	f->size = st.smb2_size;
	f->key = key_of(f->path, st.smb2_size, st.smb2_mtime);
	/* A file nobody has open starts with no blocks: a game opened again reads afresh, and a
	 * chained A/B run of the same game starts each run alike. */
	for (i = 0; i < SMB_OPEN_MAX; i++)
		if (open_files[i] && open_files[i]->key == f->key)
			break;
	if (i == SMB_OPEN_MAX)
		for (i = 0; i < SMB_BLOCKS; i++)
			if (blocks[i].key == f->key && blocks[i].state == B_READY)
				blocks[i].state = B_FREE;
	for (i = 0; i < SMB_OPEN_MAX; i++)
		if (!open_files[i]) {
			open_files[i] = f;
			break;
		}
	LWP_MutexUnlock(lock);
	return (int)f;
}

static int smb_close(struct _reent *r, void *fd)
{
	smb_file *f = (smb_file *)fd;
	int i, busy = 1, tries;

	(void)r;
	LWP_MutexLock(lock);
	/* A reply still on its way names this handle (libsmb2's read callback writes to it), so
	 * those come in first. */
	for (tries = 0; busy && tries < SMB_TIMEOUT * 5; tries++) {
		struct timespec ts = { 0, 200 * 1000 * 1000 };
		busy = 0;
		for (i = 0; i < SMB_BLOCKS; i++)
			if (blocks[i].state == B_PENDING && blocks[i].fh == f->fh)
				busy = 1;
		if (busy)
			LWP_CondTimedWait(done_cond, lock, &ts);
	}
	if (f->fh && ctx && !busy)
		smb2_close(ctx, f->fh);
	for (i = 0; i < SMB_OPEN_MAX; i++)
		if (open_files[i] == f)
			open_files[i] = NULL;
	LWP_MutexUnlock(lock);
	memset(f, 0, sizeof(*f));
	return 0;
}

static ssize_t smb_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
	smb_file *f = (smb_file *)fd;
	size_t done = 0;
	int retried = 0;

	if (f->pos >= f->size || !len)
		return 0;
	if (len > f->size - f->pos)
		len = (size_t)(f->size - f->pos);

	LWP_MutexLock(lock);
	while (done < len) {
		u64 at = f->pos + done;
		u32 want = len - done < SMB_BLOCK ? (u32)(len - done) : SMB_BLOCK;
		smb_block *b;
		u32 k;

		top_up(f, at, (u32)(len - done) > 4 * SMB_BLOCK ? 4 * SMB_BLOCK : (u32)(len - done));
		b = find_block(f->key, at);
		if (!b) {
			/* nothing on its way here: the requests so far are somewhere else */
			f->stream = at;
			top_up(f, at, want);
			b = find_block(f->key, at);
		}
		if (!b) {
			/* every block in flight, or no pump: read it here and wait */
			int n = pread_full(f, (u8 *)ptr + done, want, at);
			if (n <= 0)
				break;
			done += n;
			continue;
		}
		if (b->state == B_PENDING) {
			struct timespec ts = { 1, 0 };
			u32 failed = failures;
			u64 t0 = gettime();

			stats.waits++;
			LWP_CondTimedWait(done_cond, lock, &ts);
			stats.us += ticks_to_microsecs(diff_ticks(t0, gettime()));
			if (failures != failed && !find_block(f->key, at)) {
				/* this request failed: the connection is gone. Connect again, once. */
				if (retried++ || reconnect_all() < 0 || !f->fh)
					break;
				f->stream = at;
			}
			continue;   /* look again: answered, failed, or still on its way */
		}
		k = (u32)(b->off + b->len - at);
		if (!k)
			break;   /* end of file */
		if (k > len - done)
			k = len - done;
		memcpy(ptr + done, b->buf + (at - b->off), k);
		b->used = ++use_clock;
		stats.cache_hits++;
		done += k;
	}
	top_up(f, f->pos + done, SMB_BLOCK);   /* the next block, while the game uses this one */
	LWP_MutexUnlock(lock);
	if (!done && len) {
		r->_errno = EIO;
		return -1;
	}
	f->pos += done;
	return (ssize_t)done;
}

static off_t smb_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
	smb_file *f = (smb_file *)fd;
	s64 to;

	switch (dir) {
	case SEEK_SET: to = pos; break;
	case SEEK_CUR: to = (s64)f->pos + pos; break;
	case SEEK_END: to = (s64)f->size + pos; break;
	default: r->_errno = EINVAL; return -1;
	}
	if (to < 0) {
		r->_errno = EINVAL;
		return -1;
	}
	f->pos = (u64)to;
	return (off_t)to;
}

/* The pump: services the socket while requests are in flight, so their replies come in
 * while the game runs. It waits in IOS's poll without the lock; the replies are taken in
 * smb2_service() with it. Callers of the synchronous calls (stat, open, a folder listing)
 * service the same socket while they wait, under the lock, and take replies for the pump's
 * requests as they come. */
static void *pump_main(void *arg)
{
	struct pollsd p;
	s32 n;
	u32 gen;
	u64 t0;

	(void)arg;
	LWP_MutexLock(lock);
	for (;;) {
		while (!pending || !ctx)
			LWP_CondWait(work_cond, lock);
		p.socket = smb2_get_fd(ctx);
		p.events = smb2_which_events(ctx);
		p.revents = 0;
		gen = generation;
		LWP_MutexUnlock(lock);
		t0 = gettime();
		if (p.socket >= 0)
			n = net_poll(&p, 1, PUMP_POLL_MS);
		else {
			usleep(PUMP_POLL_MS * 1000);
			n = 0;
		}
		LWP_MutexLock(lock);
		stats.pump_polls++;
		stats.pump_poll_us += ticks_to_microsecs(diff_ticks(t0, gettime()));
		/* with no event too: libsmb2 times out lost requests in smb2_service() */
		if (ctx && gen == generation && !broken && smb2_service(ctx, n > 0 ? p.revents : 0) < 0) {
			note_error("read");
			broken = 1;
			fail_pending();
		}
	}
	return NULL;
}

static void fill_stat(struct stat *st, const struct smb2_stat_64 *s)
{
	memset(st, 0, sizeof(*st));
	st->st_mode = s->smb2_type == SMB2_TYPE_DIRECTORY ? (S_IFDIR | 0555) : (S_IFREG | 0444);
	st->st_size = (off_t)s->smb2_size;
	st->st_nlink = 1;
	st->st_mtime = (time_t)s->smb2_mtime;
	st->st_blksize = SMB_BLOCK;
}

static int smb_fstat(struct _reent *r, void *fd, struct stat *st)
{
	smb_file *f = (smb_file *)fd;

	(void)r;
	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFREG | 0444;
	st->st_size = (off_t)f->size;
	st->st_nlink = 1;
	st->st_blksize = SMB_BLOCK;
	return 0;
}

static int smb_stat(struct _reent *r, const char *path, struct stat *st)
{
	struct smb2_stat_64 s;
	const char *p = share_path(path);
	int ret;

	LWP_MutexLock(lock);
	if (!mounted || !ctx) {
		LWP_MutexUnlock(lock);
		r->_errno = ENODEV;
		return -1;
	}
	if (!*p) {   /* the share itself */
		LWP_MutexUnlock(lock);
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFDIR | 0555;
		return 0;
	}
	ret = smb2_stat(ctx, p, &s);
	LWP_MutexUnlock(lock);
	if (ret < 0) {
		r->_errno = ENOENT;
		return -1;
	}
	fill_stat(st, &s);
	return 0;
}

static DIR_ITER *smb_diropen(struct _reent *r, DIR_ITER *ds, const char *path)
{
	smb_dir *d = (smb_dir *)ds->dirStruct;

	LWP_MutexLock(lock);
	d->dir = mounted && ctx ? smb2_opendir(ctx, share_path(path)) : NULL;
	if (!d->dir && ctx)
		note_error("opendir");
	LWP_MutexUnlock(lock);
	if (!d->dir) {
		r->_errno = ENOENT;
		return NULL;
	}
	return ds;
}

static int smb_dirreset(struct _reent *r, DIR_ITER *ds)
{
	smb_dir *d = (smb_dir *)ds->dirStruct;

	(void)r;
	LWP_MutexLock(lock);
	if (d->dir && ctx)
		smb2_rewinddir(ctx, d->dir);
	LWP_MutexUnlock(lock);
	return 0;
}

static int smb_dirnext(struct _reent *r, DIR_ITER *ds, char *filename, struct stat *st)
{
	smb_dir *d = (smb_dir *)ds->dirStruct;
	struct smb2dirent *e;

	LWP_MutexLock(lock);
	e = d->dir && ctx ? smb2_readdir(ctx, d->dir) : NULL;
	if (e) {
		snprintf(filename, NAME_MAX, "%s", e->name);
		if (st)
			fill_stat(st, &e->st);
	}
	LWP_MutexUnlock(lock);
	if (!e) {
		r->_errno = ENOENT;   /* the end of the folder */
		return -1;
	}
	return 0;
}

static int smb_dirclose(struct _reent *r, DIR_ITER *ds)
{
	smb_dir *d = (smb_dir *)ds->dirStruct;

	(void)r;
	LWP_MutexLock(lock);
	if (d->dir && ctx)
		smb2_closedir(ctx, d->dir);
	d->dir = NULL;
	LWP_MutexUnlock(lock);
	return 0;
}

static const devoptab_t smb_devoptab = {
	"smb", sizeof(smb_file),
	smb_open, smb_close, NULL, smb_read, smb_seek, smb_fstat, smb_stat,
	NULL, NULL, NULL, NULL, NULL,
	sizeof(smb_dir), smb_diropen, smb_dirreset, smb_dirnext, smb_dirclose,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL
};

int smb2dev_mount(const char *ip, const char *share, const char *user, const char *password)
{
	int ret, i;

	if (lock == LWP_MUTEX_NULL)
		LWP_MutexInit(&lock, true);
	LWP_MutexLock(lock);
	if (work_cond == LWP_COND_NULL) {
		LWP_CondInit(&work_cond);
		LWP_CondInit(&done_cond);
		for (i = 0; i < SMB_BLOCKS; i++)
			blocks[i].buf = (u8 *)memalign(32, SMB_BLOCK);
		if (LWP_CreateThread(&pump_thread, pump_main, NULL, NULL, 16 * 1024, PUMP_PRIO) != 0)
			pump_thread = LWP_THREAD_NULL;   /* reads then wait for each request */
	}
	snprintf(cfg_ip, sizeof cfg_ip, "%s", ip);
	snprintf(cfg_share, sizeof cfg_share, "%s", share);
	snprintf(cfg_user, sizeof cfg_user, "%s", user ? user : "");
	snprintf(cfg_pass, sizeof cfg_pass, "%s", password ? password : "");
	ret = connect_ctx();
	if (ret == 0 && !mounted) {
		AddDevice(&smb_devoptab);
		mounted = 1;
	}
	LWP_MutexUnlock(lock);
	return ret;
}

void smb2dev_unmount(void)
{
	if (lock == LWP_MUTEX_NULL)
		LWP_MutexInit(&lock, true);
	LWP_MutexLock(lock);
	if (mounted) {
		RemoveDevice("smb:");
		mounted = 0;
	}
	fail_pending();
	generation++;
	if (ctx) {
		smb2_disconnect_share(ctx);
		smb2_destroy_context(ctx);
		ctx = NULL;
	}
	LWP_MutexUnlock(lock);
}

int smb2dev_mounted(void)
{
	return mounted;
}

const char *smb2dev_error(void)
{
	return last_error;
}

void smb2dev_stats(smb2dev_stats_t *out)
{
	*out = stats;
	out->polls = smb2_wii_count.polls;
	out->recvs = smb2_wii_count.recvs;
	out->sends = smb2_wii_count.sends;
	out->poll_us = smb2_wii_count.poll_us;
	out->recv_us = smb2_wii_count.recv_us;
	out->send_us = smb2_wii_count.send_us;
	out->recv_bytes = smb2_wii_count.recv_bytes;
}
