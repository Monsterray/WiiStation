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
 * ponytail: ports 1 and 2 as the pad plugin numbers them; multitap slots are not scripted.
 * ponytail: one mask per vblank. A press and release inside one vblank keep only the
 * release; games poll the pad once a frame, so that has not mattered. */
#define AUTOIN_MAX 4096   /* a recording makes 2-5 lines a second: about 15 minutes */
static struct { unsigned vbl; unsigned short mask; unsigned char port; } autoin[AUTOIN_MAX];
static int autoin_n = -1;
static int autoin_ports;                /* bit per port with at least one line */
static unsigned short autoin_ev_last;   /* the mask last reported to perf.log ("autoinput:") */
static int autoin_ev_first = 1;         /* each game reports its first poll */
static int autoin_cur[2];           /* autoinput_mask(port): the next line to look at */
static unsigned short autoin_m[2];  /* ... and the mask the port's lines before it give */
static unsigned autoin_fc[2];       /* ... at this vblank */
static FILE *autoin_rec;            /* "record": the recording, or NULL */
static char autoin_path[128] = "sd:/wiisxrx/autoinput.txt";
/* "trace <vblank>" lines: the debug build's primitive trace arms at these
 * vblanks (perf_prof.c reads the table), so a capture can be scheduled
 * right after a scripted press instead of guessing the overlay's shape. */
unsigned autoinput_trace_vbl[8];
int autoinput_trace_n = 0;
unsigned autoinput_dump_vbl = 0;   /* "dump <vblank>": debug build writes sd:/wiisxrx/vram.bin then */
/* "padsweep <vblank>": from that vblank the GameCube driver reads a generated sweep
 * instead of the pad -- each stick axis end to end, then every button on its own. It is
 * read in controller-GC.c, which is the only place that knows what a raw GameCube pad
 * reading looks like. The point is to exercise the real driver, the real pad plugin and
 * the real PSX packing with input nobody has to hold, on hardware as well as in Dolphin
 * (scripts/padtest.py checks what comes out). 0 means no sweep. */
unsigned autoinput_padsweep_vbl = 0;
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
	f = autoin_path[0] ? fopen(autoin_path, "r") : NULL;
	if (f) {
		while (autoin_n < AUTOIN_MAX && fgets(line, sizeof line, f)) {
			unsigned v, k;
			if (!strncmp(line, "record", 6)) {
				if (!autoin_rec)
					autoin_rec = fopen("sd:/wiisxrx/autoinput_rec.txt", "w");
				continue;
			}
			if (sscanf(line, "dump %u", &v) == 1) { autoinput_dump_vbl = v; continue; }
			if (sscanf(line, "padsweep %u", &v) == 1) { autoinput_padsweep_vbl = v; continue; }
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
				int port = strncmp(line, "p2 ", 3) ? 0 : 1;
				if (line[0] == '#' || sscanf(line + 3 * port, "%u %x", &v, &k) != 2) continue;
				autoin[autoin_n].vbl = v; autoin[autoin_n].mask = (unsigned short)k;
				autoin[autoin_n].port = (unsigned char)port; autoin_n++;
				autoin_ports |= 1 << port;
			}
		}
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
	autoinput_padsweep_vbl = 0;
	autoinput_menupage = 0;
	autoinput_menuclick = autoinput_menuclicks = 0;
	autoinput_atrace_vbl = 0;
	statetool_reset();
}

/* A script with at least one line for a port stands in for a plugged-in digital
 * pad on that port (0 = port 1, 1 = port 2): the BIOS shell (and some games) only
 * accept input from a port that answers the pad-ID poll, which the pad plugin refuses
 * when no host controller is mapped -- the usual state of an unattended Dolphin run. */
int autoinput_active(int port)
{
	autoinput_load();
	return port >= 0 && port < 2 && (autoin_ports >> port & 1);
}
/* The lines are in vblank order (a recording always is), so the mask is found by walking
 * forward from the last poll, not by reading the whole script every time. */
unsigned short autoinput_mask(int port)
{
	unsigned short m;
	if (port < 0 || port > 1)
		return 0;
	PERF_INC(ai_calls);
	autoinput_load();
	if (frame_counter < autoin_fc[port]) {    /* the clock went back: a state load, a new game */
		autoin_cur[port] = 0;
		autoin_m[port] = 0;
	}
	autoin_fc[port] = frame_counter;
	while (autoin_cur[port] < autoin_n && frame_counter >= autoin[autoin_cur[port]].vbl) {
		if (autoin[autoin_cur[port]].port == port)
			autoin_m[port] = autoin[autoin_cur[port]].mask;
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

extern virtualControllers_t virtualControllers[10];

/* "record": the real pad's buttons on a port (0 = port 1, 1 = port 2; PSX order, a set bit
 * is a press), called on every poll. Each change is written and synced at once, so a run
 * that is closed early keeps what was recorded up to then. Port 2 is written only while it
 * has a controller: a playback then connects port 2 exactly when the recording had it. */
void autoinput_record(int port, unsigned short real)
{
	static int last[2] = { -1, -1 };
	static unsigned alive;
	if (port < 0 || port > 1)
		return;
	if (!autoin_rec) {
		last[port] = -1;
		return;
	}
	/* Every 5 seconds: how far the recording got, so one ended by closing the window says
	 * where it stopped (scripts/movie_capture.sh takes the length from it). */
	if (frame_counter >= alive + 300) {
		alive = frame_counter;
		fprintf(autoin_rec, "# alive %u\n", (unsigned)frame_counter);
		fflush(autoin_rec);
		fsync(fileno(autoin_rec));
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
	fprintf(autoin_rec, "%s%u %04x\n", port ? "p2 " : "", (unsigned)frame_counter, real);
	fflush(autoin_rec);
	fsync(fileno(autoin_rec));
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
