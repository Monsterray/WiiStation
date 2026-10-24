/**
 * Wii64 - CurrentRomFrame.cpp
 * Copyright (C) 2009 sepp256
 *
 * Wii64 homepage: http://www.emulatemii.com
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
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "CurrentRomFrame.h"
#include "AchievementsFrame.h"
#include "OptionsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"
#include "../screenshot.h"
#include "MenuGlue.h"
#include "../../achievements.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-CARD.h"
extern int LoadMcd(int mcd, fileBrowser_file *savepath);
extern int SaveMcd(int mcd, fileBrowser_file *savepath);
extern long ISOgetTN(unsigned char *buffer);
void SysPrintf(const char *fmt, ...);	/* the debug layout log (PERF_PROF) */
}

void Func_ShowRomInfo();
void Func_ResetROM();
void Func_SwapCD();
void Func_LoadSave();
void Func_SaveGame();
void Func_LoadState();
void Func_SaveState();
void Func_StateCycle();
void Func_Screenshot();
void Func_ReturnFromCurrentRomFrame();

/* The Current ISO page, one line per button: its label, what A does, and its place: the row
 * and the slot (column) in it, the width, and how far below the row's top (0 = the top).
 * Add, move or rename a button here; the rows, the focus links and the cursor follow. */
struct CurrentRomButton
{
	const char	*label;
	ButtonFunc	clicked;
	int			row, slot;
	float		width, dy;
};

static const CurrentRomButton CUR_BUTTONS[] =
{	//	label				A does					row slot width   dy
	{	"Restart Game",		Func_ResetROM,			0, 0, 230.0f,  0.0f },
	{	"Swap CD",			Func_SwapCD,			0, 1, 230.0f,  0.0f },
	{	"Load MemCards",	Func_LoadSave,			1, 0, 230.0f,  0.0f },
	{	"Save MemCards",	Func_SaveGame,			1, 1, 230.0f,  0.0f },
	{	"Show ISO Info",	Func_ShowRomInfo,		2, 0, 230.0f,  0.0f },
	{	"Achievements",		Func_AchievementsList,	2, 1, 230.0f,  0.0f },
	{	"Load State",		Func_LoadState,			3, 0, 220.0f,  0.0f },
	{	"Slot 0",			Func_StateCycle,		3, 1, 120.0f, 30.0f },
	{	"Save State",		Func_SaveState,			4, 0, 220.0f,  0.0f },
	{	"Screenshot",		Func_Screenshot,		5, 0, 220.0f,  0.0f },
};
static const int NUM_FRAME_BUTTONS = sizeof CUR_BUTTONS / sizeof CUR_BUTTONS[0];
static const int SLOT_BUTTON = 7;		/* "Slot N": its label follows the slot */
static const int CUR_SLOTS_MAX = 2;		/* buttons a row can hold */

/* Each row: the x of its first button, and the gap between its buttons */
struct CurrentRomRow { float x0, gap; };
static const CurrentRomRow CUR_ROW_PLACE[] =
{
	{ 100.0f, 20.0f }, { 100.0f, 20.0f }, { 100.0f, 20.0f }, { 150.0f, 20.0f }, { 150.0f, 0.0f }, { 150.0f, 0.0f },
};
static const int CUR_ROWS_N = sizeof CUR_ROW_PLACE / sizeof CUR_ROW_PLACE[0];

/* The buttons drawn, and their labels: a Button keeps a pointer to its label */
static menu::Button *curButton[NUM_FRAME_BUTTONS];
static char curLabel[NUM_FRAME_BUTTONS][24];
static char *curLabelPtr[NUM_FRAME_BUTTONS];

/* The Current ROM page is set by the engine. The logo is at the bottom right.
 * Up and Down keep the column. The links in CUR_LINKS keep Save State and Cycle State together. */
namespace eng = menu::engine;

/* The four focus links the rule does not give: Swap CD Up and Cycle State Down (the two buttons
 * that sit above and below each other on the right), and Save State Left and Right (Cycle State,
 * its slot selector). Each is applied after the rule, from the button to the target (row, slot). */
static const eng::LinkOverride CUR_LINKS[] = {
	{ 0, 1, eng::DIR_UP,    3, 1 },		/* Swap CD up: Cycle State */
	{ 3, 1, eng::DIR_DOWN,  0, 1 },		/* Cycle State down: Swap CD */
	{ 4, 0, eng::DIR_LEFT,  3, 1 },		/* Save State left: Cycle State */
	{ 4, 0, eng::DIR_RIGHT, 3, 1 },		/* Save State right: Cycle State */
};

/* The buttons of each row, by slot: a row's actions must be consecutive, so they are kept by place. */
static eng::ActionButton CUR_ACTIONS[CUR_ROWS_N][CUR_SLOTS_MAX];
static eng::Row CUR_ROWS[CUR_ROWS_N];
static eng::Page CUR_PAGE;
static eng::Layout CUR_LAYOUT;

/* The CUR_BUTTONS entry at a row and slot of the layout, or -1. */
static int curButtonAt(int row, int slot)
{
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
		if (CUR_BUTTONS[k].row == row && CUR_BUTTONS[k].slot == slot)
			return k;
	return -1;
}

static void layoutCurrentRom(void)
{
	int rowCount[CUR_ROWS_N] = { 0 };
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
	{
		const CurrentRomButton &b = CUR_BUTTONS[k];
		eng::ActionButton &a = CUR_ACTIONS[b.row][b.slot];
		snprintf(curLabel[k], sizeof curLabel[k], "%s", b.label);
		curLabelPtr[k] = curLabel[k];
		a.text  = curLabel[k];	/* Slot N changes this text in place */
		a.lit   = false;
		a.width = b.width;
		a.dy    = b.dy;
		if (b.slot + 1 > rowCount[b.row])
			rowCount[b.row] = b.slot + 1;	/* a row holds slots 0 .. its last one */
	}
	for (int r = 0; r < CUR_ROWS_N; r++)
	{
		eng::Row &row = CUR_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = eng::ROW_BUTTONS;
		row.count   = rowCount[r];
		row.actions = CUR_ACTIONS[r];
		row.x0      = CUR_ROW_PLACE[r].x0;
		row.gap     = CUR_ROW_PLACE[r].gap;
	}
	memset(&CUR_PAGE, 0, sizeof CUR_PAGE);
	CUR_PAGE.rows       = CUR_ROWS;
	CUR_PAGE.nRows      = CUR_ROWS_N;
	CUR_PAGE.logo       = eng::LOGO_SPOT_BOTTOM;
	CUR_PAGE.focus      = eng::FOCUS_ROWS;
	CUR_PAGE.rowY0      = 60.0f;
	CUR_PAGE.overrides  = CUR_LINKS;
	CUR_PAGE.nOverrides = (int)(sizeof CUR_LINKS / sizeof CUR_LINKS[0]);

	eng::layoutPage(CUR_PAGE, menu::glue::METRICS, CUR_LAYOUT);
	eng::linkFocus(CUR_PAGE, CUR_LAYOUT);
#ifdef PERF_PROF
	/* The engine's layout checks, logged once in debug builds (as OptionsFrame does) */
	static bool checked;
	static eng::Problem probs[eng::MAX_PROBLEMS];
	if (!checked)
	{
		checked = true;
		int np = eng::checkLayout(CUR_PAGE, menu::glue::METRICS, CUR_LAYOUT, probs, eng::MAX_PROBLEMS);
		if (np > 0)
			SysPrintf("CurrentRomFrame: %d layout problem(s)\n", np);
		for (int k = 0; k < np && k < eng::MAX_PROBLEMS; k++)
		{
			const eng::Problem &p = probs[k];
			if (p.b >= 0)
				SysPrintf("CurrentRomFrame:   %s: '%s' and '%s'\n", eng::problemName(p.kind),
					CUR_LAYOUT.c[p.a].text, CUR_LAYOUT.c[p.b].text);
			else
				SysPrintf("CurrentRomFrame:   %s: '%s'\n", eng::problemName(p.kind),
					CUR_LAYOUT.c[p.a].text);
		}
	}
#endif
}

/* The button drawn for a layout control. */
static menu::Button *curButtonOf(const eng::Control &c)
{
	return curButton[curButtonAt(c.row, c.slot)];
}

CurrentRomFrame::CurrentRomFrame()
{
	layoutCurrentRom();

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its place. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const CurrentRomButton &b = CUR_BUTTONS[i];
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[b.row][b.slot]];
		curButton[i] = new menu::Button(BTN_A_NRM, &curLabelPtr[i], c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}

	/* The engine's links, handed to the buttons. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const CurrentRomButton &b = CUR_BUTTONS[i];
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[b.row][b.slot]];
		menu::glue::linkFocus(curButton[i], CUR_LAYOUT, c, curButtonOf);
		curButton[i]->setActive(true);
		curButton[i]->setClicked(b.clicked);
		curButton[i]->setReturn(Func_ReturnFromCurrentRomFrame);
		add(curButton[i]);
		menu::Cursor::getInstance().addComponent(this, curButton[i], c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}
	setDefaultFocus(curButton[0]);
	setBackFunc(Func_ReturnFromCurrentRomFrame);
	setEnabled(true);
}

CurrentRomFrame::~CurrentRomFrame()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, curButton[i]);
		delete curButton[i];
	}
}

extern MenuContext *pMenuContext;
extern char CdromId[10];
extern char CdromLabel[33];
extern char debugInfo[256];

/* Show ISO Info: the ISO Info page (OptionsFrame.cpp ISOINFO_ROWS, an engine page of readouts) */
void Func_ShowRomInfo()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_ISOINFO);
}

extern BOOL hasLoadedISO;

extern "C" {
void SysReset();
int SysInit();   /* returns int (system.h, GamecubeMain.cpp); was declared void */
void SysClose();
void CheckCdrom();
void LoadCdrom();
}

void Func_SetPlayGame();

void Func_ResetROM()
{
  SysClose();
	SysInit ();
	CheckCdrom();
  SysReset();
	LoadCdrom();
	menu::MessageBox::getInstance().setMessage("Game restarted");
	Func_SetPlayGame();
}

void Func_SwapCD()
{
	//Call Filebrowser with "Swap CD"
	pMenuContext->setActiveFrame(MenuContext::FRAME_LOADROM,FileBrowserFrame::FILEBROWSER_SWAPCD);
}

extern "C" char mcd1Written;
extern "C" char mcd2Written;
extern "C" int LoadState(int force);
extern "C" int SaveState();
extern "C" void savestates_select_slot(unsigned int s);
extern "C" int StateExists(unsigned int slot);
static void refreshStateSlot();
static unsigned int which_slot = 0;
/* Set whenever the slot, or what is in it, may have changed. drawChildren then opens the
 * file once to see. Without it the label would cost a file open on every frame. */
static bool slotLabelStale = true;

void Func_LoadSave()
{
	if(!hasLoadedISO)
	{
		menu::MessageBox::getInstance().setMessage("Please load a ISO first");
		return;
	}

	setSaveDevice();   /* fileBrowser.c: point saveFile_* at nativeSaveDevice */

  // Try loading everything
  int result = 0;
  saveFile_init(saveFile_dir);
  result += LoadMcd(1,saveFile_dir);
  result += LoadMcd(2,saveFile_dir);
  saveFile_deinit(saveFile_dir);

	switch (nativeSaveDevice)
	{
		case NATIVESAVEDEVICE_SD:
			if (result) menu::MessageBox::getInstance().setMessage("Loaded save from SD card");
			else		menu::MessageBox::getInstance().setMessage("No saves found on SD card");
			break;
		case NATIVESAVEDEVICE_USB:
			if (result) menu::MessageBox::getInstance().setMessage("Loaded save from USB device");
			else		menu::MessageBox::getInstance().setMessage("No saves found on USB device");
			break;
		case NATIVESAVEDEVICE_CARDA:
			if (result) menu::MessageBox::getInstance().setMessage("Loaded save from memcard in slot A");
			else		menu::MessageBox::getInstance().setMessage("No saves found on memcard A");
			break;
		case NATIVESAVEDEVICE_CARDB:
			if (result) menu::MessageBox::getInstance().setMessage("Loaded save from memcard in slot A");
			else		menu::MessageBox::getInstance().setMessage("No saves found on memcard B");
			break;
	}
	mcd1Written = mcd2Written = false;
}

void Func_SaveGame()
{
  if(!mcd1Written && !mcd2Written) {
    menu::MessageBox::getInstance().setMessage("Nothing to save");
    return;
  }
	setSaveDevice();   /* fileBrowser.c: point saveFile_* at nativeSaveDevice */

	// Try saving everything
	int amountSaves = mcd1Written + mcd2Written;
	int result = 0;
  saveFile_init(saveFile_dir);
  result += SaveMcd(1,saveFile_dir);
  result += SaveMcd(2,saveFile_dir);
  saveFile_deinit(saveFile_dir);

	if (result>=amountSaves) {
		switch (nativeSaveDevice)
		{
			case NATIVESAVEDEVICE_SD:
				menu::MessageBox::getInstance().setMessage("Saved game to SD card");
				break;
			case NATIVESAVEDEVICE_USB:
				menu::MessageBox::getInstance().setMessage("Saved game to USB device");
				break;
			case NATIVESAVEDEVICE_CARDA:
				menu::MessageBox::getInstance().setMessage("Saved game to memcard in Slot A");
				break;
			case NATIVESAVEDEVICE_CARDB:
				menu::MessageBox::getInstance().setMessage("Saved game to memcard in Slot B");
				break;
		}
		mcd1Written = mcd2Written = false;
	}
	else		menu::MessageBox::getInstance().setMessage("Failed to Save");

}

void Func_LoadState()
{
    int status = LoadState(0);
    if (status == -1) {
        /* The file is from another build, or it is cut short. Neither is safe to load, but
         * the alternative is losing the state, so let the player decide. What comes back
         * may not run; Restart Game is next to this button. */
        if (menu::MessageBox::getInstance().askMessage(
                "This state is damaged or from another build. Load it anyway?"))
            status = LoadState(1);
        else
            return;
    }
    if (status == 1) {
        menu::MessageBox::getInstance().setMessage("Save State Loaded Successfully");
    } else if (status == -1) {
        menu::MessageBox::getInstance().setMessage("The state could not be read at all");
    } else {
        menu::MessageBox::getInstance().setMessage("Save doesn't exist");
    }
}

void Func_SaveState()
{
  if(SaveState()) {
    menu::MessageBox::getInstance().setMessage("Save State Saved Successfully");
  } else {
    menu::MessageBox::getInstance().setMessage("Error Saving State");
  }
  slotLabelStale = true;
}

/* "Slot 3", or "Slot 3 *" when that slot holds a state this build can read. */
static void refreshStateSlot()
{
	snprintf(curLabel[SLOT_BUTTON], sizeof curLabel[SLOT_BUTTON], "Slot %u%s", which_slot, StateExists(which_slot) ? "*" : "");
	slotLabelStale = false;
}

void CurrentRomFrame::drawChildren(menu::Graphics& gfx)
{
	if (!isVisible()) return;
	if (slotLabelStale) refreshStateSlot();
	menu::Frame::drawChildren(gfx);
}

void Func_StateCycle()
{
	which_slot = (which_slot+1) %10;
	savestates_select_slot(which_slot);
	slotLabelStale = true;
}

/* Saves the picture on screen to screenshots/ on the SD card (screenshot.c) */
void Func_Screenshot()
{
	char file[40];
	char message[80];

	if (screenshot_save(file, sizeof file) == 0)
		sprintf(message, "Screenshot saved as screenshots/%s", file);
	else
		strcpy(message, "The screenshot could not be saved");
	menu::MessageBox::getInstance().setMessage(message);
}

void Func_ReturnFromCurrentRomFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}
