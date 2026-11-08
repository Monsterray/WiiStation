/**
 * WiiSX - MainFrame.cpp
 * Copyright (C) 2009 sepp256
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
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "../version.h"
#include "MainFrame.h"
#include "MenuPageView.h"
#include "SettingsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/Gui.h"
#include "../libgui/InputStatusBar.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "../../achievements.h"
#include <ogc/lwp_watchdog.h>
#include "MenuGlue.h"
#ifdef DEBUGON
# include <debug.h>
#endif
#ifdef SHOW_DEBUG
#include "../DEBUG.h"
#endif // SHOW_DEBUG
extern "C" {
#ifdef WII
#include <di/di.h>
#endif
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-CARD.h"
#include "../fileBrowser/fileBrowser-SMB.h"
#include "../../deps/opengx/GL/gl.h"
}
#include <ogc/dvd.h>
#include "../../HidController/KernelHID.h"

static void Func_BackToGame();
extern BOOL hasLoadedISO;

namespace eng = menu::engine;

/* The Play button says Play Game, or Resume Game once a game has been played and the menu was opened
 * from it. The menu (the page's buttons) exists once the frame is made. */
static menu::glue::ActionMenu *mainMenu;
static bool resumeLabel;

static void setPlayLabel(bool resume)
{
	resumeLabel = resume;
	if (mainMenu)
		mainMenu->setText(MAIN_PLAY, resume ? "Resume Game" : "Play Game");
}

MainFrame::MainFrame()
{
	inputStatusBar = new menu::InputStatusBar(450,100);
	add(inputStatusBar);

	actions = new menu::glue::ActionMenu(this, MAIN_MENU, Func_BackToGame);
	mainMenu = actions;
	setPlayLabel(resumeLabel);
	setDefaultFocus(actions->button(0));
	setBackFunc(Func_BackToGame);	/* B with the pointer on no button */
	setEnabled(true);
}

MainFrame::~MainFrame()
{
	mainMenu = NULL;
	delete actions;
	delete inputStatusBar;
}

extern MenuContext *pMenuContext;

void Func_LoadROM()
{
	explorerOpen(FileBrowserFrame::FILEBROWSER_LOADISO);   /* the explorer, on the last source used */
}

extern BOOL hasLoadedISO;

void Func_CurrentROM()
{
	if(!hasLoadedISO)
	{
		menu::MessageBox::getInstance().setMessage("Please load an ISO first");
		return;
	}

	pMenuContext->setActiveFrame(MenuContext::FRAME_CURRENTROM);
}

void Func_Settings()
{
	menu::Gui::getInstance().menuLogo->setLocation(580.0, 410.0, -50.0);
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_VIDEO);
}

void Func_Players()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_PLAYERS, 0);   /* the two-argument form runs activateSubmenu: it starts the Mii reader */
}

void Func_Credits()
{
	char CreditsInfo[512] = "";
#ifdef HW_RVL
	sprintf(CreditsInfo,"WiiStation " WIISTATION_VERSION "\n");
#else
	sprintf(CreditsInfo,"CubeStation " WIISTATION_VERSION "\n");
#endif
	strcat(CreditsInfo,"www.github.com/xjsxjs197/WiiSXRX_2022\n");
	strcat(CreditsInfo,"WiiStation: xjsxjs197 - Icon: Dakangel\n");
	strcat(CreditsInfo,"- Original WiiSX team: -\n");
	strcat(CreditsInfo,"emu_kidid - general coding\n");
	strcat(CreditsInfo,"sepp256 - graphics & menu\n");
	strcat(CreditsInfo,"tehpola - audio\n");
	strcat(CreditsInfo,"PCSX/-df/-R/-Revolution/-ReARMed\n");
	strcat(CreditsInfo,"teams\n");
	strcat(CreditsInfo,"pcercuei - Lightrec\n");

	char wiiDetails[30];
	char wiiInfo[20];
#ifdef HW_RVL
	if(!IsWiiU()) {
		sprintf(wiiInfo, "Wii (729 MHz)");
	}
	else if(IsWiiUFastCPU()) {
		sprintf(wiiInfo, "vWii (1.215 GHz)");
	}
	else {
		sprintf(wiiInfo, "vWii (729 MHz)");
	}
	sprintf(wiiDetails, "IOS: %d / %s", IOS_GetVersion(), wiiInfo);
#endif
#ifdef HW_RVL
	strcat(CreditsInfo,"FIX94 - Wii U GamePad support\n");
	strcat(CreditsInfo,"matguitarist - USB 2.0 support\n");
	strcat(CreditsInfo,"Daxtsu - libwupc support\n");
	strcat(CreditsInfo,"NiuuS - WiiSXRX fork\n");
	strcat(CreditsInfo,"\n");
	strcat(CreditsInfo, wiiDetails);
#endif

	menu::MessageBox::getInstance().setMessage(CreditsInfo);
}

extern volatile char ws_shutdown;
extern "C" void SysClose();

/* Quit, after the question (also an input script's "menupage 90", MenuContext.cpp) */
extern "C" void quit_crumb(const char *fmt, ...);   /* Gui.cpp: the Quit log */
extern "C" void quit_log_begin(const char *how);

void ExitToLoaderNow()
{
        quit_log_begin(hasLoadedISO ? "Quit, a game loaded" : "Quit, no game");
        #ifdef HW_RVL
        HIDClose(0);
        quit_crumb("HIDClose(0) done");
        #endif
        ws_shutdown = 2;
        if (hasLoadedISO)
        {
            SysClose();
            quit_crumb("SysClose done");
        }
}

void Func_ExitToLoader()
{
    if (menu::MessageBox::getInstance().askMessage("Are you sure you want to exit to loader?"))
        ExitToLoaderNow();
}

extern "C" {
void cpu_init();
void cpu_deinit();
}

extern "C" {
extern int SaveMcd(int mcd, fileBrowser_file *savepath);
void pauseAudio(void);  void pauseInput(void);
void resumeAudio(void); void resumeInput(void);
void go(void);
}


extern int backFromMenu;
extern char menuActive;
extern char autoSave;
extern "C" char mcd1Written;
extern "C" char mcd2Written;
extern "C" unsigned int usleep(unsigned int us);

extern "C" void switchToTVMode(short dWidth, short dHeight, bool retMenu);
extern "C" void gxApplyEfbFormat(int game);

void Func_PlayGame()
{
	if(!hasLoadedISO)
	{
		menu::MessageBox::getInstance().setMessage("Please load an ISO first");
		return;
	}

	//Wait until 'A' button released before play/resume game
	menu::Cursor::getInstance().setFreezeAction(true);
	menu::Focus::getInstance().setFreezeAction(true);
	int buttonHeld = 1;
	while(buttonHeld)
	{
		buttonHeld = 0;
		menu::Gui::getInstance().draw();
		for (int i=0; i<4; i++)
		{
			if(PAD_ButtonsHeld(i) & PAD_BUTTON_A) buttonHeld++;
#ifdef HW_RVL
			WPADData* wiiPad = WPAD_Data(i);
			if(wiiPad->err == WPAD_ERR_NONE && wiiPad->btns_h & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) buttonHeld++;

			WUPCData* data = WUPC_Data(i);
			if (data != NULL && data->button & WPAD_CLASSIC_BUTTON_A) buttonHeld++;

			if(i == 0)
			{
				const WiiDRCData* data = WiiDRC_Data();
				if (data != NULL && data->button & WIIDRC_BUTTON_A) buttonHeld++;
			}
#endif
		}
	}
	menu::Cursor::getInstance().setFreezeAction(false);
	menu::Focus::getInstance().setFreezeAction(false);

	menu::Gui::getInstance().gfx->clearEFB((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);   /* far: OpenGX reads depth as bit 15 = 0 there (STP_BIT15.md) */
	extern bool executingBios;
	executingBios = false;
#ifdef HW_RVL
	pause_netinit_thread();
	pauseRemovalThread();
#endif
	if (achievements_loading()) {   /* Official: the set's parse, here and not in the game's first seconds */
		u64 end = gettime() + millisecs_to_ticks(8000);
		menu::MessageBox::getInstance().fadeMessage("Loading achievements...");
		while (achievements_loading() && gettime() < end) {
			achievements_idle();
			menu::Gui::getInstance().draw();
		}
	}
	resumeAudio();
	resumeInput();
	menuActive = 0;
#ifdef DEBUGON
	_break();
#endif
	if (originalMode)
		backFromMenu = 1;
	syncFrameLimit();
	glResetCacheRegion();
	gxApplyEfbFormat(1);	/* GxColor: the game's EFB format (GraphicsGX.cpp) */
	go();
	gxApplyEfbFormat(0);

#ifdef DEBUGON
	_break();
#endif
	menuActive = 1;
	pauseInput();
	pauseAudio();

	if (originalMode)
		switchToTVMode(640, 480, 1);


  if(autoSave==AUTOSAVE_ENABLE) {
    if(mcd1Written || mcd2Written) {  //something needs saving
      setSaveDevice();   /* fileBrowser.c: point saveFile_* at nativeSaveDevice */
      // Try saving everything
    	int amountSaves = mcd1Written + mcd2Written;
    	int result = 0;
      saveFile_init(saveFile_dir);
      result += SaveMcd(1,saveFile_dir);
      result += SaveMcd(2,saveFile_dir);
      saveFile_deinit(saveFile_dir);
    	if (result>=amountSaves) {  //saved all of them ok
    		switch (nativeSaveDevice)
    		{
    			case NATIVESAVEDEVICE_SD:
    				menu::MessageBox::getInstance().fadeMessage("Automatically saved to SD card");
    				break;
    			case NATIVESAVEDEVICE_USB:
    				menu::MessageBox::getInstance().fadeMessage("Automatically saved to USB device");
    				break;
    			case NATIVESAVEDEVICE_CARDA:
    				menu::MessageBox::getInstance().fadeMessage("Automatically saved to memcard in Slot A");
    				break;
    			case NATIVESAVEDEVICE_CARDB:
    				menu::MessageBox::getInstance().fadeMessage("Automatically saved to memcard in Slot B");
    				break;
    		}
    		mcd1Written = mcd2Written = 0;  //nothing new written since save
  		}
  	  else		{
  	    menu::MessageBox::getInstance().setMessage("Failed to save game"); //one or more failed to save
	    }

    }
  }
#ifdef HW_RVL
  continueRemovalThread();
  resume_netinit_thread();
#endif
	setPlayLabel(true);
	menu::Cursor::getInstance().clearCursorFocus();
}

void Func_SetPlayGame()
{
	setPlayLabel(false);
}

void Func_SetResumeGame()
{
	setPlayLabel(true);
}

/* A game is loaded and has been played: the Play button says Resume (Func_PlayGame sets it
 * once the game stops for the menu, Func_SetPlayGame when another one is loaded). */
bool MainFrame_gameStarted()
{
	return hasLoadedISO && resumeLabel;
}

/* B on the main menu: back into the game, as the Resume button does. With no game started, B
 * does nothing, as before. */
static void Func_BackToGame()
{
	if (MainFrame_gameStarted())
		Func_PlayGame();
}
