/**
 * WiiStation - controller-HidGC.c
 * Copyright (C) 2024 WiiStation
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

/* The USB HID gamepad. Two kinds of entry in every list:
 *  - the GameCube-style pad HidController/PADReadGC.c makes from the controller ini, or from
 *    the report descriptor for a pad with no ini (A, B, X, Y, L1 ... R3, D-pad, sticks);
 *  - every input of the pad itself, by its HID name (Btn 1..32, Hat Up.., Axis Z+..), from
 *    hidState (hid_layout.h). Their mask is HID_RAW | the input number.
 * Entries are only ever appended: a button file stores each choice as its index. */

#include <string.h>
#include <ogc/pad.h>
#include "controller.h"
#include "../wiiSXconfig.h"
#include "../../HidController/KernelHID.h"
#include "../../HidController/global.h"
#include "../DEBUG.h"

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

#define HID_RAW 0x80000000u   /* a HID input, number in the low bits */

/* The 60 HID inputs, as entries base .. base+59 of a list */
#define RAW(base, i) { (base) + (i), HID_RAW | (i), (char *)hid_input_names[i] },
#define RAW4(b, i)  RAW(b, i) RAW(b, i + 1) RAW(b, i + 2) RAW(b, i + 3)
#define RAW20(b, i) RAW4(b, i) RAW4(b, i + 4) RAW4(b, i + 8) RAW4(b, i + 12) RAW4(b, i + 16)
#define RAW_ALL(b)  RAW20(b, 0) RAW20(b, 20) RAW20(b, 40)

static button_t buttons[] = {
    {  0, ~0,                                "None" },
    {  1, PAD_BUTTON_UP,                     "D-Up" },
    {  2, PAD_BUTTON_LEFT,                   "D-Left" },
    {  3, PAD_BUTTON_RIGHT,                  "D-Right" },
    {  4, PAD_BUTTON_DOWN,                   "D-Down" },
    {  5, PAD_TRIGGER_L,                     "L2" },
    {  6, PAD_TRIGGER_R,                     "R2" },
    {  7, PAD_TRIGGER_L1,                    "L1" },
    {  8, PAD_TRIGGER_R1,                    "R1" },
    {  9, PAD_BUTTON_A,                      "A" },
    { 10, PAD_BUTTON_B,                      "B" },
    { 11, PAD_BUTTON_X,                      "X" },
    { 12, PAD_BUTTON_Y,                      "Y" },
    { 13, PAD_BUTTON_START,                  "Start" },
    { 14, PAD_BUTTON_SELECT,                 "Select" },
    { 15, C_STICK_U,                         "C-Up" },
    { 16, C_STICK_L,                         "C-Left" },
    { 17, C_STICK_R,                         "C-Right" },
    { 18, C_STICK_D,                         "C-Down" },
    { 19, ANALOG_U,                          "A-Up" },
    { 20, ANALOG_L,                          "A-Left" },
    { 21, ANALOG_R,                          "A-Right" },
    { 22, ANALOG_D,                          "A-Down" },
    { 23, PAD_BUTTON_L3,                     "L3" },
    { 24, PAD_BUTTON_R3,                     "R3" },
    RAW_ALL(25)
};

/* 0, 1: the GameCube-style sticks (the ini's or the automatic layout's); then axis pairs */
static button_t analog_sources[] = {
    { 0, ANALOG_AS_ANALOG,  "A-Stick" },
    { 1, C_STICK_AS_ANALOG, "C-Stick" },
    { 2, 0, "Axes X/Y" },
    { 3, 0, "Axes Z/Rz" },
    { 4, 0, "Axes Rx/Ry" },
    { 5, 0, "Slider/Dial" },
};
static const int AXIS_PAIRS[][2] = {
    { HID_AX_X, HID_AX_Y }, { HID_AX_Z, HID_AX_RZ }, { HID_AX_RX, HID_AX_RY }, { HID_AX_SLIDER, HID_AX_DIAL } };

static button_t menu_combos[] = {
    { 0, PAD_BUTTON_X|PAD_BUTTON_Y, "X+Y" },
    { 1, PAD_BUTTON_START|PAD_BUTTON_X, "Start+X" },
    { 2, PAD_BUTTON_START|PAD_BUTTON_Y, "Start+Y" },
    RAW_ALL(3)   /* one HID input alone: a Home button, a 13th button */
};

/* Screenshot combinations, entry 0 is Off. Start is in none of them, so the game does not pause.
 * Entry 1 is the default. No menu combination may lie inside one of these (Configure Buttons
 * refuses it). */
static button_t screenshot_combos[] = {
    { 0, 0,                                              "Off" },
    { 1, PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_SELECT, "L2+R2+Sel" },
    { 2, PAD_TRIGGER_L|PAD_TRIGGER_R,                    "L2+R2" },
    RAW_ALL(3)
};

/* ANALOG button combinations (PadSSSPSX.c analog_button), entry 0 is Off, entry 1 the default:
 * the screenshot's pair with another button. While one is held the game sees no buttons, so
 * its Start does not pause. Configure Buttons keeps it apart from the menu, fast-forward and
 * screenshot combinations. */
static button_t analog_combos[] = {
    { 0, 0, "Off" },
    { 1, PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_START, "L2+R2+Start" },
    RAW_ALL(2)
};

/* An entry is held: b is the GameCube-style pad's bits (with the stick directions) */
int hidgc_held(const button_t *bt, u32 b)
{
    if (!bt || bt->mask == ~0u) return 0;
    if (bt->mask & HID_RAW) return (int)((hidState.held >> (bt->mask & 63)) & 1);
    return (b & bt->mask) == bt->mask;
}

/* The pad has the input an entry names (every GameCube-style entry: yes). Configure Buttons
 * skips the others, so a 12-button pad does not cycle through Btn 13..32. */
int hidgc_entry_present(const button_t *bt)
{
    if (!(bt->mask & HID_RAW) || bt->mask == ~0u) return 1;
    return hid_input_present(&hidLayout, bt->mask & 63);
}

/* Two combinations where one is inside the other (both pressed by pressing the outer one).
 * A HID input alone is inside only itself. */
int hidgc_combo_inside(const button_t *a, const button_t *b)
{
    if (!a->mask || !b) return 0;
    if ((a->mask | b->mask) & HID_RAW) return a->mask == b->mask;
    return (a->mask & b->mask) == a->mask;
}

/* As controller-GC.c; full is the stick's travel, GC_MAIN_FULL or GC_CSTICK_FULL (psx_analog.h). */
static void GCtoPSX(int x, int y, int invertY, float full, u8 *px, u8 *py)
{
    float fx = (float)x / full, fy = (float)(invertY ? y : -y) / full;
    psx_square(&fx, &fy);
    *px = psx_analog(fx);
    *py = psx_analog(fy);
}

/* A HID axis pair (-128..127, + right / down) as a PlayStation stick: the one mapping, the
 * same shape as GCtoPSX (the full range, the square's corners reachable) */
static void HIDtoPSX(int x, int y, int invertY, u8 *px, u8 *py)
{
    if (invertY) y = -y;
    float fx = x / (x < 0 ? 128.0f : 127.0f), fy = y / (y < 0 ? 128.0f : 127.0f);
    psx_square(&fx, &fy);
    *px = psx_analog(fx);
    *py = psx_analog(fy);
}

static void stick(int src, const PADStatus *p, int invertY, u8 *px, u8 *py)
{
    const u8 *r = (const u8 *)HID_MEM2_PACKET_ADDR;
    if (src >= 2 && src < 2 + (int)(sizeof(AXIS_PAIRS) / sizeof(AXIS_PAIRS[0])))
        HIDtoPSX(hidState.axis[AXIS_PAIRS[src - 2][0]], hidState.axis[AXIS_PAIRS[src - 2][1]], invertY, px, py);
    else if (hidAuto)
        HIDtoPSX(hid_map_stick(&hidMap, src ? HS_RX : HS_LX, &hidState, r, hidReportLen),
                 hid_map_stick(&hidMap, src ? HS_RY : HS_LY, &hidState, r, hidReportLen), invertY, px, py);
    else if (src)
        GCtoPSX(p->substickX, p->substickY, invertY, GC_CSTICK_FULL, px, py);
    else
        GCtoPSX(p->stickX, p->stickY, invertY, GC_MAIN_FULL, px, py);
}

/* The GameCube-style sticks' directions, as the A-/C- entries' bits: past half of full travel */
static u32 stickBits(const PADStatus *p)
{
    u32 b = 0;
    int m = (int)(GC_MAIN_FULL / 2), c = (int)(GC_CSTICK_FULL / 2);
    if (p->stickX < -m) b |= ANALOG_L;
    if (p->stickX > m) b |= ANALOG_R;
    if (p->stickY > m) b |= ANALOG_U;
    if (p->stickY < -m) b |= ANALOG_D;
    if (p->substickX < -c) b |= C_STICK_L;
    if (p->substickX > c) b |= C_STICK_R;
    if (p->substickY > c) b |= C_STICK_U;
    if (p->substickY < -c) b |= C_STICK_D;
    return b;
}

static void refreshAvailable(void);

static int _GetKeys(int Control, BUTTONS * Keys, controller_config_t* config)
{
    refreshAvailable();

    BUTTONS* c = Keys;
    memset(c, 0, sizeof(BUTTONS));
    //Reset buttons & sticks
    c->btns.All = 0xFFFF;
    c->leftStickX = c->leftStickY = c->rightStickX = c->rightStickY = 128;

    if (!controller_HidGC.available[Control]) return 0;

    PADStatus *Pad = (PADStatus*)(HID_MEM2_PAD_BUFF); //PadBuff
    HidFormatData();
    //only pad 0: one USB controller
    u32 b = Pad[0].button | stickBits(&Pad[0]);
    inline int isHeld(button_tp button){
        return hidgc_held(button, b) ? 0 : 1;
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

    stick(config->analogL ? config->analogL->index : 0, &Pad[0], config->invertedYL, &c->leftStickX, &c->leftStickY);
    stick(config->analogR ? config->analogR->index : 1, &Pad[0], config->invertedYR, &c->rightStickX, &c->rightStickY);

    // Return 1 if exit, 2 if fastforward
    if (!isHeld(config->exit))
    {
        return 1;
    }
    if (!isHeld(config->fastf))
    {
        return 2;
    }
    screenshot_poll(Control, config->screenshot && config->screenshot->mask && !isHeld(config->screenshot));
    analog_poll(config->analog && config->analog->mask && !isHeld(config->analog));

    return 0;
}

static u32* MotorCommand = (u32*)(HID_MEM2_MOTOR_CMD);

static void pause(int Control){
    *MotorCommand = PAD_MOTOR_STOP;
}

static void resume(int Control){ }

static void rumble(int Control, int rumble){
    *MotorCommand = (rumble && rumbleEnabled) ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP;
}

static void configure(int Control, controller_config_t* config){
    // Don't know how this should be integrated
}

static void assign(int p, int v){
    // Nothing to do here
}

controller_t controller_HidGC =
    { 'H',
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
      sizeof(screenshot_combos)/sizeof(screenshot_combos[0]),
      screenshot_combos,
      sizeof(analog_combos)/sizeof(analog_combos[0]),
      analog_combos,
      { .SQU        = &buttons[9],  // A
        .CRO        = &buttons[10], // B
        .CIR        = &buttons[11], // X
        .TRI        = &buttons[12], // Y
        .R1         = &buttons[8],  // Right Trigger - Z
        .L1         = &buttons[7],  // Left Trigger - Z
        .R2         = &buttons[6],  // Right Trigger + Z
        .L2         = &buttons[5],  // Left Trigger + Z
        .R3         = &buttons[24], // R3 (ini KeyR3, or button 12 with no ini)
        .L3         = &buttons[23], // L3 (ini KeyL3, or button 11 with no ini)
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
        .screenshot = &screenshot_combos[1],
        .analog = &analog_combos[1],
      }
     };

static void refreshAvailable(void){
    if (hidPadNeedScan)
    {
        if (loadingControllerIni)
        {
            HIDUpdateControllerIni();
        }
        hidPadNeedScan = 0;
    }

    controller_HidGC.available[0] = hidControllerConnected;
    controller_HidGC.available[1] = 0;
    controller_HidGC.available[2] = 0;
    controller_HidGC.available[3] = 0;
}
