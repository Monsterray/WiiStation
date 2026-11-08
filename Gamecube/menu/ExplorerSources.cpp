/**
 * WiiSX - ExplorerSources.cpp (was LoadRomFrame.cpp)
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

#include <sys/stat.h>
#include "MenuContext.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "ExplorerSources.h"
#include "FileBrowserFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "MenuGlue.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-SMB.h"
#include "../fileBrowser/smb2dev.h"
#include "../fileBrowser/fileBrowser-DVD.h"
#include "../fileBrowser/fileBrowser-CARD.h"
}

/* The explorer's four device sources (FileBrowserFrame.cpp): its tabs, the autoboot and the test
 * hooks call these. The Load ROM page that used to hold them as buttons is gone. */

extern MenuContext *pMenuContext;
extern int fileBrowserMode;
extern void fileBrowserFrame_OpenDirectory(fileBrowser_file* dir);
extern void fileBrowserFrame_OpenRomFolders(fileBrowser_file* deviceDefault, const char *device);

/* Points isoFile at a device source and readies it, without opening the browser: the four
 * Func_LoadFrom* below and the Recent tab (a row on another device) share it. src: 0 SD, 1 USB,
 * 2 DVD, 3 network. USB, DVD and the share mount here, lazily; check the device afterwards
 * (loadRomSourceReady). Does not touch explorerSource. */
void loadRomUseSource(int src)
{
	switch (src)
	{
	case 0:
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
		break;
	case 2:
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
		break;
	case 1:
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
#endif
		break;
	case 3:
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
#endif
		break;
	}
}

/* Is the device of source src there now? A probe that mounts nothing. */
int loadRomSourceReady(int src)
{
	struct stat st;
	switch (src)
	{
	case 0: return stat("sd:/", &st) == 0;
	case 1: return stat("usb:/", &st) == 0;
	case 2: return stat("dvd:/", &st) == 0;
#ifdef HW_RVL
	case 3: return smb2dev_mounted() != 0;
#endif
	}
	return 0;
}

void Func_LoadFromSD()
{
	explorerSource = 0;
	loadRomUseSource(0);
	if(Autoboot)
	{
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
	}
	else
	{
		pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,fileBrowserMode);
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "sd:");   /* RomDirSD */
	}
}

void Func_LoadFromDVD()
{
	explorerSource = 2;
	loadRomUseSource(2);
	if(Autoboot)
	{   /* autoboot.txt / a chain naming dvd:/... */
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
		return;
	}
	snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "dvd:/");
	pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,fileBrowserMode);
	fileBrowserFrame_OpenDirectory(isoFile_topLevel);
}

void Func_LoadFromUSB()
{
	explorerSource = 1;
#ifdef WII
	loadRomUseSource(1);
	
	if(Autoboot)
	{
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
	}
	else
	{
		pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,fileBrowserMode);
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "usb:");   /* RomDirUSB */
	}
#else
	menu::MessageBox::getInstance().setMessage("Available only for Wii");
#endif
}

void Func_LoadFromSamba()
{
	explorerSource = 3;
#ifdef HW_RVL
	loadRomUseSource(3);
	
	if(Autoboot)
	{   /* autoboot.txt / a chain naming smb:/...: MenuContext::Autoboot waited for the share */
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
		return;
	}
	pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,fileBrowserMode);
	if (smb2dev_mounted())
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, "smb:");   /* RomDirSMB */
	else
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);   /* says why: settings, network, share */
#else
	menu::MessageBox::getInstance().setMessage("Available only for Wii");
#endif
}
