/**
 * WiiSX - PadSSSPSX.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 *
 * Basic Analog PAD plugin for WiiSX
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                sepp256@gmail.com
 *                emukidid@gmail.com
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

#include <gccore.h>
#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>
#include "perf_prof.h"
#include "../plugins.h"
#include "../psxcommon.h"
#include "../psemu_plugin_defs.h"
#include "gc_input/controller.h"
#include "wiiSXconfig.h"
#include "PadSSSPSX.h"

/* Scale factor of analog sticks / 128 */

//static BUTTONS PAD_1;
//static BUTTONS PAD_2;
extern PadDataS lastport1;
extern PadDataS lastport2;

static struct
{
	SSSConfig config;	//unused?
	//int devcnt;			//unused
	u16 padStat[10];		//Digital Buttons
	int padID[10];
	int padMode1[10];	//0 = digital, 1 = analog
	int padModeE[10];	//Config mode (DualShock, after command 43h 01h)
	int padModeC[10];
	int padModeF[10];
	int padVib0[10];		//Command byte for small motor
	int padVib1[10];		//Command byte for large motor
	u8 dsRumble[10][6];	//Rumble map set by 4Dh: meaning of 42h command bytes 3..8 (psx-spx)
	int dsNewRumble[10];	//Config mode was used: 4Dh map, not the one-motor method
	u8 legacyXX[10];		//Byte 3 of the last 42h, for the one-motor method
	int padVibF[10][4];	//Sm motor value; Big motor value; Sm motor running?; Big motor running?
	//int padVibC[10];		//unused
	u64 padPress[10][16];//unused?
	int curPad;			//0=pad1; 1=pad2
	int curByte;		//current command/data byte
	int curCmd;			//current command from PSX/PS2
	int cmdLen;			//# of bytes in pad reply
	int irq10En[10];	// enable IRQ10 output for lightgun port
	int isConnected[10];	// is controller connected?
	int trAll[2];			// transfer all mode select
	int multiPad[2];	// which controller is connected to the multipad
} global;

extern void SysPrintf(char *fmt, ...);
extern int stop;

/* Controller type, later do this by a Variable in the GUI */
//extern char controllerType = 0; // 0 = standard, 1 = analog (analog fails on old games)
extern long  PadFlags;
extern int gLightgun;
extern int gMouse[4];

extern virtualControllers_t virtualControllers[10];

// Use to invoke func on the mapped controller with args
#define DO_CONTROL(Control,func,args...) \
	virtualControllers[Control].control->func( \
		virtualControllers[Control].number, ## args)

void assign_controller(int wv, controller_t* type, int wp);

void setIrq( u32 irq )
{
    psxHu32ref(0x1070) |= irq;
}

void lightgunInterrupt()
{
	int cursorX;
	int cursorY;
	int Control;
	WPADData* wpad = WPAD_Data(0);


	if (global.irq10En[0] == 0x10) Control = 0;
	else if (global.irq10En[1] == 0x10) Control = 1;
	else return;

	if ((global.padID[Control] != 0x31) && (global.padID[Control] != 0x63))
		return;

	if(screenMode == 2)	cursorX = ((wpad[virtualControllers[Control].number].ir.x*848/640 - 104));
	else cursorX = (wpad[virtualControllers[Control].number].ir.x);

	cursorY = (wpad[virtualControllers[Control].number].ir.y/2);



	if ((cursorY > 5) && (cursorY < 220) && wpad[virtualControllers[Control].number].ir.valid){
		if (gLightgun == 5){
		gLightgun--;
		set_event(PSXINT_LIGHTGUN, (Config.PsxType ? 2157: 2146)*(cursorY + (Config.PsxType ? 40 : 0)));
		return;
		}


		if (gLightgun>0){
			setIrq( SWAPu32((u32)0x400) );
			gLightgun--;
			psxRcntWcount(0,(cursorX*(rcnts[0].rate < 5 ? 2.52 : 0.4))+ (rcnts[0].rate < 5 ? 115 : 0) );
			set_event(PSXINT_LIGHTGUN, (Config.PsxType ? 2157: 2146));
		}
	}
}

/* No motor mapped: what power-on, command 44h and the Analog button leave (psx-spx) */
static void RumbleReset (const int pad)
{
	memset(global.dsRumble[pad], 0xff, sizeof(global.dsRumble[pad]));
	global.padVib0[pad] = 0;
	global.padVib1[pad] = 0;
	global.padVibF[pad][0] = 0;
	global.padVibF[pad][1] = 0;
}

/* A DualShock answers config commands; a digital pad, a mouse and a light gun do not */
static int IsDualShock (const int pad)
{
	return controllerType == CONTROLLERTYPE_ANALOG && global.padID[pad] != 0x31 &&
		global.padID[pad] != 0x63 && global.padID[pad] != 0x12;
}

static void PADsetMode (const int pad, const int mode)	//mode = 0 (digital) or 1 (analog)
{
	static const u8 padID[] = { 0x41, 0x73 };
	global.padMode1[pad] = mode;
	global.padID[pad] = padID[mode];
	RumbleReset(pad);
}

static void UpdateState (const int pad) //Note: pad = 0 or 1
{
	const int vib0 = global.padVibF[pad][0] ? 1 : 0;
	PERF_INC(pad_update);
	const int vib1 = global.padVibF[pad][1] ? 1 : 0;
	int cursorX = 0x1;
	int cursorY = 0xA;
	int curMouse;
	static int tempcursorX[4];
	static int tempcursorY[4];
	static int oldcursorX[4];
	static int oldcursorY[4];
	static BUTTONS PAD_Data;
	static WPADData* wpad;
	float sensitivity;
	int miscButton;

	//TODO: Rework/simplify the following code & reset BUTTONS when no controller in use
	int Control = pad;
#if defined(WII) && !defined(NO_BT)
	//Need to switch between Classic and WiimoteNunchuck if user swapped extensions
	/* Only an assigned port has a physical controller number: unassign_controller() leaves
	 * it at -1, which indexed padType[] and available[] one byte/entry short of the array. */
	if (virtualControllers[Control].inUse &&
		padType[virtualControllers[Control].number] == PADTYPE_WII)
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

	if (lightGun && padLightgun[pad]){
		if (virtualControllers[Control].control == &controller_Wiimote ||
			virtualControllers[Control].control == &controller_WiimoteNunchuk){
				if (lightGun == LIGHTGUN_GUNCON){
					global.padID[pad] = 0x63;
					/* This channel's data. It used to say WPAD_Data(0) and then index the
					 * result, which happens to work because WPAD_Data returns a pointer
					 * into libogc's array -- but it reads like a mistake. */
					wpad = WPAD_Data(virtualControllers[Control].number);
					if(screenMode == 2)	cursorX = ((wpad->ir.x*848/640 - 104)/1.72) + 75;
					else cursorX = (wpad->ir.x/1.72) + 75;

					cursorY = (wpad->ir.y/2) + (Config.PsxType ? 48 : 25);

					if (!wpad->ir.valid){
						cursorX = 0x1;
						cursorY = 0xA;
					}
				}
				else if (lightGun == LIGHTGUN_MOUSE){
					curMouse = virtualControllers[Control].number;
					global.padID[pad] = 0x12;
					if (!gMouse[curMouse]){
						wpad = WPAD_Data(curMouse);

						if(screenMode == 2)	cursorX = wpad->ir.x*848/640 - 104;
						else cursorX = wpad->ir.x;
						tempcursorX[curMouse] = cursorX;
						sensitivity = virtualControllers[Control].config->sensitivity;
						if (sensitivity < 0.1) sensitivity = 1.0;
						cursorX = (cursorX - oldcursorX[curMouse]) * sensitivity;
						if (cursorX > 127) cursorX = 127;
						if (cursorX < -128) cursorX = -128;

						cursorY = wpad->ir.y;
						tempcursorY[curMouse] = cursorY;
						cursorY = (cursorY - oldcursorY[curMouse]) * sensitivity;
						if (cursorY > 127) cursorY = 127;
						if (cursorY < -128) cursorY = -128;

						cursorX = (cursorX & 0xFF) | (cursorY<<8);

						oldcursorX[curMouse] = tempcursorX[curMouse];
						oldcursorY[curMouse] = tempcursorY[curMouse];
						tempcursorX[curMouse] = cursorX;

						if (!wpad->ir.valid){
							cursorX = 0;
						}
						gMouse[curMouse] = 1;
					}
					else{
						cursorX = tempcursorX[curMouse];
					}
				}

				else
					global.padID[pad] = 0x31;
			}
		else{
			if ((global.padID[pad] == 0x31) || (global.padID[pad] == 0x63) || (global.padID[pad] == 0x12))
			PADsetMode( pad, 0);   /* back from a light gun: a pad, in digital mode */
		}
	}
	else{
		if ((global.padID[pad] == 0x31) || (global.padID[pad] == 0x63) || (global.padID[pad] == 0x12))
		PADsetMode( pad, 0);
	}
#endif

	/* ControllerType Analog is a DualShock: digital at power-on (SSS_PADopen), and the game
	 * switches it to analog with config command 0x44 when it wants the sticks. This used to
	 * force analog mode on every poll, which undid the game's own choice and left games that
	 * only understand a digital pad (ID 0x41) ignoring every button -- as a real PlayStation
	 * does with the DualShock's analog light on. Standard ignores config commands, so a pad
	 * left in analog mode goes back to digital here. */
	if (controllerType != CONTROLLERTYPE_ANALOG && global.padMode1[pad])
		PADsetMode( pad, 0);

	if(virtualControllers[Control].inUse)
	{
		global.isConnected[pad] = 1;

		miscButton = DO_CONTROL(Control, GetKeys, (BUTTONS*)&PAD_Data, virtualControllers[Control].config);

#ifdef HW_RVL
		/* A gun takes its buttons from the remote itself, not through the pad mapping.
		 * Hold a Wii Remote like a gun and B is under the index finger, so B is the
		 * trigger; the mapping is for a pad, and its Circle is the remote's "2", which
		 * nobody can reach while aiming. With the plain remote the mapping gives A and B
		 * no PlayStation button at all, so a light gun had no trigger and no fire.
		 * The fields are active low: 0 is pressed. */
		if (lightGun == LIGHTGUN_GUNCON && padLightgun[pad] &&
		    (virtualControllers[Control].control == &controller_Wiimote ||
		     virtualControllers[Control].control == &controller_WiimoteNunchuk))
		{
			u32 held = WPAD_ButtonsHeld(virtualControllers[Control].number);

			PAD_Data.btns.CIRCLE_BUTTON = (held & WPAD_BUTTON_B) ? 0 : 1;   /* trigger */
			PAD_Data.btns.START_BUTTON  = (held & WPAD_BUTTON_A) ? 0 : 1;   /* gun A */
			PAD_Data.btns.CROSS_BUTTON  = (held & WPAD_BUTTON_1) ? 0 : 1;   /* gun B */
		}
		/* The Justifier keeps the pad mapping: which of its three bits is the trigger has
		 * not been checked against hardware, and guessing would be worse than leaving it. */
#endif
		if (miscButton == 1)
			stop = 1;
		else if (Control == 0 || Control == 2)
			frameLimit[0] = (miscButton == 0 ? frameLimit[1] : 0);
	}
	else
	{	//TODO: Emulate no controller present in this case.
		//Reset buttons & sticks if PAD is not in use
		extern int autoinput_active(int port);   /* PadWiiSX.c: a script has a digital pad on this port */
		global.isConnected[pad] = autoinput_active(pad) ? 1 : 0;
		PAD_Data.btns.All = 0xFFFF;
		PAD_Data.leftStickX = PAD_Data.leftStickY = PAD_Data.rightStickX = PAD_Data.rightStickY = 128;
	}

	/* Each driver already maps its own hardware's full travel onto 0..255, so the default
	 * 1.0 is a stick that is exactly what the player moved. What used to be here was a
	 * fixed 1.40625 on top of that, which reached full deflection at roughly half the
	 * throw on everything but a GameCube pad and threw the rest away.
	 * An unassigned port has no config -- including multitap slots 2..9, which are never
	 * assigned -- so it takes the default rather than dereferencing NULL. */
	sensitivity = virtualControllers[Control].config
	            ? virtualControllers[Control].config->sensitivity : 1.0f;
	if (sensitivity < 0.1f) sensitivity = 1.0f;
	if (sensitivity != 1.0f)
	{
		PAD_Data.leftStickX  = apply_sensitivity(PAD_Data.leftStickX,  sensitivity);
		PAD_Data.leftStickY  = apply_sensitivity(PAD_Data.leftStickY,  sensitivity);
		PAD_Data.rightStickX = apply_sensitivity(PAD_Data.rightStickX, sensitivity);
		PAD_Data.rightStickY = apply_sensitivity(PAD_Data.rightStickY, sensitivity);
	}

	global.padStat[pad] = (((PAD_Data.btns.All>>8)&0xFF) | ( (PAD_Data.btns.All<<8) & 0xFF00 )) &0xFFFF;
	if (pad < 2) {
		extern unsigned short autoinput_mask(int port);   /* PadWiiSX.c: scripted presses from sd:/wiisxrx/autoinput.txt */
		/* padStat is byte-swapped so that the big-endian 16-bit store in the
		 * 0x42 response emits the two PSX bytes in wire order; the script's
		 * masks are in PSX order (Start 0008), so swap them the same way. */
		extern void autoinput_record(int port, unsigned short real);   /* PadWiiSX.c: "record" */
		unsigned short s = global.padStat[pad];
		unsigned short m;
		autoinput_record(pad, ~((s << 8) | (s >> 8)) & 0xFFFF);   /* the real pad, before the script */
		m = autoinput_mask(pad);
		global.padStat[pad] &= ~(unsigned short)(((m << 8) | (m >> 8)) & 0xFFFF);   /* active low */
	}


	if ((global.padID[pad] == 0x31) || (global.padID[pad] == 0x63) || (global.padID[pad] == 0x12)){
		if (lightGun == LIGHTGUN_GUNCON) global.padStat[pad] |= ~0x860;
		else if (lightGun == LIGHTGUN_JUST) global.padStat[pad] |= ~0x8c0;
		else {
			if (!(global.padStat[pad] & 0x40)) // X button
				cursorX = 0;
			global.padStat[pad] |= ~0xF;
		}


		if ((pad==0) || (padType[global.curPad] == PADTYPE_MULTITAP))
		{
			lastport1.leftJoyX = cursorY & 0xFF; lastport1.leftJoyY = cursorY >> 8;
			lastport1.rightJoyX = cursorX & 0xFF; lastport1.rightJoyY = cursorX >> 8;
			lastport1.buttonStatus = global.padStat[pad];
		}
		else
		{
			lastport2.leftJoyX = cursorY & 0xFF; lastport2.leftJoyY = cursorY >> 8;
			lastport2.rightJoyX = cursorX & 0xFF; lastport2.rightJoyY = cursorX >> 8;
			lastport2.buttonStatus = global.padStat[pad];
		}
	}
	else{
		if ((pad==0) || (padType[global.curPad] == PADTYPE_MULTITAP))
		{
			lastport1.leftJoyX = PAD_Data.leftStickX; lastport1.leftJoyY = PAD_Data.leftStickY;
			lastport1.rightJoyX = PAD_Data.rightStickX; lastport1.rightJoyY = PAD_Data.rightStickY;
			lastport1.buttonStatus = global.padStat[pad];
		}
		else
		{
			lastport2.leftJoyX = PAD_Data.leftStickX; lastport2.leftJoyY = PAD_Data.leftStickY;
			lastport2.rightJoyX = PAD_Data.rightStickX; lastport2.rightJoyY = PAD_Data.rightStickY;
			lastport2.buttonStatus = global.padStat[pad];
		}
	}

	/* Debug builds keep a timeline of what each port handed the PlayStation, with the
	 * driver's own output beside it, so a scripted Dolphin run can be checked end to end
	 * (scripts/padtest.py). Repeats are dropped inside perf_pad_event. */
	{
		const PadDataS *out = ((pad == 0) || (padType[global.curPad] == PADTYPE_MULTITAP))
		                    ? &lastport1 : &lastport2;
		perf_pad_event(pad,
			virtualControllers[Control].control ? (unsigned char)virtualControllers[Control].control->identifier : 0,
			PAD_Data.btns.All,
			(PAD_Data.leftStickX << 8) | PAD_Data.leftStickY,
			(PAD_Data.rightStickX << 8) | PAD_Data.rightStickY,
			out->buttonStatus,
			(out->leftJoyX << 8) | out->leftJoyY,
			(out->rightJoyX << 8) | out->rightJoyY);
	}

	/* Small Motor */
	if ((global.padVibF[pad][2] != vib0) )
	{
		global.padVibF[pad][2] = vib0;
		if (virtualControllers[pad].control && virtualControllers[pad].control->rumble)
			DO_CONTROL(pad, rumble, global.padVibF[pad][0]);
	}
	/* Big Motor */
	if ((global.padVibF[pad][3] != vib1) )
	{
		global.padVibF[pad][3] = vib1;
		if (virtualControllers[pad].control && virtualControllers[pad].control->rumble)
			DO_CONTROL(pad, rumble, global.padVibF[pad][1]);
	}
}

long SSS_PADopen (void *p)
{
	int i;
	{
		extern void autoinput_load(void);   /* PadWiiSX.c: sd:/wiisxrx/autoinput.txt incl. trace/dump schedules */
		autoinput_load();
	}
	memset (&global, 0, sizeof (global));
	memset( &lastport1, 0, sizeof(lastport1) ) ;
	memset( &lastport2, 0, sizeof(lastport2) ) ;
	for(i = 0; i < 10; i++){
		global.padStat[i] = 0xffff;
		PADsetMode (i, 0);   /* digital at power-on, as a DualShock is: the game switches it */
	}
	return 0;
}

long SSS_PADclose (void)
{
	return 0 ;
}

long SSS_PADquery (void)
{
	return 3;
}

unsigned char SSS_PADstartPoll (int pad)
{
	PERF_INC(pad_startpoll);
	global.curPad = pad -1;
	global.curByte = 0;
	return 0xff;
}

void SSS_SetMultiPad(int pad, int mpad)
{
	if (pad)
		global.multiPad[1] = mpad+5;
	else
		global.multiPad[0] = mpad+1;
}

/* Config-mode replies of a DualShock (SCPH-1200): F3h, then these bytes from index 1 on
 * (psx-spx "Configuration Commands"; DuckStation, MiSTer and PsxNewLib agree). Always 9
 * bytes long. Commands whose data depend on their parameter start from cmdcfg and are
 * filled in when the parameter arrives (cur == 2). */
static const u8 cmdcfg[8] = { 0xff, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 cmd45[8] =		/* Type 01h = PS1 DualShock (03h is a DualShock 2), LED, constants */
{
	0xff, 0x5a, 0x01, 0x02, 0x00, 0x02, 0x01, 0x00,
};

unsigned char multitap[34] = { 0x80, 0x5a,
									0x41, 0x5a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
									0x41, 0x5a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
									0x41, 0x5a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
									0x41, 0x5a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

unsigned char SSS_PADpoll (const unsigned char value)
{
	int i, offset, offsetSlot;
	int pad = global.curPad;
	int slot = pad;
	if (padType[slot] == PADTYPE_MULTITAP)
		pad = global.multiPad[slot];

	const int cur = global.curByte;

//Pragma to avoid packing on "buffer" union
//Not sure if necessary on PPC
#pragma pack(push,1)
	union buffer
	{
		u16 b16[20];
		u8  b8[40];
	};

	static union buffer buf;
	if (cur == 0)
	{
		global.curByte++;
		global.curCmd = value;
		if (pad == 0 && (value & 0xf0) == 0x40)
			PERF_INC(pad_cmd[value & 0x0f]);
		/* Which commands answer (psx-spx "Configuration Commands"; DuckStation and MiSTer):
		 * a digital pad, a mouse and a light gun answer 42h only. A DualShock answers 42h
		 * and 43h in normal mode, and 40h..4Fh in config mode. Any other command gets no
		 * /ACK: FFh, and sio.c ends the transfer. */
		if (value != 0x42 && !(IsDualShock(pad) &&
		    (value == 0x43 || (global.padModeE[pad] && (value & 0xf0) == 0x40))))
		{
			global.cmdLen = 0;
			return 0xFF;
		}
		switch (global.curCmd)
		{
		case 0x42:
			if (padType[slot] == PADTYPE_MULTITAP){
				if (global.trAll[slot] == 1){
					global.cmdLen = sizeof (multitap);
					memcpy (buf.b8, multitap, sizeof (multitap));
					offsetSlot = 2+(slot*4);
					for(i = 0; i < 4; i++) {
						UpdateState (i+offsetSlot);
						offset = i*8;
						if (global.isConnected[i+offsetSlot]) {
							buf.b8[2+offset] = global.padID[i+offsetSlot];
							}
						else {
							buf.b8[2+offset] = 0xFF;
							buf.b8[3+offset] = 0xFF;
							}
						buf.b8[6+offset] = lastport1.rightJoyX ;
						buf.b8[7+offset] = lastport1.rightJoyY ;
						buf.b8[8+offset] = lastport1.leftJoyX ;
						buf.b8[9+offset] = lastport1.leftJoyY ;
						buf.b16[2+(i*4)] = global.padStat[i+offsetSlot];
					}
					return 0x80;
				}
			}
			UpdateState(pad);
			/* fall through */
		case 0x43:
			global.cmdLen = 2 + 2 * (global.padID[pad] & 0x0f);
			buf.b8[1] = global.padModeC[pad] ? 0x00 : 0x5a;
			buf.b16[1] = global.padStat[pad];
			if (value == 0x43 && global.padModeE[pad])
			{
				/* In config mode 43h answers F3h 5Ah and six 00h bytes, not the
				 * buttons, whatever mode the pad is in (psx-spx, DuckStation, PCSX-Redux). */
				global.cmdLen = 8;
				buf.b16[1] = 0;
				buf.b16[2] = 0;
				buf.b16[3] = 0;
				return 0xf3;
			}
			else
			{
				if (padType[slot] != PADTYPE_MULTITAP){
					buf.b8[4] = pad ? lastport2.rightJoyX : lastport1.rightJoyX ;
					buf.b8[5] = pad ? lastport2.rightJoyY : lastport1.rightJoyY ;
					buf.b8[6] = pad ? lastport2.leftJoyX : lastport1.leftJoyX ;
					buf.b8[7] = pad ? lastport2.leftJoyY : lastport1.leftJoyY ;
				}
				else{
					buf.b8[4] = lastport1.rightJoyX ;
					buf.b8[5] = lastport1.rightJoyY ;
					buf.b8[6] = lastport1.leftJoyX ;
					buf.b8[7] = lastport1.leftJoyY ;
				}

				//if (global.padID[pad] == 0x79)
				//{
  				// do some pressure stuff (this is for PS2 only!)
				//}
#ifdef PERF_PROF
				if (pad == 0 && !g_perf.pad_press_len && global.padStat[pad] != 0xffff) {
					g_perf.pad_press_id = (u8)global.padID[pad];
					g_perf.pad_press_len = (u8)global.cmdLen;
					memcpy(g_perf.pad_press, buf.b8, 8);
				}
#endif
				/* In config mode 42h answers F3h with the sticks, even in digital mode. A pad
				 * switched to Standard since keeps no config mode. */
				if (global.padModeE[pad] && IsDualShock(pad))
				{
					global.cmdLen = 8;
					return 0xf3;
				}
				return (u8)global.padID[pad];
			}
			break;
		case 0x44:
			/* Setting the LED resets the rumble map, whatever the parameters */
			RumbleReset(pad);
			global.cmdLen = sizeof (cmdcfg);
			memcpy (buf.b8, cmdcfg, sizeof (cmdcfg));
			return 0xf3;
		case 0x45:
			global.cmdLen = sizeof (cmd45);
			memcpy (buf.b8, cmd45, sizeof (cmd45));
			buf.b8[4] = (u8)global.padMode1[pad];
			return 0xf3;
		case 0x4d:
			/* Returns the old map, byte by byte, as the new one arrives */
			global.cmdLen = sizeof (cmdcfg);
			buf.b8[1] = 0x5a;
			memcpy (&buf.b8[2], global.dsRumble[pad], 6);
			return 0xf3;
		default:
			/* 46h, 47h, 48h, 4Ch: filled in from their parameter below. 40h, 41h, 49h..4Bh,
			 * 4Eh, 4Fh: unused on a PS1 DualShock (the DualShock 2 ones), all 00h. */
			global.cmdLen = sizeof (cmdcfg);
			memcpy (buf.b8, cmdcfg, sizeof (cmdcfg));
			return 0xf3;
		}
	}
	switch (global.curCmd)
	{
	case 0x42:
		/* Motors (psx-spx "Vibration/Rumble Control"): the small one is bit 0 of its byte,
		 * the large one takes the whole byte. Before config mode is used, the one-motor
		 * method: byte 3 40h..7Fh and byte 4 odd run the small motor. */
		if (cur == global.padVib0[pad])
			global.padVibF[pad][0] = value & 1;
		if (cur == global.padVib1[pad])
			global.padVibF[pad][1] = value;
		if (cur == 2)
		{
			global.irq10En[slot] = value;
			global.legacyXX[pad] = value;
		}
		if (cur == 3 && IsDualShock(pad) && !global.dsNewRumble[pad])
			global.padVibF[pad][0] = (global.legacyXX[pad] & 0xc0) == 0x40 && (value & 1);
		if (cur == 1 && padType[slot] == PADTYPE_MULTITAP)
			global.trAll[slot] = value & 1;
		break;
	case 0x43:
		/* 01h enters config mode, 00h leaves it; other values change nothing */
		if (cur == 2 && value <= 1)
		{
			global.padModeE[pad] = value;
			global.padModeC[pad] = 0;
			if (value) global.dsNewRumble[pad] = 1;
		}
		break;
	case 0x44:
		/* LED 00h digital, 01h analog, others ignored. Key: AND 3 = 3 locks the Analog button */
		if (cur == 2 && value <= 1)
			PADsetMode (pad, value);
		if (cur == 3)
			global.padModeF[pad] = (value & 3) == 3;
		break;
	case 0x46:		/* Actuator info: 0 = small motor, 1 = large motor, others 00h */
		if (cur == 2 && value <= 1)
		{
			static const u8 act[2][4] = { { 0x01, 0x02, 0x00, 0x0a }, { 0x01, 0x01, 0x01, 0x14 } };
			memcpy (&buf.b8[4], act[value], 4);
		}
		break;
	case 0x47:
		if (cur == 2 && value == 0)
		{
			buf.b8[4] = 0x02;
			buf.b8[6] = 0x01;
		}
		break;
	case 0x48:
		if (cur == 2 && value <= 1)
			buf.b8[6] = 0x01;
		break;
	case 0x4c:
		if (cur == 2 && value <= 1)
			buf.b8[5] = value ? 0x07 : 0x04;
		break;
	case 0x4d:
		/* Bytes 3..8 set the new map (00h small motor, 01h large motor, FFh nothing) */
		if (cur >= 2 && cur < 8)
		{
			int i;
			global.dsRumble[pad][cur - 2] = value;
			global.padVib0[pad] = global.padVib1[pad] = 0;
			for (i = 0; i < 6; i++)
			{
				if (global.dsRumble[pad][i] == 0x00) global.padVib0[pad] = i + 2;
				if (global.dsRumble[pad][i] == 0x01) global.padVib1[pad] = i + 2;
			}
			if (!global.padVib0[pad]) global.padVibF[pad][0] = 0;
			if (!global.padVib1[pad]) global.padVibF[pad][1] = 0;
		}
		break;
	}

	if (cur >= global.cmdLen)
		return 0;
	return buf.b8[global.curByte++];
//Revert packing
#pragma pack(pop)
}

long SSS_PADreadPort1 (PadDataS* pads)
{
	//#PADreadPort1 not used in PCSX
/*
	pads->buttonStatus = global.padStat[0];

	memset (pads, 0, sizeof (PadDataS));
	if ((global.padID[0] & 0xf0) == 0x40)
	{
		pads->rightJoyX = pads->rightJoyY = pads->leftJoyX = pads->leftJoyY = 128 ;
		pads->controllerType = PSE_PAD_TYPE_STANDARD;
	}
	else
	{
		pads->controllerType = PSE_PAD_TYPE_ANALOGPAD;
		int Control = 0;
#if defined(WII) && !defined(NO_BT)
		//Need to switch between Classic and WiimoteNunchuck if user swapped extensions
		if (padType[virtualControllers[Control].number] == PADTYPE_WII)
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
#endif
		if(virtualControllers[Control].inUse)
			if(DO_CONTROL(Control, GetKeys, (BUTTONS*)&PAD_1, virtualControllers[Control].config))
				stop = 1;

		pads->leftJoyX = PAD_1.leftStickX; pads->leftJoyY = PAD_1.leftStickY;
		pads->rightJoyX = PAD_1.rightStickX; pads->rightJoyY = PAD_1.rightStickY;
	}

	memcpy( &lastport1, pads, sizeof( lastport1 ) ) ;
*/
	return 0;
}

long SSS_PADreadPort2 (PadDataS* pads)
{
	//#PADreadPort2 not used in PCSX
/*
	pads->buttonStatus = global.padStat[1];

	memset (pads, 0, sizeof (PadDataS));
	if ((global.padID[1] & 0xf0) == 0x40)
	{
		pads->rightJoyX = pads->rightJoyY = pads->leftJoyX = pads->leftJoyY = 128 ;
		pads->controllerType = PSE_PAD_TYPE_STANDARD;
	}
	else
	{
		pads->controllerType = PSE_PAD_TYPE_ANALOGPAD;
		int Control = 1;
#if defined(WII) && !defined(NO_BT)
		//Need to switch between Classic and WiimoteNunchuck if user swapped extensions
		if (padType[virtualControllers[Control].number] == PADTYPE_WII)
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
#endif
		if(virtualControllers[Control].inUse)
			if(DO_CONTROL(Control, GetKeys, (BUTTONS*)&PAD_2, virtualControllers[Control].config))
				stop = 1;

		pads->leftJoyX = PAD_2.leftStickX; pads->leftJoyY = PAD_2.leftStickY;
		pads->rightJoyX = PAD_2.rightStickX; pads->rightJoyY = PAD_2.rightStickY;
	}

	memcpy( &lastport2, pads, sizeof( lastport1 ) ) ;
*/
	return 0;
}
