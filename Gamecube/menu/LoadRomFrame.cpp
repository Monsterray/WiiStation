/**
 * WiiSX - LoadRomFrame.cpp
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
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "LoadRomFrame.h"
#include "FileBrowserFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-SMB.h"
#include "../fileBrowser/smb2dev.h"
#include "../fileBrowser/fileBrowser-DVD.h"
#include "../fileBrowser/fileBrowser-CARD.h"
}

void Func_LoadFromSD();
void Func_LoadFromDVD();
void Func_LoadFromUSB();
void Func_LoadFromSamba();
void Func_ReturnFromLoadRomFrame();

static const int NUM_FRAME_BUTTONS = 4;

static char loadRomStrings[4][25] =
	{ "Load from SD",
	  "Load from DVD",
	  "Load from USB",
	  "Load from Samba"};

struct LoadRomButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
};

static LoadRomButtonInfo loadRomButtons[NUM_FRAME_BUTTONS] =
{	//	button	buttonStyle	buttonString		clickFunc			returnFunc
	{	NULL,	BTN_A_NRM,	loadRomStrings[0],	Func_LoadFromSD,	Func_ReturnFromLoadRomFrame },
	{	NULL,	BTN_A_NRM,	loadRomStrings[1],	Func_LoadFromDVD,	Func_ReturnFromLoadRomFrame },
	{	NULL,	BTN_A_NRM,	loadRomStrings[2],	Func_LoadFromUSB,	Func_ReturnFromLoadRomFrame },
	{	NULL,	BTN_A_NRM,	loadRomStrings[3],	Func_LoadFromSamba,	Func_ReturnFromLoadRomFrame },
};

/* The Load ROM page: four rows, one action button each. */
namespace eng = menu::engine;

static eng::ActionButton LOADROM_ACTIONS[NUM_FRAME_BUTTONS];
static eng::Row LOADROM_ROWS[NUM_FRAME_BUTTONS];
static eng::Page LOADROM_PAGE;
static eng::Layout LOADROM_LAYOUT;

static int loadRomWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int loadRomHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}

static const eng::Metrics LOADROM_METRICS = { loadRomWidth, loadRomHeight, NULL };

static void layoutLoadRom(void)
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		LOADROM_ACTIONS[i].text  = loadRomButtons[i].buttonString;
		LOADROM_ACTIONS[i].width = 340.0f;
		LOADROM_ACTIONS[i].lit   = false;
		LOADROM_ACTIONS[i].dy    = 0.0f;

		eng::Row &row = LOADROM_ROWS[i];
		memset(&row, 0, sizeof row);
		row.kind    = eng::ROW_BUTTONS;
		row.count   = 1;
		row.actions = &LOADROM_ACTIONS[i];	/* x0 0: centred */
	}
	memset(&LOADROM_PAGE, 0, sizeof LOADROM_PAGE);
	LOADROM_PAGE.rows  = LOADROM_ROWS;
	LOADROM_PAGE.nRows = NUM_FRAME_BUTTONS;
	LOADROM_PAGE.logo  = eng::LOGO_SPOT_TOP;
	LOADROM_PAGE.focus = eng::FOCUS_ROWS;
	LOADROM_PAGE.rowDy = 80.0f;
	LOADROM_PAGE.rowY0 = 100.0f;

	eng::layoutPage(LOADROM_PAGE, LOADROM_METRICS, LOADROM_LAYOUT);
	eng::linkFocus(LOADROM_PAGE, LOADROM_LAYOUT);
}

LoadRomFrame::LoadRomFrame()
{
	layoutLoadRom();

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its row. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = LOADROM_LAYOUT.c[LOADROM_LAYOUT.slot[i][0]];
		loadRomButtons[i].button = new menu::Button(loadRomButtons[i].buttonStyle, &loadRomButtons[i].buttonString,
										c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}

	/* The engine's links, handed to the buttons. */
	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = LOADROM_LAYOUT.c[LOADROM_LAYOUT.slot[i][0]];
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			loadRomButtons[i].button->setNextFocus(DIRS[d],
				to >= 0 ? loadRomButtons[LOADROM_LAYOUT.c[to].row].button : NULL);
		}
		loadRomButtons[i].button->setActive(true);
		if (loadRomButtons[i].clickedFunc) loadRomButtons[i].button->setClicked(loadRomButtons[i].clickedFunc);
		if (loadRomButtons[i].returnFunc) loadRomButtons[i].button->setReturn(loadRomButtons[i].returnFunc);
		add(loadRomButtons[i].button);
		menu::Cursor::getInstance().addComponent(this, loadRomButtons[i].button, c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}
	setDefaultFocus(loadRomButtons[0].button);
	setBackFunc(Func_ReturnFromLoadRomFrame);
	setEnabled(true);
}

LoadRomFrame::~LoadRomFrame()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, loadRomButtons[i].button);
		delete loadRomButtons[i].button;
	}
}

int loadRomMode = FileBrowserFrame::FILEBROWSER_LOADISO;

void LoadRomFrame::activateSubmenu(int submenu)
{
	loadRomMode = submenu;
}


extern MenuContext *pMenuContext;
extern void fileBrowserFrame_OpenDirectory(fileBrowser_file* dir);
extern void fileBrowserFrame_OpenRomFolders(fileBrowser_file* deviceDefault, const char *device);

void Func_LoadFromSD()
{
	// Deinit any existing romFile state
	if(isoFile_deinit) isoFile_deinit( &isoFile );
	// Change all the romFile pointers
	isoFile_topLevel = &topLevel_libfat_Default;
	isoFile_readDir  = fileBrowser_libfat_readDir;
	isoFile_open     = fileBrowser_libfat_open;
	isoFile_readFile = fileBrowser_libfatROM_readFile;
	isoFile_seekFile = fileBrowser_libfat_seekFile;
	isoFile_init     = fileBrowser_libfat_init;
	isoFile_deinit   = fileBrowser_libfatROM_deinit;
	// Make sure the romFile system is ready before we browse the filesystem
	isoFile_init( isoFile_topLevel );

	if(Autoboot)
	{
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
	}
	else
	{
		pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,loadRomMode);
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "sd:");   /* RomDirSD */
	}
}

void Func_LoadFromDVD()
{
	// Deinit any existing romFile state
	if(isoFile_deinit) isoFile_deinit( &isoFile );
	// Change all the romFile pointers
	isoFile_topLevel = &topLevel_DVD;
	isoFile_readDir  = fileBrowser_DVD_readDir;
	isoFile_open     = fileBrowser_DVD_open;
	isoFile_readFile = fileBrowser_DVD_readFile;
	isoFile_seekFile = fileBrowser_DVD_seekFile;
	isoFile_init     = fileBrowser_DVD_init;
	isoFile_deinit   = fileBrowser_DVD_deinit;
	// Mount the disc as "dvd:"; a failure is said by the browser, from readDir's code
	isoFile_init( isoFile_topLevel );

	if(Autoboot)
	{   /* autoboot.txt / a chain naming dvd:/... */
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
		return;
	}
	snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "dvd:/");
	pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,loadRomMode);
	fileBrowserFrame_OpenDirectory(isoFile_topLevel);
}

void Func_LoadFromUSB()
{
#ifdef WII
	// Deinit any existing romFile state
	if(isoFile_deinit) isoFile_deinit( &isoFile );
	// Change all the romFile pointers
	isoFile_topLevel = &topLevel_libfat_USB;
	isoFile_readDir  = fileBrowser_libfat_readDir;
	isoFile_open     = fileBrowser_libfat_open;
	isoFile_readFile = fileBrowser_libfatROM_readFile;
	isoFile_seekFile = fileBrowser_libfat_seekFile;
	isoFile_init     = fileBrowser_libfat_init;
	isoFile_deinit   = fileBrowser_libfatROM_deinit;
	// Make sure the romFile system is ready before we browse the filesystem
	isoFile_init( isoFile_topLevel );
	
	if(Autoboot)
	{
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
	}
	else
	{
		pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,loadRomMode);
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "usb:");   /* RomDirUSB */
	}
#else
	menu::MessageBox::getInstance().setMessage("Available only for Wii");
#endif
}

void Func_LoadFromSamba()
{
#ifdef HW_RVL
	// Deinit any existing romFile state
	if(isoFile_deinit) isoFile_deinit( &isoFile );
	// Change all the romFile pointers
	isoFile_topLevel = &topLevel_SMB;
	isoFile_readDir  = fileBrowser_SMB_readDir;
	isoFile_open     = fileBrowser_SMB_open;
	isoFile_readFile = fileBrowser_SMB_readFile;
	isoFile_seekFile = fileBrowser_SMB_seekFile;
	isoFile_init     = fileBrowser_SMB_init;
	isoFile_deinit   = fileBrowser_SMB_deinit;
	// Make sure the romFile system is ready before we browse the filesystem
	isoFile_init( isoFile_topLevel );
	
	if(Autoboot)
	{   /* autoboot.txt / a chain naming smb:/...: MenuContext::Autoboot waited for the share */
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
		return;
	}
	pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,loadRomMode);
	if (smb2dev_mounted())
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "smb:");   /* RomDirSMB */
	else
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);   /* says why: settings, network, share */
#else
	menu::MessageBox::getInstance().setMessage("Available only for Wii");
#endif
}

void Func_ReturnFromLoadRomFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}
