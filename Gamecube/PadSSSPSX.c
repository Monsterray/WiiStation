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
	int curPad;			//0=pad1; 1=pad2: the port SSS_PADstartPoll/SSS_PADpoll talk to
	int irq10En[10];	// enable IRQ10 output for lightgun port
	int isConnected[10];	// is controller connected?
} global;

/* Each controller's side of the transfer in progress (psx-spx "Controller Communication
 * Sequence"): the bytes after the address byte, cur = 0 for the command. A multitap clocks
 * its four slots' controllers at once, so each has its own. len = bytes of the reply, the
 * ID byte included; the controller acknowledges every byte but its last. */
typedef struct
{
	int cur, cmd, len;
	union { u16 b16[20]; u8 b8[40]; } buf;
} PadXfer;

/* A multitap (SCPH-1070) on a port, as Mednafen's psx/input/multitap.cpp models it from
 * tests on the real thing; the same model gives psx-spx's "garbage" response:
 * - the first byte 01h..04h addresses slot A..D; a slot with nothing in it, or any other
 *   address, gets no /ACK;
 * - bit 0 of the third byte (the TAP byte) chooses the mode of the NEXT transfer, whatever
 *   slot it went to (setting); a transfer in that mode (full) answers 80h 5Ah, then 8 bytes
 *   for each slot A..D, FFh where a slot is empty or its controller has finished;
 * - in a full transfer the four controllers are clocked together during bytes 3..10, with
 *   the bytes the console sent for their slot blocks in the PREVIOUS full transfer (sb), or
 *   42h 00h.. when that one did not complete (prev_ok). A command other than 42h, or a
 *   controller that does not acknowledge its command byte, cuts the transfer after byte 3:
 *   four bytes, FFh 80h 5Ah and slot A's ID (psx-spx).
 * The PadTest DX ROM's probe sequence (github.com/Monsterray/padtest, docs/PROTOCOL.md)
 * checks each rule. */
typedef struct
{
	u8 setting, full, prev_ok, err, dp, done;
	s8 sel;		/* single-slot transfer: the slot (0..3), or -1 */
	u8 n;		/* bytes of this transfer so far, the address byte = 0 */
	u8 sb[4][8], fm[4][8];
} Mtap;

/* The transfers in progress: per controller, per multitap, and whether a port's single
 * controller took its address byte. Saved in a save state, after global. */
static struct
{
	PadXfer x[10];
	Mtap tap[2];
	int dev[2];
} io;

/* What each controller hands the PlayStation: buttons (active low, wire order) and sticks */
static PadDataS padOut[10];

extern void SysPrintf(char *fmt, ...);
extern int stop;

/* Controller type, later do this by a Variable in the GUI */
//extern char controllerType = 0; // 0 = standard, 1 = analog (analog fails on old games)
extern long  PadFlags;
extern int gLightgun;
extern int gMouse[4];

extern virtualControllers_t virtualControllers[NUM_VIRTUAL_CONTROLLERS];

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

/* Which autoinput.txt port drives this pad (PadWiiSX.c): 0 = "p1" (port 1, or multitap 1
 * slot A), 1 = "p2" (port 2, or multitap 2 slot A), 2..4 = "p1b".."p1d", 5..7 = "p2b".."p2d".
 * Slot A shares its port's lines, so a recording made with a pad plays a multitap game too.
 * -1: none. */
static int script_port(int pad)
{
	if (pad < 2) return pad;
	if (pad < 6) {
		if (padType[0] != PADTYPE_MULTITAP) return -1;
		return pad == 2 ? 0 : pad - 1;
	}
	if (padType[1] != PADTYPE_MULTITAP) return -1;
	return pad == 6 ? 1 : pad - 2;
}

extern int autoinput_active(int port);   /* PadWiiSX.c: a script has a pad on this port */
/* PadWiiSX.c "sweep": generated presses (PSX order, 1 = pressed) and sticks LX LY RX RY */
extern int autoinput_sweep(int port, unsigned short *press, unsigned char *sticks);

/* Is there a controller in this multitap slot (2..9) to answer its address byte? One the
 * settings put there (a type other than None) that has a host controller, or that an input
 * script plays. A slot whose type is None is empty, whatever was assigned to it before. */
static int slot_present(int pad)
{
	int sp;
	if (padType[pad] == PADTYPE_NONE)
		return 0;
	if (virtualControllers[pad].inUse)
		return 1;
	sp = script_port(pad);
	return sp >= 0 && autoinput_active(sp);
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
	/* padType[] is per port and multitap slot, as Control is; it used to be indexed by the
	 * Wii channel (.number), which on a multitap slot read some other port's type. */
	if (virtualControllers[Control].inUse &&
		padType[Control] == PADTYPE_WII)
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

	if (pad < 2 && padType[pad] == PADTYPE_COOP)
	{	/* Co-Op: the port's players, mixed into one pad (coop.c) */
		int any;
		miscButton = coop_get_keys(pad, (BUTTONS*)&PAD_Data, &any);
		global.isConnected[pad] = any || (script_port(pad) >= 0 && autoinput_active(script_port(pad)));
		if (miscButton == 1)
			stop = 1;
		else if (pad == 0)
			frameLimit[0] = (miscButton == 0 ? frameLimit[1] : 0);
	}
	else if(virtualControllers[Control].inUse)
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
	{	/* No host controller: a script may play it (SSS_PortStart decides whether anything
		 * answers at all); otherwise the buttons and sticks rest. */
		global.isConnected[pad] = script_port(pad) >= 0 && autoinput_active(script_port(pad)) ? 1 : 0;
		PAD_Data.btns.All = 0xFFFF;
		PAD_Data.leftStickX = PAD_Data.leftStickY = PAD_Data.rightStickX = PAD_Data.rightStickY = 128;
	}

	/* "sweep" in the input script: generated sticks in place of the controller's (its
	 * presses are added below, with the script's own) */
	{
		unsigned short sw_press = 0;
		unsigned char st[4];
		if (script_port(pad) >= 0 && autoinput_sweep(script_port(pad), &sw_press, st)) {
			PAD_Data.leftStickX = st[0]; PAD_Data.leftStickY = st[1];
			PAD_Data.rightStickX = st[2]; PAD_Data.rightStickY = st[3];
		}
	}

	/* Controller Type "Stick D-pad": a digital pad whose D-pad the left stick also
	 * presses (psx_analog.h), for games that take no analog input. Active low. After the
	 * sweep, so that a scripted stick presses the D-pad as a real one does. */
	if (controllerType == CONTROLLERTYPE_STICKDPAD)
	{
		int d = stick_dpad(PAD_Data.leftStickX, PAD_Data.leftStickY);
		if (d & STICK_DPAD_UP)    PAD_Data.btns.U_DPAD = 0;
		if (d & STICK_DPAD_DOWN)  PAD_Data.btns.D_DPAD = 0;
		if (d & STICK_DPAD_LEFT)  PAD_Data.btns.L_DPAD = 0;
		if (d & STICK_DPAD_RIGHT) PAD_Data.btns.R_DPAD = 0;
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
	if (script_port(pad) >= 0) {
		const int sp = script_port(pad);
		extern unsigned short autoinput_mask(int port);   /* PadWiiSX.c: scripted presses from sd:/wiisxrx/autoinput.txt */
		/* padStat is byte-swapped so that the big-endian 16-bit store in the
		 * 0x42 response emits the two PSX bytes in wire order; the script's
		 * masks are in PSX order (Start 0008), so swap them the same way. */
		extern void autoinput_record(int port, unsigned short real);   /* PadWiiSX.c: "record" */
		unsigned short s = global.padStat[pad];
		unsigned short m;
		unsigned char st[4];
		unsigned short sw = 0;
		autoinput_record(sp, ~((s << 8) | (s >> 8)) & 0xFFFF);   /* the real pad, before the script */
		m = autoinput_mask(sp);
		if (autoinput_sweep(sp, &sw, st))
			m |= sw;
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


		padOut[pad].leftJoyX = cursorY & 0xFF; padOut[pad].leftJoyY = cursorY >> 8;
		padOut[pad].rightJoyX = cursorX & 0xFF; padOut[pad].rightJoyY = cursorX >> 8;
		padOut[pad].buttonStatus = global.padStat[pad];
	}
	else{
		padOut[pad].leftJoyX = PAD_Data.leftStickX; padOut[pad].leftJoyY = PAD_Data.leftStickY;
		padOut[pad].rightJoyX = PAD_Data.rightStickX; padOut[pad].rightJoyY = PAD_Data.rightStickY;
		padOut[pad].buttonStatus = global.padStat[pad];
	}

	/* Debug builds keep a timeline of what each port handed the PlayStation, with the
	 * driver's own output beside it, so a scripted Dolphin run can be checked end to end
	 * (scripts/padtest.py). Repeats are dropped inside perf_pad_event. */
	{
		const PadDataS *out = &padOut[pad];
		perf_pad_event(pad,
			virtualControllers[Control].control ? (unsigned char)virtualControllers[Control].control->identifier : 0,
			PAD_Data.btns.All,
			(PAD_Data.leftStickX << 8) | PAD_Data.leftStickY,
			(PAD_Data.rightStickX << 8) | PAD_Data.rightStickY,
			out->buttonStatus,
			(out->leftJoyX << 8) | out->leftJoyY,
			(out->rightJoyX << 8) | out->rightJoyY);
	}

	/* A Wii-side controller has one motor. It runs while either DualShock motor runs:
	 * before, the small motor stopping stopped it while the big one still ran. */
	if (global.padVibF[pad][2] != vib0 || global.padVibF[pad][3] != vib1)
	{
		global.padVibF[pad][2] = vib0;
		global.padVibF[pad][3] = vib1;
		if (vib0 | vib1) PERF_INC(rumble_on); else PERF_INC(rumble_off);
		if (pad < 2 && padType[pad] == PADTYPE_COOP)
			coop_rumble(pad, vib0 | vib1);
		else if (virtualControllers[pad].control && virtualControllers[pad].control->rumble)
			DO_CONTROL(pad, rumble, vib0 | vib1);
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
	memset (&io, 0, sizeof (io));
	{
		extern unsigned char pad_unplug[2];   /* PlugPAD.c: a type change before the game is no swap */
		pad_unplug[0] = pad_unplug[1] = 0;
	}
	memset( padOut, 0, sizeof(padOut) ) ;
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

/* Config-mode replies of a DualShock (SCPH-1200): F3h, then these bytes from index 1 on
 * (psx-spx "Configuration Commands"; DuckStation, MiSTer and PsxNewLib agree). Always 9
 * bytes long. Commands whose data depend on their parameter start from cmdcfg and are
 * filled in when the parameter arrives (cur == 2). */
static const u8 cmdcfg[8] = { 0xff, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 cmd45[8] =		/* Type 01h = PS1 DualShock (03h is a DualShock 2), LED, constants */
{
	0xff, 0x5a, 0x01, 0x02, 0x00, 0x02, 0x01, 0x00,
};

/* Save states (misc.c, section PAD1): everything the pad keeps between polls -- DualShock
 * or digital mode, config mode, the rumble map, a transfer half done, a multitap's mode.
 * With data NULL, returns the size. A different size on load is another build's layout:
 * the pad stays as it is. */
int SSS_PADfreeze (int save, void *data, int len)
{
	const int size = (int)(sizeof(global) + sizeof(io));

	if (!data)
		return size;
	if (save) {
		memcpy(data, &global, sizeof(global));
		memcpy((char *)data + sizeof(global), &io, sizeof(io));
		return size;
	}
	if (len != size)
		return -1;
	memcpy(&global, data, sizeof(global));
	memcpy(&io, (char *)data + sizeof(global), sizeof(io));
	return size;
}

/* A controller took its address byte (01h): its reply starts with the next byte. */
static void pad_select (const int pad)
{
	io.x[pad].cur = 0;
	io.x[pad].cmd = 0;
	io.x[pad].len = 0;
}

/* The ID byte a controller shifts out with a command byte: F3h in config mode. */
static u8 pad_id_byte (const int pad)
{
	return (global.padModeE[pad] && IsDualShock(pad)) ? 0xF3 : (u8)global.padID[pad];
}

/* One byte after the address byte, to controller pad (0..9). Returns its reply; *ack says
 * whether it acknowledges, that is, asks for another byte: every byte of its reply but
 * the last. */
static unsigned char pad_byte (const int pad, const unsigned char value, int *ack)
{
	PadXfer *x = &io.x[pad];
	const int cur = x->cur;
	unsigned char r;

	x->cur++;
	if (cur == 0)
	{
		x->cmd = value;
		if (pad == 0 && (value & 0xf0) == 0x40)
			PERF_INC(pad_cmd[value & 0x0f]);
		/* Which commands answer (psx-spx "Configuration Commands"; DuckStation and MiSTer):
		 * a digital pad, a mouse and a light gun answer 42h only. A DualShock answers 42h
		 * and 43h in normal mode, and 40h..4Fh in config mode. Any other command gets no
		 * /ACK: FFh, and the transfer ends. */
		if (value != 0x42 && !(IsDualShock(pad) &&
		    (value == 0x43 || (global.padModeE[pad] && (value & 0xf0) == 0x40))))
		{
			x->len = 0;
			*ack = 0;
			return 0xFF;
		}
		switch (x->cmd)
		{
		case 0x42:
			UpdateState(pad);
			/* fall through */
		case 0x43:
			x->len = 2 + 2 * (global.padID[pad] & 0x0f);
			x->buf.b8[1] = global.padModeC[pad] ? 0x00 : 0x5a;
			x->buf.b16[1] = global.padStat[pad];
			if (value == 0x43 && global.padModeE[pad])
			{
				/* In config mode 43h answers F3h 5Ah and six 00h bytes, not the
				 * buttons, whatever mode the pad is in (psx-spx, DuckStation, PCSX-Redux). */
				x->len = 8;
				x->buf.b16[1] = 0;
				x->buf.b16[2] = 0;
				x->buf.b16[3] = 0;
				r = 0xf3;
				break;
			}
			x->buf.b8[4] = padOut[pad].rightJoyX;
			x->buf.b8[5] = padOut[pad].rightJoyY;
			x->buf.b8[6] = padOut[pad].leftJoyX;
			x->buf.b8[7] = padOut[pad].leftJoyY;

			//if (global.padID[pad] == 0x79)
			//{
			// do some pressure stuff (this is for PS2 only!)
			//}
#ifdef PERF_PROF
			if (pad == 0 && !g_perf.pad_press_len && global.padStat[pad] != 0xffff) {
				g_perf.pad_press_id = (u8)global.padID[pad];
				g_perf.pad_press_len = (u8)x->len;
				memcpy(g_perf.pad_press, x->buf.b8, 8);
			}
#endif
			/* In config mode 42h answers F3h with the sticks, even in digital mode. A pad
			 * switched to Standard since keeps no config mode. */
			if (global.padModeE[pad] && IsDualShock(pad))
			{
				x->len = 8;
				r = 0xf3;
				break;
			}
			r = (u8)global.padID[pad];
			break;
		case 0x44:
			/* Setting the LED resets the rumble map, whatever the parameters */
			RumbleReset(pad);
			x->len = sizeof (cmdcfg);
			memcpy (x->buf.b8, cmdcfg, sizeof (cmdcfg));
			r = 0xf3;
			break;
		case 0x45:
			x->len = sizeof (cmd45);
			memcpy (x->buf.b8, cmd45, sizeof (cmd45));
			x->buf.b8[4] = (u8)global.padMode1[pad];
			r = 0xf3;
			break;
		case 0x4d:
			/* Returns the old map, byte by byte, as the new one arrives */
			x->len = sizeof (cmdcfg);
			x->buf.b8[1] = 0x5a;
			memcpy (&x->buf.b8[2], global.dsRumble[pad], 6);
			r = 0xf3;
			break;
		default:
			/* 46h, 47h, 48h, 4Ch: filled in from their parameter below. 40h, 41h, 49h..4Bh,
			 * 4Eh, 4Fh: unused on a PS1 DualShock (the DualShock 2 ones), all 00h. */
			x->len = sizeof (cmdcfg);
			memcpy (x->buf.b8, cmdcfg, sizeof (cmdcfg));
			r = 0xf3;
			break;
		}
		*ack = x->len > 1;
		return r;
	}
	switch (x->cmd)
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
			global.irq10En[pad] = value;
			global.legacyXX[pad] = value;
		}
		if (cur == 3 && IsDualShock(pad) && !global.dsNewRumble[pad])
			global.padVibF[pad][0] = (global.legacyXX[pad] & 0xc0) == 0x40 && (value & 1);
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
			memcpy (&x->buf.b8[4], act[value], 4);
		}
		break;
	case 0x47:
		if (cur == 2 && value == 0)
		{
			x->buf.b8[4] = 0x02;
			x->buf.b8[6] = 0x01;
		}
		break;
	case 0x48:
		if (cur == 2 && value <= 1)
			x->buf.b8[6] = 0x01;
		break;
	case 0x4c:
		if (cur == 2 && value <= 1)
			x->buf.b8[5] = value ? 0x07 : 0x04;
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

	*ack = cur + 1 < x->len;
	if (cur >= x->len)
		return 0xFF;
	return x->buf.b8[cur];
}

/* A port answers at all: it has a type, and is not in the gap a type change leaves */
static int port_present (const int port)
{
	extern unsigned char pad_unplug[2];   /* PlugPAD.c */
	return padType[port] != PADTYPE_NONE && !pad_unplug[port];
}

/* The first byte of a transfer on PlayStation port `port` (0 or 1), not a memory card's
 * (sio.c sends those elsewhere). Returns the reply (HiZ: FFh); *ack = something answered
 * and takes the next byte. */
unsigned char SSS_PortStart (const int port, const unsigned char addr, int *ack)
{
	Mtap *t = &io.tap[port & 1];
	const int base = port ? 6 : 2;
	int s;

	*ack = 0;
	io.dev[port & 1] = 0;
	t->n = 0;
	t->sel = -1;
	if (!port_present(port) || (addr & 0xf0))
		return 0xFF;

	if (padType[port] != PADTYPE_MULTITAP)
	{
		/* A controller answers address 01h only (psx-spx, Mednafen, DuckStation); 02h..04h
		 * are for a multitap's slots B..D, and a plain controller ignores them. */
		if (addr == 0x01)
		{
			pad_select(port);
			io.dev[port & 1] = 1;
			*ack = 1;
		}
		return 0xFF;
	}

	/* The slot blocks the controllers get in a full transfer: the console's from the last
	 * one, if it went all the way, else a plain read */
	t->full = t->setting;
	if (!t->prev_ok)
	{
		memset(t->sb, 0, sizeof(t->sb));
		for (s = 0; s < 4; s++)
			t->sb[s][0] = 0x42;
	}
	t->prev_ok = 0;
	t->err = 0;
	if (t->full)
	{
		/* Every controller sees an address byte 01h, whatever the console sent; the
		 * multitap acknowledges even with all four slots empty */
		PERF_INC(mtap_full[port & 1]);
		memset(t->fm, 0xFF, sizeof(t->fm));
		t->dp = t->done = 0;
		for (s = 0; s < 4; s++)
			if (slot_present(base + s))
			{
				pad_select(base + s);
				t->dp |= 1 << s;
			}
		*ack = 1;
		return 0xFF;
	}

	/* Single-slot transfer: 01h..04h is slot A..D */
	PERF_INC(mtap_single[port & 1]);
	if (addr >= 0x01 && addr <= 0x04)
	{
		PERF_INC(mtap_addr[port & 1][addr - 1]);
		if (slot_present(base + addr - 1))
		{
			t->sel = (s8)(addr - 1);
			pad_select(base + t->sel);
			*ack = 1;
		}
		else
			PERF_INC(mtap_empty[port & 1]);
	}
	return 0xFF;
}

/* The next byte of a transfer that SSS_PortStart acknowledged. */
unsigned char SSS_PortByte (const int port, const unsigned char value, int *ack)
{
	Mtap *t = &io.tap[port & 1];
	const int base = port ? 6 : 2;
	int n, k, s, a;
	unsigned char r;

	*ack = 0;
	if (padType[port] != PADTYPE_MULTITAP)
		return io.dev[port & 1] ? pad_byte(port, value, ack) : 0xFF;

	n = ++t->n;
	if (n == 2)
	{
		t->setting = value & 1;   /* the TAP byte: the next transfer's mode */
		PERF_INC(mtap_tap[port & 1][value & 1]);
	}
	if (!t->full)
	{
		if (t->sel < 0)
			return 0xFF;
		return pad_byte(base + t->sel, value, ack);
	}

	if (n == 1)
	{
		t->err = value != 0x42;
		*ack = 1;
		return 0x80;
	}
	if (n == 2)
	{
		*ack = t->dp != 0;   /* nothing in any slot: the transfer ends here */
		return 0x5A;
	}
	if (n > 34)
		return 0xFF;
	k = n - 3;
	if (k < 8)
	{
		/* Bytes 3..10: the four controllers at once, each with its slot block */
		for (s = 0; s < 4; s++)
		{
			if (!(t->dp & (1 << s)) || (t->done & (1 << s)))
				continue;
			t->fm[s][k] = pad_byte(base + s, t->sb[s][k], &a);
			if (!a)
				t->done |= 1 << s;
			if (k == 0 && !a)
				t->err = 1;   /* a controller there refused its command */
		}
	}
	r = t->fm[k >> 3][k & 7];
	t->sb[k >> 3][k & 7] = value;
	if (k == 0 && t->err)
	{
		/* Cut short: FFh 80h 5Ah and the ID slot A's controller shifted out with the
		 * command it refused (psx-spx "garbage"), FFh with no controller there */
		PERF_INC(mtap_short[port & 1]);
		return (t->dp & 1) ? pad_id_byte(base) : 0xFF;
	}
	if (n == 33)
		t->prev_ok = 1;
	*ack = n < 34;
	return r;
}

/* The pad plugin's own entry points, for what does not go through sio.c's transfer (the
 * netplay path): port pad - 1, an address byte 01h, then a byte at a time. */
unsigned char SSS_PADstartPoll (int pad)
{
	int ack;
	PERF_INC(pad_startpoll);
	global.curPad = (pad - 1) & 1;
	return SSS_PortStart(global.curPad, 0x01, &ack);
}

unsigned char SSS_PADpoll (const unsigned char value)
{
	int ack;
	return SSS_PortByte(global.curPad, value, &ack);
}

long SSS_PADreadPort1 (PadDataS* pads)
{
	//#PADreadPort1 not used in PCSX
	return 0;
}

long SSS_PADreadPort2 (PadDataS* pads)
{
	//#PADreadPort2 not used in PCSX
	return 0;
}
