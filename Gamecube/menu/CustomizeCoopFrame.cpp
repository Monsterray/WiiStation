/**
 * WiiStation - CustomizeCoopFrame.cpp
 *
 * Which PlayStation buttons one Co-Op player works (Gamecube/coop.c). It looks like
 * Configure Buttons -- the same controller picture and the same places for each button --
 * without that page's mapping controls (menu combination, fast forward, sensitivity, stick
 * inversion). Each button lights when this player has it; the presets above choose a set,
 * and each button turns on or off by itself (the Custom layout, kept in CoopPButtonsK).
 *
 * The page is on the menu engine (Docs/MENU_PLAN.md, step 5): its 24 buttons are
 * free buttons at the rectangles this file gives, and its focus is the engine's geometry rule
 * (the nearest button in each direction). The controller picture and its lines are drawn here.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <stdio.h>
#include <string.h>
#include "MenuContext.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "ConfigureInputFrame.h"
#include "CustomizeCoopFrame.h"
#include "../libgui/GuiTypes.h"
#include "../libgui/GuiResources.h"
#include "../libgui/Button.h"
#include "../libgui/Image.h"
#include "../libgui/IPLFont.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "MenuGlue.h"
#include "MenuPageView.h"
extern "C" {
#include "../gc_input/controller.h"
#include "../wiiSXconfig.h"
}

extern MenuContext *pMenuContext;

namespace eng = menu::engine;

static int coopPort, coopPlayer;   /* whose layout this page shows */

static void Func_ReturnFromCustomizeCoopFrame()
{
	/* back to Configure Input, which has no logo either */
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREINPUT, ConfigureInputFrame::SUBMENU_REINIT);
}

static void setLayout(int layout)
{
	coopLayout[coopPort][coopPlayer] = (char)layout;
	pMenuContext->getFrame(MenuContext::FRAME_CUSTOMIZECOOP)->activateSubmenu(coopPort * COOP_MAX + coopPlayer);
}

/* The PlayStation buttons, where Configure Buttons puts them (ConfigureButtonsFrame.cpp) */
enum { IND_L1, IND_L2, IND_SELECT, IND_START, IND_R1, IND_R2, IND_DUP, IND_DLEFT, IND_DRIGHT,
       IND_DDOWN, IND_TRI, IND_SQU, IND_CIR, IND_CRO, IND_LSTICK, IND_RSTICK, IND_L3, IND_R3,
       NUM_INDICATORS };
static const int NUM_PRESETS = COOP_LAYOUT_CUSTOM;   /* Custom is no button: a toggled set that is no preset */
static const int NUM_FRAME_BUTTONS = NUM_PRESETS + NUM_INDICATORS;

static char indicatorNames[NUM_INDICATORS][10] = {
	"L1", "L2", "Select", "Start", "R1", "R2", "Up", "Left", "Right", "Down",
	"Tri", "Squ", "Cir", "Cro", "L Stick", "R Stick", "L3", "R3" };
static const float indicatorRect[NUM_INDICATORS][4] = {
	{140, 130,  80, 40}, {140, 180,  80, 40}, {235, 140,  80, 40}, {325, 140,  80, 40},
	{420, 130,  80, 40}, {420, 180,  80, 40}, { 75, 230,  80, 40}, { 30, 280,  80, 40},
	{120, 280,  80, 40}, { 75, 330,  80, 40}, {485, 230,  80, 40}, {440, 280,  80, 40},
	{530, 280,  80, 40}, {485, 330,  80, 40}, {160, 345, 100, 40}, {380, 345, 100, 40},
	{235, 395,  80, 40}, {325, 395,  80, 40} };
static char presetNames[NUM_PRESETS][16];

/* Button i (coop.c coop_button_names' order, which is this page's) on or off for this
 * player. A set that matches a preset becomes that preset; any other is the Custom layout. */
static void toggleButton(int i)
{
	int sticks, s2;
	u32 mask = coop_player_buttons(coopPort, coopPlayer, &sticks);
	mask = (mask | ((u32)sticks << 16)) ^ coop_button_mask(i);
	for (int l = 0; l < NUM_PRESETS; l++) {
		u32 preset = coop_layout_buttons(l, &s2);
		if ((preset | ((u32)s2 << 16)) == mask) {
			setLayout(l);
			return;
		}
	}
	coop_buttons_format(mask, coopButtons[coopPort][coopPlayer], COOP_BUTTONS_SIZE);
	setLayout(COOP_LAYOUT_CUSTOM);
}
/* A preset (0..5) chooses a layout; an indicator turns one button on or off */
static void coopClick(void *, int id)
{
	if (id < NUM_PRESETS)
		setLayout(id);
	else
		toggleButton(id - NUM_PRESETS);
}
typedef char coop_names_match[NUM_INDICATORS == COOP_NUM_BUTTONS ? 1 : -1];

/* The look of a button the player has (lit) and of one it has not (grey, still pressable):
 * focus, inactive, active, selected, label */
static GXColor litColors[5]   = {{255, 100, 100, 255}, {90, 90, 90, 255}, {255, 255, 255, 255},
                                 {255, 255, 255, 255}, {255, 255, 255, 255}};
static GXColor unlitColors[5] = {{255, 100, 100, 255}, {90, 90, 90, 255}, {90, 90, 90, 255},
                                 {255, 255, 255, 255}, {120, 120, 120, 255}};

static char *buttonStrings[NUM_FRAME_BUTTONS];
static char titleString[64];
static menu::glue::Editor *editor;	/* the 24 buttons and the title: layout, focus, click by id */

/* Presets in two rows of three: 0..2 at y=12, 3..5 at y=50, 34 high, clear of the title */
static void presetRect(int i, float *x, float *y)
{
	*x = 20.0f + 205.0f * (i % 3);
	*y = i < 3 ? 12.0f : 50.0f;
}

/* Each button at its rectangle, the title centred at y 112 (no logo). Focus is the engine's geometry rule. */
CustomizeCoopFrame::CustomizeCoopFrame()
{
	editor = new menu::glue::Editor(this, NUM_FRAME_BUTTONS, 0, coopClick, NULL, Func_ReturnFromCustomizeCoopFrame);
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		float x, y, w, h;
		if (i < NUM_PRESETS)
		{
			snprintf(presetNames[i], sizeof(presetNames[i]), "%s", coop_layout_names[i]);
			buttonStrings[i] = presetNames[i];
			presetRect(i, &x, &y);
			w = 190.0f; h = 34.0f;
		}
		else
		{
			buttonStrings[i] = indicatorNames[i - NUM_PRESETS];
			const float *r = indicatorRect[i - NUM_PRESETS];
			x = r[0]; y = r[1]; w = r[2]; h = r[3];
		}
		editor->add(i, BTN_A_SEL, &buttonStrings[i], x, y, w, h);
	}
	editor->setTitle(titleString, 112.0f);
	editor->relayout(false);	/* the title's box is made here, after the buttons */

	setDefaultFocus(editor->defaultButton());
	setBackFunc(Func_ReturnFromCustomizeCoopFrame);
	setEnabled(true);
}

CustomizeCoopFrame::~CustomizeCoopFrame()
{
	delete editor;
	editor = NULL;
}

void CustomizeCoopFrame::activateSubmenu(int submenu)
{
	static const char *typeNames[] = { "None", "GameCube pad", "Wii controller", "HID pad" };
	_BUTTONS b;
	int sticks, layout;

	if (submenu < 0 || submenu >= 2 * COOP_MAX)
		submenu = 0;
	coopPort = submenu / COOP_MAX;
	coopPlayer = submenu % COOP_MAX;
	layout = coopLayout[coopPort][coopPlayer];
	if (layout < 0 || layout >= COOP_LAYOUT_COUNT)
		layout = COOP_LAYOUT_FULL;
	snprintf(titleString, sizeof(titleString), "PSX Port %d, player %d: %s %d%s", coopPort + 1,
		coopPlayer + 1, typeNames[(int)coopType[coopPort][coopPlayer] & 3],
		coopAssign[coopPort][coopPlayer] + 1, layout == COOP_LAYOUT_CUSTOM ? ", custom" : "");

	for (int i = 0; i < NUM_PRESETS; i++)
		editor->item(i).lit = (i == layout);
	b.All = coop_player_buttons(coopPort, coopPlayer, &sticks);
	const bool lit[NUM_INDICATORS] = {
		(bool)b.L1_BUTTON, (bool)b.L2_BUTTON, (bool)b.SELECT_BUTTON, (bool)b.START_BUTTON,
		(bool)b.R1_BUTTON, (bool)b.R2_BUTTON, (bool)b.U_DPAD, (bool)b.L_DPAD, (bool)b.R_DPAD,
		(bool)b.D_DPAD, (bool)b.TRIANGLE_BUTTON, (bool)b.SQUARE_BUTTON, (bool)b.CIRCLE_BUTTON,
		(bool)b.CROSS_BUTTON, (sticks & COOP_STICK_L) != 0, (sticks & COOP_STICK_R) != 0,
		(bool)b.L3_BUTTON, (bool)b.R3_BUTTON };
	for (int i = 0; i < NUM_INDICATORS; i++)
		editor->item(NUM_PRESETS + i).lit = lit[i];

	/* The title's text is new, and so are the lit states: the page is laid out again, as it is shown. */
	editor->relayout(true);
	/* a button this player has is lit; the others are grey, image and label (the font
	 * ignores alpha, so grey it is), and still pressable: A turns one on or off */
	for (int i = 0; i < NUM_INDICATORS; i++)
	{
		editor->button(NUM_PRESETS + i)->setButtonColors(lit[i] ? litColors : unlitColors);
	}
}

/* The controller picture and its lines, as Configure Buttons draws them */
void CustomizeCoopFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;
	static const int lines[][4] = {
		{164, 265, 221, 244}, {476, 265, 419, 244}, {275, 160, 302, 240}, {365, 160, 338, 242},
		{220, 170, 242, 192}, {420, 170, 398, 192}, {220, 200, 232, 204}, {420, 200, 408, 204},
		{250, 345, 278, 294}, {390, 345, 362, 294} };
	GXColor white = {255, 255, 255, 255};

	gfx.setColor(white);
	menu::Image *pad = menu::Resources::getInstance().getImage(menu::Resources::IMAGE_PSX_CONTROLLER);
	pad->activateImage(GX_TEXMAP0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	gfx.enableBlending(true);
	gfx.drawImage(0, 204, 188, 232, 152, 0, 1, 0, 1);
	gfx.setTEV(GX_PASSCLR);

	gfx.setColor(white);
	gfx.setLineWidth(1);
	gfx.drawCircle(115, 300, 60, 33);
	gfx.drawCircle(525, 300, 60, 33);
	for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		gfx.drawLine(lines[i][0], lines[i][1], lines[i][2], lines[i][3]);

	menu::ComponentList::const_iterator it;
	for (it = componentList.begin(); it != componentList.end(); ++it)
		(*it)->draw(gfx);
}
