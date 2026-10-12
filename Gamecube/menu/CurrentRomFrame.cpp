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
/*#include "../gc_memory/memory.h"
#include "../gc_memory/Saves.h"
#include "../main/rom.h"
#include "../main/plugin.h"
#include "../main/savestates.h"*/
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

#define NUM_FRAME_BUTTONS 8
#define FRAME_BUTTONS currentRomFrameButtons
#define FRAME_STRINGS currentRomFrameStrings

/* Button Layout:
 * [Restart Game] [Swap CD]
 * [Load MemCard] [Save MemCard]
 * [Show ISO Info]
 * [Load State] [Slot "x"]
 * [Save State]
 */

static char FRAME_STRINGS[8][15] =
	{ "Restart Game",
	  "Swap CD",
	  "Load MemCards",
	  "Save MemCards",
	  "Show ISO Info",
	  "Load State",
	  "Save State",
	  "Slot 0"};

struct ButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =
{ //	button	buttonStyle	buttonString		clickFunc			returnFunc
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[0],	Func_ResetROM,		Func_ReturnFromCurrentRomFrame }, // Reset ROM
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[1],	Func_SwapCD,		Func_ReturnFromCurrentRomFrame }, // Swap CD
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[2],	Func_LoadSave,		Func_ReturnFromCurrentRomFrame }, // Load MemCards
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[3],	Func_SaveGame,		Func_ReturnFromCurrentRomFrame }, // Save MemCards
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[4],	Func_ShowRomInfo,	Func_ReturnFromCurrentRomFrame }, // Show ISO Info
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[5],	Func_LoadState,		Func_ReturnFromCurrentRomFrame }, // Load State
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[6],	Func_SaveState,		Func_ReturnFromCurrentRomFrame }, // Save State
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[7],	Func_StateCycle,	Func_ReturnFromCurrentRomFrame }, // Cycle State
};

/*  The Current ROM page, on the menu engine (Docs/MENU_FRAMEWORK_PLAN.md, step 3)  */

/* Two button columns, kept as the page has always had them: Restart Game and Swap CD on the first
 * row, Load and Save MemCards on the second (each column 210 wide, 20 apart, from x 100 and x 330).
 * Then Show ISO Info across the middle, Load State with the Cycle State button beside it, and Save
 * State below. The Cycle State button sits 30 lower than its row, between two rows (dy). Rows are
 * 60 apart from y 60. The logo is bottom right (MenuContext's LOGO_BOTTOM_FRAMES). Focus is the
 * engine's row rule: Up and Down keep the column, and Left and Right move within a row, except
 * for the four links in CUR_LINKS, which keep Save State and Cycle State together. The layout
 * is fixed, so it is done once, here, and tests/menu_engine_test.cpp checks its numbers. */
namespace eng = menu::engine;

/* The row and slot each FRAME_BUTTONS entry is in. */
static const int CUR_AT[NUM_FRAME_BUTTONS][2] = {
	{ 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 3, 1 },
};
#define CUR_ROWS_N 5

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
static eng::ActionButton CUR_ACTIONS[CUR_ROWS_N][2];
static eng::Row CUR_ROWS[CUR_ROWS_N];
static eng::Page CUR_PAGE;
static eng::Layout CUR_LAYOUT;

/* The FRAME_BUTTONS entry at a row and slot of the layout, or -1. */
static int curButtonAt(int row, int slot)
{
	for (int k = 0; k < NUM_FRAME_BUTTONS; k++)
		if (CUR_AT[k][0] == row && CUR_AT[k][1] == slot)
			return k;
	return -1;
}

/* The engine's text widths: IplFont, as the other frames use it (SettingsFrame.cpp). */
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
		eng::ActionButton &a = CUR_ACTIONS[CUR_AT[k][0]][CUR_AT[k][1]];
		a.text  = FRAME_BUTTONS[k].buttonString;	/* Slot N changes this text in place */
		a.lit   = false;
		a.dy    = (k == 7) ? 30.0f : 0.0f;			/* Cycle State: between its row and the next */
		a.width = (k == 0 || k == 1 || k == 2 || k == 3) ? 210.0f
		        : (k == 4) ? 340.0f
		        : (k == 7) ? 120.0f
		        : 220.0f;
	}

	/* Each row's buttons, in slot order; a row's gap and first x are the page's two columns and
	 * the centred rows' left edges. */
	static const int ROW_COUNT[CUR_ROWS_N]  = { 2, 2, 1, 2, 1 };
	static const float ROW_X0[CUR_ROWS_N]   = { 100.0f, 100.0f, 150.0f, 150.0f, 150.0f };
	static const float ROW_GAP[CUR_ROWS_N]  = { 20.0f, 20.0f, 0.0f, 20.0f, 0.0f };
	for (int r = 0; r < CUR_ROWS_N; r++)
	{
		eng::Row &row = CUR_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = eng::ROW_BUTTONS;
		row.count   = ROW_COUNT[r];
		row.actions = CUR_ACTIONS[r];
		row.x0      = ROW_X0[r];
		row.gap     = ROW_GAP[r];
	}
	CUR_PAGE.title   = NULL;
	CUR_PAGE.tabs    = NULL;
	CUR_PAGE.nTabs   = 0;
	CUR_PAGE.curTab  = 0;
	CUR_PAGE.rows    = CUR_ROWS;
	CUR_PAGE.nRows   = CUR_ROWS_N;
	CUR_PAGE.help    = NULL;
	CUR_PAGE.nHelp   = 0;
	CUR_PAGE.logo    = eng::LOGO_SPOT_BOTTOM;
	CUR_PAGE.focus   = eng::FOCUS_ROWS;
	CUR_PAGE.labelCx = 0.0f;
	CUR_PAGE.labelDy = 0.0f;
	CUR_PAGE.rowDy   = 0.0f;	/* the default pitch, 60 */
	CUR_PAGE.rowY0   = 60.0f;
	CUR_PAGE.overrides  = CUR_LINKS;
	CUR_PAGE.nOverrides = (int)(sizeof CUR_LINKS / sizeof CUR_LINKS[0]);

	/* The tables are fixed: a page that does not fit is a programming error, which the host
	 * test catches before it reaches the menu. */
	eng::layoutPage(CUR_PAGE, CUR_METRICS, CUR_LAYOUT);
	eng::linkFocus(CUR_PAGE, CUR_LAYOUT);
}

CurrentRomFrame::CurrentRomFrame()
{
	layoutCurrentRom();

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its place. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[CUR_AT[i][0]][CUR_AT[i][1]]];
		FRAME_BUTTONS[i].button = new menu::Button(FRAME_BUTTONS[i].buttonStyle, &FRAME_BUTTONS[i].buttonString,
										c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}

	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = CUR_LAYOUT.c[CUR_LAYOUT.slot[CUR_AT[i][0]][CUR_AT[i][1]]];
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			FRAME_BUTTONS[i].button->setNextFocus(DIRS[d],
				to >= 0 ? FRAME_BUTTONS[curButtonAt(CUR_LAYOUT.c[to].row, CUR_LAYOUT.c[to].slot)].button : NULL);
		}
		FRAME_BUTTONS[i].button->setActive(true);
		if (FRAME_BUTTONS[i].clickedFunc) FRAME_BUTTONS[i].button->setClicked(FRAME_BUTTONS[i].clickedFunc);
		if (FRAME_BUTTONS[i].returnFunc) FRAME_BUTTONS[i].button->setReturn(FRAME_BUTTONS[i].returnFunc);
		add(FRAME_BUTTONS[i].button);
		menu::Cursor::getInstance().addComponent(this, FRAME_BUTTONS[i].button, c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}
	setDefaultFocus(FRAME_BUTTONS[0].button);
	setBackFunc(Func_ReturnFromCurrentRomFrame);
	setEnabled(true);
}

CurrentRomFrame::~CurrentRomFrame()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, FRAME_BUTTONS[i].button);
		delete FRAME_BUTTONS[i].button;
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
	sprintf(FRAME_STRINGS[7], "Slot %u%s", which_slot, StateExists(which_slot) ? "*" : "");
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
