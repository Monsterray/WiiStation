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
	int padMode1[10];	//0 = digital (41h), 1 = analog red (73h), 2 = analog green (53h: Dual Analog, Joystick)
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

/* The ANALOG combination was held on a controller read for the pad being updated (analog_poll,
 * from the drivers), and on the pad's previous update. Not in `global`: that is a save state's
 * layout. */
static int analog_held_now;
static u8 analog_was_held[10];
/* The mode of this pad was chosen by the game (44h) or the player (the ANALOG combination):
 * AnalogAuto leaves it alone from then on. Not in `global` for the same reason. */
static u8 analog_chosen[10];
/* AnalogAuto: armed when a game leaves config mode without having chosen a mode; the switch
 * comes at the first 42h read after analog_due (a vblank number), if the game and the player
 * have still chosen nothing. Games that choose by themselves, but not at once (Ape Escape:
 * 44h at vblank 307, 278 after its first 43h 00h; Spyro: 3 vblanks), so see the SIO traffic
 * they always saw. 600 vblanks (10 s) is twice Ape Escape's, the slowest measured. */
#define ANALOG_AUTO_DELAY 600
static u8 analog_armed[10];
/* The game sent 44h to some pad: it manages the modes itself, so AnalogAuto leaves every pad
 * alone (Ape Escape sets port 1 and never port 2, and a recording of it must replay as it
 * was made) */
static u8 analog_game_chose;
static u32 analog_due[10];

extern void SysPrintf(char *fmt, ...);
extern volatile int stop;

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

/* The pointer of Wii Remote chan, 640x480 TV pixels: the remote's own, or in debug builds the
 * input script's "irfake" while the remote sees no sensor bar (Dolphin with no device bound) */
static const struct ir_t *pad_ir(int chan)
{
	WPADData *wpad = WPAD_Data(chan);
#ifdef PERF_PROF
	extern int autoinput_irfake(int *x, int *y);   /* PadWiiSX.c */
	static struct ir_t fake;
	int x, y;
	if (!wpad->ir.valid && autoinput_irfake(&x, &y)) {
		fake = wpad->ir;
		fake.valid = 1;
		fake.x = x;
		fake.y = y;
		return &fake;
	}
#endif
	return &wpad->ir;
}

void lightgunInterrupt()
{
	int cursorX;
	int cursorY;
	int Control;
	const struct ir_t *ir;


	if (global.irq10En[0] == 0x10) Control = 0;
	else if (global.irq10En[1] == 0x10) Control = 1;
	else return;

	if ((global.padID[Control] != 0x31) && (global.padID[Control] != 0x63))
		return;

	ir = pad_ir(virtualControllers[Control].number);
	if(screenMode == 2)	cursorX = ((ir->x*848/640 - 104));
	else cursorX = (ir->x);

	cursorY = (ir->y/2);



	if ((cursorY > 5) && (cursorY < 220) && ir->valid){
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

/* An analog pad of a type with no config commands: the Dual Analog (SCPH-1150) and the Analog
 * Joystick (SCPH-1110). No 43h..4Fh, so a game switches nothing and AnalogAuto has no signal
 * to wait for. */
static int IsPlainAnalog (const int pad)
{
	return (controllerType == CONTROLLERTYPE_DUALANALOG || controllerType == CONTROLLERTYPE_JOYSTICK) &&
		global.padID[pad] != 0x31 && global.padID[pad] != 0x63 && global.padID[pad] != 0x12;
}

/* Rumble of the old one-motor method (psx-spx: SCPH-1150, SCPH-1200 and SCPH-110): the DualShock
 * and the Dual Analog. The Analog Joystick has no motor. */
static int HasRumble (const int pad)
{
	return IsDualShock(pad) || (controllerType == CONTROLLERTYPE_DUALANALOG && IsPlainAnalog(pad));
}

/* The modes a type has: 0 always, 1 (red) on the DualShock and Dual Analog, 2 (green, ID 53h) on
 * the Dual Analog and the Joystick. The ANALOG button steps through them in this order: the
 * DualShock 41h <-> 73h, the Dual Analog 41h -> 73h -> 53h -> 41h, the Joystick 41h <-> 53h
 * (its digital/analog switch). */
static int mode_valid (const int mode)
{
	switch (controllerType) {
	case CONTROLLERTYPE_ANALOG:     return mode <= 1;
	case CONTROLLERTYPE_DUALANALOG: return mode <= 2;
	case CONTROLLERTYPE_JOYSTICK:   return mode == 0 || mode == 2;
	}
	return mode == 0;
}

static int next_mode (const int mode)
{
	if (controllerType == CONTROLLERTYPE_DUALANALOG)
		return (mode + 1) % 3;
	return mode ? 0 : (controllerType == CONTROLLERTYPE_JOYSTICK ? 2 : 1);
}

static void PADsetMode (const int pad, const int mode)	//mode = 0 (digital), 1 (analog, red) or 2 (analog, green)
{
	static const u8 padID[] = { 0x41, 0x73, 0x53 };
	global.padMode1[pad] = mode;
	global.padID[pad] = padID[mode];
	RumbleReset(pad);
}

/* The mode a pad is in when it is a pad again after power-on or a light gun: a DualShock's
 * digital, unless AnalogAuto "Always" puts a pad of Controller Type Analog in analog mode,
 * its light red, as DuckStation's "force analog mode on reset" does. A game that has
 * a use for the sticks then finds them; Standard and Stick D-pad are digital pads. */
static int power_on_mode (void)
{
	if (analogAuto == ANALOGAUTO_OFF)
		return 0;
	/* No config commands, so no game to wait for: Auto and Always both start analog */
	if (controllerType == CONTROLLERTYPE_DUALANALOG)
		return 1;
	if (controllerType == CONTROLLERTYPE_JOYSTICK)
		return 2;
	return controllerType == CONTROLLERTYPE_ANALOG && analogAuto == ANALOGAUTO_ALWAYS;
}

/* The note the ANALOG button leaves on screen for 2 s (showFpsAndDebugInfo, SoftGPU/drawGX.c):
 * its text, and whether the light is on (red) */
extern u32 frame_counter;   /* psxcounters.c: +1 per vblank */
static char pad_note_text[48];
static u32 pad_note_vbl;
static int pad_note_red;

const char *pad_note(int *red)
{
	if (!pad_note_text[0] || frame_counter - pad_note_vbl >= 120)
		return NULL;
	if (red)
		*red = pad_note_red;
	return pad_note_text;
}

void analog_poll(int held)
{
	if (pad_probing)   /* PlugPAD.c pad_menu_combo_held: no game reads this */
		return;
	analog_held_now |= held;
}

/* The 2 s note on screen (not for AnalogAuto, whose switch the player did not ask for and
 * which many games trigger at start-up on both ports), and a line in the debug build's tty.log */
static void pad_note_show(const int pad, const char *what, const int on_screen)
{
	char t[sizeof pad_note_text];
	if (pad < 2)
		snprintf(t, sizeof t, "Port %d: %s", pad + 1, what);
	else
		snprintf(t, sizeof t, "Port %d%c: %s", (pad - 2) / 4 + 1, 'A' + (pad - 2) % 4, what);
	if (on_screen) {
		memcpy(pad_note_text, t, sizeof t);
		pad_note_red = global.padMode1[pad] == 1 && (IsDualShock(pad) || IsPlainAnalog(pad));
		pad_note_vbl = frame_counter;
	}
#ifdef PERF_PROF
	{
		char l[80];
		snprintf(l, sizeof l, "analog vbl=%lu %s\n", (unsigned long)g_perf.vblanks, t);
		perf_tty(l);
	}
#endif
}

/* The DualShock's ANALOG button, pressed with the controller's ANALOG combination (Configure
 * Buttons), as the real one works (Docs/ANALOG_BUTTON.md): each press switches between digital
 * (41h, light off) and analog (73h, red light), unless the game locked the mode (44h with key 3).
 * The switch stops the motors and forgets their map, and once the game has used config mode the
 * second ID byte reads 00h instead of 5Ah until its next 43h, which tells it the mode changed.
 * A digital pad, a light gun and a mouse have no such button. */
static void analog_button(const int pad, const int held)
{
	const char *what;
	if (!held || analog_was_held[pad]) {
		analog_was_held[pad] = held != 0;
		return;
	}
	analog_was_held[pad] = 1;
	if (!IsDualShock(pad) && !IsPlainAnalog(pad))
		what = "no ANALOG button (PSX Pad Type)";
	else if (global.padModeF[pad])
		what = "ANALOG locked by the game";
	else {
		PADsetMode(pad, next_mode(global.padMode1[pad]));
		analog_chosen[pad] = 1;   /* the player's choice: AnalogAuto keeps out of it */
		if (global.dsNewRumble[pad])
			global.padModeC[pad] = 1;
		what = global.padMode1[pad] == 2 ? "Analog (green light, 53h)" :
		       global.padMode1[pad] ? "Analog (red light)" : "Digital (light off)";
	}
	pad_note_show(pad, what, 1);
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

static int script_has(int pad)
{
	return script_port(pad) >= 0 && autoinput_active(script_port(pad));
}

/* PadMirror: the pad (port 2, a multitap slot) with no controller and no script lines of its own
 * plays port 1's (multitap 1 slot A's): its controller, or its script lines. One player then
 * drives every player of a split screen. Returns that pad, or -1 (off, a pad with its own
 * input, a Co-Op port, nothing on port 1). */
static int mirror_src(int pad)
{
	const int src = padType[0] == PADTYPE_MULTITAP ? 2 : 0;
	if (!padMirror || pad == src || virtualControllers[pad].inUse || script_has(pad) ||
	    (pad < 2 && padType[pad] == PADTYPE_COOP))
		return -1;
	/* A multitap on port 1 holds the players: port 2 stays as set. A mirrored pad there was a
	 * fifth controller, and CTR's pause menu took every press from it as well (2026-11-05). */
	if (pad == 1 && src == 2)
		return -1;
	return virtualControllers[src].inUse || script_has(src) ? src : -1;
}

/* Is there a controller in this multitap slot (2..9) to answer its address byte? One the
 * settings put there (a type other than None) that has a host controller, or that an input
 * script plays. A slot whose type is None is empty, whatever was assigned to it before. */
static int slot_present(int pad)
{
	int sp;
	if (mirror_src(pad) >= 0)
		return 1;
	if (padType[pad] == PADTYPE_NONE)
		return 0;
	if (virtualControllers[pad].inUse)
		return 1;
	sp = script_port(pad);
	return sp >= 0 && autoinput_active(sp);
}

static void UpdateState (const int pad) //Note: pad = 0 or 1
{
	PERF_INC(pad_update);
	/* PadMirror: this pad hands the PlayStation exactly what port 1 (multitap slot A) got at its
	 * last read -- in a multitap transfer, this same transfer, as slot A is read first. Reading
	 * the controller or the script again here put a press on another game frame for the mirrors
	 * now and then, and a menu that reads every player (CTR's pause) moved two rows for one
	 * press (2026-11-05). */
	{
		const int m = mirror_src(pad);
		if (m >= 0) {
			global.isConnected[pad] = 1;
			global.padStat[pad] = global.padStat[m];
			padOut[pad] = padOut[m];
			return;
		}
	}
	int cursorX = 0x1;
	int cursorY = 0xA;
	int curMouse;
	static int tempcursorX[4];
	static int tempcursorY[4];
	static int oldcursorX[4];
	static int oldcursorY[4];
	static BUTTONS PAD_Data;
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
					const struct ir_t *ir = pad_ir(virtualControllers[Control].number);
					if(screenMode == 2)	cursorX = ((ir->x*848/640 - 104)/1.72) + 75;
					else cursorX = (ir->x/1.72) + 75;

					cursorY = (ir->y/2) + (Config.PsxType ? 48 : 25);

					if (!ir->valid){
						cursorX = 0x1;
						cursorY = 0xA;
					}
				}
				else if (lightGun == LIGHTGUN_MOUSE){
					curMouse = virtualControllers[Control].number;
					global.padID[pad] = 0x12;
					/* gMouse: this remote was read in this vblank (pad_vblank clears it), so a
					 * second poll in the same frame gets the same movement */
					if (!gMouse[curMouse]){
						const struct ir_t *ir = pad_ir(curMouse);

						if(screenMode == 2)	cursorX = ir->x*848/640 - 104;
						else cursorX = ir->x;
						tempcursorX[curMouse] = cursorX;
						sensitivity = virtualControllers[Control].config->sensitivity;
						if (sensitivity < 0.1) sensitivity = 1.0;
						cursorX = (cursorX - oldcursorX[curMouse]) * sensitivity;
						if (cursorX > 127) cursorX = 127;
						if (cursorX < -128) cursorX = -128;

						cursorY = ir->y;
						tempcursorY[curMouse] = cursorY;
						cursorY = (cursorY - oldcursorY[curMouse]) * sensitivity;
						if (cursorY > 127) cursorY = 127;
						if (cursorY < -128) cursorY = -128;

						cursorX = (cursorX & 0xFF) | (cursorY<<8);

						oldcursorX[curMouse] = tempcursorX[curMouse];
						oldcursorY[curMouse] = tempcursorY[curMouse];

						if (!ir->valid){
							cursorX = 0;
						}
						tempcursorX[curMouse] = cursorX;   /* the second poll's: no movement either */
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
			PADsetMode( pad, power_on_mode());   /* back from a light gun: a pad again, as at power-on */
		}
	}
	else{
		if ((global.padID[pad] == 0x31) || (global.padID[pad] == 0x63) || (global.padID[pad] == 0x12))
		PADsetMode( pad, power_on_mode());
	}
#endif

	/* ControllerType Analog is a DualShock: digital at power-on (SSS_PADopen), and the game
	 * switches it to analog with config command 0x44 when it wants the sticks, or the player
	 * with the ANALOG combination (analog_button). This used to
	 * force analog mode on every poll, which undid the game's own choice and left games that
	 * only understand a digital pad (ID 0x41) ignoring every button -- as a real PlayStation
	 * does with the DualShock's analog light on. Standard ignores config commands, so a pad
	 * left in analog mode goes back to digital here. */
	if (!mode_valid(global.padMode1[pad]))
		PADsetMode( pad, 0);

	analog_held_now = 0;
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
		global.isConnected[pad] = script_has(pad);
		PAD_Data.btns.All = 0xFFFF;
		PAD_Data.leftStickX = PAD_Data.leftStickY = PAD_Data.rightStickX = PAD_Data.rightStickY = 128;
	}

	/* The ANALOG combination: the button, and no buttons for the game while it is held, so
	 * that its Start does not pause */
	analog_button(pad, analog_held_now);
	if (analog_held_now)
		PAD_Data.btns.All = 0xFFFF;

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
		extern unsigned short autoinput_mask(int port);   /* PadWiiSX.c: scripted presses from sd:/wiistation/autoinput.txt */
		/* padStat is byte-swapped so that the big-endian 16-bit store in the
		 * 0x42 response emits the two PSX bytes in wire order; the script's
		 * masks are in PSX order (Start 0008), so swap them the same way. */
		extern void autoinput_record(int port, unsigned short real);   /* PadWiiSX.c: "record" */
		unsigned short s = global.padStat[pad];
		unsigned short m;
		unsigned char st[4];
		unsigned short sw = 0;
		extern void autoinput_record_sticks(int port, const unsigned char st[4]);
		extern int autoinput_sticks(int port, unsigned char st[4]);
		autoinput_record(sp, ~((s << 8) | (s >> 8)) & 0xFFFF);   /* the real pad, before the script */
		m = autoinput_mask(sp);
		if (autoinput_sweep(sp, &sw, st))
			m |= sw;
		global.padStat[pad] &= ~(unsigned short)(((m << 8) | (m >> 8)) & 0xFFFF);   /* active low */
		/* the sticks the game gets: recorded as they are, or a script's in their place
		 * ("sticks" lines, after the read count above) */
		st[0] = PAD_Data.leftStickX; st[1] = PAD_Data.leftStickY;
		st[2] = PAD_Data.rightStickX; st[3] = PAD_Data.rightStickY;
		autoinput_record_sticks(sp, st);
		if (autoinput_sticks(sp, st)) {
			PAD_Data.leftStickX = st[0]; PAD_Data.leftStickY = st[1];
			PAD_Data.rightStickX = st[2]; PAD_Data.rightStickY = st[3];
		}
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

	/* The motors are driven once per vblank, in pad_vblank() below */
}

/* Rumble. A Wii Remote and a GameCube pad have one motor, on or off. The DualShock's small
 * motor is on or off too; its large one takes a strength, 00h..FFh (psx-spx "Vibration/Rumble
 * Control"). The Wii motor runs while the small one runs, and for the large one it runs a
 * share of every RUMBLE_PERIOD vblanks that follows the strength: FFh all of them, 01h one.
 * A motor keeps the value the game last sent until the game sends another, as on the pad
 * (polls that stop, during a load, do not stop it), so the duty cycle runs from the vblank,
 * not from the polls. The period starts when the large motor starts, on-time first, so a
 * short knock (MediEvil: 5Ah 2Dh 16h 0Ah 04h 01h, two vblanks each) is felt at once.
 * padVibF[pad][2] is what the Wii motor was last told, [3] the vblank in the period.
 * ponytail: RUMBLE_PERIOD 8 = 15 radio reports a second at most per remote, 9 strengths;
 * shorter is finer but busier, tune it on hardware. */
#define RUMBLE_PERIOD 8

static void rumble_send(const int pad, const int on)
{
	global.padVibF[pad][2] = on;
	if (on) PERF_INC(rumble_on); else PERF_INC(rumble_off);
	if (pad < 2 && padType[pad] == PADTYPE_COOP)
		coop_rumble(pad, on);
	else if (virtualControllers[pad].control && virtualControllers[pad].control->rumble)
		DO_CONTROL(pad, rumble, on);
}

/* Every emulated vblank (psxcounters.c) */
void pad_vblank(void)
{
	int pad;

	/* A mouse reads its remote's pointer once a vblank (UpdateState). This was cleared by
	 * the GPU plugins at their vblank, and only for a light gun in gpulib.c and never in
	 * OpenGX: the mouse there kept the first movement it read. */
	memset(gMouse, 0, sizeof(int) * 4);

	for (pad = 0; pad < 10; pad++)
	{
		const int big = global.padVibF[pad][1] & 0xff;
		int *phase = &global.padVibF[pad][3];
		const int on = global.padVibF[pad][0] || (big && *phase < (big * RUMBLE_PERIOD + 254) / 255);
		*phase = big ? (*phase + 1) % RUMBLE_PERIOD : 0;
#ifdef PERF_PROF
		/* rumble log (tty.log): the game's motor values at each change, and the vblanks and
		 * motor-on vblanks of the stretch they replace, so the duty can be checked */
		static int last_small[10], last_big[10];
		static unsigned frames[10], frames_on[10];
		if (global.padVibF[pad][0] != last_small[pad] || big != last_big[pad])
		{
			char l[96];
			snprintf(l, sizeof l, "rumble vbl=%lu pad=%d small=%d large=%02x prev: vbl=%u on=%u\n",
				(unsigned long)g_perf.vblanks, pad, global.padVibF[pad][0], big, frames[pad], frames_on[pad]);
			perf_tty(l);
			last_small[pad] = global.padVibF[pad][0];
			last_big[pad] = big;
			frames[pad] = frames_on[pad] = 0;
		}
		frames[pad]++;
		frames_on[pad] += on;
#endif
		if (on != global.padVibF[pad][2])
			rumble_send(pad, on);
	}
#if defined(WII) && !defined(NO_BT)
	{
		extern void wpad_leds_update(void);   /* controller-WiimoteNunchuk.c: a remote that reconnected */
		wpad_leds_update();
	}
#endif
}

/* The game stops or goes to the menu (PlugPAD.c pauseInput): every motor off. The values the
 * game set stay, so the motors start again where they were when it goes on. */
void pad_rumble_stop(void)
{
	int pad;
	for (pad = 0; pad < 10; pad++)
		if (global.padVibF[pad][2])
			rumble_send(pad, 0);
}

long SSS_PADopen (void *p)
{
	int i;
	{
		extern void autoinput_load(void);   /* PadWiiSX.c: sd:/wiistation/autoinput.txt incl. trace/dump schedules */
		autoinput_load();
	}
	memset (&global, 0, sizeof (global));
	memset (&io, 0, sizeof (io));
	memset (analog_was_held, 0, sizeof (analog_was_held));
	memset (analog_chosen, 0, sizeof (analog_chosen));
	memset (analog_armed, 0, sizeof (analog_armed));
	analog_game_chose = 0;
	{
		extern unsigned char pad_unplug[2];   /* PlugPAD.c: a type change before the game is no swap */
		pad_unplug[0] = pad_unplug[1] = 0;
	}
	memset( padOut, 0, sizeof(padOut) ) ;
	for(i = 0; i < 10; i++){
		global.padStat[i] = 0xffff;
		PADsetMode (i, power_on_mode());   /* digital at power-on, as a DualShock is, unless AnalogAuto is Always */
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

#ifdef PERF_PROF
/* Pad command log (tty.log, debug builds): each transfer's bytes from the console, the
 * command first, with the ID byte the pad answered. A 42h read is logged only when its
 * bytes or the ID change, so a game's motor values and mode switches stand out. */
static u8 probe_tx[10][9], probe_last42[10][9], probe_id[10], probe_lastid[10];
static int probe_n[10];

static void pad_probe (const int pad, const int cur, const u8 value)
{
	if (cur == 0 && probe_n[pad])
	{
		const int is42 = probe_tx[pad][0] == 0x42;
		if (!is42 || memcmp(probe_tx[pad], probe_last42[pad], 9) || probe_id[pad] != probe_lastid[pad])
		{
			char l[96];
			int k, n = snprintf(l, sizeof l, "pad vbl=%lu p=%d id=%02x tx=",
				(unsigned long)g_perf.vblanks, pad, probe_id[pad]);
			for (k = 0; k < probe_n[pad]; k++)
				n += snprintf(l + n, sizeof l - n, "%02x ", probe_tx[pad][k]);
			snprintf(l + n, sizeof l - n, "\n");
			perf_tty(l);
			if (is42)
			{
				memcpy(probe_last42[pad], probe_tx[pad], 9);
				probe_lastid[pad] = probe_id[pad];
			}
		}
		memset(probe_tx[pad], 0, 9);
		probe_n[pad] = 0;
	}
	if (cur < 9)
	{
		probe_tx[pad][cur] = value;
		probe_n[pad] = cur + 1;
	}
}
#define PAD_PROBE(pad, cur, value) pad_probe(pad, cur, value)
#define PAD_PROBE_ID(pad, id) (probe_id[pad] = (id))
#else
#define PAD_PROBE(pad, cur, value) ((void)0)
#define PAD_PROBE_ID(pad, id) ((void)0)
#endif

/* One byte after the address byte, to controller pad (0..9). Returns its reply; *ack says
 * whether it acknowledges, that is, asks for another byte: every byte of its reply but
 * the last. */
static unsigned char pad_byte (const int pad, const unsigned char value, int *ack)
{
	PadXfer *x = &io.x[pad];
	const int cur = x->cur;
	unsigned char r;

	PAD_PROBE(pad, cur, value);
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
			PAD_PROBE_ID(pad, 0xFF);
			return 0xFF;
		}
		switch (x->cmd)
		{
		case 0x42:
			/* AnalogAuto's deferred switch, between transfers, on a pad that has been read */
			if (analog_armed[pad] && !global.padModeE[pad] && (s32)(frame_counter - analog_due[pad]) >= 0)
			{
				analog_armed[pad] = 0;
				if (!analog_game_chose && !analog_chosen[pad] && !global.padMode1[pad] && !global.padModeF[pad] &&
				    global.isConnected[pad] && IsDualShock(pad))
				{
					/* What the player's press does: analog mode, and the byte after the ID
					 * reads 00h until the game's next 43h, so the game asks again and
					 * finds the sticks */
					PADsetMode (pad, 1);
					analog_chosen[pad] = 1;
					global.padModeC[pad] = 1;
					pad_note_show (pad, "Analog (red light) by AnalogAuto", 0);
				}
			}
			UpdateState(pad);
			/* fall through */
		case 0x43:
			x->len = 2 + 2 * (global.padID[pad] & 0x0f);
			x->buf.b8[1] = global.padModeC[pad] ? 0x00 : 0x5a;
			x->buf.b16[1] = global.padStat[pad];
			if (global.padID[pad] == 0x53)
			{
				/* Green mode (psx-spx "Dual Analog Pad in LED=Green Mode"): L3/R3 read 1, and
				 * bit9=L1 bit10=Square bit11=Triangle bit12=R1 bit15=R2 (the others stay).
				 * padStat is the word byte-swapped, active low. */
				const u16 w = (u16)((global.padStat[pad] << 8) | (global.padStat[pad] >> 8));
				#define GB(to, from) (((w >> (from)) & 1) << (to))
				const u16 g = (w & 0x61f9 /* Select, Start, D-pad, L2, Circle, Cross */) | 0x0006 |
					GB(9, 10) | GB(10, 15) | GB(11, 12) | GB(12, 11) | GB(15, 9);
				#undef GB
				x->buf.b16[1] = (u16)((g << 8) | (g >> 8));
			}
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
		PAD_PROBE_ID(pad, r);
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
		if (cur == 3 && HasRumble(pad) && !global.dsNewRumble[pad])
			global.padVibF[pad][0] = (global.legacyXX[pad] & 0xc0) == 0x40 && (value & 1);
		break;
	case 0x43:
		/* 01h enters config mode, 00h leaves it; other values change nothing */
		if (cur == 2 && value <= 1)
		{
			const int was_config = global.padModeE[pad];
			global.padModeE[pad] = value;
			global.padModeC[pad] = 0;
			if (value) global.dsNewRumble[pad] = 1;
			/* AnalogAuto: a game that asked what this pad can do (it entered config mode)
			 * and leaves without choosing a mode knows the DualShock, but the pad is still
			 * in digital mode, where it reads no sticks (Gex 3 and MediEvil send 43h 01h,
			 * 45h, 4Ch, 47h, 46h, 43h 00h and never 44h: on the real pad the player
			 * presses ANALOG). Arm the switch (see analog_due): after ANALOG_AUTO_DELAY
			 * vblanks without a 44h or a press it goes analog, once. Not at once: Ape
			 * Escape and Spyro leave config mode too, and choose later. */
			if (!value && was_config && analogAuto != ANALOGAUTO_OFF && IsDualShock(pad) &&
			    !analog_armed[pad] && !analog_chosen[pad] && !global.padMode1[pad])
			{
				analog_armed[pad] = 1;
				analog_due[pad] = frame_counter + ANALOG_AUTO_DELAY;
			}
		}
		break;
	case 0x44:
		/* LED 00h digital, 01h analog, others ignored. Key: AND 3 = 3 locks the Analog button */
		if (cur == 2 && value <= 1)
		{
			PADsetMode (pad, value);
			analog_chosen[pad] = 1;   /* the game chose: AnalogAuto keeps out of it */
			analog_game_chose = 1;
		}
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
	return (padType[port] != PADTYPE_NONE || mirror_src(port) >= 0) && !pad_unplug[port];
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
