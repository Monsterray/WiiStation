/**
 * WiiStation - AdvancedSoundFrame.cpp
 *
 * The "Advanced" page under Settings -> Audio: a table-driven list of sound options that
 * are being tried out. Rows are meant to come and go while things are tested, so the page
 * is built from one table and nothing else has to be touched to change it.
 *
 * HOW TO ADD AN OPTION
 *   1. Give it a setting: a `char` variable and an enum of its values in
 *      Gamecube/wiiSXconfig.h; its definition, its default in loadSettings() and one
 *      OPTIONS[] line in Gamecube/GamecubeMain.cpp (that line is what saves and loads it);
 *      and a row in SETTINGS.md.
 *   2. Add one entry to ADV_OPTIONS[] below: the label, the variable, its lowest and
 *      highest value, the value names in that order, and an apply function if the emulator
 *      has to be told about a change (NULL when the code reads the variable directly).
 * Layout, focus order, click handlers and the label refresh are all derived from the table.
 * Remove a row by deleting its entry. Up to ADV_MAX_ROWS rows fit on the screen.
 *
 * Every row is one button that cycles through the values on A, the same control as
 * "Interpolation" on the Audio tab. Strings go through gettext like the rest of the menu,
 * so a translation only needs the new words added to the .lang files.
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
#include "AdvancedSoundFrame.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../wiiSXconfig.h"

extern MenuContext *pMenuContext;

void Func_ReturnFromAdvancedSoundFrame();

/*  The table  */

struct AdvOption
{
	const char*			label;		// text on the left
	char*				var;		// the setting it edits (a char, like everything in OPTIONS[])
	char				min, max;	// A cycles min..max and wraps
	const char* const*	names;		// names[value - min], shown on the button
	void				(*apply)(void);	// tell the emulator about a change; NULL = it reads var itself
};

static const char* const RESAMPLER_NAMES[] = { "Hold", "Linear", "Cubic" };

static const AdvOption ADV_OPTIONS[] =
{ //	label					variable			min						max						names				apply
	{	"Output Resampler",		&soundResampler,	SOUND_RESAMPLE_HOLD,	SOUND_RESAMPLE_CUBIC,	RESAMPLER_NAMES,	NULL },
};

#define ADV_NUM_ROWS	((int)(sizeof(ADV_OPTIONS) / sizeof(ADV_OPTIONS[0])))
#define ADV_MAX_ROWS	5

/*  Layout: title, then a row every ROW_DY from ROW_Y0, then Back at the bottom. The
 *  numbers match the Audio tab (56-high buttons, labels 28 below the button's top). */
#define TITLE_X		320.0
#define TITLE_Y		52.0
#define ROW_Y0		92.0
#define ROW_DY		62.0
#define LABEL_X		175.0
#define BUTTON_X	340.0
#define BUTTON_W	250.0
#define BUTTON_H	56.0
#define BACK_X		270.0
#define BACK_Y		418.0
#define BACK_W		100.0
#define BACK_H		40.0

static char TITLE_STRING[] = "Advanced Sound";
static char BACK_STRING[]  = "Back";

struct AdvRow
{
	menu::Button*	button;
	menu::TextBox*	textBox;
	char*			labelString;	// the TextBox keeps a pointer to this
	char*			valueString;	// the Button keeps a pointer to this
};

static AdvRow		ROWS[ADV_MAX_ROWS];
static menu::Button*	backButton;
static menu::TextBox*	titleBox;
static char*		titleString = TITLE_STRING;
static char*		backString  = BACK_STRING;

/*  One click handler per possible row; the Button API takes a plain function pointer with
 *  no argument, so the row index has to be baked in. Add a line here and to ROW_FUNCS if
 *  ADV_MAX_ROWS grows. */
static void advCycle(int row);
static void Func_AdvRow0() { advCycle(0); }
static void Func_AdvRow1() { advCycle(1); }
static void Func_AdvRow2() { advCycle(2); }
static void Func_AdvRow3() { advCycle(3); }
static void Func_AdvRow4() { advCycle(4); }
static void (*const ROW_FUNCS[ADV_MAX_ROWS])() =
	{ Func_AdvRow0, Func_AdvRow1, Func_AdvRow2, Func_AdvRow3, Func_AdvRow4 };

typedef char adv_rows_fit_on_screen[ADV_NUM_ROWS <= ADV_MAX_ROWS ? 1 : -1];

static void refreshRow(int row)
{
	const AdvOption& o = ADV_OPTIONS[row];
	int v = *o.var;
	if (v < o.min || v > o.max) v = o.min;	// a value the file parser let through but the table does not know
	ROWS[row].valueString = (char*)o.names[v - o.min];
}

static void advCycle(int row)
{
	if (row >= ADV_NUM_ROWS) return;
	const AdvOption& o = ADV_OPTIONS[row];
	int v = *o.var + 1;
	if (v > o.max || v < o.min) v = o.min;
	*o.var = (char)v;
	refreshRow(row);
	if (o.apply) o.apply();
}

AdvancedSoundFrame::AdvancedSoundFrame()
{
	titleBox = new menu::TextBox(&titleString, TITLE_X, TITLE_Y, 1.0, true);
	add(titleBox);

	for (int i = 0; i < ADV_NUM_ROWS; i++)
	{
		float y = ROW_Y0 + i * ROW_DY;
		ROWS[i].labelString = (char*)ADV_OPTIONS[i].label;
		refreshRow(i);
		ROWS[i].textBox = new menu::TextBox(&ROWS[i].labelString, LABEL_X, y + 28.0, 1.0, true);
		ROWS[i].button  = new menu::Button(BTN_A_NRM, &ROWS[i].valueString, BUTTON_X, y, BUTTON_W, BUTTON_H);
		ROWS[i].button->setActive(true);
		ROWS[i].button->setClicked(ROW_FUNCS[i]);
		ROWS[i].button->setReturn(Func_ReturnFromAdvancedSoundFrame);
		add(ROWS[i].textBox);
		add(ROWS[i].button);
		menu::Cursor::getInstance().addComponent(this, ROWS[i].button, BUTTON_X, BUTTON_X + BUTTON_W, y, y + BUTTON_H);
	}

	backButton = new menu::Button(BTN_A_NRM, &backString, BACK_X, BACK_Y, BACK_W, BACK_H);
	backButton->setActive(true);
	backButton->setClicked(Func_ReturnFromAdvancedSoundFrame);
	backButton->setReturn(Func_ReturnFromAdvancedSoundFrame);
	add(backButton);
	menu::Cursor::getInstance().addComponent(this, backButton, BACK_X, BACK_X + BACK_W, BACK_Y, BACK_Y + BACK_H);

	// Focus runs down the rows to Back and wraps.
	for (int i = 0; i < ADV_NUM_ROWS; i++)
	{
		menu::Button* up   = (i == 0) ? backButton : ROWS[i - 1].button;
		menu::Button* down = (i == ADV_NUM_ROWS - 1) ? backButton : ROWS[i + 1].button;
		ROWS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, up);
		ROWS[i].button->setNextFocus(menu::Focus::DIRECTION_DOWN, down);
	}
	backButton->setNextFocus(menu::Focus::DIRECTION_UP, ADV_NUM_ROWS ? ROWS[ADV_NUM_ROWS - 1].button : NULL);
	backButton->setNextFocus(menu::Focus::DIRECTION_DOWN, ADV_NUM_ROWS ? ROWS[0].button : NULL);

	setDefaultFocus(ADV_NUM_ROWS ? ROWS[0].button : backButton);
	setBackFunc(Func_ReturnFromAdvancedSoundFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_REINIT);
}

AdvancedSoundFrame::~AdvancedSoundFrame()
{
	delete titleBox;
	for (int i = 0; i < ADV_NUM_ROWS; i++)
	{
		delete ROWS[i].textBox;
		menu::Cursor::getInstance().removeComponent(this, ROWS[i].button);
		delete ROWS[i].button;
	}
	menu::Cursor::getInstance().removeComponent(this, backButton);
	delete backButton;
}

void AdvancedSoundFrame::activateSubmenu(int submenu)
{
	// Settings can change behind this page's back (a settings file, a per-game file), so
	// the labels are re-read from the variables every time the page is shown.
	for (int i = 0; i < ADV_NUM_ROWS; i++)
		refreshRow(i);
	setDefaultFocus(ADV_NUM_ROWS ? ROWS[0].button : backButton);
}

void Func_ReturnFromAdvancedSoundFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, SettingsFrame::SUBMENU_AUDIO);
}
