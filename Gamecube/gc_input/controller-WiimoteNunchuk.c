/**
 * WiiSX - controller-WiimoteNunchuk.c
 * Copyright (C) 2007, 2008, 2009, 2010 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 * 
 * Wiimote + Nunchuk input module
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                sepp256@gmail.com
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

#ifdef HW_RVL

#include <string.h>
#include <math.h>
#include <wiiuse/wpad.h>
#include "controller.h"
#include "../wiiSXconfig.h"
#include "../perf_prof.h"
#include <stdio.h>

#ifndef PI
#define PI 3.14159f
#endif

enum { STICK_X, STICK_Y };
static int getStickValue(joystick_t* j, int axis, int maxAbsValue){
	float angle = PI * j->ang/180.0f;
	float magnitude = (j->mag > 1.0f) ? 1.0f :
	                    (j->mag < -1.0f) ? -1.0f : j->mag;
	float value;
	if(axis == STICK_X)
		value = magnitude * sin( angle );
	else
		value = magnitude * cos( angle );
	return (int)(value * maxAbsValue);
}

enum {
	NUNCHUK_AS_ANALOG, IR_AS_ANALOG,
	TILT_AS_ANALOG, WHEEL_AS_ANALOG,
	NO_ANALOG,
};

enum {
	NUNCHUK_L = 0x10 << 16,
	NUNCHUK_R = 0x20 << 16,
	NUNCHUK_U = 0x40 << 16,
	NUNCHUK_D = 0x80 << 16,
};

#define NUM_WIIMOTE_BUTTONS 12
static button_t buttons[] = {
	{  0, ~0,                    "None" },
	{  1, WPAD_BUTTON_UP,        "D-Up" },
	{  2, WPAD_BUTTON_LEFT,      "D-Left" },
	{  3, WPAD_BUTTON_RIGHT,     "D-Right" },
	{  4, WPAD_BUTTON_DOWN,      "D-Down" },
	{  5, WPAD_BUTTON_A,         "A" },
	{  6, WPAD_BUTTON_B,         "B" },
	{  7, WPAD_BUTTON_PLUS,      "+" },
	{  8, WPAD_BUTTON_MINUS,     "-" },
	{  9, WPAD_BUTTON_HOME,      "Home" },
	{ 10, WPAD_BUTTON_1,         "1" },
	{ 11, WPAD_BUTTON_2,         "2" },
	{ 12, WPAD_NUNCHUK_BUTTON_C, "C" },
	{ 13, WPAD_NUNCHUK_BUTTON_Z, "Z" },
	{ 14, NUNCHUK_U,             "NC-Up" },
	{ 15, NUNCHUK_L,             "NC-Left" },
	{ 16, NUNCHUK_R,             "NC-Right" },
	{ 17, NUNCHUK_D,             "NC-Down" },
};

static button_t analog_sources_wmn[] = {
	{ 0, NUNCHUK_AS_ANALOG,  "Nunchuk" },
	{ 1, IR_AS_ANALOG,       "IR" },
};

static button_t analog_sources_wm[] = {
	{ 0, TILT_AS_ANALOG,     "Tilt" },
	{ 1, WHEEL_AS_ANALOG,    "Wheel" },
	{ 2, IR_AS_ANALOG,       "IR" },
	{ 3, NO_ANALOG,          "None" },
};

static button_t menu_combos[] = {
	{ 0, WPAD_BUTTON_1|WPAD_BUTTON_2, "1+2" },
	{ 1, WPAD_BUTTON_PLUS|WPAD_BUTTON_MINUS, "+&-" },
	{ 2, WPAD_BUTTON_HOME, "Home" },
};

/* Screenshot combinations, entry 0 is Off. Start is in none of them, so the game does not pause.
 * Entry 1 is the default. No menu combination may lie inside one of these (Configure Buttons
 * refuses it). */
static button_t screenshot_combos[] = {
	{ 0, 0,                                                  "Off" },
	{ 1, WPAD_BUTTON_A|WPAD_BUTTON_B|WPAD_BUTTON_1, "A+B+1" },
	{ 2, WPAD_BUTTON_A|WPAD_BUTTON_B,                "A+B" },
};

/* ANALOG button combinations (PadSSSPSX.c analog_button), entry 0 is Off, entry 1 the default:
 * the screenshot's pair with another button. While one is held the game sees no buttons, so
 * its Start does not pause. Configure Buttons keeps it apart from the menu, fast-forward and
 * screenshot combinations. */
static button_t analog_combos[] = {
	{ 0, 0, "Off" },
	{ 1, WPAD_BUTTON_A|WPAD_BUTTON_B|WPAD_BUTTON_2, "A+B+2" },
};

static int _GetKeys(int Control, BUTTONS * Keys, controller_config_t* config,
                    int (*available)(int),
                    unsigned int (*getButtons)(WPADData*))
{
	wpad_scan_if_needed();
	WPADData* wpad = WPAD_Data(Control);
	BUTTONS* c = Keys;
	memset(c, 0, sizeof(BUTTONS));
	//Reset buttons & sticks
	c->btns.All = 0xFFFF;
	c->leftStickX = c->leftStickY = c->rightStickX = c->rightStickY = 128;

	// Only use a connected nunchuck controller
	if(!available(Control))
		return 0;

	unsigned int b = getButtons(wpad);
	inline int isHeld(button_tp button){
		return (b & button->mask) == button->mask ? 0 : 1;
	}
	
	c->btns.SQUARE_BUTTON    = isHeld(config->SQU);
	c->btns.CROSS_BUTTON     = isHeld(config->CRO);
	c->btns.CIRCLE_BUTTON    = isHeld(config->CIR);
	c->btns.TRIANGLE_BUTTON  = isHeld(config->TRI);

	c->btns.R1_BUTTON    = isHeld(config->R1);
	c->btns.L1_BUTTON    = isHeld(config->L1);
	c->btns.R2_BUTTON    = isHeld(config->R2);
	c->btns.L2_BUTTON    = isHeld(config->L2);

	c->btns.L_DPAD       = isHeld(config->DL);
	c->btns.R_DPAD       = isHeld(config->DR);
	c->btns.U_DPAD       = isHeld(config->DU);
	c->btns.D_DPAD       = isHeld(config->DD);

	c->btns.START_BUTTON  = isHeld(config->START);
	c->btns.R3_BUTTON    = isHeld(config->R3);
	c->btns.L3_BUTTON    = isHeld(config->L3);
	c->btns.SELECT_BUTTON = isHeld(config->SELECT);

	//adjust values by 128 cause PSX sticks range 0-255 with a 128 center pos
	s8 stickX = 0;
	s8 stickY = 0;
	if(config->analogL->mask == NUNCHUK_AS_ANALOG){
		stickX = getStickValue(&wpad->exp.nunchuk.js, STICK_X, 127);
		stickY = getStickValue(&wpad->exp.nunchuk.js, STICK_Y, 127);
	} else if(config->analogL->mask == IR_AS_ANALOG){
		if(wpad->ir.smooth_valid){
			stickX = ((short)(wpad->ir.sx - 512)) >> 2;
			stickY = -(signed char)((wpad->ir.sy - 384) / 3);
		} else {
			stickX = 0;
			stickY = 0;
		}
	} else if(config->analogL->mask == TILT_AS_ANALOG){
		stickX = -wpad->orient.pitch;
		stickY = wpad->orient.roll;
	} else if(config->analogL->mask == WHEEL_AS_ANALOG){
		stickX = 512 - wpad->accel.y;
		stickY = wpad->accel.z - 512;
	}
	/* +127 centred this stick one below every other controller, so it rested just left
	 * of centre and drifted in any game that reads the analog sticks. */
	c->leftStickX  = psx_analog((float)stickX / 127.0f);
	c->leftStickY  = psx_analog((float)(config->invertedYL ? stickY : -stickY) / 127.0f);

	stickX = 0;
	stickY = 0;
	if(config->analogR->mask == NUNCHUK_AS_ANALOG){
		stickX = getStickValue(&wpad->exp.nunchuk.js, STICK_X, 127);
		stickY = getStickValue(&wpad->exp.nunchuk.js, STICK_Y, 127);
	} else if(config->analogR->mask == IR_AS_ANALOG){
		if(wpad->ir.smooth_valid){
			stickX = ((short)(wpad->ir.sx - 512)) >> 2;
			stickY = -(signed char)((wpad->ir.sy - 384) / 3);
		} else {
			stickX = 0;
			stickY = 0;
		}
	} else if(config->analogR->mask == TILT_AS_ANALOG){
		stickX = -wpad->orient.pitch;
		stickY = wpad->orient.roll;
	} else if(config->analogR->mask == WHEEL_AS_ANALOG){
		stickX = 512 - wpad->accel.y;
		stickY = wpad->accel.z - 512;
	}
	c->rightStickX  = psx_analog((float)stickX / 127.0f);
	c->rightStickY  = psx_analog((float)(config->invertedYR ? stickY : -stickY) / 127.0f);

	// Return 1 if exit, 2 if fastforward
	if (!isHeld(config->exit)) return 1;
	if (!isHeld(config->fastf)) return 2;
	screenshot_poll(Control, config->screenshot && config->screenshot->mask && !isHeld(config->screenshot));
	analog_poll(config->analog && config->analog->mask && !isHeld(config->analog));
	return 0;
}

static int checkType(int Control, int type){
	int err;
	u32 expType;
	err = WPAD_Probe(Control, &expType);

	if(err != WPAD_ERR_NONE)
		return -1;
	
	switch(expType){
	case WPAD_EXP_NONE:
		controller_Wiimote.available[Control] = 1;
		break;
	case WPAD_EXP_NUNCHUK:
		controller_WiimoteNunchuk.available[Control] = 1;
		break;
	case WPAD_EXP_CLASSIC:
		controller_Classic.available[Control] = 1;
		break;
	}
	
	return expType;
}

static int availableWM(int Control){
	if(checkType(Control, WPAD_EXP_NONE) != WPAD_EXP_NONE){
		controller_Wiimote.available[Control] = 0;
		return 0;
	} else {
		return 1;
	}
}

static int availableWMN(int Control){
	if(checkType(Control, WPAD_EXP_NUNCHUK) != WPAD_EXP_NUNCHUK){
		controller_WiimoteNunchuk.available[Control] = 0;
		return 0;
	} else {
		return 1;
	}
}

static unsigned int getButtonsWM(WPADData* controller){
	return controller->btns_h;
}

static unsigned int getButtonsWMN(WPADData* controller){
	unsigned int b = controller->btns_h;
	s8 stickX      = getStickValue(&controller->exp.nunchuk.js, STICK_X, 7);
	s8 stickY      = getStickValue(&controller->exp.nunchuk.js, STICK_Y, 7);
	
	if(stickX    < -3) b |= NUNCHUK_L;
	if(stickX    >  3) b |= NUNCHUK_R;
	if(stickY    >  3) b |= NUNCHUK_U;
	if(stickY    < -3) b |= NUNCHUK_D;
	
	return b;
}

static int GetKeysWM(int con, BUTTONS * keys, controller_config_t* cfg){
	return _GetKeys(con, keys, cfg, availableWM, getButtonsWM);
}

static int GetKeysWMN(int con, BUTTONS * keys, controller_config_t* cfg){
	return _GetKeys(con, keys, cfg, availableWMN, getButtonsWMN);
}

static void pause(int Control){
	WPAD_Rumble(Control, 0);
}

static void resume(int Control){ }

static void rumble(int Control, int rumble){
	WPAD_Rumble(Control, (rumble && rumbleEnabled) ? 1 : 0);
}

static void configure(int Control, controller_config_t* config){
	static s32 analog_fmts[] = {
		WPAD_DATA_EXPANSION, // Nunchuk
		WPAD_DATA_IR,        // IR
		WPAD_DATA_ACCEL,     // Tilt
		WPAD_DATA_ACCEL,     // Wheel
		WPAD_DATA_BUTTONS,   // None
	};
	WPAD_SetDataFormat(Control, analog_fmts[config->analogL->mask] | analog_fmts[config->analogR->mask]);
}

/* Player LEDs. In the menu a Wii Remote shows its own player number: the channel it synced
 * on, LED 1 for remote 1, as the System Menu shows it. While a game runs it shows the
 * PlayStation player it plays (PlugPAD.c led_player: port, multitap slot or Co-Op player),
 * and its own number when it plays none. PlugPAD.c pauseInput/resumeInput switch between
 * the two (wpad_leds_mode).
 * Each WPAD_ControlLed is a Bluetooth report, and the menu assigns the pads many times a
 * second (a stream of reports made the LEDs flicker), so one goes out only when the LEDs
 * should change. led_shown is what the remote shows, -1 when that is not known: the remote
 * is not connected (a probe fails), or libogc set the LEDs itself, which it does when a remote
 * connects (wpad_leds.c calls wpad_led_lost, LIBOGC=3). pad_vblank (PadSSSPSX.c) calls
 * wpad_leds_update every vblank, so a remote that drops and comes back in a game gets its
 * player LED again. Shared with controller-Classic.c: a Classic Controller is on its
 * Wiimote's channel. */
static int led_player[4] = { -1, -1, -1, -1 };   /* the PlayStation player, 0..3, or -1 */
static int led_shown[4] = { -1, -1, -1, -1 };
static int led_game;

static void led_send(int chan)
{
	const int leds = WPAD_LED_1 << ((led_game && led_player[chan] >= 0) ? led_player[chan] : chan);
	u32 type;
	if (WPAD_Probe(chan, &type) != WPAD_ERR_NONE) {
		led_shown[chan] = -1;
		return;
	}
	if (led_shown[chan] == leds)
		return;
	led_shown[chan] = leds;
	WPAD_ControlLed(chan, leds);
#ifdef PERF_PROF
	{
		char l[64];
		snprintf(l, sizeof l, "led vbl=%lu chan=%d leds=%x game=%d\n",
			(unsigned long)g_perf.vblanks, chan, leds, led_game);
		perf_tty(l);
	}
#endif
}

void wpad_led_lost(int chan)
{
	if (chan >= 0 && chan < 4)
		led_shown[chan] = -1;
}

void wpad_leds_update(void)
{
	int chan;
	for (chan = 0; chan < 4; chan++)
		led_send(chan);
}

void wpad_leds_mode(int game)
{
	led_game = game;
	wpad_leds_update();
}

/* player: the PlayStation player this channel's controller plays, -1 none */
void wpad_player_led(int chan, int player)
{
	if (chan < 0 || chan > 3)
		return;
	led_player[chan] = player < 0 ? -1 : player & 3;
	led_send(chan);
}

static void assign(int p, int v){
	wpad_player_led(p, v);
}

static void refreshAvailableWM(void);
static void refreshAvailableWMN(void);

controller_t controller_Wiimote =
	{ 'W',
	  GetKeysWM,
	  configure,
	  assign,
	  pause,
	  resume,
	  rumble,
	  refreshAvailableWM,
	  {0, 0, 0, 0},
	  NUM_WIIMOTE_BUTTONS,
	  buttons,
	  sizeof(analog_sources_wm)/sizeof(analog_sources_wm[0]),
	  analog_sources_wm,
	  sizeof(menu_combos)/sizeof(menu_combos[0]),
	  menu_combos,
	  sizeof(screenshot_combos)/sizeof(screenshot_combos[0]),
	  screenshot_combos,
	  sizeof(analog_combos)/sizeof(analog_combos[0]),
	  analog_combos,
	  { .SQU        = &buttons[0],  // None
	    .CRO        = &buttons[10], // 1
	    .CIR        = &buttons[11], // 2
	    .TRI        = &buttons[0],  // None
	    .R1         = &buttons[7],  // +
	    .L1         = &buttons[8],  // -
	    .R2         = &buttons[0],  // None
	    .L2         = &buttons[0],  // None
	    .R3         = &buttons[0],  // None
	    .L3         = &buttons[0],  // None
	    .DL         = &buttons[1],  // D Up
	    .DR         = &buttons[4],  // D Down
	    .DU         = &buttons[3],  // D Right
	    .DD         = &buttons[2],  // D Left
	    .START      = &buttons[9],  // Home
	    .SELECT     = &buttons[0],  // None
	    .analogL    = &analog_sources_wm[0], // Tilt
	    .analogR    = &analog_sources_wm[3], // None
	    .exit       = &menu_combos[1], // +&-
	    .invertedYL = 0,
	    .invertedYR = 0,
		.sensitivity = 1.0,
		.fastf       = &menu_combos[0], // 1+2
		.screenshot = &screenshot_combos[1],
		.analog = &analog_combos[1],
	  }
	};

controller_t controller_WiimoteNunchuk =
	{ 'N',
	  GetKeysWMN,
	  configure,
	  assign,
	  pause,
	  resume,
	  rumble,
	  refreshAvailableWMN,
	  {0, 0, 0, 0},
	  sizeof(buttons)/sizeof(buttons[0]),
	  buttons,
	  sizeof(analog_sources_wmn)/sizeof(analog_sources_wmn[0]),
	  analog_sources_wmn,
	  sizeof(menu_combos)/sizeof(menu_combos[0]),
	  menu_combos,
	  sizeof(screenshot_combos)/sizeof(screenshot_combos[0]),
	  screenshot_combos,
	  sizeof(analog_combos)/sizeof(analog_combos[0]),
	  analog_combos,
	  { .SQU        = &buttons[2], // D Left
	    .CRO        = &buttons[4], // D Down
	    .CIR        = &buttons[3], // D Right
	    .TRI        = &buttons[1], // D Up
	    .R1         = &buttons[5],  // A
	    .L1         = &buttons[13], // Z
	    .R2         = &buttons[6],  // B
	    .L2         = &buttons[12], // C
	    .R3         = &buttons[0],  // None
	    .L3         = &buttons[0],  // None
	    .DL         = &buttons[0],  // None
	    .DR         = &buttons[0],  // None
	    .DU         = &buttons[0],  // None
	    .DD         = &buttons[0],  // None
	    .START      = &buttons[8],  // -
	    .SELECT     = &buttons[7],  // +
	    .analogL    = &analog_sources_wmn[0], // Nunchuck
	    .analogR    = &analog_sources_wmn[1], // IR
	    .exit       = &menu_combos[0], // 1+2
	    .invertedYL = 0,
	    .invertedYR = 0,
		.sensitivity = 1.0,
		.fastf       = &menu_combos[1], // +&-
		.screenshot = &screenshot_combos[1],
		.analog = &analog_combos[1],
	  }
	 };

static void refreshAvailableWM(void){
	int i;
	WPAD_ScanPads();
	for(i=0; i<4; ++i){
		availableWM(i);
	}
}

static void refreshAvailableWMN(void){
	int i;
	WPAD_ScanPads();
	for(i=0; i<4; ++i){
		availableWMN(i);
	}
}
#endif
