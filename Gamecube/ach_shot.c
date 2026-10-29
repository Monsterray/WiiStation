/* The unlock pictures (ach_shot.h): the game's frame at the moment an achievement unlocks,
 * saved as a PNG without a visible hitch.
 *
 * Three steps, each off the emulated frame as far as it can be:
 *  1. ach_shot_request, at the unlock (a vblank, the emulator thread): the displayed picture is
 *     copied out of VRAM as PNG scanlines (screenshot_grab). That is the only cost in the frame
 *     (perf.log "ach_shot:" has it as grab). The toast is never in it: the overlay is drawn on
 *     the picture sent to the TV, not in VRAM.
 *  2. ach_shot_tick, once a vblank: deflate takes one slice (SLICE_IN bytes, about a
 *     millisecond) of the oldest waiting picture. No thread has to be scheduled for this: the
 *     frame limiter spins, so a low-priority thread would not run while a game does.
 *  3. A worker thread above the emulator's priority writes the file (mkdir, a temp file, a
 *     rename). It only waits on the card, so the emulator runs while it does; it does not
 *     compress.
 * Memory is MEM2: the picture, its PNG and deflate's workspace (a bump arena given to zlib), all
 * freed when the file is made. A request that finds no memory or no free job is dropped.
 * One picture per achievement and console: a file that exists is never replaced, and the
 * requests of one set and id are made once (the `have` table). */
#ifdef HW_RVL
#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/semaphore.h>
#include <zlib.h>
#include "ach_shot.h"
#include "screenshot.h"
#include "perf_prof.h"
#include "../achievements.h"
#include "../mem2_manager.h"

extern void SysPrintf(const char *fmt, ...);

#define JOBS 3
#define SLICE_IN (24 * 1024)         /* input bytes deflated per vblank */
#define ARENA_BYTES (400 * 1024)     /* deflate's workspace: about 290 KB at level 1 and a 32 KB window */
#define HAVE_N 256

enum { J_FREE, J_COMPRESS, J_WRITE, J_DONE };
enum { R_FAILED, R_SAVED, R_EXISTS };

typedef struct {
	volatile u8 state;
	volatile u8 result;
	char set[ACH_SET_ID_MAX];
	unsigned id, w, h, seq;
	u8 *raw, *out;
	unsigned long rawlen, inpos, png_len;
	z_stream zs;
	u8 zinit;
	u32 grab_us, comp_us, comp_max_us, slices;
	volatile u32 write_us;
} Job;

static Job jobs[JOBS];
static unsigned seq_next;
static struct { char set[ACH_SET_ID_MAX]; unsigned id; } have[HAVE_N];
static unsigned have_n, have_next;
static u8 *arena;
static unsigned long arena_used;
static lwp_t worker = LWP_THREAD_NULL;
static sem_t wake = LWP_SEM_NULL;
static u8 worker_stack[16 * 1024] __attribute__((aligned(32)));

static u32 us_since(u64 t0) { return (u32)ticks_to_microsecs(diff_ticks(t0, gettime())); }

static int have_it(const char *set, unsigned id)
{
	unsigned i;
	for (i = 0; i < have_n; i++)
		if (have[i].id == id && !strcmp(have[i].set, set)) return 1;
	return 0;
}

static void have_add(const char *set, unsigned id)
{
	if (have_it(set, id)) return;
	snprintf(have[have_next].set, sizeof have[have_next].set, "%s", set);
	have[have_next].id = id;
	have_next = (have_next + 1) % HAVE_N;
	if (have_n < HAVE_N) have_n++;
}

static int pending(const char *set, unsigned id)
{
	unsigned i;
	for (i = 0; i < JOBS; i++)
		if (jobs[i].state != J_FREE && jobs[i].id == id && !strcmp(jobs[i].set, set)) return 1;
	return 0;
}

/* zlib's allocator: a bump arena, so no deflate workspace comes from the heap (MEM1) and a
 * request never waits on the allocator's lock */
static void *z_alloc(void *opaque, uInt items, uInt size)
{
	unsigned long n = ((unsigned long)items * size + 31) & ~31ul;
	void *p;
	(void)opaque;
	if (!arena || arena_used + n > ARENA_BYTES) return Z_NULL;
	p = arena + arena_used;
	arena_used += n;
	return p;
}
static void z_free(void *opaque, void *p) { (void)opaque; (void)p; }

/* ---- the worker: the file ---- */

static void write_file(Job *j)
{
	char dir[96], path[112], tmp[120];
	FILE *f;
	u64 t0 = gettime();
	j->result = R_FAILED;
	if (!ach_shot_path(j->set, j->id, path, sizeof path)) return;
	mkdir("sd:/wiistation", 0777);
	mkdir("sd:/wiistation/achievements", 0777);
	mkdir("sd:/wiistation/achievements/shots", 0777);
	snprintf(dir, sizeof dir, ACH_SHOT_DIR "%s", j->set);
	mkdir(dir, 0777);
	if ((f = fopen(path, "rb"))) {   /* the first picture stays */
		fclose(f);
		j->result = R_EXISTS;
		goto done;
	}
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if (!(f = fopen(tmp, "wb"))) goto done;
	if (fwrite(j->out, 1, j->png_len, f) != j->png_len) {
		fclose(f);
		remove(tmp);
		goto done;
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		remove(tmp);
		goto done;
	}
	j->result = R_SAVED;
done:
	j->write_us = us_since(t0);
#if !PERF_PLAY
	{
		FILE *lf = perf_log_open();   /* here, off the emulator thread: this is an append to the card */
		if (lf) {
			fprintf(lf, "ach_shot: %s %s/%u %ux%u %lu bytes: grab %u us, %u slices %u us (max %u us), write %u us\n",
			        j->result == R_SAVED ? "saved" : j->result == R_EXISTS ? "exists" : "FAILED", j->set, j->id,
			        j->w, j->h, j->png_len, j->grab_us, j->slices, j->comp_us, j->comp_max_us, j->write_us);
			perf_log_close(lf);
		}
	}
#endif
}

static void *worker_main(void *arg)
{
	unsigned i;
	(void)arg;
	for (;;) {
		LWP_SemWait(wake);
		for (i = 0; i < JOBS; i++)
			if (jobs[i].state == J_WRITE) {
				write_file(&jobs[i]);
				jobs[i].state = J_DONE;
			}
	}
	return NULL;
}

/* ---- the emulator thread ---- */

static void job_free(Job *j)
{
	if (j->zinit) deflateEnd(&j->zs);
	j->zinit = 0;
	if (j->raw) _mem2_free(j->raw);
	if (j->out) _mem2_free(j->out);
	j->raw = j->out = NULL;
	j->state = J_FREE;
}

/* The compression is over, in whatever way: the arena is let go, and the file is next (or the end) */
static void compress_done(Job *j, int ok)
{
	if (j->zinit) deflateEnd(&j->zs);
	j->zinit = 0;
	if (arena) _mem2_free(arena);
	arena = NULL;
	if (j->raw) _mem2_free(j->raw);
	j->raw = NULL;
	if (!ok) {
		SysPrintf("Achievements: no picture for %s/%u (compression failed)\n", j->set, j->id);
		job_free(j);
		return;
	}
	j->png_len = screenshot_png_seal(j->out, j->zs.total_out, j->w, j->h);
	j->state = J_WRITE;
	LWP_SemPost(wake);
}

/* One slice of j's deflate */
static void slice(Job *j)
{
	u64 t0 = gettime();
	unsigned long left = j->rawlen - j->inpos, n = left > SLICE_IN ? SLICE_IN : left;
	u32 us;
	int r;
	if (!j->zinit) {   /* the first slice: the workspace, then deflate's start */
		unsigned long cap = compressBound(j->rawlen) + 64;
		if (!arena) {
			arena = _mem2_malloc(ARENA_BYTES);
			arena_used = 0;
		}
		memset(&j->zs, 0, sizeof j->zs);
		j->zs.zalloc = z_alloc;
		j->zs.zfree = z_free;
		if (!arena || deflateInit2(&j->zs, Z_BEST_SPEED, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
			compress_done(j, 0);
			return;
		}
		j->zinit = 1;
		j->zs.next_out = j->out + SHOT_PNG_HEAD;
		j->zs.avail_out = (uInt)(cap > 0x7fffffff ? 0x7fffffff : cap);
	}
	j->zs.next_in = j->raw + j->inpos;
	j->zs.avail_in = (uInt)n;
	r = deflate(&j->zs, n == left ? Z_FINISH : Z_NO_FLUSH);
	j->inpos += n;
	us = us_since(t0);
	j->comp_us += us;
	if (us > j->comp_max_us) j->comp_max_us = us;
	j->slices++;
	if (n == left)
		compress_done(j, r == Z_STREAM_END);
	else if (r != Z_OK)
		compress_done(j, 0);
}

/* A finished job: its set and id are known to have a file now (or not to be wanted) */
static void reap(Job *j)
{
	if (j->result != R_FAILED) have_add(j->set, j->id);
	else SysPrintf("Achievements: the picture for %s/%u could not be saved\n", j->set, j->id);
	job_free(j);
}

void ach_shot_request(const char *set, unsigned id)
{
	Job *j = NULL;
	unsigned i;
	u64 t0;
	if (!ach_shot_set_ok(set) || have_it(set, id) || pending(set, id)) return;
	for (i = 0; i < JOBS; i++)
		if (jobs[i].state == J_FREE) { j = &jobs[i]; break; }
	if (!j) {
		SysPrintf("Achievements: no picture for %s/%u (the others are still being made)\n", set, id);
		return;
	}
	if (worker == LWP_THREAD_NULL) {
		LWP_SemInit(&wake, 0, JOBS);
		if (LWP_CreateThread(&worker, worker_main, NULL, worker_stack, sizeof worker_stack, 66) < 0) {
			worker = LWP_THREAD_NULL;
			return;
		}
	}
	t0 = gettime();
	memset(j, 0, sizeof *j);
	snprintf(j->set, sizeof j->set, "%s", set);
	j->id = id;
	j->raw = screenshot_grab(&j->w, &j->h);
	if (!j->raw) return;
	j->rawlen = (unsigned long)j->h * (1 + 3 * j->w);
	j->out = _mem2_malloc(SHOT_PNG_HEAD + compressBound(j->rawlen) + 64 + SHOT_PNG_TAIL);
	if (!j->out) {
		_mem2_free(j->raw);
		j->raw = NULL;
		return;
	}
	j->seq = ++seq_next;
	j->grab_us = us_since(t0);
	j->state = J_COMPRESS;
}

/* The oldest picture still to compress */
static Job *next_compress(void)
{
	Job *best = NULL;
	unsigned i;
	for (i = 0; i < JOBS; i++)
		if (jobs[i].state == J_COMPRESS && (!best || jobs[i].seq < best->seq)) best = &jobs[i];
	return best;
}

void ach_shot_tick(void)
{
	Job *j;
	unsigned i;
	for (i = 0; i < JOBS; i++)
		if (jobs[i].state == J_DONE) reap(&jobs[i]);
	if ((j = next_compress())) slice(j);
}

void ach_shot_flush(void)
{
	unsigned i, waited = 0;
	Job *j;
	if (worker == LWP_THREAD_NULL) return;
	for (;;) {
		int busy = 0;
		for (i = 0; i < JOBS; i++) {
			if (jobs[i].state == J_DONE) reap(&jobs[i]);
			busy |= jobs[i].state != J_FREE;
		}
		if (!busy || waited > 5000) return;
		if ((j = next_compress())) {
			slice(j);
		} else {
			usleep(1000);
			waited++;
		}
	}
}

#else /* the GameCube build has no pictures */
#include "ach_shot.h"
void ach_shot_request(const char *set, unsigned id) { (void)set; (void)id; }
void ach_shot_tick(void) {}
void ach_shot_flush(void) {}
#endif
