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
#include "MenuLayout.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "../libgui/IPLFont.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "MenuGlue.h"

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
static const int NUM_FRAME_BUTTONS = 70;
#define COOP_BTN(port, k, col) (22 + (port) * 24 + (k) * 3 + (col))
static const int NUM_FRAME_TEXTBOXES = 21;	/* 5..20: the Co-Op rows' P1..P8 labels */
#define COOP_LABEL(port, k) (5 + (port) * 8 + (k))

static char ciStrings[42][15] =
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
	  "1P", "2P", "3P", "4P", "5P", "6P", "7P", "8P",	// [32..39] a Co-Op port's player count
	  "Co-Op 1", "Co-Op 2"};	// [40..41] a Co-Op port's column heading

/* One button of the page. layoutTables sets its box (x, y, width, height).
 * The box fields come last. The table fills only the fields before them. */
struct CiButton
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
	float			x, y, width, height;
};

/* Buttons 0..21 are listed here. The Co-Op buttons 22..69 are filled in by layoutTables. */
static CiButton ciButtons[NUM_FRAME_BUTTONS] =
{	//	button	style		string			clickFunc				returnFunc
	{	NULL,	BTN_A_SEL,	ciStrings[5],	Func_AutoSelectInput,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_SEL,	ciStrings[6],	Func_ManualSelectInput,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad0Type,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad1Type,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad0AType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad0BType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad0CType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad0DType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad1AType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad1BType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad1CType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[11],	Func_TogglePad1DType,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad0Assign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad1Assign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad0AAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad0BAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad0CAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad0DAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad1AAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad1BAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad1CAssign,	Func_ReturnFromConfigureInputFrame },
	{	NULL,	BTN_A_NRM,	ciStrings[12],	Func_TogglePad1DAssign,	Func_ReturnFromConfigureInputFrame },
};

/* One label of the page. layoutTables sets its position (x, y).
 * The position fields come last. The table fills only the fields before them. */
struct CiTextBox
{
	menu::TextBox	*textBox;
	char*			textBoxString;
	float			scale;
	bool			centered;
	float			x, y;
};

/* Labels 0..4 are listed here. The Co-Op labels 5..20 are filled in by layoutTables. */
static CiTextBox ciTextBoxes[NUM_FRAME_TEXTBOXES] =
{	//	textBox	textBoxString	scale	centered
	{	NULL,	ciStrings[0],	1.0f,	true },	/* Pad Assignment */
	{	NULL,	ciStrings[1],	1.0f,	true },	/* Pad 1 */
	{	NULL,	ciStrings[2],	1.0f,	true },	/* Pad 2 */
	{	NULL,	ciStrings[3],	1.0f,	true },	/* Multitap 1 */
	{	NULL,	ciStrings[4],	1.0f,	true },	/* Multitap 2 */
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

/* The layout: Pad Assignment as a row of the Settings tabs' grid (MenuLayout.h: label
 * centred on TAB_LABEL_CX, buttons from TAB_BUTTON_X, TAB_GAP apart); below it two port
 * columns side by side, each headed by its PSX Port label and that port's type and number,
 * then the multitap slots or the Co-Op players of that port. No logo on this frame: it needs the
 * room (activateSubmenu hides it). Style-A buttons make themselves 56 high in their
 * constructor, so every button gets these bounds again afterwards (setBounds). */
#define CI_ROW_Y(r)		(20.0f + 62.0f * (r))		/* 0: Pad Assignment */
#define CI_ROW_H		56.0f
#define CI_PORTLABEL_Y	98.0f		/* PSX Port N, centred on its column */
#define CI_PORT_Y		114.0f		/* the port's type and number */
#define CI_TYPE_W		204.0f		/* fits "Gamecube Pad" (176) and padding */
#define CI_NUM_W		60.0f		/* fits "8P" */
#define CI_COL_X(p)		(8.0f + 316.0f * (p))
#define CI_COL_W		308.0f
#define CI_HEAD_Y		190.0f		/* a column's Multitap / Co-Op heading, centred */
#define CI_SLOT_Y(s)	(206.0f + 62.0f * (s))	/* multitap slots: 4 rows end at 448 */
#define CI_SLOT_H		56.0f
#define CI_COOP_Y(k)	(206.0f + 33.0f * (k))	/* Co-Op players: 8 rows end at 467 */
#define CI_COOP_H		30.0f
static void setRect(int i, float x, float y, float w, float h)
{
	ciButtons[i].x = x; ciButtons[i].y = y;
	ciButtons[i].width = w; ciButtons[i].height = h;
}
static void layoutTables(void)
{
	/* a Co-Op row is as wide as its column: label, type, Customize, number, 6 apart */
	static const float coopX[3] = { 32.0f, 104.0f, 256.0f }, coopW[3] = { 66.0f, 146.0f, 52.0f };
	const float slotX = (CI_COL_W - (CI_TYPE_W + TAB_GAP + CI_NUM_W)) / 2;

	setRect(0, TAB_BUTTON_X, CI_ROW_Y(0), 140.0f, CI_ROW_H);   /* width: auto-fitted later */
	setRect(1, TAB_BUTTON_X + 150.0f, CI_ROW_Y(0), 100.0f, CI_ROW_H);
	for (int p = 0; p < 2; p++)
	{
		setRect(2 + p, CI_COL_X(p) + slotX, CI_PORT_Y, CI_TYPE_W, CI_ROW_H);
		setRect(12 + p, CI_COL_X(p) + slotX + CI_TYPE_W + TAB_GAP, CI_PORT_Y, CI_NUM_W, CI_ROW_H);
		for (int sl = 0; sl < 4; sl++)
		{
			int i = 2 + p * 4 + sl;
			setRect(i + 2, CI_COL_X(p) + slotX, CI_SLOT_Y(sl), CI_TYPE_W, CI_SLOT_H);
			setRect(i + 12, CI_COL_X(p) + slotX + CI_TYPE_W + TAB_GAP, CI_SLOT_Y(sl), CI_NUM_W, CI_SLOT_H);
		}
		for (int k = 0; k < COOP_MAX; k++)
		{
			for (int c = 0; c < 3; c++)
			{
				CiButton &b = ciButtons[COOP_BTN(p, k, c)];
				b.buttonStyle = BTN_A_NRM;
				b.buttonString = ciStrings[c == 1 ? 19 : 12];
						b.clickedFunc = coopFuncs[p][k][c];
				b.returnFunc = Func_ReturnFromConfigureInputFrame;
				setRect(COOP_BTN(p, k, c), CI_COL_X(p) + coopX[c], CI_COOP_Y(k), coopW[c], CI_COOP_H);
			}
			CiTextBox &t = ciTextBoxes[COOP_LABEL(p, k)];
			t.textBoxString = ciStrings[20 + k];
			t.x = CI_COL_X(p) + 15.0f;
			t.y = CI_COOP_Y(k) + CI_COOP_H / 2;
			t.scale = 0.8f;
			t.centered = true;
		}
		/* the port's label at the top of its column, then the column's heading */
		ciTextBoxes[1 + p].x = CI_COL_X(p) + CI_COL_W / 2;
		ciTextBoxes[1 + p].y = CI_PORTLABEL_Y;
		ciTextBoxes[3 + p].x = CI_COL_X(p) + CI_COL_W / 2;
		ciTextBoxes[3 + p].y = CI_HEAD_Y;
	}
	ciTextBoxes[0].x = TAB_LABEL_CX;
	ciTextBoxes[0].y = CI_ROW_Y(0) + CI_ROW_H / 2;
}

ConfigureInputFrame::ConfigureInputFrame()
{
	layoutTables();
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		ciButtons[i].button = new menu::Button(ciButtons[i].buttonStyle, &ciButtons[i].buttonString,
										ciButtons[i].x, ciButtons[i].y,
										ciButtons[i].width, ciButtons[i].height);

	/* A style-A button makes itself 56 high whatever it is given: every one gets its own
	 * bounds back. Automatic and Manual take their width from their label, as the Settings
	 * tabs' buttons do, Manual TAB_GAP after Automatic. Their rectangles are then the page's. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		ciButtons[i].button->setBounds(ciButtons[i].x, ciButtons[i].y,
		                                   ciButtons[i].width, ciButtons[i].height);
	{
		float x = TAB_BUTTON_X;
		for (int i = 0; i < 2; i++)
		{
			menu::Button *b = ciButtons[i].button;
			b->setAutoSize(menu::Button::BTN_FIT_WIDTH);
			b->setBounds(x, CI_ROW_Y(0), 0, CI_ROW_H);
			ciButtons[i].x = x;
			ciButtons[i].width = b->getWidth();
			ciButtons[i].height = CI_ROW_H;
			x += b->getWidth() + TAB_GAP;
		}
	}

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		ciButtons[i].button->setActive(true);
		if (ciButtons[i].clickedFunc) ciButtons[i].button->setClicked(ciButtons[i].clickedFunc);
		if (ciButtons[i].returnFunc) ciButtons[i].button->setReturn(ciButtons[i].returnFunc);
		add(ciButtons[i].button);
		menu::Cursor::getInstance().addComponent(this, ciButtons[i].button, ciButtons[i].x,
												ciButtons[i].x+ciButtons[i].width, ciButtons[i].y,
												ciButtons[i].y+ciButtons[i].height);
	}

	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
	{
		ciTextBoxes[i].textBox = new menu::TextBox(&ciTextBoxes[i].textBoxString,
										ciTextBoxes[i].x, ciTextBoxes[i].y,
										ciTextBoxes[i].scale, ciTextBoxes[i].centered);
		add(ciTextBoxes[i].textBox);
	}

	setDefaultFocus(ciButtons[0].button);
	setBackFunc(Func_ReturnFromConfigureInputFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_REINIT);
}

ConfigureInputFrame::~ConfigureInputFrame()
{
	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
		delete ciTextBoxes[i].textBox;
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, ciButtons[i].button);
		delete ciButtons[i].button;
	}

}

/*  The page, on the engine (Docs/MENU_FRAMEWORK_PLAN.md, step 5)  */

/* The page changes with the state: which buttons and labels are there, which are active, which lit,
 * and where focus goes. activateSubmenu() works those out, then the engine lays the page out and
 * links it (layoutState), as it does when the page is shown -- never per frame. The buttons keep
 * their places (the table's rectangles), so the Cursor ranges set in the constructor stay right. */
namespace eng = menu::engine;

/* The state: shown, active and lit, by button and by label (ciButtons and ciTextBoxes index) */
static bool ciShown[NUM_FRAME_BUTTONS], ciActive[NUM_FRAME_BUTTONS], ciLit[NUM_FRAME_BUTTONS];
static bool ciTextShown[NUM_FRAME_TEXTBOXES];

/* The focus the state gives: from button `from`, each direction to a button (-1: none). The links are
 * the engine's FreeLink overrides, which replace the rule's link where both are focus targets. */
struct CiLink { int from; eng::Dir dir; int to; };
#define CI_MAX_LINKS (NUM_FRAME_BUTTONS * eng::DIR_COUNT)
static CiLink ciLinks[CI_MAX_LINKS];
static int nCiLinks;

static void linkButton(int from, int up, int down, int left, int right)
{
	const int to[eng::DIR_COUNT] = { up, down, left, right };
	static const eng::Dir dirs[eng::DIR_COUNT] = { eng::DIR_UP, eng::DIR_DOWN, eng::DIR_LEFT, eng::DIR_RIGHT };
	for (int d = 0; d < eng::DIR_COUNT; d++)
	{
		ciLinks[nCiLinks].from = from;
		ciLinks[nCiLinks].dir = dirs[d];
		ciLinks[nCiLinks].to = to[d];
		nCiLinks++;
	}
}

/* The page's own arrays: the engine keeps none of them. */
static eng::FreeButton CI_FREE[NUM_FRAME_BUTTONS];
static int             CI_FREE_ID[NUM_FRAME_BUTTONS];	/* free index -> ciButtons index */
static eng::FreeText   CI_TEXTS[NUM_FRAME_TEXTBOXES];
static int             CI_TEXT_ID[NUM_FRAME_TEXTBOXES];	/* free text index -> ciTextBoxes index */
static eng::FreeLink   CI_FLINKS[CI_MAX_LINKS];
static eng::Page       CI_PAGE;
static eng::Layout     CI_LAYOUT;

/* Lays the page out for the state, links it, and gives each button and label its place, state and links.
 * A link to a button the state does not show is left out: the state never gives one. */
/* The button drawn for a layout control. */
static menu::Button *ciButtonOf(const eng::Control &c)
{
	return ciButtons[CI_FREE_ID[c.slot]].button;
}

static void layoutState(void)
{
	int nf = 0, nt = 0, nl = 0;
	int freeOf[NUM_FRAME_BUTTONS];

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		freeOf[i] = -1;
		if (!ciShown[i])
			continue;
		eng::FreeButton &b = CI_FREE[nf];
		b.text = ciButtons[i].buttonString;
		b.x = ciButtons[i].x;
		b.y = ciButtons[i].y;
		b.w = ciButtons[i].width;
		b.h = ciButtons[i].height;
		b.lit = ciLit[i];
		b.inactive = !ciActive[i];
		CI_FREE_ID[nf] = i;
		freeOf[i] = nf++;
	}
	for (int t = 0; t < NUM_FRAME_TEXTBOXES; t++)
	{
		if (!ciTextShown[t])
			continue;
		eng::FreeText &x = CI_TEXTS[nt];
		x.text = ciTextBoxes[t].textBoxString;
		x.ax = ciTextBoxes[t].x;
		x.ay = ciTextBoxes[t].y;
		x.scale = ciTextBoxes[t].scale;
		x.centred = ciTextBoxes[t].centered;
		CI_TEXT_ID[nt++] = t;
	}
	for (int k = 0; k < nCiLinks; k++)
	{
		const CiLink &l = ciLinks[k];
		const int to = l.to < 0 ? -1 : freeOf[l.to];
		if (freeOf[l.from] < 0 || (l.to >= 0 && to < 0))
			continue;
		CI_FLINKS[nl].from = freeOf[l.from];
		CI_FLINKS[nl].dir = l.dir;
		CI_FLINKS[nl].to = to;
		nl++;
	}

	memset(&CI_PAGE, 0, sizeof CI_PAGE);
	CI_PAGE.logo = eng::LOGO_SPOT_NONE;	/* no logo on this frame: it needs the room */
	CI_PAGE.focus = eng::FOCUS_GEOMETRY;
	CI_PAGE.freeButtons = CI_FREE;
	CI_PAGE.nFreeButtons = nf;
	CI_PAGE.freeTexts = CI_TEXTS;
	CI_PAGE.nFreeTexts = nt;
	CI_PAGE.freeLinks = CI_FLINKS;
	CI_PAGE.nFreeLinks = nl;
	if (eng::layoutPage(CI_PAGE, menu::glue::METRICS, CI_LAYOUT) < 0)
		return;		/* the tables are fixed: a page that does not fit is a programming error */
	eng::linkFocus(CI_PAGE, CI_LAYOUT);

	for (int k = 0; k < nf; k++)
	{
		const int i = CI_FREE_ID[k];
		menu::Button *b = ciButtons[i].button;
		b->setVisible(true);
		b->setActive(ciActive[i]);
		b->setSelected(ciLit[i]);
	}
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		if (!ciShown[i])
		{
			ciButtons[i].button->setVisible(false);
			ciButtons[i].button->setActive(false);
		}

	for (int c = 0; c < CI_LAYOUT.n; c++)
	{
		const eng::Control &ctl = CI_LAYOUT.c[c];
		if (ctl.role == eng::ROLE_FREE)
		{
			menu::Button *b = ciButtons[CI_FREE_ID[ctl.slot]].button;
			b->setBounds(ctl.rect.x, ctl.rect.y, ctl.rect.w, ctl.rect.h);
			menu::glue::linkFocus(b, CI_LAYOUT, ctl, ciButtonOf);
		}
		else if (ctl.role == eng::ROLE_FREE_TEXT)
		{
			ciTextBoxes[CI_TEXT_ID[ctl.slot]].textBox->setPosition(ctl.ax, ctl.ay);
		}
	}
	for (int t = 0; t < NUM_FRAME_TEXTBOXES; t++)
		ciTextBoxes[t].textBox->setVisible(ciTextShown[t]);
}

/* What each port shows: its type and number on its own row; in its column below, four
 * multitap slots (Multitap) or its Co-Op players (Co-Op), nothing otherwise. Focus runs
 * down the three rows, then down each column; left and right cross a row and, in the
 * columns, step to the other column's row at the same height. */
void ConfigureInputFrame::activateSubmenu(int submenu)
{
	const bool manual = padAutoAssign != PADAUTOASSIGN_AUTOMATIC;
	int rows[2] = { 0, 0 };            /* rows shown in each column */
	int cell[2][COOP_MAX][3];          /* each column's buttons by row and column, -1: none */
	int ncol[2] = { 0, 0 };            /* buttons in each of a column's rows */

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		ciShown[i] = ciActive[i] = ciLit[i] = false;
	for (int t = 0; t < NUM_FRAME_TEXTBOXES; t++)
		ciTextShown[t] = t <= 2;		/* Pad Assignment and the two port labels */
	nCiLinks = 0;

	ciShown[0] = ciShown[1] = ciActive[0] = ciActive[1] = true;
	ciLit[0] = !manual;
	ciLit[1] = manual;
	for (int p = 0; p < 2; p++)
	{
		const bool mtap = padType[p] == PADTYPE_MULTITAP, coop = padType[p] == PADTYPE_COOP;

		/* the port's own row */
		ciButtons[2 + p].buttonString = !manual ? ciStrings[11] :
			padType[p] == PADTYPE_HID ? ciStrings[17] : mtap ? ciStrings[10] :
			coop ? ciStrings[18] : ciStrings[padType[p] + 7];
		ciButtons[12 + p].buttonString = !manual || mtap ? ciStrings[12] :
			coop ? ciStrings[31 + coopPlayers[p]] : ciStrings[padAssign[p] + 13];
		ciShown[2 + p] = true;
		ciActive[2 + p] = manual;
		ciShown[12 + p] = !mtap;		/* a multitap has no number of its own */
		ciActive[12 + p] = manual;

		/* its column: a heading, then the slots or the players */
		ciTextBoxes[3 + p].textBoxString = coop ? ciStrings[40 + p] : ciStrings[3 + p];
		ciTextShown[3 + p] = mtap || coop;
		for (int sl = 0; sl < 4; sl++)
		{
			int i = 2 + p * 4 + sl;
			ciButtons[i + 2].buttonString = padType[i] == PADTYPE_HID ? ciStrings[17] :
				ciStrings[padType[i] + 7];
			ciButtons[i + 12].buttonString = ciStrings[padAssign[i] + 13];
			ciShown[i + 2] = ciShown[i + 12] = mtap;
			ciActive[i + 2] = ciActive[i + 12] = manual;
			if (mtap)
			{
				cell[p][sl][0] = i + 2;
				cell[p][sl][1] = i + 12;
				ncol[p] = 2;
				rows[p] = sl + 1;
			}
		}
		for (int k = 0; k < COOP_MAX; k++)
		{
			const bool on = coop && k < coopPlayers[p];
			ciButtons[COOP_BTN(p, k, 0)].buttonString = ciStrings[28 + coopType[p][k]];
			ciButtons[COOP_BTN(p, k, 2)].buttonString = ciStrings[13 + coopAssign[p][k]];
			for (int c = 0; c < 3; c++)
			{
				ciShown[COOP_BTN(p, k, c)] = on;
				ciActive[COOP_BTN(p, k, c)] = manual;
			}
			ciTextShown[COOP_LABEL(p, k)] = on;
			if (on)
			{
				for (int c = 0; c < 3; c++)
					cell[p][k][c] = COOP_BTN(p, k, c);
				ncol[p] = 3;
				rows[p] = k + 1;
			}
		}
	}

	if (!manual)
	{
		linkButton(0, -1, -1, 1, 1);
		linkButton(1, -1, -1, 0, 0);
		layoutState();
		return;
	}
	/* Pad Assignment, then the ports' row: port 1's type and number, port 2's */
	const bool num0 = padType[0] != PADTYPE_MULTITAP;   /* a port's number can be focused */
	const bool num1 = padType[1] != PADTYPE_MULTITAP;
	const int r0 = num0 ? 12 : 2, r1 = num1 ? 13 : 3;   /* each port's rightmost button */
	const int lastCol0 = rows[0] ? cell[0][rows[0] - 1][0] : 2;
	const int lastCol1 = rows[1] ? cell[1][rows[1] - 1][ncol[1] - 1] : r1;
	linkButton(0, lastCol0, 2, 1, 1);
	linkButton(1, lastCol1, 3, 0, 0);
	linkButton(2, 0, rows[0] ? cell[0][0][0] : 0, r1, num0 ? 12 : 3);
	if (num0) linkButton(12, 0, rows[0] ? cell[0][0][ncol[0] - 1] : 0, 2, 3);
	linkButton(3, 1, rows[1] ? cell[1][0][0] : 1, r0, num1 ? 13 : 2);
	if (num1) linkButton(13, 1, rows[1] ? cell[1][0][ncol[1] - 1] : 1, 3, 2);

	/* the columns */
	for (int p = 0; p < 2; p++)
		for (int r = 0; r < rows[p]; r++)
			for (int c = 0; c < ncol[p]; c++)
			{
				int o = 1 - p;
				int up = r ? cell[p][r - 1][c] : (c && (p ? num1 : num0) ? 12 + p : 2 + p);
				int down = r + 1 < rows[p] ? cell[p][r + 1][c] : (p == 0 ? 0 : 1);
				int left = c ? cell[p][r][c - 1] : (r < rows[o] ? cell[o][r][ncol[o] - 1] : cell[p][r][ncol[p] - 1]);
				int right = c + 1 < ncol[p] ? cell[p][r][c + 1] : (r < rows[o] ? cell[o][r][0] : cell[p][r][0]);
				linkButton(cell[p][r][c], up, down, left, right);
			}
	layoutState();
}


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

static void Func_AssignPad(int i)
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

/* Each toggle changes a port setting. Then it draws the page again. */
static void refreshInput(void)
{
	pMenuContext->getFrame(MenuContext::FRAME_CONFIGUREINPUT)->activateSubmenu(ConfigureInputFrame::SUBMENU_REINIT);
}

/* Set a port type. On the Wii the order is None, GC, Wii, HID, Multitap, Co-Op. */
static void togglePortType(int i)
{
#ifdef HW_RVL
	set_port_type(i, (padType[i]+1) % 6);
#else
	set_port_type(i, (padType[i]+1) & 1);
#endif
	refreshInput();
}

/* Set a multitap slot type. On the Wii the order is None, GC, Wii, HID.
 * The slot controller is assigned at once, or removed. */
static void toggleSlotType(int i)
{
#ifdef HW_RVL
	padType[i] = (padType[i]+1) % 4;
#else
	padType[i] = (padType[i]+1) % 2;
#endif
	if (padType[i]) Func_AssignPad(i);
	else			unassign_controller(i);
	refreshInput();
}

/* A port's controller number, or a Co-Op port's player count, 1..8. */
static void togglePortAssign(int i)
{
	if (padType[i] == PADTYPE_COOP)
	{
		coopPlayers[i] = coopPlayers[i] % COOP_MAX + 1;
		coopRefresh(i);
		return;
	}
	padAssign[i] = (padAssign[i]+1) % 4;

	if (padType[i] && padType[i] != PADTYPE_MULTITAP) Func_AssignPad(i);

	refreshInput();
}

/* A multitap slot's controller number, assigned at once when the slot has a type. */
static void toggleSlotAssign(int i)
{
	padAssign[i] = (padAssign[i]+1) % 4;

	if (padType[i]) Func_AssignPad(i);

	refreshInput();
}

void Func_TogglePad0Type()    { togglePortType(PADASSIGN_INPUT0); }
void Func_TogglePad1Type()    { togglePortType(PADASSIGN_INPUT1); }
void Func_TogglePad0AType()   { toggleSlotType(PADASSIGN_INPUT0A); }
void Func_TogglePad0BType()   { toggleSlotType(PADASSIGN_INPUT0B); }
void Func_TogglePad0CType()   { toggleSlotType(PADASSIGN_INPUT0C); }
void Func_TogglePad0DType()   { toggleSlotType(PADASSIGN_INPUT0D); }
void Func_TogglePad1AType()   { toggleSlotType(PADASSIGN_INPUT1A); }
void Func_TogglePad1BType()   { toggleSlotType(PADASSIGN_INPUT1B); }
void Func_TogglePad1CType()   { toggleSlotType(PADASSIGN_INPUT1C); }
void Func_TogglePad1DType()   { toggleSlotType(PADASSIGN_INPUT1D); }

void Func_TogglePad0Assign()  { togglePortAssign(PADASSIGN_INPUT0); }
void Func_TogglePad1Assign()  { togglePortAssign(PADASSIGN_INPUT1); }
void Func_TogglePad0AAssign() { toggleSlotAssign(PADASSIGN_INPUT0A); }
void Func_TogglePad0BAssign() { toggleSlotAssign(PADASSIGN_INPUT0B); }
void Func_TogglePad0CAssign() { toggleSlotAssign(PADASSIGN_INPUT0C); }
void Func_TogglePad0DAssign() { toggleSlotAssign(PADASSIGN_INPUT0D); }
void Func_TogglePad1AAssign() { toggleSlotAssign(PADASSIGN_INPUT1A); }
void Func_TogglePad1BAssign() { toggleSlotAssign(PADASSIGN_INPUT1B); }
void Func_TogglePad1CAssign() { toggleSlotAssign(PADASSIGN_INPUT1C); }
void Func_TogglePad1DAssign() { toggleSlotAssign(PADASSIGN_INPUT1D); }

void Func_ReturnFromConfigureInputFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_INPUT);
}
