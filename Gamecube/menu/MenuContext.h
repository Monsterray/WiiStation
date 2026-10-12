/**
 * WiiSX - MenuContext.h
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

#ifndef MENUCONTEXT_H
#define MENUCONTEXT_H

#include "../libgui/Gui.h"
#include "../libgui/Frame.h"
#include "../libgui/InputManager.h"
#include "MainFrame.h"
#include "LoadRomFrame.h"
#include "FileBrowserFrame.h"
#include "CurrentRomFrame.h"
#include "SettingsFrame.h"
#include "ConfigureInputFrame.h"
#include "ConfigureButtonsFrame.h"
#include "CustomizeCoopFrame.h"
#include "OptionsFrame.h"

#include "MenuTypes.h"

class MenuContext
{
public:
	explicit MenuContext(GXRModeObj *vmode);
	~MenuContext();
	bool isRunning();
	void Autoboot();
	void setActiveFrame(int frameIndex);
	void setActiveFrame(int frameIndex, int submenu);
	menu::Frame* getFrame(int frameIndex);
	enum FrameIndices
	{
		FRAME_MAIN=1,
		FRAME_LOADROM,
		FRAME_FILEBROWSER,
		FRAME_CURRENTROM,
		FRAME_SETTINGS,
		FRAME_CONFIGUREINPUT,
		FRAME_CONFIGUREBUTTONS,
		FRAME_OPTIONS,
		FRAME_CUSTOMIZECOOP
	};

private:
	void draw();
	void enterFrame(int frameIndex, int submenu);
	menu::Frame *currentActiveFrame;
	MainFrame *mainFrame;
	LoadRomFrame *loadRomFrame;
	FileBrowserFrame *fileBrowserFrame;
	CurrentRomFrame *currentRomFrame;
	SettingsFrame *settingsFrame;
	ConfigureInputFrame *configureInputFrame;
	ConfigureButtonsFrame *configureButtonsFrame;
	OptionsFrame *optionsFrame;
	CustomizeCoopFrame *customizeCoopFrame;

	/* Where focus goes on a Back: the button a screen was left by, and the submenu (tab) it
	 * was left from. Set when the screen is left by a press; see enterFrame().
	 * ponytail: one slot per screen, so a screen left from two tabs remembers only the last one.
	 * A table per tab is the upgrade if both tabs must return where they were. */
	struct FocusMemory
	{
		menu::Component *button;
		int submenu;
	};
	FocusMemory focusMemory[FRAME_CUSTOMIZECOOP + 1];
	int activeIndex, activeSubmenu;

};

extern bool Autoboot;
extern char AutobootROM[1024];
extern char AutobootPath[1024];

#endif
