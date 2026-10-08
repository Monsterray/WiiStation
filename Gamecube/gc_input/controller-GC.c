/**
 * WiiSX - controller-GC.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 *
 * Gamecube controller input module
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


#include <string.h>
#include <ogc/pad.h>
#include "controller.h"
#include "../wiiSXconfig.h"

enum {
	ANALOG_AS_ANALOG = 1, C_STICK_AS_ANALOG = 2,
};

enum {
	ANALOG_L         = 0x01 << 16,
	ANALOG_R         = 0x02 << 16,
	ANALOG_U         = 0x04 << 16,
	ANALOG_D         = 0x08 << 16,
	C_STICK_L        = 0x10 << 16,
	C_STICK_R        = 0x20 << 16,
	C_STICK_U        = 0x40 << 16,
	C_STICK_D        = 0x80 << 16,
	PAD_TRIGGER_Z_UP = 0x0100 << 16,
};

static button_t buttons[] = {
	{  0, ~0,                                "None" },
	{  1, PAD_BUTTON_UP,                     "D-Up" },
	{  2, PAD_BUTTON_LEFT,                   "D-Left" },
	{  3, PAD_BUTTON_RIGHT,                  "D-Right" },
	{  4, PAD_BUTTON_DOWN,                   "D-Down" },
	{  5, PAD_TRIGGER_L|PAD_TRIGGER_Z_UP,    "L-Z" },
	{  6, PAD_TRIGGER_R|PAD_TRIGGER_Z_UP,    "R-Z" },
	{  7, PAD_TRIGGER_L|PAD_TRIGGER_Z,       "L+Z" },
	{  8, PAD_TRIGGER_R|PAD_TRIGGER_Z,       "R+Z" },
	{  9, PAD_BUTTON_A,                      "A" },
	{ 10, PAD_BUTTON_B,                      "B" },
	{ 11, PAD_BUTTON_X,                      "X" },
	{ 12, PAD_BUTTON_Y,                      "Y" },
	{ 13, PAD_BUTTON_START|PAD_TRIGGER_Z_UP, "Start-Z" },
	{ 14, PAD_BUTTON_START|PAD_TRIGGER_Z,    "Start+Z" },
	{ 15, C_STICK_U,                         "C-Up" },
	{ 16, C_STICK_L,                         "C-Left" },
	{ 17, C_STICK_R,                         "C-Right" },
	{ 18, C_STICK_D,                         "C-Down" },
	{ 19, ANALOG_U,                          "A-Up" },
	{ 20, ANALOG_L,                          "A-Left" },
	{ 21, ANALOG_R,                          "A-Right" },
	{ 22, ANALOG_D,                          "A-Down" },
};

static button_t analog_sources[] = {
	{ 0, ANALOG_AS_ANALOG,  "A-Stick" },
	{ 1, C_STICK_AS_ANALOG, "C-Stick" },
};

static button_t menu_combos[] = {
	{ 0, PAD_BUTTON_X|PAD_BUTTON_Y, "X+Y" },
	{ 1, PAD_BUTTON_START|PAD_BUTTON_X, "Start+X" },
	{ 2, PAD_BUTTON_START|PAD_BUTTON_Y, "Start+Y" },
};

u32 gc_connected;

/* One reading of a GameCube pad: the buttons held and the two sticks, as libogc reports
 * them (origin already taken off, so the sticks are signed and centred on 0). */
typedef struct { unsigned int buttons; s8 sx, sy, cx, cy; } gc_raw_t;

/*  The scripted sweep  */

/* "padsweep <vblank>" in sd:/wiistation/autoinput.txt (PadWiiSX.c) replaces the pad with a
 * generated sweep from that vblank on: each stick axis walked end to end a step per
 * vblank, then each button held on its own. Everything downstream -- this driver's
 * conversion, the pad plugin, the PSX packing -- runs on it exactly as on a real pad, so
 * a run of any game produces a trace that says whether the whole path is intact. See
 * Docs/CONTROLLER_TESTING.md.
 *
 * The sticks are swept over GC_STICK_FULL rather than the whole s8 range: past full
 * deflection every value maps to the same end, which would look like dead travel. */
#define SWEEP_FULL   96            /* one axis runs -96..96 */
#define SWEEP_STEPS  (2 * SWEEP_FULL + 1)
#define SWEEP_HOLD   8             /* vblanks each button is held, and released, for */
/* The combinations the default mapping below actually uses, not the bare buttons: Z on
 * its own is not mapped to anything, and L, R and Start each mean one thing alone and
 * another with Z held. These fourteen reach fourteen of the PlayStation's sixteen
 * buttons; L3 and R3 are "None" by default and no press can produce them. */
static const unsigned int SWEEP_BUTTONS[] = {
	PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y,
	PAD_TRIGGER_L, PAD_TRIGGER_R,
	PAD_TRIGGER_L | PAD_TRIGGER_Z, PAD_TRIGGER_R | PAD_TRIGGER_Z,
	PAD_BUTTON_START, PAD_BUTTON_START | PAD_TRIGGER_Z,
	PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT,
};
#define SWEEP_NBUTTONS ((int)(sizeof(SWEEP_BUTTONS) / sizeof(SWEEP_BUTTONS[0])))
#define SWEEP_STICKS   (4 * SWEEP_STEPS)
#define SWEEP_LEN      (SWEEP_STICKS + SWEEP_NBUTTONS * 2 * SWEEP_HOLD)
/* "padsweep <vblank> fast": a short sweep for runs that check many setups (PadTest DX's
 * matrix, scripts/padtest_dx.sh). Each button held 3 vblanks; the two X axes walked
 * together in steps of 3, then the two Y axes: 214 vblanks, against 996. A step of 3 gives
 * 65 values an axis, still finely graded; the full sweep is what padtest.py's 150 needs. */
#define FAST_HOLD      3
#define FAST_STEP      3
#define FAST_RAMP      (2 * SWEEP_FULL / FAST_STEP + 1)
#define FAST_LEN       (SWEEP_NBUTTONS * 2 * FAST_HOLD + 2 * FAST_RAMP)

extern unsigned autoinput_padsweep_vbl;   /* PadWiiSX.c */
extern int autoinput_padsweep_fast;
/* psxcounters.h would drag in unistd.h, whose pause() collides with this file's own. */
extern u32 frame_counter;

static int gc_sweep(gc_raw_t *r)
{
	int t;
	s8 *axis[4];

	if (!autoinput_padsweep_vbl || frame_counter < autoinput_padsweep_vbl) return 0;
	r->buttons = 0;
	r->sx = r->sy = r->cx = r->cy = 0;
	if (autoinput_padsweep_fast) {
		t = (int)((frame_counter - autoinput_padsweep_vbl) % FAST_LEN);
		if (t < SWEEP_NBUTTONS * 2 * FAST_HOLD) {
			if ((t / FAST_HOLD) & 1)
				r->buttons = SWEEP_BUTTONS[t / FAST_HOLD / 2];
		} else {
			t -= SWEEP_NBUTTONS * 2 * FAST_HOLD;
			if (t < FAST_RAMP)
				r->sx = r->cx = (s8)(t * FAST_STEP - SWEEP_FULL);
			else
				r->sy = r->cy = (s8)((t - FAST_RAMP) * FAST_STEP - SWEEP_FULL);
		}
		return 1;
	}
	/* Repeat, so that a run started before the game finished loading still catches one. */
	t = (int)((frame_counter - autoinput_padsweep_vbl) % SWEEP_LEN);
	if (t < SWEEP_STICKS) {
		axis[0] = &r->sx; axis[1] = &r->sy; axis[2] = &r->cx; axis[3] = &r->cy;
		*axis[t / SWEEP_STEPS] = (s8)(t % SWEEP_STEPS - SWEEP_FULL);
	} else {
		t -= SWEEP_STICKS;
		if ((t / SWEEP_HOLD) & 1)   /* every other slot is released, so each press is its own */
			r->buttons = SWEEP_BUTTONS[(t / SWEEP_HOLD / 2) % SWEEP_NBUTTONS];
	}
	return 1;
}

static void gc_read(int Control, gc_raw_t *r)
{
	if (gc_sweep(r)) return;
	r->buttons = PAD_ButtonsHeld(Control);
	r->sx = PAD_StickX(Control);
	r->sy = PAD_StickY(Control);
	r->cx = PAD_SubStickX(Control);
	r->cy = PAD_SubStickY(Control);
}

static unsigned int getButtons(const gc_raw_t *r)
{
	unsigned int b = r->buttons;
	s8 stickX      = r->sx;
	s8 stickY      = r->sy;
	s8 substickX   = r->cx;
	s8 substickY   = r->cy;
	
	if(stickX    < -48) b |= ANALOG_L;
	if(stickX    >  48) b |= ANALOG_R;
	if(stickY    >  48) b |= ANALOG_U;
	if(stickY    < -48) b |= ANALOG_D;
	
	if(substickX < -48) b |= C_STICK_L;
	if(substickX >  48) b |= C_STICK_R;
	if(substickY >  48) b |= C_STICK_U;
	if(substickY < -48) b |= C_STICK_D;
	
	if(!(b & PAD_TRIGGER_Z)) b |= PAD_TRIGGER_Z_UP;

	return b;
}

/* One PlayStation stick from the GameCube stick the config names (psx_analog.h has the two
 * sticks' full travel). If a pad cannot quite reach the edges, raise its sensitivity setting
 * rather than the constants: the setting is per controller and needs no rebuild. A source
 * of none rests. */
static void GCtoPSX(const gc_raw_t *r, unsigned int source, int invertY, u8 *px, u8 *py)
{
	float fx = 0.0f, fy = 0.0f;

	if (source == ANALOG_AS_ANALOG) {
		fx = (float)r->sx / GC_MAIN_FULL;
		fy = (float)r->sy / GC_MAIN_FULL;
	} else if (source == C_STICK_AS_ANALOG) {
		fx = (float)r->cx / GC_CSTICK_FULL;
		fy = (float)r->cy / GC_CSTICK_FULL;
	}
	if (!invertY) fy = -fy;
	psx_square(&fx, &fy);
	*px = psx_analog(fx);
	*py = psx_analog(fy);
}

static int _GetKeys(int Control, BUTTONS * Keys, controller_config_t* config)
{
	if(padNeedScan){ gc_connected = PAD_ScanPads(); padNeedScan = 0; }
	BUTTONS* c = Keys;
	memset(c, 0, sizeof(BUTTONS));
	//Reset buttons & sticks
	c->btns.All = 0xFFFF;
	c->leftStickX = c->leftStickY = c->rightStickX = c->rightStickY = 128;

	controller_GC.available[Control] = (gc_connected & (1<<Control)) ? 1 : 0;
	if (!controller_GC.available[Control]) return 0;

	gc_raw_t raw;
	gc_read(Control, &raw);
	unsigned int b = getButtons(&raw);
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

	GCtoPSX(&raw, config->analogL->mask, config->invertedYL, &c->leftStickX, &c->leftStickY);
	GCtoPSX(&raw, config->analogR->mask, config->invertedYR, &c->rightStickX, &c->rightStickY);

	// Return 1 if exit, 2 if fastforward
	if (!isHeld(config->exit)) return 1;
	if (!isHeld(config->fastf)) return 2;
	else 
		return 0;
}

static void pause(int Control){
	PAD_ControlMotor(Control, PAD_MOTOR_STOP);
}

static void resume(int Control){ }

static void rumble(int Control, int rumble){
	PAD_ControlMotor(Control, (rumble && rumbleEnabled) ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
}

static void configure(int Control, controller_config_t* config){
	// Don't know how this should be integrated
}

static void assign(int p, int v){
	// Nothing to do here
}

static void refreshAvailable(void);

controller_t controller_GC =
	{ 'G',
	  _GetKeys,
	  configure,
	  assign,
	  pause,
	  resume,
	  rumble,
	  refreshAvailable,
	  {0, 0, 0, 0},
	  sizeof(buttons)/sizeof(buttons[0]),
	  buttons,
	  sizeof(analog_sources)/sizeof(analog_sources[0]),
	  analog_sources,
	  sizeof(menu_combos)/sizeof(menu_combos[0]),
	  menu_combos,
	  { .SQU        = &buttons[10], // B
	    .CRO        = &buttons[9],  // A
	    .CIR        = &buttons[11], // X
	    .TRI        = &buttons[12], // Y
	    .R1         = &buttons[6],  // Right Trigger - Z
	    .L1         = &buttons[5],  // Left Trigger - Z
	    .R2         = &buttons[8],  // Right Trigger + Z
	    .L2         = &buttons[7],  // Left Trigger + Z
	    .R3         = &buttons[0],  // None
	    .L3         = &buttons[0],  // None
	    .DL         = &buttons[2],  // D-Pad Left
	    .DR         = &buttons[3],  // D-Pad Right
	    .DU         = &buttons[1],  // D-Pad Up
	    .DD         = &buttons[4],  // D-Pad Down
	    .START      = &buttons[13], // Start - Z
	    .SELECT     = &buttons[14], // Start + Z
	    .analogL    = &analog_sources[0], // Analog Stick
	    .analogR    = &analog_sources[1], // C stick
	    .exit       = &menu_combos[1], // Start+X
	    .invertedYL = 0,
	    .invertedYR = 0,
		.sensitivity = 1.0,
		.fastf       = &menu_combos[2], // Start+Y
	  }
	 };


static void refreshAvailable(void){

	if(padNeedScan){ gc_connected = PAD_ScanPads(); padNeedScan = 0; }

	int i;
	for(i=0; i<4; ++i)
		controller_GC.available[i] = (gc_connected & (1<<i));
}
