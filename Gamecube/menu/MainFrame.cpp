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
#include "SettingsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/Gui.h"
#include "../libgui/InputStatusBar.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
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

void Func_LoadROM();
void Func_CurrentROM();
void Func_Settings();
void Func_Credits();
void Func_ExitToLoader();
void Func_PlayGame();

static const int NUM_MAIN_BUTTONS = 6;

static char mainStrings[7][20] =
	{ "Load ISO",
	  "Current ISO",
	  "Settings",
	  "Credits",
	  "Quit",
	  "Play Game",
	  "Resume Game"};

namespace eng = menu::engine;

struct MainButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
};

static MainButtonInfo mainButtons[NUM_MAIN_BUTTONS] =
{	//	button	buttonStyle	buttonString		clickFunc			returnFunc
	{	NULL,	BTN_A_NRM,	mainStrings[0],	Func_LoadROM,		NULL },
	{	NULL,	BTN_A_NRM,	mainStrings[1],	Func_CurrentROM,	NULL },
	{	NULL,	BTN_A_NRM,	mainStrings[2],	Func_Settings,		NULL },
	{	NULL,	BTN_A_NRM,	mainStrings[3],	Func_Credits,		NULL },
	{	NULL,	BTN_A_NRM,	mainStrings[4],	Func_ExitToLoader,	NULL },
	{	NULL,	BTN_A_NRM,	mainStrings[5],	Func_PlayGame,		NULL },
};

/* The Main menu page: six rows, one action button each. */
static eng::ActionButton MAIN_ACTIONS[NUM_MAIN_BUTTONS];
static eng::Row MAIN_ROWS[NUM_MAIN_BUTTONS];
static eng::Page MAIN_PAGE;
static eng::Layout MAIN_LAYOUT;

static int mainWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int mainHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}

static const eng::Metrics MAIN_METRICS = { mainWidth, mainHeight, NULL };

static void layoutMain(void)
{
	for (int i = 0; i < NUM_MAIN_BUTTONS; i++)
	{
		MAIN_ACTIONS[i].text  = mainButtons[i].buttonString;	/* Play/Resume changes this text */
		MAIN_ACTIONS[i].width = 200.0f;
		MAIN_ACTIONS[i].lit   = false;
		MAIN_ACTIONS[i].dy    = 0.0f;

		eng::Row &row = MAIN_ROWS[i];
		memset(&row, 0, sizeof row);
		row.kind    = eng::ROW_BUTTONS;
		row.count   = 1;
		row.actions = &MAIN_ACTIONS[i];
		row.x0      = 315.0f;
	}
	memset(&MAIN_PAGE, 0, sizeof MAIN_PAGE);
	MAIN_PAGE.rows  = MAIN_ROWS;
	MAIN_PAGE.nRows = NUM_MAIN_BUTTONS;
	MAIN_PAGE.logo  = eng::LOGO_SPOT_TOP;
	MAIN_PAGE.focus = eng::FOCUS_ROWS;
	MAIN_PAGE.rowY0 = 60.0f;

	eng::layoutPage(MAIN_PAGE, MAIN_METRICS, MAIN_LAYOUT);
	eng::linkFocus(MAIN_PAGE, MAIN_LAYOUT);
}

MainFrame::MainFrame()
{
	inputStatusBar = new menu::InputStatusBar(450,100);
	add(inputStatusBar);

	layoutMain();

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its row. */
	for (int i = 0; i < NUM_MAIN_BUTTONS; i++)
	{
		const eng::Control &c = MAIN_LAYOUT.c[MAIN_LAYOUT.slot[i][0]];
		mainButtons[i].button = new menu::Button(mainButtons[i].buttonStyle, &mainButtons[i].buttonString,
										c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}

	/* The engine's links, handed to the buttons. */
	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < NUM_MAIN_BUTTONS; i++)
	{
		const eng::Control &c = MAIN_LAYOUT.c[MAIN_LAYOUT.slot[i][0]];
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			mainButtons[i].button->setNextFocus(DIRS[d],
				to >= 0 ? mainButtons[MAIN_LAYOUT.c[to].row].button : NULL);
		}
		mainButtons[i].button->setActive(true);
		if (mainButtons[i].clickedFunc) mainButtons[i].button->setClicked(mainButtons[i].clickedFunc);
		if (mainButtons[i].returnFunc) mainButtons[i].button->setReturn(mainButtons[i].returnFunc);
		add(mainButtons[i].button);
		menu::Cursor::getInstance().addComponent(this, mainButtons[i].button, c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}
	setDefaultFocus(mainButtons[0].button);
	setEnabled(true);
}

MainFrame::~MainFrame()
{
	for (int i = 0; i < NUM_MAIN_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, mainButtons[i].button);
		delete mainButtons[i].button;
	}
	delete inputStatusBar;
}

extern MenuContext *pMenuContext;

void Func_LoadROM()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_LOADROM,FileBrowserFrame::FILEBROWSER_LOADISO);
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
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_GENERAL);
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

extern volatile char shutdown;
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
        shutdown = 2;
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

	menu::Gui::getInstance().gfx->clearEFB((GXColor){0, 0, 0, 0xFF}, 0x000000);
	extern bool executingBios;
	executingBios = false;
#ifdef HW_RVL
	pause_netinit_thread();
	pauseRemovalThread();
#endif
	resumeAudio();
	resumeInput();
	menuActive = 0;
#ifdef DEBUGON
	_break();
#endif
	if (originalMode)
		backFromMenu = 1;
	frameLimit[0] = frameLimit[1];
	glResetCacheRegion();
	go();

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
	mainButtons[5].buttonString = mainStrings[6];
	menu::Cursor::getInstance().clearCursorFocus();
}

void Func_SetPlayGame()
{
	mainButtons[5].buttonString = mainStrings[5];
}

void Func_SetResumeGame()
{
	mainButtons[5].buttonString = mainStrings[6];
}
