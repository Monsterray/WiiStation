/**
 * WiiSX - PadWiiSX.c
 * Copyright (C) 1999-2002  Pcsx Team
 * Copyright (C) 2010 sepp256
 * 
 * PAD plugin for WiiSX based on Pcsxbox sources
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../psxcommon.h"
#include "../psemu_plugin_defs.h"
#include "gc_input/controller.h"
#include "wiiSXconfig.h"
#include "../psxcounters.h"
#include "perf_prof.h"
#include "../mem2_manager.h"

/* Scripted input for unattended runs: sd:/wiisxrx/autoinput.txt holds lines
 * "<vblank> <hex button mask>"; from that emulated vblank on, the listed
 * buttons are held on PSX port 1 until the next line. Bits are the PSX pad
 * word (Start 0008, Select 0001, Up 0010, Right 0020, Down 0040, Left 0080,
 * L2 0100, R2 0200, L1 0400, R1 0800, Triangle 1000, Circle 2000,
 * Cross 4000, Square 8000). Together with autoboot.txt this makes a boot
 * deterministic up to any screen -- e.g. Start at the title, Start again in
 * the level to hold the pause menu -- with no host input. A missing file
 * means no effect. Real buttons still work; the script only adds presses.
 * Applied in PadSSSPSX.c (the bound pad plugin) and here.
 *
 * A line "record" makes the run write every change of the real pad on port 1 to
 * sd:/wiisxrx/autoinput_rec.txt, in this same format: a person plays the game once
 * (scripts/movie_capture.sh), and the file plays it back. It counts emulated vblanks, the
 * clock playback uses; a Dolphin input movie counts host frames, which drift from them
 * whenever the emulation runs below full speed.
 *
 * Port 2 lines are the same with "p2 " in front: "p2 <vblank> <mask>". A recording writes
 * them when port 2 has a controller, starting with its state at the first poll, so a script
 * with any p2 line also stands in for a pad on port 2 (autoinput_active()).
 * Multitap slots: "p1a" is "p1" (port 1, or multitap 1 slot A), "p1b".."p1d" slots B..D,
 * "p2a" = "p2", "p2b".."p2d" multitap 2. Script ports: 0 p1, 1 p2, 2..4 p1b..p1d, 5..7
 * p2b..p2d (PadSSSPSX.c script_port()). A line for a slot stands in for a pad there.
 * ponytail: one mask per vblank. A press and release inside one vblank keep only the
 * release; games poll the pad once a frame, so that has not mattered. */
/* A recording makes 2-5 lines a second: 32768 is about two hours of play. The table (256 KB)
 * comes from the MEM2 heap on the first load, not MEM1, which is kept for what runs hot. */
#define AUTOIN_MAX 32768
static struct autoin_line { unsigned vbl; unsigned short mask; unsigned char port, idx; } *autoin;
/* The pad reads of the current vblank, per port (0 = the first read of the vblank). A game can
 * read a pad twice in one vblank (Crash 3: 3985 reads in 3600 vblanks), so a change is stamped
 * with the read that first saw it ("<vblank> <mask> <read>"; no third field = read 0) and
 * replayed from that same read. Stamped by vblank alone, a change seen at a vblank's second
 * read was replayed from its first, one read early, and ten-minute recordings drifted. */
#define AUTOIN_PORTS 8                  /* script ports: p1, p2, p1b..p1d, p2b..p2d */
static unsigned rd_fc[AUTOIN_PORTS] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u }, rd_n[AUTOIN_PORTS];
static unsigned rd_applied_fc[AUTOIN_PORTS] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };   /* the vblank a line without a read stamp took effect */

/* Every pad read, first of all (autoinput_record is called before autoinput_mask). */
static void autoinput_read_tick(int port)
{
	if (rd_fc[port] != frame_counter) {
		rd_fc[port] = frame_counter;
		rd_n[port] = 0;
	} else {
		rd_n[port]++;
		/* a second read in a vblank where an unstamped line took effect: that line's read
		 * was ambiguous (perf.log "pad: ... ambiguous") -- an old recording may drift here */
		if (rd_n[port] == 1 && rd_applied_fc[port] == frame_counter)
			PERF_INC(ai_ambiguous);
	}
}
static int autoin_n = -1;
static int autoin_ports;                /* bit per port with at least one line */
static unsigned short autoin_ev_last;   /* the mask last reported to perf.log ("autoinput:") */
static int autoin_ev_first = 1;         /* each game reports its first poll */
static int autoin_cur[AUTOIN_PORTS];           /* autoinput_mask(port): the next line to look at */
static unsigned short autoin_m[AUTOIN_PORTS];  /* ... and the mask the port's lines before it give */
static unsigned autoin_fc[AUTOIN_PORTS];       /* ... at this vblank */
/* "sweep <vblank> <port>...": from that vblank each script port named (p1 p2 p1b..p2d)
 * gets generated input: its buttons one at a time, then its sticks end to end. Each port
 * presses the buttons in its own order, so a controller that turns up in the wrong slot
 * shows (scripts/padtest_dx.py). autoinput_sweep() makes it. */
static unsigned autoin_sweep_vbl[AUTOIN_PORTS];
static int autoin_sweeps;                      /* bit per script port with a sweep */
static FILE *autoin_rec;            /* "record": the recording, or NULL */
static char autoin_path[128] = "sd:/wiisxrx/autoinput.txt";
/* "trace <vblank>" lines: the debug build's primitive trace arms at these
 * vblanks (perf_prof.c reads the table), so a capture can be scheduled
 * right after a scripted press instead of guessing the overlay's shape. */
unsigned autoinput_trace_vbl[8];
int autoinput_trace_n = 0;
unsigned autoinput_dump_vbl = 0;   /* "dump <vblank>": debug build writes sd:/wiisxrx/vram.bin then */
unsigned autoinput_crash_vbl = 0;  /* "crashtest <vblank>": debug build stores through a NULL base then (a DSI) */
/* "padtype <vblank> <port 1|2> <type>": set_port_type() then (perf_prof.c, through
 * autoinput_padtype_due()). Up to eight lines, in vblank order. */
#define AUTOIN_PADTYPES 8
static struct { unsigned vbl, port, type; } autoin_padtype[AUTOIN_PADTYPES];
static int autoin_padtype_n, autoin_padtype_i;
unsigned autoinput_hang_vbl = 0;   /* "hangtest <vblank>": debug build spins for good then (ws_crash.c watchdog) */
int autoinput_crumbs = 0;          /* "crumbs": debug build writes sd:/wiisxrx/crumb.log once a second (crash hunts) */
/* "padsweep <vblank>": from that vblank the GameCube driver reads a generated sweep
 * instead of the pad -- each stick axis end to end, then every button on its own. It is
 * read in controller-GC.c, which is the only place that knows what a raw GameCube pad
 * reading looks like. The point is to exercise the real driver, the real pad plugin and
 * the real PSX packing with input nobody has to hold, on hardware as well as in Dolphin
 * (scripts/padtest.py checks what comes out). 0 means no sweep. */
unsigned autoinput_padsweep_vbl = 0;
int autoinput_padsweep_fast = 0;   /* "padsweep <vblank> fast": controller-GC.c's short sweep */
/* "menupage <n>": open one Settings tab or Options page a few frames after the menu comes
 * up, so an unattended run can photograph it. Nothing else can -- the menu reads the pads
 * directly rather than through the controller drivers, so padsweep cannot drive it.
 * 1..5 are the Settings tabs in order, 6..10 the Options pages in the order of
 * OptionsFrame::OptionsPages: Advanced Sound, Advanced Graphics, Plugins, CD, Memory.
 * 0 leaves the menu alone.
 *
 * "menuclick <button> <times>": press one button of the Settings tabs, that many times,
 * ten frames after the page opens. The menu reads the pads directly rather than through
 * the controller drivers, so nothing else in a scripted run can reach a menu button, and
 * anything that only happens on a press could not be tested at all. The index is into
 * SettingsFrame.cpp's FRAME_BUTTONS. */
unsigned autoinput_menuclick = 0;
unsigned autoinput_menuclicks = 0;
/*
 * Read in MenuContext.cpp. */
unsigned autoinput_menupage = 0;
/* "state save NAME <vblank>", "state load NAME <vblank>", "statecheck <vblank> <n>" (and the
 * old "statetest <vblank>", now statecheck with n = 120): Gamecube/statetool.cpp. */
void statetool_request(int op, const char *name, unsigned vbl, unsigned n);
void statetool_reset(void);
unsigned autoinput_atrace_vbl = 0; /* "atrace <vblank>": debug build starts the audio timeline (perf_prof.c) */
/* A script port's name: p1 (or p1a), p2 (or p2a), p1b..p1d, p2b..p2d. -1 if it is none. */
static int script_port_name(const char *q)
{
	int port, slot = 0;
	if (q[0] != 'p' || (q[1] != '1' && q[1] != '2'))
		return -1;
	port = q[1] - '1';
	if (q[2] >= 'a' && q[2] <= 'd' && !q[3])
		slot = q[2] - 'a';
	else if (q[2])
		return -1;
	if (!slot)
		return port;
	return (port ? 5 : 2) + slot - 1;
}

/* Parse the script once. Called from the pad plugin's open (so the trace and
 * dump schedules exist even before the first pad poll, e.g. in the BIOS
 * shell) and lazily from autoinput_mask(). */
void autoinput_load(void)
{
	FILE *f;
	char line[128];
	if (autoin_n >= 0) return;
	autoin_n = 0;
	autoin_ports = 0;
	autoin_sweeps = 0;
	autoin_padtype_n = autoin_padtype_i = 0;
	if (!autoin)
		autoin = (struct autoin_line *)_mem2_malloc(AUTOIN_MAX * sizeof(*autoin));
	f = (autoin && autoin_path[0]) ? fopen(autoin_path, "r") : NULL;
	if (f) {
		while (autoin_n < AUTOIN_MAX && fgets(line, sizeof line, f)) {
			unsigned v, k;
			if (!strncmp(line, "record", 6)) {
				if (!autoin_rec)
					autoin_rec = fopen("sd:/wiisxrx/autoinput_rec.txt", "w");
				continue;
			}
			if (sscanf(line, "dump %u", &v) == 1) { autoinput_dump_vbl = v; continue; }
			if (sscanf(line, "crashtest %u", &v) == 1) { autoinput_crash_vbl = v; continue; }
			if (sscanf(line, "hangtest %u", &v) == 1) { autoinput_hang_vbl = v; continue; }
			{
				unsigned pp, tt;
				if (sscanf(line, "padtype %u %u %u", &v, &pp, &tt) == 3 && pp >= 1 && pp <= 2 && tt <= 4) {
					if (autoin_padtype_n < AUTOIN_PADTYPES) {
						autoin_padtype[autoin_padtype_n].vbl = v;
						autoin_padtype[autoin_padtype_n].port = pp - 1;
						autoin_padtype[autoin_padtype_n].type = tt;
						autoin_padtype_n++;
					}
					continue;
				}
			}
			{
				int off, sp;
				char *q;
				if (sscanf(line, "sweep %u%n", &v, &off) == 1) {
					for (q = strtok(line + off, " \t\r\n"); q; q = strtok(NULL, " \t\r\n"))
						if ((sp = script_port_name(q)) >= 0) {
							autoin_sweep_vbl[sp] = v;
							autoin_sweeps |= 1 << sp;
						}
					continue;
				}
			}
			if (!strncmp(line, "crumbs", 6)) { autoinput_crumbs = 1; continue; }
			if (sscanf(line, "padsweep %u", &v) == 1) {
				autoinput_padsweep_vbl = v;
				autoinput_padsweep_fast = strstr(line, "fast") != NULL;
				continue;
			}
			if (sscanf(line, "menupage %u", &v) == 1) { autoinput_menupage = v; continue; }
			{
				unsigned b, t;
				if (sscanf(line, "menuclick %u %u", &b, &t) == 2)
					{ autoinput_menuclick = b; autoinput_menuclicks = t; continue; }
			}
			{   /* save states (statetool.cpp) */
				char nm[48];
				unsigned n2;
				if (sscanf(line, "state save %47s %u", nm, &v) == 2) { statetool_request(1, nm, v, 0); continue; }
				if (sscanf(line, "state load %47s %u", nm, &v) == 2) { statetool_request(2, nm, v, 0); continue; }
				if (sscanf(line, "statecheck %u %u", &v, &n2) == 2) { statetool_request(3, "", v, n2); continue; }
				if (sscanf(line, "statetest %u", &v) == 1) { statetool_request(3, "", v, 120); continue; }
				if (sscanf(line, "statefp %u", &v) == 1) { statetool_request(4, "", v, strstr(line, "dump") != NULL); continue; }
			}
			if (sscanf(line, "atrace %u", &v) == 1) { autoinput_atrace_vbl = v; continue; }
			if (sscanf(line, "trace %u", &v) == 1) { if (autoinput_trace_n < 8) autoinput_trace_vbl[autoinput_trace_n++] = v; continue; }
			{
				int port = 0, skip = 0;
				unsigned rd = 0;
				if (line[0] == 'p') {   /* "p2 ", "p1b " ...: the script port; none is p1 */
					char name[8];
					if (sscanf(line, "%7s%n", name, &skip) != 1 || (port = script_port_name(name)) < 0) continue;
				}
				if (line[0] == '#' || sscanf(line + skip, "%u %x %u", &v, &k, &rd) < 2) continue;
				autoin[autoin_n].vbl = v; autoin[autoin_n].mask = (unsigned short)k;
				autoin[autoin_n].port = (unsigned char)port;
				autoin[autoin_n].idx = (unsigned char)(rd > 255 ? 255 : rd); autoin_n++;
				autoin_ports |= 1 << port;
			}
		}
		if (autoin_n == AUTOIN_MAX && !feof(f))   /* tty.log: playback would stop pressing here */
			SysPrintf("autoinput: %s has more than %d lines; the rest is ignored\n", autoin_path, AUTOIN_MAX);
		fclose(f);
	}
}
/* A chained autoboot (GamecubeMain.cpp) gives each game its own script, or none: an
 * empty path. Everything the parser sets goes back to its default, so nothing one game's
 * script scheduled happens in the next. */
void autoinput_reset(const char *path)
{
	snprintf(autoin_path, sizeof autoin_path, "%s", path ? path : "");
	autoin_n = -1;
	autoin_ev_first = 1;
	memset(autoin_cur, 0, sizeof autoin_cur);
	memset(autoin_m, 0, sizeof autoin_m);
	memset(autoin_fc, 0, sizeof autoin_fc);
	if (autoin_rec) {
		fclose(autoin_rec);
		autoin_rec = NULL;
	}
	autoinput_trace_n = 0;
	autoinput_dump_vbl = 0;
	autoinput_crash_vbl = 0;
	autoin_padtype_n = autoin_padtype_i = 0;
	autoin_sweeps = 0;
	autoinput_hang_vbl = 0;
	autoinput_crumbs = 0;
	autoinput_padsweep_vbl = 0;
	autoinput_padsweep_fast = 0;
	autoinput_menupage = 0;
	autoinput_menuclick = autoinput_menuclicks = 0;
	autoinput_atrace_vbl = 0;
	statetool_reset();
	/* Parse it now, not at the first pad poll: a game that polls late (FF7: vblank 876)
	 * otherwise learns of a "dump 700" after vblank 700, and a chain's game can end
	 * before the line takes effect. */
	if (autoin_path[0])
		autoinput_load();
}

/* A script with at least one line for a port stands in for a plugged-in digital
 * pad on that port (0 = port 1, 1 = port 2): the BIOS shell (and some games) only
 * accept input from a port that answers the pad-ID poll, which the pad plugin refuses
 * when no host controller is mapped -- the usual state of an unattended Dolphin run. */
int autoinput_active(int port)
{
	autoinput_load();
	return port >= 0 && port < AUTOIN_PORTS && ((autoin_ports | autoin_sweeps) >> port & 1);
}

/* perf_prof.c, once a vblank: the next "padtype" line that is due, if any. */
int autoinput_padtype_due(unsigned now, unsigned *port, unsigned *type)
{
	if (autoin_padtype_i >= autoin_padtype_n || now < autoin_padtype[autoin_padtype_i].vbl)
		return 0;
	*port = autoin_padtype[autoin_padtype_i].port;
	*type = autoin_padtype[autoin_padtype_i].type;
	autoin_padtype_i++;
	return 1;
}

/* The order "sweep" presses the buttons in (PSX bits: Select 0001, Start 0008, L2 0100, R2
 * 0200, L1 0400, R1 0800, Triangle 1000, Circle 2000, Cross 4000, Square 8000, then the
 * D-pad Up 0010, Right 0020, Down 0040, Left 0080): the same fourteen the GameCube padsweep
 * reaches. Script port k > 0 swaps the pair at k - 1 and k, so no two ports press them in
 * the same cyclic order; the D-pad stays last, where Stick D-pad presses it from the stick. */
static const unsigned short SWEEP_ORDER[14] = {
	0x0001, 0x0008, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
	0x0010, 0x0020, 0x0040, 0x0080,
};
#define SWEEP_HOLD   3                    /* vblanks each button is held, and released, for */
#define SWEEP_BTN    (14 * 2 * SWEEP_HOLD)
#define SWEEP_STEP   4                    /* stick values 0, 4, .. 252, 255 */
#define SWEEP_RAMP   (256 / SWEEP_STEP + 1)
#define SWEEP_PERIOD (SWEEP_BTN + 2 * SWEEP_RAMP)

/* "sweep": this vblank's generated presses (PSX order, a set bit is held) and sticks (LX LY
 * RX RY, 0..255, 128 at rest) for a script port. Each period: the buttons one at a time,
 * then LX and RX together end to end, then LY and RY. Returns 0 when the port has none. */
int autoinput_sweep(int port, unsigned short *press, unsigned char *sticks)
{
	unsigned t;
	int i;
	autoinput_load();
	if (port < 0 || port >= AUTOIN_PORTS || !(autoin_sweeps >> port & 1) || frame_counter < autoin_sweep_vbl[port])
		return 0;
	t = (frame_counter - autoin_sweep_vbl[port]) % SWEEP_PERIOD;
	sticks[0] = sticks[1] = sticks[2] = sticks[3] = 128;
	*press = 0;
	if (t < SWEEP_BTN) {
		if ((t / SWEEP_HOLD) & 1)   /* every other step is a release, so each press is its own */
			return 1;
		i = (int)(t / SWEEP_HOLD / 2);
		if (port > 0 && i == port - 1)
			i++;
		else if (port > 0 && i == port)
			i--;
		*press = SWEEP_ORDER[i];
		return 1;
	}
	t -= SWEEP_BTN;
	{
		unsigned v = (t % SWEEP_RAMP) * SWEEP_STEP;
		int a = t < SWEEP_RAMP ? 0 : 1;   /* 0: the X axes, 1: the Y axes */
		if (v > 255) v = 255;
		sticks[a] = sticks[a + 2] = (unsigned char)v;
	}
	return 1;
}
/* The lines are in vblank order (a recording always is), so the mask is found by walking
 * forward from the last poll, not by reading the whole script every time. */
unsigned short autoinput_mask(int port)
{
	unsigned short m;
	if (port < 0 || port >= AUTOIN_PORTS)
		return 0;
	PERF_INC(ai_calls);
	autoinput_load();
	if (frame_counter < autoin_fc[port]) {    /* the clock went back: a state load, a new game */
		autoin_cur[port] = 0;
		autoin_m[port] = 0;
	}
	autoin_fc[port] = frame_counter;
	while (autoin_cur[port] < autoin_n && (frame_counter > autoin[autoin_cur[port]].vbl ||
	       (frame_counter == autoin[autoin_cur[port]].vbl && rd_n[port] >= autoin[autoin_cur[port]].idx))) {
		if (autoin[autoin_cur[port]].port == port) {
			autoin_m[port] = autoin[autoin_cur[port]].mask;
			if (!autoin[autoin_cur[port]].idx && autoin[autoin_cur[port]].vbl == frame_counter)
				rd_applied_fc[port] = frame_counter;
		}
		autoin_cur[port]++;
	}
	m = autoin_m[port];
	if (port == 0 && (autoin_ev_first || m != autoin_ev_last)) {   /* perf.log: port 1 only */
		perf_autoinput_event(frame_counter, m);
		autoin_ev_last = m;
		autoin_ev_first = 0;
	}
	return m;
}

extern virtualControllers_t virtualControllers[NUM_VIRTUAL_CONTROLLERS];

/* "record": the real pad's buttons on a port (0 = port 1, 1 = port 2; PSX order, a set bit
 * is a press), called on every poll. Each change is written and synced at once, so a run
 * that is closed early keeps what was recorded up to then. Port 2 is written only while it
 * has a controller: a playback then connects port 2 exactly when the recording had it. */
void autoinput_record(int port, unsigned short real)
{
	static int last[2] = { -1, -1 };
	static unsigned alive, synced;
	if (port < 0 || port > 1)
		return;
	autoinput_read_tick(port);   /* every read, recording or playing back */
	if (!autoin_rec) {
		last[port] = -1;
		return;
	}
	/* Every 5 seconds: how far the recording got, so one ended by closing the window says
	 * where it stopped (scripts/movie_capture.sh takes the length from it). The lines go to
	 * the card every 30 s, not at each change: an fsync stalls the frame, and one per button
	 * press was a stutter while recording. A chain that ends normally unmounts the card and
	 * writes everything; a window closed early can lose its last 30 s. */
	if (frame_counter >= alive + 300) {
		alive = frame_counter;
		fprintf(autoin_rec, "# alive %u\n", (unsigned)frame_counter);
		fflush(autoin_rec);
		if (frame_counter >= synced + 1800) {
			synced = frame_counter;
			fsync(fileno(autoin_rec));
		}
	}
	if (real == last[port] || (port == 1 && !virtualControllers[1].inUse))
		return;
	if (last[port] < 0)   /* the first poll: say whether the recording can see a pad at all */
		fprintf(autoin_rec, "# recorded by WiiStation (\"record\"); port %d %s"
			" (automatic assignment %s, GameCube pad %d %s)\n", port + 1,
			virtualControllers[port].inUse ? "has a controller" :
			"has NO controller, so no press is seen",
			padAutoAssign ? "on" : "off", port + 1,
			controller_GC.available[port] ? "seen" : "not seen");
	last[port] = real;
	if (rd_n[port])   /* not the vblank's first read: say which, so playback uses the same one */
		fprintf(autoin_rec, "%s%u %04x %u\n", port ? "p2 " : "", (unsigned)frame_counter, real, rd_n[port]);
	else
		fprintf(autoin_rec, "%s%u %04x\n", port ? "p2 " : "", (unsigned)frame_counter, real);
}

extern int stop;

// Use to invoke func on the mapped controller with args
#define DO_CONTROL(Control,func,args...) \
	virtualControllers[Control].control->func( \
		virtualControllers[Control].number, ## args)

void assign_controller(int wv, controller_t* type, int wp);

static BUTTONS PAD_1;
static BUTTONS PAD_2;

//extern unsigned int m_psxfix_controller1 ;
//extern unsigned int m_psxfix_controller2 ;
//extern unsigned int m_psxfix_controller3 ;
//extern unsigned int m_psxfix_controller4 ;
//extern unsigned int m_psxfix_multitap ;


static unsigned char buf[256];
unsigned char stdpar[10] = { 0x00, 0x41, 0x5a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
unsigned char mousepar[8] = { 0x00, 0x12, 0x5a, 0xff, 0xff, 0xff, 0xff };
unsigned char analogpar[10] = { 0x00, 0xff, 0x5a, 0xff, 0xff,0xff,0xff,0xff,0xff };
//unsigned char multipar[36] = { 0x00, 0x80, 0x5a, 0xff, 0xff,0xff,0xff,0xff,0xff };

static int bufcount, bufc;

//PadDataS padd1, padd2;
//int readnopoll( int port );
//void xbox_read_sticks( unsigned int port, unsigned char *lx, unsigned char *ly, unsigned char *rx, unsigned char *ry ) ;

long PAD__readPort1(PadDataS* ppad)
{
	int Control = 0;
#if defined(WII) && !defined(NO_BT)
	//Need to switch between Classic and WiimoteNunchuck if user swapped extensions
	if (padType[virtualControllers[Control].number] == PADTYPE_WII)
	{
		if (virtualControllers[Control].control != &controller_WiiUPro &&
			virtualControllers[Control].control != &controller_WiiUGamepad)
		{
			if (virtualControllers[Control].control == &controller_Classic &&
				!controller_Classic.available[virtualControllers[Control].number] &&
				controller_WiimoteNunchuk.available[virtualControllers[Control].number])
				assign_controller(Control, &controller_WiimoteNunchuk, virtualControllers[Control].number);
			else if (virtualControllers[Control].control == &controller_WiimoteNunchuk &&
				!controller_WiimoteNunchuk.available[virtualControllers[Control].number] &&
				controller_Classic.available[virtualControllers[Control].number])
				assign_controller(Control, &controller_Classic, virtualControllers[Control].number);
		}
	}
#endif
	if(virtualControllers[Control].inUse)
		if(DO_CONTROL(Control, GetKeys, (BUTTONS*)&PAD_1, virtualControllers[Control].config))
			stop = 1;


    autoinput_record(0, ~PAD_1.btns.All & 0xFFFF);
    ppad->buttonStatus = (PAD_1.btns.All&0xFFFF) & ~autoinput_mask(0);   /* active low: clearing a bit presses it */
	if ( controllerType == CONTROLLERTYPE_ANALOG )
	{
		ppad->controllerType = PSE_PAD_TYPE_ANALOGPAD; 
		//ppad->controllerType = PSE_PAD_TYPE_ANALOGJOY ;
		ppad->leftJoyX = PAD_1.leftStickX; ppad->leftJoyY = PAD_1.leftStickY;
		ppad->rightJoyX = PAD_1.rightStickX; ppad->rightJoyY = PAD_1.rightStickY;
	}
	else
	{
		ppad->controllerType = PSE_PAD_TYPE_STANDARD; // standard
		ppad->rightJoyX = ppad->rightJoyY = ppad->leftJoyX = ppad->leftJoyY = 128 ;
	}

	return 0 ;
}

long PAD__readPort2(PadDataS* ppad)
{
	int Control = 1;
#if defined(WII) && !defined(NO_BT)
	//Need to switch between Classic and WiimoteNunchuck if user swapped extensions
	if (padType[virtualControllers[Control].number] == PADTYPE_WII)
	{
		if (virtualControllers[Control].control != &controller_WiiUPro &&
			virtualControllers[Control].control != &controller_WiiUGamepad)
		{
			if (virtualControllers[Control].control == &controller_Classic &&
				!controller_Classic.available[virtualControllers[Control].number] &&
				controller_WiimoteNunchuk.available[virtualControllers[Control].number])
				assign_controller(Control, &controller_WiimoteNunchuk, virtualControllers[Control].number);
			else if (virtualControllers[Control].control == &controller_WiimoteNunchuk &&
				!controller_WiimoteNunchuk.available[virtualControllers[Control].number] &&
				controller_Classic.available[virtualControllers[Control].number])
				assign_controller(Control, &controller_Classic, virtualControllers[Control].number);
		}
	}
#endif
	if(virtualControllers[Control].inUse)
		if(DO_CONTROL(Control, GetKeys, (BUTTONS*)&PAD_2, virtualControllers[Control].config))
			stop = 1;


    autoinput_record(1, ~PAD_2.btns.All & 0xFFFF);
    ppad->buttonStatus = (PAD_2.btns.All&0xFFFF) & ~autoinput_mask(1);
	if ( controllerType == CONTROLLERTYPE_ANALOG )
	{
		ppad->controllerType = PSE_PAD_TYPE_ANALOGPAD; 
		//ppad->controllerType = PSE_PAD_TYPE_ANALOGJOY ;
		ppad->leftJoyX = PAD_2.leftStickX; ppad->leftJoyY = PAD_2.leftStickY;
		ppad->rightJoyX = PAD_2.rightStickX; ppad->rightJoyY = PAD_2.rightStickY;
	}
	else
	{
		ppad->controllerType = PSE_PAD_TYPE_STANDARD; // standard
		ppad->rightJoyX = ppad->rightJoyY = ppad->leftJoyX = ppad->leftJoyY = 128 ;
	}

	return 0 ;
}

unsigned char _PADstartPoll(PadDataS *pad, unsigned int ismultitap) {
	//int i ;
	//unsigned short value ;
	bufc = 0;

	if ( ismultitap )
	{
/* TODO: Implement 0,1,or 2 multitaps and integrate with gc_input
		buf[0] = 0x00 ;
		buf[1] = 0x80 ;
		buf[2] = 0x5a ;

		for ( i = 0 ; i < 4 ; i++ )
		{
			value = readnopoll( i ) ;
			buf[ (i*8) + 3] = 0x41 ;
			buf[ (i*8) + 4] = 0x5a ;
			buf[ (i*8) + 5] = value & 0xff ;
			buf[ (i*8) + 6] = value >> 8 ;
			buf[ (i*8) + 7] = 0xFF ;
			buf[ (i*8) + 8] = 0xFF ;
			buf[ (i*8) + 9] = 0xFF ;
			buf[ (i*8) + 10]= 0xFF ;
		}

		if ( m_psxfix_controller1 )
		{
			buf[ (0*8) + 3] = 0x73 ;
//			xbox_read_sticks( 0, &(buf[ (0*8) + 9]), &(buf[ (0*8) + 10]), &(buf[ (0*8) + 7]), &(buf[ (0*8) + 8]) ) ;
		}
		if ( m_psxfix_controller2 )
		{
			buf[ (1*8) + 3] = 0x73 ;
//			xbox_read_sticks( 1, &(buf[ (1*8) + 9]), &(buf[ (1*8) + 10]), &(buf[ (1*8) + 7]), &(buf[ (1*8) + 8]) ) ;
		}
		if ( m_psxfix_controller3 )
		{
			buf[ (2*8) + 3] = 0x73 ;
//			xbox_read_sticks( 2, &(buf[ (2*8) + 9]), &(buf[ (2*8) + 10]), &(buf[ (2*8) + 7]), &(buf[ (2*8) + 8]) ) ;
		}
		if ( m_psxfix_controller4 )
		{
			buf[ (3*8) + 3] = 0x73 ;
//			xbox_read_sticks( 3, &(buf[ (3*8) + 9]), &(buf[ (3*8) + 10]), &(buf[ (3*8) + 7]), &(buf[ (3*8) + 8]) ) ;
		}
		bufcount = 34;
*/
	}
	else
	{
		switch (pad->controllerType) {
			case PSE_PAD_TYPE_MOUSE:
				mousepar[3] = pad->buttonStatus & 0xff;
				mousepar[4] = pad->buttonStatus >> 8;
				mousepar[5] = pad->moveX;
				mousepar[6] = pad->moveY;

				memcpy(buf, mousepar, 7);
				bufcount = 6;
				break;
			case PSE_PAD_TYPE_ANALOGPAD: // scph1150
				analogpar[1] = 0x73;
				analogpar[3] = pad->buttonStatus & 0xff;
				analogpar[4] = pad->buttonStatus >> 8;
				analogpar[5] = pad->rightJoyX;
				analogpar[6] = pad->rightJoyY;
				analogpar[7] = pad->leftJoyX;
				analogpar[8] = pad->leftJoyY;

				memcpy(buf, analogpar, 9);
				bufcount = 8;
				break;
			case PSE_PAD_TYPE_ANALOGJOY: // scph1110
				analogpar[1] = 0x53;
				analogpar[3] = pad->buttonStatus & 0xff;
				analogpar[4] = pad->buttonStatus >> 8;
				analogpar[5] = pad->rightJoyX;
				analogpar[6] = pad->rightJoyY;
				analogpar[7] = pad->leftJoyX;
				analogpar[8] = pad->leftJoyY;

				memcpy(buf, analogpar, 9);
				bufcount = 8;
				break;
			case 0 : //nothing plugged in
				buf[0] = 0xFF ;
				buf[1] = 0xFF ;
				buf[2] = 0xFF ;
				buf[3] = 0xFF ;
				buf[4] = 0xFF ;
				bufcount = 4 ;
				break ;
			case PSE_PAD_TYPE_STANDARD:
			default:
				stdpar[3] = pad->buttonStatus & 0xff;
				stdpar[4] = pad->buttonStatus >> 8;

				memcpy(buf, stdpar, 5);
				bufcount = 4;
		}
	}

	//writexbox("ending padpoll\r\n") ;
	return buf[bufc++];
}

unsigned char PAD__poll(const unsigned char value) {
	//writexbox("padpoll\r\n") ;
	if (bufc > bufcount) return 0xff;
	return buf[bufc++];
}

unsigned char PAD__startPoll(int pad) {
	PadDataS padd;

	//writexbox("pad1 startpoll\r\n") ;

	memset( &padd, 0, sizeof(padd) ) ;

	if (pad == 1)	PAD__readPort1(&padd);
	else			PAD__readPort2(&padd);

//	return _PADstartPoll(&padd, m_psxfix_multitap);
	return _PADstartPoll(&padd, 0);
}
