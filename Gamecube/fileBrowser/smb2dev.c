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
 * Speed. The Wii's Wi-Fi delivers about 350 KB/s of TCP data: IOS hands over about one
 * segment per call, and a call takes about 2 ms (deps/libsmb2/lib/compat.c). So the device
 * fetches as little as it can. stdio asks for its buffer (CdBuffer, 16 KB) at a time; each
 * open file keeps two read windows. A read that jumps (a seek, a CHD hunk) fetches 16 KB;
 * each read that carries on where the last fill ended fetches twice as much as that fill,
 * up to 128 KB, so a game streaming its disc costs few requests and a game hopping about
 * does not pay for data it skips. A handle also looks in the windows of other handles on
 * the same file. A read larger than a window goes straight to the caller's buffer.
 * cdriso.c starts no CD read-ahead for a game here: sectors read ahead and never used
 * would cost the game time at this speed.
 *
 * The windows come from one pool, allocated at the first mount and never freed. Allocating
 * them per open and freeing them per close let a later game's start reuse that memory,
 * and the second game from the share in one boot crashed in Lightrec (a map count of 0:
 * something in the game-switch path still used memory it had freed).
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
#include "smb2dev.h"

#define SMB_WINDOW   (128 * 1024)   /* the largest fill: a long sequential read */
#define SMB_JUMP     (16 * 1024)    /* the fill after a jump: one stdio buffer */
#define SMB_WINDOWS  2              /* per open file */
#define SMB_POOL     8              /* windows in all: four files open at once */
#define SMB_OPEN_MAX 8
#define SMB_TIMEOUT  10             /* seconds before a request counts as lost */

typedef struct {
	u8 *buf;
	u64 off;       /* file offset of buf[0] */
	u32 len;       /* bytes valid; 0 = empty */
	u32 used;      /* last use, for the replacement choice */
} smb_window;

typedef struct smb_file_s {
	struct smb2fh *fh;
	char path[256];
	u64 size, pos;
	u64 next_seq;   /* where the last fill ended: a miss there is sequential */
	u32 seq_fill;   /* the size of the last fill: sequential fills double, up to SMB_WINDOW */
	smb_window win[SMB_WINDOWS];
} smb_file;

typedef struct {
	struct smb2dir *dir;
} smb_dir;

static struct smb2_context *ctx;
static mutex_t lock = LWP_MUTEX_NULL;
static int mounted;
static u32 use_clock;
static char cfg_ip[64], cfg_share[128], cfg_user[64], cfg_pass[64];
static char last_error[128];
static smb2dev_stats_t stats;
static u8 *pool_buf[SMB_POOL];
static u8 pool_used[SMB_POOL];
static smb_file *open_files[SMB_OPEN_MAX];

/* deps/libsmb2/lib/compat.c: what the sockets cost */
extern struct smb2_wii_counters {
	unsigned polls, recvs, sends;
	unsigned long long poll_us, recv_us, send_us, recv_bytes;
} smb2_wii_count;

/* Pool and open-file table: called with the lock held */
static u8 *pool_get(void)
{
	int i;
	for (i = 0; i < SMB_POOL; i++)
		if (pool_buf[i] && !pool_used[i]) {
			pool_used[i] = 1;
			return pool_buf[i];
		}
	return NULL;   /* the file reads without windows */
}

static void pool_put(u8 *b)
{
	int i;
	for (i = 0; i < SMB_POOL; i++)
		if (b && pool_buf[i] == b)
			pool_used[i] = 0;
}

/* "smb:/a/b" -> "a/b"; libsmb2 wants the path inside the share, and turns '/' into '\'. */
static const char *share_path(const char *path)
{
	const char *p = strchr(path, ':');
	p = p ? p + 1 : path;
	while (*p == '/')
		++p;
	return p;
}

static void note_error(const char *what)
{
	snprintf(last_error, sizeof last_error, "%s: %s", what, ctx ? smb2_get_error(ctx) : "no context");
}

/* Make the context and connect it. Called with the lock held. */
static int connect_ctx(void)
{
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

/* The connection is gone: connect again and open the file again. Lock held. */
static int reconnect_file(smb_file *f)
{
	stats.reconnects++;
	if (connect_ctx() < 0)
		return -1;
	f->fh = smb2_open(ctx, f->path, O_RDONLY);
	if (!f->fh) {
		note_error("open after reconnect");
		return -1;
	}
	return 0;
}

/* One read at an offset, whole, into buf; reconnects once if the request fails. Lock held. */
static int pread_full(smb_file *f, u8 *buf, u32 len, u64 off)
{
	u32 got = 0;
	int retried = 0;
	u64 t0 = gettime();

	while (got < len) {
		int n = f->fh && ctx ? smb2_pread(ctx, f->fh, buf + got, len - got, off + got) : -ENOTCONN;
		if (n < 0) {
			note_error("read");
			if (retried++ || reconnect_file(f) < 0)
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

/* A window of this file, or of another handle on the same file, that holds offset at */
static smb_window *find_window(smb_file *f, u64 at)
{
	int k, i;

	for (k = -1; k < SMB_OPEN_MAX; k++) {
		smb_file *o = k < 0 ? f : open_files[k];
		if (!o || (k >= 0 && (o == f || strcmp(o->path, f->path))))
			continue;
		for (i = 0; i < SMB_WINDOWS; i++)
			if (o->win[i].len && at >= o->win[i].off && at < o->win[i].off + o->win[i].len)
				return &o->win[i];
	}
	return NULL;
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
	for (i = 0; i < SMB_WINDOWS; i++)
		f->win[i].buf = pool_get();
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
	int i;

	(void)r;
	LWP_MutexLock(lock);
	if (f->fh && ctx)
		smb2_close(ctx, f->fh);
	for (i = 0; i < SMB_OPEN_MAX; i++)
		if (open_files[i] == f)
			open_files[i] = NULL;
	for (i = 0; i < SMB_WINDOWS; i++)
		pool_put(f->win[i].buf);
	LWP_MutexUnlock(lock);
	memset(f, 0, sizeof(*f));
	return 0;
}

static ssize_t smb_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
	smb_file *f = (smb_file *)fd;
	size_t done = 0;

	if (f->pos >= f->size || !len)
		return 0;
	if (len > f->size - f->pos)
		len = (size_t)(f->size - f->pos);

	LWP_MutexLock(lock);
	while (done < len) {
		u64 at = f->pos + done;
		smb_window *w = find_window(f, at);
		u32 want;
		int i, n;

		if (w) {
			u32 k = (u32)(w->off + w->len - at);
			if (k > len - done)
				k = len - done;
			memcpy(ptr + done, w->buf + (at - w->off), k);
			w->used = ++use_clock;
			stats.cache_hits++;
			done += k;
			continue;
		}
		if (len - done >= SMB_WINDOW || !f->win[0].buf) {
			/* a large read, or no window: straight into the caller's buffer */
			n = pread_full(f, (u8 *)ptr + done, (u32)(len - done), at);
			if (n <= 0)
				break;
			done += n;
			continue;
		}
		/* a miss: fill this file's least recently used window */
		w = &f->win[0];
		for (i = 1; i < SMB_WINDOWS; i++)
			if (f->win[i].buf && f->win[i].used < w->used)
				w = &f->win[i];
		if (at == f->next_seq && f->seq_fill)
			want = f->seq_fill * 2 > SMB_WINDOW ? SMB_WINDOW : f->seq_fill * 2;
		else
			want = SMB_JUMP;
		f->seq_fill = want;
		if (want > f->size - at)
			want = (u32)(f->size - at);
		w->len = 0;
		n = pread_full(f, w->buf, want, at);
		if (n <= 0)
			break;
		w->off = at;
		w->len = n;
		w->used = ++use_clock;
		f->next_seq = at + n;
	}
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

static void fill_stat(struct stat *st, const struct smb2_stat_64 *s)
{
	memset(st, 0, sizeof(*st));
	st->st_mode = s->smb2_type == SMB2_TYPE_DIRECTORY ? (S_IFDIR | 0555) : (S_IFREG | 0444);
	st->st_size = (off_t)s->smb2_size;
	st->st_nlink = 1;
	st->st_mtime = (time_t)s->smb2_mtime;
	st->st_blksize = SMB_WINDOW;
}

static int smb_fstat(struct _reent *r, void *fd, struct stat *st)
{
	smb_file *f = (smb_file *)fd;

	(void)r;
	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFREG | 0444;
	st->st_size = (off_t)f->size;
	st->st_nlink = 1;
	st->st_blksize = SMB_WINDOW;
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
	if (!pool_buf[0])
		for (i = 0; i < SMB_POOL; i++)
			pool_buf[i] = (u8 *)memalign(32, SMB_WINDOW);
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
