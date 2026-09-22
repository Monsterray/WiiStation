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
 * Applied in PadSSSPSX.c (the bound pad plugin) and here. */
static struct { unsigned vbl; unsigned short mask; } autoin[64];
static int autoin_n = -1;
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
 * Read in MenuContext.cpp. */
unsigned autoinput_menupage = 0;
/* "statetest <vblank>": save a state at that vblank, then load it back 120 vblanks later.
 * The Current ROM menu is the only other way to reach SaveState, and no script can drive a
 * menu. perf.log's "save:" line then holds the time each one took. 0 means do nothing. */
unsigned autoinput_statetest_vbl = 0;
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
	f = fopen("sd:/wiisxrx/autoinput.txt", "r");
	if (f) {
		while (autoin_n < 64 && fgets(line, sizeof line, f)) {
			unsigned v, k;
			if (sscanf(line, "dump %u", &v) == 1) { autoinput_dump_vbl = v; continue; }
			if (sscanf(line, "padsweep %u", &v) == 1) { autoinput_padsweep_vbl = v; continue; }
			if (sscanf(line, "menupage %u", &v) == 1) { autoinput_menupage = v; continue; }
			if (sscanf(line, "statetest %u", &v) == 1) { autoinput_statetest_vbl = v; continue; }
			if (sscanf(line, "atrace %u", &v) == 1) { autoinput_atrace_vbl = v; continue; }
			if (sscanf(line, "trace %u", &v) == 1) { if (autoinput_trace_n < 8) autoinput_trace_vbl[autoinput_trace_n++] = v; continue; }
			if (line[0] == '#' || sscanf(line, "%u %x", &v, &k) != 2) continue;
			autoin[autoin_n].vbl = v; autoin[autoin_n].mask = (unsigned short)k; autoin_n++;
		}
		fclose(f);
	}
}
/* A script with at least one press line stands in for a plugged-in digital
 * pad on port 1: the BIOS shell (and some games) only accept input from a
 * port that answers the pad-ID poll, which the pad plugin refuses when no
 * host controller is mapped -- the usual state of an unattended Dolphin run. */
int autoinput_active(void)
{
	autoinput_load();
	return autoin_n > 0;
}
unsigned short autoinput_mask(void)
{
	int i;
	unsigned short m = 0;
	PERF_INC(ai_calls);
	autoinput_load();
	for (i = 0; i < autoin_n; i++)
		if (frame_counter >= autoin[i].vbl) m = autoin[i].mask;
	{
		static unsigned short last = 0; static int first = 1;
		if (first || m != last) { perf_autoinput_event(frame_counter, m); last = m; first = 0; }
	}
	return m;
}

extern virtualControllers_t virtualControllers[10];
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


    ppad->buttonStatus = (PAD_1.btns.All&0xFFFF) & ~autoinput_mask();   /* active low: clearing a bit presses it */
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


    ppad->buttonStatus = (PAD_2.btns.All&0xFFFF);
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
