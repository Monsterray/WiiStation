/* play_log.c - the play build's logs (scripts/build.sh PROBES=play, WiiSXRX_play.dol).
 * Docs/DEBUG_PLAY.md says the same for players; scripts/play_logs.py reads the folder.
 *
 * Everything goes to sd:/wiistation/debuglogs/, nothing anywhere else:
 *   play.log      per game session: a start line (time, build, game ID, file), the settings
 *                 file read, the settings in force at every start from the menu ("play: go"),
 *                 the perf reports (every 1800 presents and at every return to the menu), a
 *                 crash HBC kept from an earlier run, and an end line. Over 4 MB it becomes
 *                 play_old.log (the older one is deleted).
 *   vtl_<id>.bin  the session's per-vblank timeline (VTL2, scripts/vtl_view.py): 14 bytes a
 *                 vblank, 16 MB (about 5 hours) at most per session. At a session start the
 *                 oldest are deleted while all of them take more than 32 MB.
 *   hb.log        the heartbeat (hb_probe.c), appended at every boot; over 1 MB -> hb_old.log.
 * So the folder never holds much more than 60 MB.
 *
 * The emulator thread never writes the card: perf.log lines and vtl records go into two RAM
 * queues (MEM2), which the heartbeat thread appends to the files every 2 s and the main
 * thread empties at the safe points (a session's end, a fatal stop). A full queue drops what
 * does not fit (counted on the end line); it never waits. */
#include "perf_prof.h"

#if defined(PERF_PROF) && PERF_PLAY
#include <gccore.h>
#include <ogc/machine/processor.h>   /* _CPU_ISR_Disable */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "version.h"
#include "../mem2_manager.h"
#define HBC_AGENT_LAYOUT_ONLY
#include "../deps/hbc_agent/sdk/hbc_agent.h"

#define PLAY_DIR        "sd:/wiistation/debuglogs"
#define TEXT_CAP        (4u << 20)
#define HB_CAP          (1u << 20)
#define VTL_SESSION_CAP (16u << 20)
#define VTL_TOTAL_CAP   (32u << 20)
#define QSIZE           (64u << 10)
#define SCRATCH         (32u << 10)   /* one perf report is about 5 KB */

typedef struct { char *buf; volatile unsigned head, tail; unsigned drops; } queue_t;
static queue_t text_q, vtl_q;
static char *scratch;
static int scratch_busy;
static mutex_t drain_mx = LWP_MUTEX_NULL;
static char session[20];       /* "20261019-153000"; empty outside a session */
static char vtl_path[64];
static int vtl_fresh;          /* the session's first write truncates (two sessions in one second) */
static unsigned vtl_bytes;

/* Any thread; interrupts off only for the copy, a few microseconds for a report. */
static void q_put(queue_t *q, const void *p, unsigned n)
{
	u32 level;
	unsigned h, k, first;

	if (!q->buf)
		return;
	_CPU_ISR_Disable(level);
	h = q->head;
	if (h - q->tail + n > QSIZE) {
		q->drops++;
	} else {
		k = h % QSIZE;
		first = n < QSIZE - k ? n : QSIZE - k;
		memcpy(q->buf + k, p, first);
		memcpy(q->buf, (const char *)p + first, n - first);
		q->head = h + n;
	}
	_CPU_ISR_Restore(level);
}

/* Under drain_mx. Producers only write past head, so [tail, head) is stable while written. */
static void q_drain(queue_t *q, const char *path, const char *mode)
{
	unsigned t = q->tail, n = q->head - t, k, first;
	FILE *f;

	if (!n || !(f = fopen(path, mode)))
		return;
	k = t % QSIZE;
	first = n < QSIZE - k ? n : QSIZE - k;
	fwrite(q->buf + k, 1, first, f);
	if (n > first)
		fwrite(q->buf, 1, n - first, f);
	fclose(f);
	q->tail = t + n;
}

static void rotate(const char *path, const char *old, unsigned cap)
{
	struct stat st;
	if (!stat(path, &st) && (unsigned)st.st_size > cap) {
		remove(old);
		rename(path, old);
	}
}

void play_drain(void)
{
	if (drain_mx == LWP_MUTEX_NULL)
		return;
	LWP_MutexLock(drain_mx);
	rotate(PLAY_DIR "/play.log", PLAY_DIR "/play_old.log", TEXT_CAP);
	rotate(PLAY_DIR "/hb.log", PLAY_DIR "/hb_old.log", HB_CAP);
	q_drain(&text_q, PLAY_DIR "/play.log", "a");
	if (vtl_path[0]) {
		unsigned before = vtl_q.tail;
		q_drain(&vtl_q, vtl_path, vtl_fresh ? "wb" : "ab");
		if (vtl_q.tail != before)
			vtl_fresh = 0;
	}
	LWP_MutexUnlock(drain_mx);
}

FILE *perf_log_open(void)
{
	FILE *f;
	if (!scratch || scratch_busy || !(f = fmemopen(scratch, SCRATCH, "w")))
		return NULL;
	scratch_busy = 1;
	return f;
}

void perf_log_close(FILE *f)
{
	long n;
	fflush(f);
	n = ftell(f);
	fclose(f);
	if (n > 0)
		q_put(&text_q, scratch, n < (long)SCRATCH ? (unsigned)n : SCRATCH);
	scratch_busy = 0;
}

void play_vtl_put(const void *rec, unsigned n)
{
	if (!session[0] || vtl_bytes + n > VTL_SESSION_CAP)
		return;
	vtl_bytes += n;
	q_put(&vtl_q, rec, n);
}

static int by_name(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* The oldest timelines go while all of them take more than VTL_TOTAL_CAP. The names hold
 * the start time, so their order is their age. */
static void prune_vtl(void)
{
	static char names[128][32];
	unsigned sizes[128], total = 0;
	int n = 0, i;
	char path[64];
	struct stat st;
	struct dirent *e;
	DIR *d = opendir(PLAY_DIR);

	if (!d)
		return;
	while ((e = readdir(d)) && n < 128)
		if (!strncmp(e->d_name, "vtl_", 4) && strlen(e->d_name) < sizeof names[0])
			strcpy(names[n++], e->d_name);
	closedir(d);
	qsort(names, n, sizeof names[0], by_name);
	for (i = 0; i < n; i++) {
		snprintf(path, sizeof path, PLAY_DIR "/%s", names[i]);
		sizes[i] = stat(path, &st) ? 0 : (unsigned)st.st_size;
		total += sizes[i];
	}
	for (i = 0; i < n && total > VTL_TOTAL_CAP; i++) {
		snprintf(path, sizeof path, PLAY_DIR "/%s", names[i]);
		remove(path);
		total -= sizes[i];
	}
}

static void log_line(const char *fmt, const char *a, const char *b)
{
	FILE *f = perf_log_open();
	if (f) {
		fprintf(f, fmt, a, b);
		perf_log_close(f);
	}
}

void play_session_end(const char *why)
{
	char when[32], line[160];
	if (!session[0])
		return;
	perf_datetime(when, sizeof when);
	snprintf(line, sizeof line, "at=%s why=%s text_drops=%u vtl_drops=%u vtl_bytes=%u", when,
		why, text_q.drops, vtl_q.drops, vtl_bytes);
	log_line("=== play session %s end %s ===\n", session, line);
	play_drain();
	LWP_MutexLock(drain_mx);   /* the heartbeat thread may be writing it */
	session[0] = 0;
	vtl_path[0] = 0;
	LWP_MutexUnlock(drain_mx);
}

void play_session_begin(const char *game_id, const char *file)
{
	static const unsigned hdr[3] = { 0x56544c32, 9, 0 };   /* "VTL2", VTL_SHIFT (perf_prof.c), count 0 = to the end */
	char when[32], line[320];
	const char *s;
	int k = 0;

	if (drain_mx == LWP_MUTEX_NULL)
		return;
	play_session_end("next_game");
	perf_datetime(when, sizeof when);
	for (s = when; *s && k < (int)sizeof session - 1; s++)   /* "2026-10-19 15:30:00" -> "20261019-153000" */
		if (*s >= '0' && *s <= '9')
			session[k++] = *s;
		else if (*s == ' ')
			session[k++] = '-';
	session[k] = 0;
	prune_vtl();
	LWP_MutexLock(drain_mx);
	snprintf(vtl_path, sizeof vtl_path, PLAY_DIR "/vtl_%s.bin", session);
	vtl_fresh = 1;
	vtl_bytes = 0;
	text_q.drops = vtl_q.drops = 0;
	LWP_MutexUnlock(drain_mx);
	play_vtl_put(hdr, sizeof hdr);
	snprintf(line, sizeof line, "at=%s build=%s %s %s game=%s file=%s", when, WIISTATION_VERSION,
		__DATE__, __TIME__, game_id && game_id[0] ? game_id : "-", file ? file : "-");
	log_line("=== play session %s start %s ===\n", session, line);
}

/* A crash or hang the HBC agent recorded (deps/hbc_agent, `hbc.py crash`) stays in MEM2 until
 * HBC clears it, so the same one can be seen at several boots: play_logs.py keeps one per
 * check word. */
static void log_kept_crash(void)
{
	const hbc_crash_block *b = (const hbc_crash_block *)HBC_CRASH_ADDR;
	FILE *f;
	int i;

	DCInvalidateRange((void *)b, sizeof *b);
	if (b->magic != HBC_CRASH_MAGIC || b->version < 2 || b->check != hbc_crash_check(b))
		return;
	if (!(f = perf_log_open()))
		return;
	fprintf(f, "crash: check=%08x kind=%u exception=%u code=%u pc=%08x lr=%08x dar=%08x dsisr=%08x"
		" uptime_ms=%u app=%.20s reason=%.64s frames=", (unsigned)b->check, (unsigned)b->kind,
		(unsigned)b->exception, (unsigned)b->code, (unsigned)b->pc, (unsigned)b->lr,
		(unsigned)b->dar, (unsigned)b->dsisr, (unsigned)b->uptime_ms, b->app, b->reason);
	for (i = 0; i < HBC_CRASH_FRAMES && b->frames[i]; i++)
		fprintf(f, "%s%08x", i ? "," : "", (unsigned)b->frames[i]);
	fprintf(f, "\n");
	perf_log_close(f);
}

void play_boot(void)
{
	char when[32], line[96];

	mkdir(PLAY_DIR, 0777);
	text_q.buf = (char *)_mem2_malloc(QSIZE);
	vtl_q.buf = (char *)_mem2_malloc(QSIZE);
	scratch = (char *)_mem2_malloc(SCRATCH);
	LWP_MutexInit(&drain_mx, false);
	perf_datetime(when, sizeof when);
	snprintf(line, sizeof line, "build=%s %s %s", WIISTATION_VERSION, __DATE__, __TIME__);
	log_line("=== play boot at=%s %s ===\n", when, line);
	log_kept_crash();
	play_drain();
}
#endif
