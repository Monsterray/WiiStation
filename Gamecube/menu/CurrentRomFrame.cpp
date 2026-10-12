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
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-CARD.h"
extern int LoadMcd(int mcd, fileBrowser_file *savepath);
extern int SaveMcd(int mcd, fileBrowser_file *savepath);
extern long ISOgetTN(unsigned char *buffer);
}

void Func_ShowRomInfo();
void Func_ResetROM();
void Func_SwapCD();
void Func_LoadSave();
void Func_SaveGame();
void Func_LoadState();
void Func_SaveState();
void Func_StateCycle();
void Func_ReturnFromCurrentRomFrame();

static const int NUM_FRAME_BUTTONS = 8;
static const int CUR_ROWS_N = 5;

static char currentRomStrings[8][15] =
	{ "Restart Game",
	  "Swap CD",
	  "Load MemCards",
	  "Save MemCards",
	  "Show ISO Info",
	  "Load State",
	  "Save State",
	  "Slot 0"};

struct CurrentRomButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
	int				row, slot;	/* the place on the page */
	float			width;		/* the engine's action button */
	float			dy;			/* below its row's top; 0 = the row's top */
};

static CurrentRomButtonInfo currentRomButtons[NUM_FRAME_BUTTONS] =
{	//	button	style		string					clickFunc			returnFunc						row slot width   dy
	{	NULL,	BTN_A_NRM,	currentRomStrings[0],	Func_ResetROM,		Func_ReturnFromCurrentRomFrame,	0, 0, 210.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[1],	Func_SwapCD,		Func_ReturnFromCurrentRomFrame,	0, 1, 210.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[2],	Func_LoadSave,		Func_ReturnFromCurrentRomFrame,	1, 0, 210.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[3],	Func_SaveGame,		Func_ReturnFromCurrentRomFrame,	1, 1, 210.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[4],	Func_ShowRomInfo,	Func_ReturnFromCurrentRomFrame,	2, 0, 340.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[5],	Func_LoadState,		Func_ReturnFromCurrentRomFrame,	3, 0, 220.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[6],	Func_SaveState,		Func_ReturnFromCurrentRomFrame,	4, 0, 220.0f,  0.0f },
	{	NULL,	BTN_A_NRM,	currentRomStrings[7],	Func_StateCycle,	Func_ReturnFromCurrentRomFrame,	3, 1, 120.0f, 30.0f },
};

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

/* Each row has a button count, the x of its first button, and the gap between its buttons. */
static const int   CUR_ROW_COUNT[CUR_ROWS_N] = { 2, 2, 1, 2, 1 };
static const float CUR_ROW_X0[CUR_ROWS_N]    = { 100.0f, 100.0f, 150.0f, 150.0f, 150.0f };
static const float CUR_ROW_GAP[CUR_ROWS_N]   = { 20.0f, 20.0f, 0.0f, 20.0f, 0.0f };

/* The buttons of each row, by slot: a row's actions must be consecutive, so they are kept by place. */
static eng::ActionButton CUR_ACTIONS[CUR_ROWS_N][2];
static eng::Row CUR_ROWS[CUR_ROWS_N];
static eng::Page CUR_PAGE;
static eng::Layout CUR_LAYOUT;

/* The currentRomButtons entry at a row and slot of the layout, or -1. */
static int curButtonAt(int row, int slot)
{
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
		if (currentRomButtons[k].row == row && currentRomButtons[k].slot == slot)
			return k;
	return -1;
}

static int curWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int curHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}

static const eng::Metrics CUR_METRICS = { curWidth, curHeight, NULL };

static void layoutCurrentRom(void)
{
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
	{
		const CurrentRomButtonInfo &b = currentRomButtons[k];
		eng::ActionButton &a = CUR_ACTIONS[b.row][b.slot];
		a.text  = b.buttonString;	/* Slot N changes this text in place */
		a.lit   = false;
		a.width = b.width;
		a.dy    = b.dy;
	}
	for (int r = 0; r < CUR_ROWS_N; r++)
	{
		eng::Row &row = CUR_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = eng::ROW_BUTTONS;
		row.count   = CUR_ROW_COUNT[r];
		row.actions = CUR_ACTIONS[r];
		row.x0      = CUR_ROW_X0[r];
		row.gap     = CUR_ROW_GAP[r];
	}
	memset(&CUR_PAGE, 0, sizeof CUR_PAGE);
	CUR_PAGE.rows       = CUR_ROWS;
	CUR_PAGE.nRows      = CUR_ROWS_N;
	CUR_PAGE.logo       = eng::LOGO_SPOT_BOTTOM;
	CUR_PAGE.focus      = eng::FOCUS_ROWS;
	CUR_PAGE.rowY0      = 60.0f;
	CUR_PAGE.overrides  = CUR_LINKS;
	CUR_PAGE.nOverrides = (int)(sizeof CUR_LINKS / sizeof CUR_LINKS[0]);

	eng::layoutPage(CUR_PAGE, CUR_METRICS, CUR_LAYOUT);
	eng::linkFocus(CUR_PAGE, CUR_LAYOUT);
}

CurrentRomFrame::CurrentRomFrame()
{
	layoutCurrentRom();

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its place. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		CurrentRomButtonInfo &b = currentRomButtons[i];
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[b.row][b.slot]];
		b.button = new menu::Button(b.buttonStyle, &b.buttonString,
										c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}

	/* The engine's links, handed to the buttons. */
	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		CurrentRomButtonInfo &b = currentRomButtons[i];
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[b.row][b.slot]];
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			b.button->setNextFocus(DIRS[d],
				to >= 0 ? currentRomButtons[curButtonAt(CUR_LAYOUT.c[to].row, CUR_LAYOUT.c[to].slot)].button : NULL);
		}
		b.button->setActive(true);
		if (b.clickedFunc) b.button->setClicked(b.clickedFunc);
		if (b.returnFunc) b.button->setReturn(b.returnFunc);
		add(b.button);
		menu::Cursor::getInstance().addComponent(this, b.button, c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}
	setDefaultFocus(currentRomButtons[0].button);
	setBackFunc(Func_ReturnFromCurrentRomFrame);
	setEnabled(true);
}

CurrentRomFrame::~CurrentRomFrame()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, currentRomButtons[i].button);
		delete currentRomButtons[i].button;
	}
}

extern MenuContext *pMenuContext;
extern char CdromId[10];
extern char CdromLabel[33];
extern char debugInfo[256];

// Appends src to dst without overflowing dst's declared size. Used to build
// up RomInfo from several independently-gated pieces whose combined length
// isn't bounded by any single piece's fixed size.
static void appendBounded(char* dst, size_t dstSize, const char* src)
{
	size_t used = strlen(dst);
	if (used < dstSize - 1)
		strncat(dst, src, dstSize - used - 1);
}

void Func_ShowRomInfo()
{
	char RomInfo[256] = "";
	char buffer [50];

	sprintf(buffer,"CD-ROM Label: %s\n",CdromLabel);
  appendBounded(RomInfo, sizeof(RomInfo), buffer);
  sprintf(buffer,"CD-ROM ID: %s\n", CdromId);

	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  if (Config.hacks.gpu_slow_list_walking)
  {
  	sprintf(buffer, "GpuSlowListWalking auto fixed\n");
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }
  if (Config.cycle_multiplier_override)
  {
  	sprintf(buffer, "CycleMultiplierOverride fixed\n");
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }
  if (Config.hacks.gpu_busy_hack)
  {
  	sprintf(buffer, "GPU 'Fake Busy States' hacked\n");
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }
  if (Config.hacks.dwActFixes)
  {
  	sprintf(buffer, "Special game auto fixed %08lx\n", Config.hacks.dwActFixes);
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }
  if (Config.hacks.lightrec_hacks)
  {
  	sprintf(buffer, "Applied Lightrec hacks\n");
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }
  if (Config.pR3000Fix)
  {
  	sprintf(buffer, "pR3000 auto fixed\n");
  	appendBounded(RomInfo, sizeof(RomInfo), buffer);
  }

  /* Under 1 MB in KB (rounded up), so a small image does not read "0 MB" */
  if (isoFile.size < 1024 * 1024)
    sprintf(buffer, "ISO Size: %u KB\n", (isoFile.size + 1023) / 1024);
  else
    sprintf(buffer, "ISO Size: %u MB\n", isoFile.size / 1024 / 1024);
  appendBounded(RomInfo, sizeof(RomInfo), buffer);
  sprintf(buffer,"Country: %s\n",(!Config.PsxType) ? "NTSC":"PAL");
  appendBounded(RomInfo, sizeof(RomInfo), buffer);
  sprintf(buffer,"BIOS: %s\n",(Config.HLE==BIOS_USER_DEFINED) ? "PSX":"HLE");
  appendBounded(RomInfo, sizeof(RomInfo), buffer);
  unsigned char tracks[2];
  ISOgetTN(&tracks[0]);
  sprintf(buffer,"Number of tracks %u\n", tracks[1]);
	appendBounded(RomInfo, sizeof(RomInfo), buffer);

	menu::MessageBox::getInstance().setMessage(RomInfo);
}

extern BOOL hasLoadedISO;

extern "C" {
void SysReset();
void SysInit();
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
	sprintf(currentRomStrings[7], "Slot %u%s", which_slot, StateExists(which_slot) ? "*" : "");
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

void Func_ReturnFromCurrentRomFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}
