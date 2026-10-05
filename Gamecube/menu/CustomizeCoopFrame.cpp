/**
 * WiiStation - CustomizeCoopFrame.cpp
 *
 * Which PlayStation buttons one Co-Op player works (Gamecube/coop.c). It looks like
 * Configure Buttons -- the same controller picture and the same places for each button --
 * without that page's mapping controls (menu combination, fast forward, sensitivity, stick
 * inversion). Each button lights when this player has it; the presets above choose them.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <stdio.h>
#include "MenuContext.h"
#include "ConfigureInputFrame.h"
#include "CustomizeCoopFrame.h"
#include "../libgui/GuiTypes.h"
#include "../libgui/GuiResources.h"
#include "../libgui/Button.h"
#include "../libgui/Image.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
extern "C" {
#include "../gc_input/controller.h"
#include "../wiiSXconfig.h"
}

extern MenuContext *pMenuContext;

static int coopPort, coopPlayer;   /* whose layout this page shows */

static void Func_ReturnFromCustomizeCoopFrame()
{
	menu::Gui::getInstance().menuLogo->setVisible(true);
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREINPUT, ConfigureInputFrame::SUBMENU_REINIT);
}

static void setLayout(int layout)
{
	coopLayout[coopPort][coopPlayer] = (char)layout;
	pMenuContext->getFrame(MenuContext::FRAME_CUSTOMIZECOOP)->activateSubmenu(coopPort * COOP_MAX + coopPlayer);
}
static void Func_LayoutFull()      { setLayout(COOP_LAYOUT_FULL); }
static void Func_LayoutLeft()      { setLayout(COOP_LAYOUT_LEFT); }
static void Func_LayoutRight()     { setLayout(COOP_LAYOUT_RIGHT); }
static void Func_LayoutDpadStick() { setLayout(COOP_LAYOUT_DPAD_STICKS); }
static void Func_LayoutAction()    { setLayout(COOP_LAYOUT_ACTION); }
static void Func_LayoutShoulders() { setLayout(COOP_LAYOUT_SHOULDERS); }

/* The PlayStation buttons, where Configure Buttons puts them (ConfigureButtonsFrame.cpp) */
enum { IND_L1, IND_L2, IND_SELECT, IND_START, IND_R1, IND_R2, IND_DUP, IND_DLEFT, IND_DRIGHT,
       IND_DDOWN, IND_TRI, IND_SQU, IND_CIR, IND_CRO, IND_LSTICK, IND_RSTICK, IND_L3, IND_R3,
       NUM_INDICATORS };
#define NUM_PRESETS COOP_LAYOUT_COUNT
#define NUM_FRAME_BUTTONS (NUM_PRESETS + NUM_INDICATORS)

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
static ButtonFunc presetFuncs[NUM_PRESETS] = {
	Func_LayoutFull, Func_LayoutLeft, Func_LayoutRight,
	Func_LayoutDpadStick, Func_LayoutAction, Func_LayoutShoulders };

static menu::Button *buttons[NUM_FRAME_BUTTONS];
static char *buttonStrings[NUM_FRAME_BUTTONS];
static char titleString[64];
static char *titlePtr = titleString;
static menu::TextBox *title;

/* Presets in two rows of three: 0..2 at y=12, 3..5 at y=50, 34 high, clear of the title */
static void presetRect(int i, float *x, float *y)
{
	*x = 20.0f + 205.0f * (i % 3);
	*y = i < 3 ? 12.0f : 50.0f;
}

CustomizeCoopFrame::CustomizeCoopFrame()
{
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
			const float *r = indicatorRect[i - NUM_PRESETS];
			buttonStrings[i] = indicatorNames[i - NUM_PRESETS];
			x = r[0]; y = r[1]; w = r[2]; h = r[3];
		}
		buttons[i] = new menu::Button(BTN_A_SEL, &buttonStrings[i], x, y, w, h);
		buttons[i]->setBounds(x, y, w, h);   /* style A makes itself 56 high otherwise */
		if (i >= NUM_PRESETS)
		{	/* focus, inactive (grey: a button this player does not have), active, selected, label */
			GXColor c[5] = {{255, 100, 100, 255}, {90, 90, 90, 255}, {255, 255, 255, 130},
			                {255, 255, 255, 255}, {255, 255, 255, 255}};
			buttons[i]->setButtonColors(c);
		}
		buttons[i]->setActive(true);
		if (i < NUM_PRESETS)
			buttons[i]->setClicked(presetFuncs[i]);
		buttons[i]->setReturn(Func_ReturnFromCustomizeCoopFrame);
		add(buttons[i]);
		menu::Cursor::getInstance().addComponent(this, buttons[i], x, x + w, y, y + h);
	}
	/* Focus moves among the presets only: the buttons below show, they do not change */
	for (int i = 0; i < NUM_PRESETS; i++)
	{
		int row = i / 3, col = i % 3;
		buttons[i]->setNextFocus(menu::Focus::DIRECTION_LEFT,  buttons[row * 3 + (col + 2) % 3]);
		buttons[i]->setNextFocus(menu::Focus::DIRECTION_RIGHT, buttons[row * 3 + (col + 1) % 3]);
		buttons[i]->setNextFocus(menu::Focus::DIRECTION_UP,    buttons[(1 - row) * 3 + col]);
		buttons[i]->setNextFocus(menu::Focus::DIRECTION_DOWN,  buttons[(1 - row) * 3 + col]);
	}
	title = new menu::TextBox(&titlePtr, 320.0f, 112.0f, 1.0f, true);
	add(title);

	setDefaultFocus(buttons[0]);
	setBackFunc(Func_ReturnFromCustomizeCoopFrame);
	setEnabled(true);
}

CustomizeCoopFrame::~CustomizeCoopFrame()
{
	delete title;
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, buttons[i]);
		delete buttons[i];
	}
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
	snprintf(titleString, sizeof(titleString), "PSX Port %d, player %d: %s %d", coopPort + 1,
		coopPlayer + 1, typeNames[(int)coopType[coopPort][coopPlayer] & 3],
		coopAssign[coopPort][coopPlayer] + 1);
	menu::Gui::getInstance().menuLogo->setVisible(false);

	for (int i = 0; i < NUM_PRESETS; i++)
		buttons[i]->setSelected(i == layout);
	b.All = coop_layout_buttons(layout, &sticks);
	const bool lit[NUM_INDICATORS] = {
		(bool)b.L1_BUTTON, (bool)b.L2_BUTTON, (bool)b.SELECT_BUTTON, (bool)b.START_BUTTON,
		(bool)b.R1_BUTTON, (bool)b.R2_BUTTON, (bool)b.U_DPAD, (bool)b.L_DPAD, (bool)b.R_DPAD,
		(bool)b.D_DPAD, (bool)b.TRIANGLE_BUTTON, (bool)b.SQUARE_BUTTON, (bool)b.CIRCLE_BUTTON,
		(bool)b.CROSS_BUTTON, (sticks & COOP_STICK_L) != 0, (sticks & COOP_STICK_R) != 0,
		(bool)b.L3_BUTTON, (bool)b.R3_BUTTON };
	/* a button this player has is lit; the others are greyed, image and label (an inactive
	 * button draws both in its inactive colour; the font ignores alpha, so it is grey) */
	for (int i = 0; i < NUM_INDICATORS; i++)
	{
		buttons[NUM_PRESETS + i]->setSelected(lit[i]);
		buttons[NUM_PRESETS + i]->setActive(lit[i]);
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
