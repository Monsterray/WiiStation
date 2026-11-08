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
#include "MenuPageView.h"
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

void Func_ReturnFromCurrentRomFrame();
void Func_SetPlayGame();

namespace eng = menu::engine;

static menu::glue::ActionMenu *curMenu;

CurrentRomFrame::CurrentRomFrame()
{
	actions = new menu::glue::ActionMenu(this, CUR_MENU, Func_ReturnFromCurrentRomFrame);
	curMenu = actions;
	setDefaultFocus(actions->button(0));
	setBackFunc(Func_ReturnFromCurrentRomFrame);
	setEnabled(true);
}

CurrentRomFrame::~CurrentRomFrame()
{
	curMenu = NULL;
	delete actions;
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
	explorerOpen(FileBrowserFrame::FILEBROWSER_SWAPCD);
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
	char text[24];
	snprintf(text, sizeof text, "Slot %u%s", which_slot, StateExists(which_slot) ? "*" : "");
	if (curMenu)
		curMenu->setText(CUR_SLOT_BUTTON, text);
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
