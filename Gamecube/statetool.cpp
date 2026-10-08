/* statetool.cpp - save states for scripted runs (Docs/SAVE_STATES.md).
 *
 * A run can save the game at a vblank, load a saved game back, and check that a loaded state
 * goes on exactly as the game did. Input script lines (PadWiiSX.c):
 *
 *   state save NAME <vblank>     save to sd:/wiistation/states/NAME.st, with NAME.txt beside it
 *   state load NAME <vblank>     load it
 *   statefp <vblank>             log a fingerprint of the machine (two boots can be compared)
 *   statecheck <vblank> <n>      save at <vblank>, run n vblanks and take a fingerprint of the
 *                                machine; load the state, run the same n vblanks, fingerprint
 *                                again. perf.log's "statecheck:" line says if they match.
 *
 * and a chain line's State=NAME loads that state as soon as the game has booted, so several
 * chain lines can run from the same moment with different settings.
 *
 * A request stops the CPU at its vblank (psxcounters.c), and the save or load runs in go()
 * with the CPU stopped, as the menu does it. Saving from inside the vblank handler, which the
 * old "statetest" did, stopped the game part way through a frame. */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>

extern "C" {
#include "../psxcommon.h"
#include "../misc.h"
#include "../psxmem.h"
#include "../r3000a.h"
#include "../mem2_manager.h"
	extern int stop;
	extern u32 frame_counter;
	extern unsigned chain_stop_vbl;
	extern int state_quiet;
	void state_fingerprint(unsigned out[6]);
	void statetool_vblank(void);
	int statetool_service(void);
	void statetool_reset(void);
	void statetool_request(int op, const char *name, unsigned vbl, unsigned n);
	void makeParentDirs(const char *path);
}
extern char AutobootROM[1024];
extern char AutobootPath[1024];
void writeConfig(FILE *f);

enum { ST_SAVE = 1, ST_LOAD, ST_CHECK, ST_FP };
#define ST_MAX 8
#define ST_DIR "sd:/wiistation/states/"
#define ST_CHECK_FILE ST_DIR "_statecheck.st"

static struct {
	int op;
	char name[48];
	unsigned vbl, n;
	int step;          /* statecheck: 0 save, 1 first fingerprint + load, 2 second fingerprint */
	bool done;
	unsigned fp[6];    /* statecheck: the first fingerprint */
	unsigned at_save[6];   /* statecheck: the machine as it was saved */
} req[ST_MAX];
static int nreq;
static int due = -1;   /* the request that stopped the CPU, or -1 */

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* statecheck: psxH and psxRegs as saved, to name the bytes a load does not put back.
 * The copies (2.1 MB with snap_ram) come from MEM2 and are given back when the check ends.
 * They were MEM1 mallocs kept for the rest of the boot: in a chain, the next game had
 * ~700 KB of MEM1 left, and SaveStateFile's 540 KB SPU buffer failed (ENOMEM), so every
 * statecheck after the first chained game said "save ... FAILED". */
static u8 *snap_h, *snap_regs, *snap_ram, *snap_regs2;
static const unsigned SNAP_REGS = sizeof(psxRegisters);

static void snap_free(void)
{
	u8 **p[4] = { &snap_h, &snap_regs, &snap_ram, &snap_regs2 };
	int k;
	for (k = 0; k < 4; k++)
		if (*p[k]) { _mem2_free(*p[k]); *p[k] = NULL; }
}

static void snap_take(void)
{
	if (!snap_h) snap_h = (u8 *)_mem2_malloc(0x10000);
	if (!snap_regs) snap_regs = (u8 *)_mem2_malloc(SNAP_REGS);
	if (snap_h) memcpy(snap_h, psxH, 0x10000);
	if (snap_regs) memcpy(snap_regs, &psxRegs, SNAP_REGS);
}

/* One line per region: the first differing 32-bit words, as offset:saved>loaded */
static void snap_diff(const char *what, const u8 *a, const u8 *b, unsigned len)
{
	char line[512];
	int n = snprintf(line, sizeof line, "statecheck: %s differs at", what), shown = 0, total = 0;
	unsigned o;
	for (o = 0; o + 4 <= len; o += 4) {
		u32 x, y;
		memcpy(&x, a + o, 4); memcpy(&y, b + o, 4);
		if (x == y) continue;
		total++;
		if (shown < 12 && n < (int)sizeof line - 40) {
			n += snprintf(line + n, sizeof line - n, " %04x:%08x>%08x", o, (unsigned)x, (unsigned)y);
			shown++;
		}
	}
	if (total) log_line("%s (%d words)\n", line, total);
}

/* statecheck: RAM and psxRegs at the first fingerprint, to name what the second one differs in */
static void snap_take2(void)
{
	if (!snap_ram) snap_ram = (u8 *)_mem2_malloc(0x200000);
	if (!snap_regs2) snap_regs2 = (u8 *)_mem2_malloc(SNAP_REGS);
	if (snap_ram) memcpy(snap_ram, psxM, 0x200000);
	if (snap_regs2) memcpy(snap_regs2, &psxRegs, SNAP_REGS);
}

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...)
{
	FILE *f = fopen("sd:/wiistation/perf.log", "a");
	va_list ap;
	if (!f) return;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fclose(f);
}

void statetool_reset(void)
{
	nreq = 0;
	due = -1;
	snap_free();
}

void statetool_request(int op, const char *name, unsigned vbl, unsigned n)
{
	if (nreq >= ST_MAX) return;
	memset(&req[nreq], 0, sizeof(req[nreq]));
	req[nreq].op = op;
	snprintf(req[nreq].name, sizeof(req[nreq].name), "%s", name ? name : "");
	req[nreq].vbl = vbl ? vbl : 1;
	req[nreq].n = n;
	nreq++;
}

/* The vblank a request next acts on */
static unsigned when(int i)
{
	return req[i].op == ST_CHECK && req[i].step ? req[i].vbl + req[i].n : req[i].vbl;
}

/* psxcounters.c, once per vblank: stop the CPU if a request is due */
void statetool_vblank(void)
{
	int i;
	if (due >= 0) return;
	for (i = 0; i < nreq; i++)
		if (!req[i].done && frame_counter >= when(i)) {
			due = i;
			stop = 1;
			return;
		}
}

/* A NAME is a file in ST_DIR; a full path (sd:/..., a menu slot's file) is taken as it is */
static void state_path(char *out, int len, const char *name, const char *ext)
{
	if (strchr(name, ':'))
		snprintf(out, len, "%s", name);
	else
		snprintf(out, len, ST_DIR "%s.%s", name, ext);
}

/* NAME.txt: what the state is, and the settings it was saved with */
static void write_info(const char *name)
{
	char path[128];
	FILE *f;
	time_t t = time(NULL);
	if (strchr(name, ':'))
		snprintf(path, sizeof path, "%s.txt", name);
	else
		state_path(path, sizeof path, name, "txt");
	f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "# WiiStation save state %s.st, written by a script (statetool.cpp)\n", name);
	fprintf(f, "# folder: %s\n", AutobootPath);
	fprintf(f, "# cue: %s\n", AutobootROM);
	fprintf(f, "# game id: %s\n", CdromId);
	fprintf(f, "# vblank: %u\n", (unsigned)frame_counter);
	fprintf(f, "# saved: %s", ctime(&t));
	fprintf(f, "# build: %s %s\n", __DATE__, __TIME__);
	fprintf(f, "# settings when saved:\n");
	writeConfig(f);
	fclose(f);
}

static int save_errno;

static int save_quiet(const char *path)
{
	int r;
	makeParentDirs(path);
	state_quiet = 1;
	errno = 0;
	r = SaveStateFile(path);
	state_quiet = 0;
	if (r != 1)   /* r 0: the file did not open; -1: a buffer did not allocate */
		save_errno = errno;
	return r;
}

/* A load moves the game's clock; a chained game still runs its number of vblanks */
static int load_quiet(const char *path)
{
	u32 before = frame_counter;
	int r;
	state_quiet = 1;
	r = LoadStateFile(path, 0);
	state_quiet = 0;
	if (r == 1 && chain_stop_vbl)
		chain_stop_vbl += frame_counter - before;
	return r;
}

/* go(), after the CPU stopped: carry out the request that stopped it. Returns 1 if there was
 * one, and the CPU is to go on. */
int statetool_service(void)
{
	char path[128];
	int i = due, r;
	unsigned fp[6];

	if (i < 0) return 0;
	due = -1;
	stop = 0;

	switch (req[i].op) {
	case ST_SAVE:
		state_path(path, sizeof path, req[i].name, "st");
		r = save_quiet(path);
		if (r == 1) write_info(req[i].name);
		log_line("state: save %s at vblank %u %s%s\n", req[i].name, (unsigned)frame_counter,
			r == 1 ? "ok" : "FAILED: ", r == 1 ? "" : strerror(save_errno));
		req[i].done = true;
		break;
	case ST_LOAD:
		state_path(path, sizeof path, req[i].name, "st");
		log_line("state: load %s at vblank %u", req[i].name, (unsigned)frame_counter);
		r = load_quiet(path);
		log_line(" %s, now vblank %u\n", r == 1 ? "ok" : r == 0 ? "FAILED (no file)" :
			"FAILED (another build or damaged)", (unsigned)frame_counter);
		req[i].done = true;
		break;
	case ST_FP:
		state_fingerprint(fp);
		if (req[i].n) {   /* "statefp V dump": RAM to sd:/wiistation/fpram_<k>.bin, for two boots to diff */
			static int k;
			FILE *f;
			snprintf(path, sizeof path, "sd:/wiistation/fpram_%d.bin", ++k);
			if ((f = fopen(path, "wb"))) { fwrite(psxM, 1, 0x200000, f); fclose(f); }
		}
		log_line("statefp: vblank %u: ram %08x vram %08x hw %08x cpu %08x spu %08x cycle %u\n",
			(unsigned)frame_counter, fp[0], fp[1], fp[2], fp[3], fp[4], fp[5]);
		req[i].done = true;
		break;
	case ST_CHECK:
		if (req[i].step == 0) {
			r = save_quiet(ST_CHECK_FILE);
			state_fingerprint(req[i].at_save);   /* after: a save writes its own block into psxH */
			snap_take();
			req[i].step = 1;
			if (r != 1) {
				log_line("statecheck: save at vblank %u FAILED (%d, %s)\n", (unsigned)frame_counter,
					r, strerror(save_errno));
				req[i].done = true;
				snap_free();
			}
		} else if (req[i].step == 1) {
			state_fingerprint(req[i].fp);
			snap_take2();
			req[i].step = 2;
			r = load_quiet(ST_CHECK_FILE);
			if (r != 1) {
				log_line("statecheck: load FAILED (%d)\n", r);
				req[i].done = true;
				snap_free();
			} else {
				/* The machine straight after the load must be the one that was saved */
				state_fingerprint(fp);
				if (snap_h) snap_diff("psxH", snap_h, (const u8 *)psxH, 0x10000);
				if (snap_regs) snap_diff("psxRegs", snap_regs, (const u8 *)&psxRegs, SNAP_REGS);
				log_line("statecheck: vblank %u, saved/loaded: ram %08x/%08x vram %08x/%08x hw %08x/%08x "
					"cpu %08x/%08x spu %08x/%08x cycle %u/%u\n", req[i].vbl,
					req[i].at_save[0], fp[0], req[i].at_save[1], fp[1], req[i].at_save[2], fp[2],
					req[i].at_save[3], fp[3], req[i].at_save[4], fp[4], req[i].at_save[5], fp[5]);
			}
		} else {
			static const char *part[6] = { "ram", "vram", "hw", "cpu", "spu", "cycle" };
			char diff[64] = "";
			int k;
			state_fingerprint(fp);
			if (snap_ram) snap_diff("RAM +n", snap_ram, (const u8 *)psxM, 0x200000);
			if (snap_regs2) snap_diff("psxRegs +n", snap_regs2, (const u8 *)&psxRegs, SNAP_REGS);
			for (k = 0; k < 6; k++)
				if (fp[k] != req[i].fp[k]) {
					strcat(diff, diff[0] ? "," : "");
					strcat(diff, part[k]);
				}
			log_line("statecheck: vblank %u + %u: ram %08x/%08x vram %08x/%08x hw %08x/%08x "
				"cpu %08x/%08x spu %08x/%08x cycle %u/%u -> %s%s\n",
				req[i].vbl, req[i].n, req[i].fp[0], fp[0], req[i].fp[1], fp[1],
				req[i].fp[2], fp[2], req[i].fp[3], fp[3], req[i].fp[4], fp[4],
				req[i].fp[5], fp[5], diff[0] ? "DIFFER: " : "match", diff);
			req[i].done = true;
			snap_free();
		}
		break;
	}
	return 1;
}
