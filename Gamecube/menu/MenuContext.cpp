/**
 * WiiSX - MenuContext.cpp
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

#include <unistd.h>   /* usleep: the wait for the share */
#include "MenuContext.h"
#include "MenuLayout.h"
#include "../perf_prof.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"

extern bool AutobootBios;      /* GamecubeMain.cpp: autoboot.txt said "BIOS" */
void Func_ExecuteBios();       /* SettingsFrame.cpp */
void Func_LoadFromSD();        /* LoadRomFrame.cpp */
void Func_LoadFromSamba();     /* LoadRomFrame.cpp */
#include "../fileBrowser/smb2dev.h"
extern "C" void glResetCacheRegion(void);
extern int backFromMenu;
extern char originalMode;
#include "../hbc_home.h"
extern BOOL hasLoadedISO;      /* GamecubeMain.cpp */
extern char shutdown;          /* GamecubeMain.cpp: 2 = back to the loader, through Gui.cpp's fade */
extern "C" void SysClose();

MenuContext *pMenuContext;

MenuContext::MenuContext(GXRModeObj *vmode)
		: currentActiveFrame(0),
		  mainFrame(0),
		  loadRomFrame(0),
		  fileBrowserFrame(0),
		  currentRomFrame(0),
		  settingsFrame(0),
		  configureInputFrame(0),
		  configureButtonsFrame(0),
		  optionsFrame(0),
		  customizeCoopFrame(0)
{
	pMenuContext = this;

	menu::Gui::getInstance().setVmode(vmode);

	mainFrame = new MainFrame();
	loadRomFrame = new LoadRomFrame();
	fileBrowserFrame = new FileBrowserFrame();
	currentRomFrame = new CurrentRomFrame();
	settingsFrame = new SettingsFrame();
	configureInputFrame = new ConfigureInputFrame();
	configureButtonsFrame = new ConfigureButtonsFrame();
	optionsFrame = new OptionsFrame();
	customizeCoopFrame = new CustomizeCoopFrame();

	menu::Gui::getInstance().addFrame(mainFrame);
	menu::Gui::getInstance().addFrame(loadRomFrame);
	menu::Gui::getInstance().addFrame(fileBrowserFrame);
	menu::Gui::getInstance().addFrame(currentRomFrame);
	menu::Gui::getInstance().addFrame(settingsFrame);
	menu::Gui::getInstance().addFrame(configureInputFrame);
	menu::Gui::getInstance().addFrame(configureButtonsFrame);
	menu::Gui::getInstance().addFrame(optionsFrame);
	menu::Gui::getInstance().addFrame(customizeCoopFrame);

	menu::Focus::getInstance().setFocusActive(true);

	// Don't show the menu if we autoboot a game.
	if (!strlen(&AutobootROM[0]))
		setActiveFrame(FRAME_MAIN);
}

MenuContext::~MenuContext()
{
	delete customizeCoopFrame;
	delete optionsFrame;
	delete configureButtonsFrame;
	delete configureInputFrame;
	delete settingsFrame;
	delete currentRomFrame;
	delete fileBrowserFrame;
	delete loadRomFrame;
	delete mainFrame;
	pMenuContext = NULL;
}

/* The frames that put something where the logo sits in the top right. On these the logo
 * moves to the bottom right. The reason for each is the widest thing it draws up there;
 * scripts/menu_text_width.py reads this list, measures the frames' own button tables
 * against the logo's box, and fails when a frame gains or loses content there. */
static const int LOGO_BOTTOM_FRAMES[] = {
	MenuContext::FRAME_SETTINGS,          /* the tab strip reaches x=615 */
	MenuContext::FRAME_OPTIONS,           /* a radio row reaches x=634 */
	MenuContext::FRAME_CONFIGUREBUTTONS,  /* one button at (480, 20) */
	MenuContext::FRAME_CURRENTROM,        /* Swap CD reaches x=540 */
};

/* sd:/wiistation/autoinput.txt's "menupage <n>": see PadWiiSX.c, where it is parsed. */
extern "C" {
	void autoinput_load(void);
	void autoinput_reset(const char *path);
	extern unsigned autoinput_menupage;
	extern unsigned autoinput_menuclick, autoinput_menuclicks;
	void SettingsFrame_ScriptClick(int button);
}

static int smbPageWait;   /* menupage 71: frames left to wait for the share */

bool MenuContext::isRunning()
{
	bool isRunning = true;

	/* The Homebrew Channel's agent (hbc_home.c): HOME opens its overlay; an exit asked for
	 * from the PC leaves as Exit to Loader does, the game closed first. */
	if (hbc_home_menu_frame()) {
		if (hasLoadedISO)
			SysClose();
		shutdown = 2;
	}
//	printf("MenuContext isRunning\n");

	/* A few frames in, so that the menu has drawn itself once and the video mode has
	 * settled, open the page the script asked for -- then never again. */
	{
		static int framesUntilMenuPage = 30;
		if (framesUntilMenuPage > 0 && --framesUntilMenuPage == 0) {
			perf_stack_log("menu frame 30");
			/* read the script anew: a read before the SD card was mounted (the controller
			 * assignment at power-on asks for scripted ports) found nothing, and kept that */
			autoinput_reset("sd:/wiistation/autoinput.txt");
			autoinput_load();
			if (autoinput_menupage >= 1 && autoinput_menupage <= 5)
				setActiveFrame(FRAME_SETTINGS,
					SettingsFrame::SUBMENU_GENERAL + (int)autoinput_menupage - 1);
			else if (autoinput_menupage >= 6 && autoinput_menupage <= 10)
				setActiveFrame(FRAME_OPTIONS,
					OptionsFrame::PAGE_SOUND + (int)autoinput_menupage - 6);
			else if (autoinput_menupage == 20)   /* Configure Input */
				setActiveFrame(FRAME_CONFIGUREINPUT, ConfigureInputFrame::SUBMENU_REINIT);
			else if (autoinput_menupage >= 21 && autoinput_menupage <= 36)   /* Customize Co-Op */
				setActiveFrame(FRAME_CUSTOMIZECOOP, (int)autoinput_menupage - 21);
			else if (autoinput_menupage >= 40 && autoinput_menupage <= 65)
				setActiveFrame(FRAME_CONFIGUREBUTTONS, (int)autoinput_menupage - 40);   /* Configure Buttons, by virtual controller */
			else if (autoinput_menupage == 70)
				Func_LoadFromSD();   /* Load ROM > Load from SD: the RomDir folders */
			else if (autoinput_menupage == 71)
				smbPageWait = 60 * 30;   /* Load from SMB, once the share is mounted (below) */
		}
	}

	/* menupage 71: the share mounts a few seconds after the menu comes up (network, then
	 * the SMB session), so wait for it -- at most 30 s -- then open Load from SMB. */
	if (smbPageWait > 0 && (smb2dev_mounted() || --smbPageWait == 0)) {
		smbPageWait = 0;
		Func_LoadFromSamba();
	}

	/* Ten frames after that, press what the script asked for: late enough that the page
	 * has drawn itself once, so the frame dump holds both the before and the after. */
	{
		static int framesUntilClick = 40;
		if (framesUntilClick > 0 && --framesUntilClick == 0 && autoinput_menuclicks) {
			for (unsigned i = 0; i < autoinput_menuclicks; i++)
				SettingsFrame_ScriptClick((int)autoinput_menuclick);
		}
	}

	draw();

/*	PADStatus* gcPad = menu::Input::getInstance().getPad();
	if(gcPad[0].button & PAD_BUTTON_START)
		isRunning = false;*/
	
	return isRunning;
}

void MenuContext::setActiveFrame(int frameIndex)
{
	if(currentActiveFrame)
		currentActiveFrame->hideFrame();

	switch(frameIndex) {
	case FRAME_MAIN:
		currentActiveFrame = mainFrame;
		break;
	case FRAME_LOADROM:
		currentActiveFrame = loadRomFrame;
		break;
	case FRAME_FILEBROWSER:
		currentActiveFrame = fileBrowserFrame;
		break;
	case FRAME_CURRENTROM:
		currentActiveFrame = currentRomFrame;
		break;
	case FRAME_SETTINGS:
		currentActiveFrame = settingsFrame;
		break;
	case FRAME_CONFIGUREINPUT:
		currentActiveFrame = configureInputFrame;
		break;
	case FRAME_CONFIGUREBUTTONS:
		currentActiveFrame = configureButtonsFrame;
		break;
	case FRAME_OPTIONS:
		currentActiveFrame = optionsFrame;
		break;
	case FRAME_CUSTOMIZECOOP:
		currentActiveFrame = customizeCoopFrame;
		break;
	}

	/* The logo goes in the top right, where it has always been, unless this frame puts
	 * something there. Then it goes to the bottom right. */
	{
		bool bottom = false;
		for(unsigned i = 0; i < sizeof(LOGO_BOTTOM_FRAMES)/sizeof(LOGO_BOTTOM_FRAMES[0]); i++)
			if(LOGO_BOTTOM_FRAMES[i] == frameIndex) bottom = true;
		menu::Gui::getInstance().menuLogo->setLocation(
			bottom ? LOGO_PAGE_X : LOGO_MAIN_X,
			bottom ? LOGO_PAGE_Y : LOGO_MAIN_Y, LOGO_Z);
	}

	if(currentActiveFrame)
	{
		currentActiveFrame->showFrame();
		menu::Focus::getInstance().setCurrentFrame(currentActiveFrame);
		menu::Cursor::getInstance().setCurrentFrame(currentActiveFrame);
	}
}

void MenuContext::setActiveFrame(int frameIndex, int submenu)
{
	setActiveFrame(frameIndex);
	if(currentActiveFrame) currentActiveFrame->activateSubmenu(submenu);
}

menu::Frame* MenuContext::getFrame(int frameIndex)
{
	menu::Frame* pFrame = NULL;
	switch(frameIndex) {
	case FRAME_MAIN:
		pFrame = mainFrame;
		break;
	case FRAME_LOADROM:
		pFrame = loadRomFrame;
		break;
	case FRAME_FILEBROWSER:
		pFrame = fileBrowserFrame;
		break;
	case FRAME_CURRENTROM:
		pFrame = currentRomFrame;
		break;
	case FRAME_SETTINGS:
		pFrame = settingsFrame;
		break;
	case FRAME_CONFIGUREINPUT:
		pFrame = configureInputFrame;
		break;
	case FRAME_CONFIGUREBUTTONS:
		pFrame = configureButtonsFrame;
		break;
	case FRAME_OPTIONS:
		pFrame = optionsFrame;
		break;
	case FRAME_CUSTOMIZECOOP:
		pFrame = customizeCoopFrame;
		break;
	}

	return pFrame;
}

void MenuContext::draw()
{
	menu::Gui::getInstance().draw();
}

void MenuContext::Autoboot()
{
	if(AutobootBios)
	{
		/* Same preamble as Func_PlayGame(): one GUI frame so the video output
		 * is set up, a cleared EFB, the TV-mode switch request and a fresh
		 * texture cache. Without it the BIOS runs but the screen stays black. */
		AutobootBios = false;
		menu::Gui::getInstance().draw();
		menu::Gui::getInstance().gfx->clearEFB((GXColor){0, 0, 0, 0xFF}, 0x000000);
		if (originalMode)
			backFromMenu = 1;
		glResetCacheRegion();
		Func_ExecuteBios();
		return;
	}
	if(strcasestr(AutobootPath,"sd:/") != NULL)
		Func_LoadFromSD();
	else if(strcasestr(AutobootPath,"smb:/") != NULL)
	{
		/* a game on the share: wait for the network thread to mount it, at most 30 s */
		for (int i = 0; i < 300 && !smb2dev_mounted(); i++)
			usleep(100 * 1000);
		Func_LoadFromSamba();
	}
	else
		Func_LoadFromUSB();

	fileBrowserFrame_AutoBootFile();
}
