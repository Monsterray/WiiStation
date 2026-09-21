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

#include "MenuContext.h"
#include "MenuLayout.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"

extern bool AutobootBios;      /* GamecubeMain.cpp: autoboot.txt said "BIOS" */
void Func_ExecuteBios();       /* SettingsFrame.cpp */
extern "C" void glResetCacheRegion(void);
extern int backFromMenu;
extern char originalMode;

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
		  optionsFrame(0)
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

	menu::Gui::getInstance().addFrame(mainFrame);
	menu::Gui::getInstance().addFrame(loadRomFrame);
	menu::Gui::getInstance().addFrame(fileBrowserFrame);
	menu::Gui::getInstance().addFrame(currentRomFrame);
	menu::Gui::getInstance().addFrame(settingsFrame);
	menu::Gui::getInstance().addFrame(configureInputFrame);
	menu::Gui::getInstance().addFrame(configureButtonsFrame);
	menu::Gui::getInstance().addFrame(optionsFrame);

	menu::Focus::getInstance().setFocusActive(true);

	// Don't show the menu if we autoboot a game.
	if (!strlen(&AutobootROM[0]))
		setActiveFrame(FRAME_MAIN);
}

MenuContext::~MenuContext()
{
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

/* sd:/wiisxrx/autoinput.txt's "menupage <n>": see PadWiiSX.c, where it is parsed. */
extern "C" {
	void autoinput_load(void);
	extern unsigned autoinput_menupage;
}

bool MenuContext::isRunning()
{
	bool isRunning = true;
//	printf("MenuContext isRunning\n");

	/* A few frames in, so that the menu has drawn itself once and the video mode has
	 * settled, open the page the script asked for -- then never again. */
	{
		static int framesUntilMenuPage = 30;
		if (framesUntilMenuPage > 0 && --framesUntilMenuPage == 0) {
			autoinput_load();
			if (autoinput_menupage >= 1 && autoinput_menupage <= 5)
				setActiveFrame(FRAME_SETTINGS,
					SettingsFrame::SUBMENU_GENERAL + (int)autoinput_menupage - 1);
			else if (autoinput_menupage >= 6 && autoinput_menupage <= 9)
				setActiveFrame(FRAME_OPTIONS,
					OptionsFrame::PAGE_SOUND + (int)autoinput_menupage - 6);
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
	}

	/* The logo is drawn over every frame, so where it can sit depends on the frame. The
	 * main screen's top right is empty and that is where it has always been; the Settings
	 * tabs put a tab button there, so on those it moves to the corner MenuLayout.h reserves
	 * for it. x and y are the middle of the logo. */
	if(frameIndex == FRAME_MAIN)
		menu::Gui::getInstance().menuLogo->setLocation(LOGO_MAIN_X, LOGO_MAIN_Y, LOGO_Z);
	else
		menu::Gui::getInstance().menuLogo->setLocation(LOGO_PAGE_X, LOGO_PAGE_Y, LOGO_Z);

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
	else
		Func_LoadFromUSB();

	fileBrowserFrame_AutoBootFile();
}
