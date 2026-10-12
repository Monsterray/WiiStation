/**
 * WiiStation - CustomizeCoopFrame.cpp
 *
 * Which PlayStation buttons one Co-Op player works (Gamecube/coop.c). It looks like
 * Configure Buttons -- the same controller picture and the same places for each button --
 * without that page's mapping controls (menu combination, fast forward, sensitivity, stick
 * inversion). Each button lights when this player has it; the presets above choose a set,
 * and each button turns on or off by itself (the Custom layout, kept in CoopPButtonsK).
 *
 * The page is on the menu engine (Docs/MENU_FRAMEWORK_PLAN.md, step 5): its 24 buttons are
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
static ButtonFunc presetFuncs[NUM_PRESETS] = {
	Func_LayoutFull, Func_LayoutLeft, Func_LayoutRight,
	Func_LayoutDpadStick, Func_LayoutAction, Func_LayoutShoulders };

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
#define TOGGLE(n) static void Func_Toggle##n() { toggleButton(n); }
TOGGLE(0) TOGGLE(1) TOGGLE(2) TOGGLE(3) TOGGLE(4) TOGGLE(5) TOGGLE(6) TOGGLE(7) TOGGLE(8)
TOGGLE(9) TOGGLE(10) TOGGLE(11) TOGGLE(12) TOGGLE(13) TOGGLE(14) TOGGLE(15) TOGGLE(16) TOGGLE(17)
static ButtonFunc toggleFuncs[NUM_INDICATORS] = {
	Func_Toggle0, Func_Toggle1, Func_Toggle2, Func_Toggle3, Func_Toggle4, Func_Toggle5,
	Func_Toggle6, Func_Toggle7, Func_Toggle8, Func_Toggle9, Func_Toggle10, Func_Toggle11,
	Func_Toggle12, Func_Toggle13, Func_Toggle14, Func_Toggle15, Func_Toggle16, Func_Toggle17 };
typedef char coop_names_match[NUM_INDICATORS == COOP_NUM_BUTTONS ? 1 : -1];

/* The look of a button the player has (lit) and of one it has not (grey, still pressable):
 * focus, inactive, active, selected, label */
static GXColor litColors[5]   = {{255, 100, 100, 255}, {90, 90, 90, 255}, {255, 255, 255, 255},
                                 {255, 255, 255, 255}, {255, 255, 255, 255}};
static GXColor unlitColors[5] = {{255, 100, 100, 255}, {90, 90, 90, 255}, {90, 90, 90, 255},
                                 {255, 255, 255, 255}, {120, 120, 120, 255}};

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

/*  The page, on the engine  */

static eng::FreeButton COOP_FREE[NUM_FRAME_BUTTONS];
static eng::Page       COOP_PAGE;
static eng::Layout     COOP_LAYOUT;

/* The engine's text widths: IplFont, as the other frames use it. */
static int coopWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int coopHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}

static const eng::Metrics COOP_METRICS = { coopWidth, coopHeight, NULL };

/* The button a layout control is, or NULL. */
static menu::Button *buttonAt(int control)
{
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
		if (COOP_LAYOUT.freeCtl[k] == control)
			return buttons[k];
	return NULL;
}

/* The page as the engine lays it out: each button at its rectangle, its lit state from COOP_FREE,
 * the title centred at y 112 (no logo). Focus is the engine's geometry rule, which links every
 * button to the nearest one in each direction. Laid out when the page is shown (activateSubmenu),
 * never per frame. */
static void layoutCoop(void)
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		eng::FreeButton &b = COOP_FREE[i];
		if (i < NUM_PRESETS)
		{
			presetRect(i, &b.x, &b.y);
			b.w = 190.0f; b.h = 34.0f;
		}
		else
		{
			const float *r = indicatorRect[i - NUM_PRESETS];
			b.x = r[0]; b.y = r[1]; b.w = r[2]; b.h = r[3];
		}
		b.text = buttonStrings[i];
		b.inactive = false;
	}
	memset(&COOP_PAGE, 0, sizeof COOP_PAGE);
	COOP_PAGE.title        = titleString;
	COOP_PAGE.titleY       = 112.0f;
	COOP_PAGE.logo         = eng::LOGO_SPOT_NONE;
	COOP_PAGE.focus        = eng::FOCUS_GEOMETRY;
	COOP_PAGE.freeButtons  = COOP_FREE;
	COOP_PAGE.nFreeButtons = NUM_FRAME_BUTTONS;
	eng::layoutPage(COOP_PAGE, COOP_METRICS, COOP_LAYOUT);
	eng::linkFocus(COOP_PAGE, COOP_LAYOUT);
}

CustomizeCoopFrame::CustomizeCoopFrame()
{
	for (int i = 0; i < NUM_PRESETS; i++)
	{
		snprintf(presetNames[i], sizeof(presetNames[i]), "%s", coop_layout_names[i]);
		buttonStrings[i] = presetNames[i];
	}
	for (int i = NUM_PRESETS; i < NUM_FRAME_BUTTONS; i++)
		buttonStrings[i] = indicatorNames[i - NUM_PRESETS];
	layoutCoop();

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = COOP_LAYOUT.c[COOP_LAYOUT.freeCtl[i]];
		buttons[i] = new menu::Button(BTN_A_SEL, &buttonStrings[i], c.rect.x, c.rect.y, c.rect.w, c.rect.h);
		buttons[i]->setBounds(c.rect.x, c.rect.y, c.rect.w, c.rect.h);   /* style A makes itself 56 high otherwise */
		buttons[i]->setActive(true);
		buttons[i]->setClicked(i < NUM_PRESETS ? presetFuncs[i] : toggleFuncs[i - NUM_PRESETS]);
		buttons[i]->setReturn(Func_ReturnFromCustomizeCoopFrame);
		add(buttons[i]);
		menu::Cursor::getInstance().addComponent(this, buttons[i], c.rect.x, c.rect.x + c.rect.w,
		                                         c.rect.y, c.rect.y + c.rect.h);
	}
	/* The engine's links (linkFocus, FOCUS_GEOMETRY), by direction; a link of -1 is none. */
	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = COOP_LAYOUT.c[COOP_LAYOUT.freeCtl[i]];
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];
			buttons[i]->setNextFocus(DIRS[d], to >= 0 ? buttonAt(to) : NULL);
		}
	}
	const eng::Control &t = COOP_LAYOUT.c[COOP_LAYOUT.title];
	title = new menu::TextBox(&titlePtr, t.ax, t.ay, 1.0f, true);
	add(title);

	setDefaultFocus(buttonAt(COOP_LAYOUT.defaultFocus));
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
	snprintf(titleString, sizeof(titleString), "PSX Port %d, player %d: %s %d%s", coopPort + 1,
		coopPlayer + 1, typeNames[(int)coopType[coopPort][coopPlayer] & 3],
		coopAssign[coopPort][coopPlayer] + 1, layout == COOP_LAYOUT_CUSTOM ? ", custom" : "");

	for (int i = 0; i < NUM_PRESETS; i++)
		COOP_FREE[i].lit = (i == layout);
	b.All = coop_player_buttons(coopPort, coopPlayer, &sticks);
	const bool lit[NUM_INDICATORS] = {
		(bool)b.L1_BUTTON, (bool)b.L2_BUTTON, (bool)b.SELECT_BUTTON, (bool)b.START_BUTTON,
		(bool)b.R1_BUTTON, (bool)b.R2_BUTTON, (bool)b.U_DPAD, (bool)b.L_DPAD, (bool)b.R_DPAD,
		(bool)b.D_DPAD, (bool)b.TRIANGLE_BUTTON, (bool)b.SQUARE_BUTTON, (bool)b.CIRCLE_BUTTON,
		(bool)b.CROSS_BUTTON, (sticks & COOP_STICK_L) != 0, (sticks & COOP_STICK_R) != 0,
		(bool)b.L3_BUTTON, (bool)b.R3_BUTTON };
	for (int i = 0; i < NUM_INDICATORS; i++)
		COOP_FREE[NUM_PRESETS + i].lit = lit[i];

	/* The title's text is new, and so are the lit states: the page is laid out again, as it is shown. */
	layoutCoop();
	{
		const eng::Control &t = COOP_LAYOUT.c[COOP_LAYOUT.title];
		title->setPosition(t.ax, t.ay);
	}
	for (int i = 0; i < NUM_PRESETS; i++)
		buttons[i]->setSelected(COOP_FREE[i].lit);
	/* a button this player has is lit; the others are grey, image and label (the font
	 * ignores alpha, so grey it is), and still pressable: A turns one on or off */
	for (int i = 0; i < NUM_INDICATORS; i++)
	{
		buttons[NUM_PRESETS + i]->setSelected(lit[i]);
		buttons[NUM_PRESETS + i]->setButtonColors(lit[i] ? litColors : unlitColors);
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
