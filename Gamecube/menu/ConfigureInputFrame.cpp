/**
 * WiiSX - ConfigureInputFrame.cpp
 * Copyright (C) 2009, 2010 sepp256
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

#include "MenuContext.h"
#include "SettingsFrame.h"
#include "ConfigureInputFrame.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
//#include "../libgui/MessageBox.h"
//#include "../main/timers.h"
//#include "../main/wii64config.h"

extern "C" {
#include "../gc_input/controller.h"
}

void Func_AutoSelectInput();
void Func_ManualSelectInput();

void Func_TogglePad0Type();
void Func_TogglePad1Type();
void Func_TogglePad0AType();
void Func_TogglePad0BType();
void Func_TogglePad0CType();
void Func_TogglePad0DType();
void Func_TogglePad1AType();
void Func_TogglePad1BType();
void Func_TogglePad1CType();
void Func_TogglePad1DType();

void Func_TogglePad0Assign();
void Func_TogglePad1Assign();
void Func_TogglePad0AAssign();
void Func_TogglePad0BAssign();
void Func_TogglePad0CAssign();
void Func_TogglePad0DAssign();
void Func_TogglePad1AAssign();
void Func_TogglePad1BAssign();
void Func_TogglePad1CAssign();
void Func_TogglePad1DAssign();

void Func_ReturnFromConfigureInputFrame();


/* 0..21: the original table. 22..69: Co-Op rows, 24 per port (8 players x type,
 * Customize, number), filled in by the constructor (COOP_BTN). Appended, so no index moves. */
#define NUM_FRAME_BUTTONS 70
#define COOP_BTN(port, k, col) (22 + (port) * 24 + (k) * 3 + (col))
#define FRAME_BUTTONS configureInputFrameButtons
#define FRAME_STRINGS configureInputFrameStrings
#define NUM_FRAME_TEXTBOXES 21   /* 5..20: the Co-Op rows' P1..P8 labels */
#define COOP_LABEL(port, k) (5 + (port) * 8 + (k))
#define FRAME_TEXTBOXES configureInputFrameTextBoxes

static char FRAME_STRINGS[40][15] =
	{ "Pad Assignment",
	  "PSX Port 1",
	  "PSX Port 2",
	  "Multitap 1",
	  "Multitap 2",

	  "Automatic",
	  "Manual",
	  "None",
	  "Gamecube Pad",
	  "Wii Pad",
	  "Multitap",
	  "Auto Assign",
	  "",
	  "1",
	  "2",
	  "3",
	  "4",
	  "HID Pad",
	  "Co-Op",					// [18] port type
	  "Customize",				// [19] Co-Op row: the player's layout
	  "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8",	// [20..27] Co-Op row labels
	  "None", "GC", "Wii", "HID",	// [28..31] Co-Op row: controller type, short
	  "1P", "2P", "3P", "4P", "5P", "6P", "7P", "8P"};	// [32..39] a Co-Op port's player count

struct ButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	float			x;
	float			y;
	float			width;
	float			height;
	int				focusUp;
	int				focusDown;
	int				focusLeft;
	int				focusRight;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =
{ //	button	buttonStyle buttonString		x		y		width	height	Up	Dwn	Lft	Rt	clickFunc				returnFunc
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[5],	240.0,	 30.0,	135.0,	56.0,	 17, 2,	 1,	 1,	Func_AutoSelectInput,	Func_ReturnFromConfigureInputFrame }, // Automatic Pad Assignment
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[6],	395.0,	 30.0,	120.0,	56.0,	 21, 3,	 0,	 0,	Func_ManualSelectInput,	Func_ReturnFromConfigureInputFrame }, // Manual Pad Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	30.0,	135.0,	200.0,	56.0,	 0,	 4,	 13, 12,Func_TogglePad0Type,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0 Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	330.0,	135.0,	200.0,	56.0,	 1,	 8,	 12, 13,Func_TogglePad1Type,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1 Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	30.0,	250.0,	200.0,	56.0,	 3,	 5,	 18, 14,Func_TogglePad0AType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0A Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	30.0,	306.0,	200.0,	56.0,	 4,	 6,	 19, 15,Func_TogglePad0BType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0B Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	30.0,	362.0,	200.0,	56.0,	 5,	 7,	 20, 16,Func_TogglePad0CType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0C Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	30.0,	418.0,	200.0,	56.0,	 6,	 0,	 21, 17,Func_TogglePad0DType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0D Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	330.0,	250.0,	200.0,	56.0,	 3,	 9,	 14, 18,Func_TogglePad1AType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1A Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	330.0,	306.0,	200.0,	56.0,	 8,	 10, 15, 19,Func_TogglePad1BType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1B Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	330.0,	362.0,	200.0,	56.0,	 9,	 11, 16, 20,Func_TogglePad1CType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1C Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[11],	330.0,	418.0,	200.0,	56.0,	 10, 1,	 17, 21,Func_TogglePad1DType,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1D Type
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	250.0,	135.0,	 55.0,	56.0,	 0,	 14, 2,	 3,	Func_TogglePad0Assign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0 Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	550.0,	135.0,	 55.0,	56.0,	 1,	 18, 3,	 2,	Func_TogglePad1Assign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1 Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	250.0,	250.0,	 55.0,	56.0,	 12, 15, 4,	 8,	Func_TogglePad0AAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0A Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	250.0,	306.0,	 55.0,	56.0,	 14, 16, 5,	 9,	Func_TogglePad0BAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0B Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	250.0,	362.0,	 55.0,	56.0,	 15, 17, 6,	 10,Func_TogglePad0CAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0C Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	250.0,	418.0,	 55.0,	56.0,	 16, 0,	 7,	 11,Func_TogglePad0DAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 0D Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	550.0,	250.0,	 55.0,	56.0,	 13, 19, 8,	 4,	Func_TogglePad1AAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1A Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	550.0,	306.0,	 55.0,	56.0,	 18, 20, 9,	 5,	Func_TogglePad1BAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1B Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	550.0,	362.0,	 55.0,	56.0,	 19, 21, 10, 6,	Func_TogglePad1CAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1C Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[12],	550.0,	418.0,	 55.0,	56.0,	 20, 1,	 11, 7,	Func_TogglePad1DAssign,	Func_ReturnFromConfigureInputFrame }, // Toggle Pad 1D Assignment
};

struct TextBoxInfo
{
	menu::TextBox	*textBox;
	char*			textBoxString;
	float			x;
	float			y;
	float			scale;
	bool			centered;
} FRAME_TEXTBOXES[NUM_FRAME_TEXTBOXES] =
{ //	textBox	textBoxString		x		y		scale	centered
	{	NULL,	FRAME_STRINGS[0],	125.0,	68.0,	 1.0,	true }, // Pad Assignment
	{	NULL,	FRAME_STRINGS[1],	125.0,	115.0,	 1.0,	true }, // Pad 1
	{	NULL,	FRAME_STRINGS[2],	425.0,	115.0,	 1.0,	true }, // Pad 2
	{	NULL,	FRAME_STRINGS[3],	125.0,	228.0,	 1.0,	true }, // Multitap 1
	{	NULL,	FRAME_STRINGS[4],	425.0,	228.0,	 1.0,	true }, // Multitap 2
};

/* ---- Co-Op rows: each port's players, P1..P8 -------------------------------------
 * A row: the controller type (None, GC, Wii, HID), Customize (the player's layout, in
 * CustomizeCoopFrame) and which controller of that type (1..4). ButtonFunc takes no
 * arguments, so each button gets its own small function. */
extern MenuContext *pMenuContext;
static void coopRefresh(int port)
{
	coop_assign_port(port);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}
static void coopToggleType(int port, int k)
{
#ifdef HW_RVL
	coopType[port][k] = (coopType[port][k] + 1) % 4;   /* None, GC, Wii, HID */
#else
	coopType[port][k] = (coopType[port][k] + 1) % 2;
#endif
	coopRefresh(port);
}
static void coopToggleAssign(int port, int k)
{
	coopAssign[port][k] = (coopAssign[port][k] + 1) % 4;
	coopRefresh(port);
}
static void coopCustomize(int port, int k)
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_CUSTOMIZECOOP, port * COOP_MAX + k);
}
#define COOP_ROW_FUNCS(p, k) 	static void Func_Coop##p##k##Type(void)   { coopToggleType(p, k); } 	static void Func_Coop##p##k##Custom(void) { coopCustomize(p, k); } 	static void Func_Coop##p##k##Assign(void) { coopToggleAssign(p, k); }
#define COOP_PORT_FUNCS(p) COOP_ROW_FUNCS(p,0) COOP_ROW_FUNCS(p,1) COOP_ROW_FUNCS(p,2) COOP_ROW_FUNCS(p,3) 	COOP_ROW_FUNCS(p,4) COOP_ROW_FUNCS(p,5) COOP_ROW_FUNCS(p,6) COOP_ROW_FUNCS(p,7)
COOP_PORT_FUNCS(0)
COOP_PORT_FUNCS(1)
#define COOP_ROW_TABLE(p, k) { Func_Coop##p##k##Type, Func_Coop##p##k##Custom, Func_Coop##p##k##Assign }
static ButtonFunc coopFuncs[2][COOP_MAX][3] = {
	{ COOP_ROW_TABLE(0,0), COOP_ROW_TABLE(0,1), COOP_ROW_TABLE(0,2), COOP_ROW_TABLE(0,3),
	  COOP_ROW_TABLE(0,4), COOP_ROW_TABLE(0,5), COOP_ROW_TABLE(0,6), COOP_ROW_TABLE(0,7) },
	{ COOP_ROW_TABLE(1,0), COOP_ROW_TABLE(1,1), COOP_ROW_TABLE(1,2), COOP_ROW_TABLE(1,3),
	  COOP_ROW_TABLE(1,4), COOP_ROW_TABLE(1,5), COOP_ROW_TABLE(1,6), COOP_ROW_TABLE(1,7) } };

/* Where a Co-Op row sits: under its port, from y=196, 34 pixels apart (8 rows end at 464) */
#define COOP_ROW_Y(k) (196.0f + 34.0f * (k))
static void coopFillTables(void)
{
	static const float colX[3] = { 26.0f, 88.0f, 212.0f }, colW[3] = { 58.0f, 120.0f, 42.0f };
	for (int p = 0; p < 2; p++)
		for (int k = 0; k < COOP_MAX; k++) {
			for (int c = 0; c < 3; c++) {
				ButtonInfo &b = FRAME_BUTTONS[COOP_BTN(p, k, c)];
				b.buttonStyle = BTN_A_NRM;
				b.buttonString = FRAME_STRINGS[c == 1 ? 19 : 12];
				b.x = 30.0f + 300.0f * p + colX[c];
				b.y = COOP_ROW_Y(k);
				b.width = colW[c];
				b.height = 30.0f;
				b.focusUp = b.focusDown = b.focusLeft = b.focusRight = -1;
				b.clickedFunc = coopFuncs[p][k][c];
				b.returnFunc = Func_ReturnFromConfigureInputFrame;
			}
			TextBoxInfo &t = FRAME_TEXTBOXES[COOP_LABEL(p, k)];
			t.textBoxString = FRAME_STRINGS[20 + k];
			t.x = 30.0f + 300.0f * p + 13.0f;
			t.y = COOP_ROW_Y(k) + 15.0f;
			t.scale = 0.8f;
			t.centered = true;
		}
}

ConfigureInputFrame::ConfigureInputFrame()
{
	coopFillTables();
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		FRAME_BUTTONS[i].button = new menu::Button(FRAME_BUTTONS[i].buttonStyle, &FRAME_BUTTONS[i].buttonString,
										FRAME_BUTTONS[i].x, FRAME_BUTTONS[i].y,
										FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].height);

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		if (FRAME_BUTTONS[i].focusUp != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, FRAME_BUTTONS[FRAME_BUTTONS[i].focusUp].button);
		if (FRAME_BUTTONS[i].focusDown != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_DOWN, FRAME_BUTTONS[FRAME_BUTTONS[i].focusDown].button);
		if (FRAME_BUTTONS[i].focusLeft != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_LEFT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusLeft].button);
		if (FRAME_BUTTONS[i].focusRight != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_RIGHT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusRight].button);
		FRAME_BUTTONS[i].button->setActive(true);
		if (FRAME_BUTTONS[i].clickedFunc) FRAME_BUTTONS[i].button->setClicked(FRAME_BUTTONS[i].clickedFunc);
		if (FRAME_BUTTONS[i].returnFunc) FRAME_BUTTONS[i].button->setReturn(FRAME_BUTTONS[i].returnFunc);
		add(FRAME_BUTTONS[i].button);
		menu::Cursor::getInstance().addComponent(this, FRAME_BUTTONS[i].button, FRAME_BUTTONS[i].x,
												FRAME_BUTTONS[i].x+FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].y,
												FRAME_BUTTONS[i].y+FRAME_BUTTONS[i].height);
	}

	/* A style-A button makes itself 56 high whatever it is given: the Co-Op rows are lower */
	for (int i = COOP_BTN(0, 0, 0); i < NUM_FRAME_BUTTONS; i++)
		FRAME_BUTTONS[i].button->setBounds(FRAME_BUTTONS[i].x, FRAME_BUTTONS[i].y,
		                                   FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].height);

	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
	{
		FRAME_TEXTBOXES[i].textBox = new menu::TextBox(&FRAME_TEXTBOXES[i].textBoxString,
										FRAME_TEXTBOXES[i].x, FRAME_TEXTBOXES[i].y,
										FRAME_TEXTBOXES[i].scale, FRAME_TEXTBOXES[i].centered);
		add(FRAME_TEXTBOXES[i].textBox);
	}

	setDefaultFocus(FRAME_BUTTONS[0].button);
	setBackFunc(Func_ReturnFromConfigureInputFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_REINIT);
}

ConfigureInputFrame::~ConfigureInputFrame()
{
	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
		delete FRAME_TEXTBOXES[i].textBox;
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, FRAME_BUTTONS[i].button);
		delete FRAME_BUTTONS[i].button;
	}

}

static void showButton(int i, bool on, bool active)
{
	FRAME_BUTTONS[i].button->setVisible(on);
	FRAME_BUTTONS[i].button->setActive(on && active);
}

static void setLinks(int i, int up, int down, int left, int right)
{
	menu::Button *b = FRAME_BUTTONS[i].button;
	b->setNextFocus(menu::Focus::DIRECTION_UP,    up    < 0 ? NULL : FRAME_BUTTONS[up].button);
	b->setNextFocus(menu::Focus::DIRECTION_DOWN,  down  < 0 ? NULL : FRAME_BUTTONS[down].button);
	b->setNextFocus(menu::Focus::DIRECTION_LEFT,  left  < 0 ? NULL : FRAME_BUTTONS[left].button);
	b->setNextFocus(menu::Focus::DIRECTION_RIGHT, right < 0 ? NULL : FRAME_BUTTONS[right].button);
}

/* What each port shows: its type and number; under it four multitap slots (Multitap) or
 * its Co-Op players (Co-Op), nothing otherwise. Focus follows what is shown: the table's
 * own for the multitap rows, set here for the ports, the top row and the Co-Op rows. */
void ConfigureInputFrame::activateSubmenu(int submenu)
{
	const bool manual = padAutoAssign != PADAUTOASSIGN_AUTOMATIC;
	int bottom[2][2];   /* per port: the last button of its left and of its right column */

	for (int i = 0; i < 22; i++)
		setLinks(i, FRAME_BUTTONS[i].focusUp, FRAME_BUTTONS[i].focusDown,
		      FRAME_BUTTONS[i].focusLeft, FRAME_BUTTONS[i].focusRight);
	FRAME_BUTTONS[0].button->setSelected(!manual);
	FRAME_BUTTONS[1].button->setSelected(manual);
	for (int p = 0; p < 2; p++)
	{
		const bool mtap = padType[p] == PADTYPE_MULTITAP, coop = padType[p] == PADTYPE_COOP;

		/* the port's own two buttons */
		FRAME_BUTTONS[2 + p].buttonString = !manual ? FRAME_STRINGS[11] :
			padType[p] == PADTYPE_HID ? FRAME_STRINGS[17] : mtap ? FRAME_STRINGS[10] :
			coop ? FRAME_STRINGS[18] : FRAME_STRINGS[padType[p] + 7];
		FRAME_BUTTONS[12 + p].buttonString = !manual || mtap ? FRAME_STRINGS[12] :
			coop ? FRAME_STRINGS[31 + coopPlayers[p]] : FRAME_STRINGS[padAssign[p] + 13];
		showButton(2 + p, true, manual);
		showButton(12 + p, true, manual && !mtap);
		bottom[p][0] = 2 + p;
		bottom[p][1] = mtap ? 2 + p : 12 + p;

		/* multitap slots A..D: buttons 4..7/8..11 (type) and 14..17/18..21 (number) */
		FRAME_TEXTBOXES[3 + p].textBox->setVisible(mtap);
		for (int sl = 0; sl < 4; sl++)
		{
			int i = 2 + p * 4 + sl;
			FRAME_BUTTONS[i + 2].buttonString = padType[i] == PADTYPE_HID ? FRAME_STRINGS[17] :
				FRAME_STRINGS[padType[i] + 7];
			FRAME_BUTTONS[i + 12].buttonString = FRAME_STRINGS[padAssign[i] + 13];
			showButton(i + 2, mtap, manual);
			showButton(i + 12, mtap, manual);
		}
		if (mtap)
		{
			bottom[p][0] = 2 + p * 4 + 3 + 2;
			bottom[p][1] = 2 + p * 4 + 3 + 12;
		}

		/* Co-Op players */
		for (int k = 0; k < COOP_MAX; k++)
		{
			const bool on = coop && k < coopPlayers[p];
			int t = COOP_BTN(p, k, 0), c = COOP_BTN(p, k, 1), n = COOP_BTN(p, k, 2);
			bool otherOn = padType[1 - p] == PADTYPE_COOP && k < coopPlayers[1 - p];
			FRAME_BUTTONS[t].buttonString = FRAME_STRINGS[28 + coopType[p][k]];
			FRAME_BUTTONS[n].buttonString = FRAME_STRINGS[13 + coopAssign[p][k]];
			showButton(t, on, manual);
			showButton(c, on, manual);
			showButton(n, on, manual);
			FRAME_TEXTBOXES[COOP_LABEL(p, k)].textBox->setVisible(on);
			if (!on)
				continue;
			bool last = k + 1 >= coopPlayers[p];
			setLinks(t, k ? COOP_BTN(p, k - 1, 0) : 2 + p, last ? p : COOP_BTN(p, k + 1, 0),
			      otherOn ? COOP_BTN(1 - p, k, 2) : n, c);
			setLinks(c, k ? COOP_BTN(p, k - 1, 1) : 2 + p, last ? p : COOP_BTN(p, k + 1, 1), t, n);
			setLinks(n, k ? COOP_BTN(p, k - 1, 2) : 12 + p, last ? p : COOP_BTN(p, k + 1, 2), c,
			      otherOn ? COOP_BTN(1 - p, k, 0) : t);
			bottom[p][0] = t;
			bottom[p][1] = n;
		}

		/* down from the port's buttons: its first row, or back to the top */
		setLinks(2 + p, p, mtap ? 4 + p * 4 : coop ? COOP_BTN(p, 0, 0) : p, 12 + (1 - p), 12 + p);
		setLinks(12 + p, p, mtap ? 14 + p * 4 : coop ? COOP_BTN(p, 0, 2) : p, 2 + p, 2 + (1 - p));
	}
	setLinks(0, bottom[0][0], 2, 1, 1);
	setLinks(1, bottom[1][1], 3, 0, 0);
	if (!manual)
	{
		setLinks(0, -1, -1, 1, 1);
		setLinks(1, -1, -1, 0, 0);
	}
}

extern MenuContext *pMenuContext;

void Func_AutoSelectInput()
{
	/* Automatic greys out the port types, so a port Manual had made a Multitap stayed one
	 * where nobody could see or change it (only Configure Buttons gave it away). It goes
	 * back to a plain port, and the controllers are assigned at once. */
	for (int port = 0; port < 2; port++)
		if (padType[port] == PADTYPE_MULTITAP || padType[port] == PADTYPE_COOP)
			set_port_type(port, PADTYPE_NONE);
	padAutoAssign = PADAUTOASSIGN_AUTOMATIC;
	auto_assign_controllers();
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_ManualSelectInput()
{
	padAutoAssign = PADAUTOASSIGN_MANUAL;
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_AssignPad(int i)
{
	controller_t* type = NULL;
	switch (padType[i])
	{
	case PADTYPE_GAMECUBE:
		type = &controller_GC;
		break;
#ifdef HW_RVL
    case PADTYPE_HID:
    	type = &controller_HidGC;
		break;

	case PADTYPE_WII:
		if (controller_WiiUPro.available[(int)padAssign[i]])
			type = &controller_WiiUPro;
		else if (controller_WiiUGamepad.available[(int)padAssign[i]])
			type = &controller_WiiUGamepad;
		//Note: Wii expansion detection is done in InputStatusBar.cpp during MainFrame draw
		else if (controller_Classic.available[(int)padAssign[i]])
			type = &controller_Classic;
		else
			type = &controller_WiimoteNunchuk;
		break;
#endif
	case PADTYPE_NONE:
		unassign_controller(i);
		return;
	}
		assign_controller(i, type, (int) padAssign[i]);
}

void Func_TogglePad0Type()
{
	int i = PADASSIGN_INPUT0;
#ifdef HW_RVL
	set_port_type(i, (padType[i]+1) % 6);   /* None GC Wii HID Multitap Co-Op */
#else
	set_port_type(i, (padType[i]+1) & 1);
#endif
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}


void Func_TogglePad1Type()
{
	int i = PADASSIGN_INPUT1;
#ifdef HW_RVL
	set_port_type(i, (padType[i]+1) % 6);   /* None GC Wii HID Multitap Co-Op */
#else
	set_port_type(i, (padType[i]+1) & 1);
#endif
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0Assign()
{
	int i = PADASSIGN_INPUT0;
	if (padType[i] == PADTYPE_COOP)   /* a Co-Op port's number is its player count, 1..8 */
	{
		coopPlayers[i] = coopPlayers[i] % COOP_MAX + 1;
		coopRefresh(i);
		return;
	}
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i] && padType[i] != PADTYPE_MULTITAP) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1Assign()
{
	int i = PADASSIGN_INPUT1;
	if (padType[i] == PADTYPE_COOP)   /* a Co-Op port's number is its player count, 1..8 */
	{
		coopPlayers[i] = coopPlayers[i] % COOP_MAX + 1;
		coopRefresh(i);
		return;
	}
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i] && padType[i] != PADTYPE_MULTITAP) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_ReturnFromConfigureInputFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_INPUT);
}


//////////////////////////////////
//		Multitap functions		//
//////////////////////////////////

void Func_TogglePad0AType()
{
	int i = PADASSIGN_INPUT0A;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0BType()
{
	int i = PADASSIGN_INPUT0B;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0CType()
{
	int i = PADASSIGN_INPUT0C;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0DType()
{
	int i = PADASSIGN_INPUT0D;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

///////////////////////////////////////////// Second Pad Type

void Func_TogglePad1AType()
{
	int i = PADASSIGN_INPUT1A;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1BType()
{
	int i = PADASSIGN_INPUT1B;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1CType()
{
	int i = PADASSIGN_INPUT1C;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1DType()
{
	int i = PADASSIGN_INPUT1D;
#ifdef HW_RVL
	padType[i] = (padType[i]+1) %4;
#else
	padType[i] = (padType[i]+1) %2;
#endif

	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}


///////////////////////////////////////////// Assign

void Func_TogglePad0AAssign()
{
	int i = PADASSIGN_INPUT0A;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0BAssign()
{
	int i = PADASSIGN_INPUT0B;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0CAssign()
{
	int i = PADASSIGN_INPUT0C;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad0DAssign()
{
	int i = PADASSIGN_INPUT0D;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

///////////////////////////////////////////// Second Pad Assign

void Func_TogglePad1AAssign()
{
	int i = PADASSIGN_INPUT1A;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1BAssign()
{
	int i = PADASSIGN_INPUT1B;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1CAssign()
{
	int i = PADASSIGN_INPUT1C;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_TogglePad1DAssign()
{
	int i = PADASSIGN_INPUT1D;
	padAssign[i] = (padAssign[i]+1) %4;

	if (padType[i]) Func_AssignPad(i);

	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

