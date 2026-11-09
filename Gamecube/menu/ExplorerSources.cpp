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
#include "ExplorerSources.h"
#include "FileBrowserFrame.h"
#include "../libgui/MessageBox.h"

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
	struct Ops {
		fileBrowser_file *top;
		int (*readDir)(fileBrowser_file*, fileBrowser_file**);
		int (*open)(fileBrowser_file*);
		int (*readFile)(fileBrowser_file*, void*, unsigned int);
		int (*seekFile)(fileBrowser_file*, unsigned int, unsigned int);
		int (*init)(fileBrowser_file*);
		int (*deinit)(fileBrowser_file*);
	};
	static const Ops OPS[4] = {
		{ &topLevel_libfat_Default, fileBrowser_libfat_readDir, fileBrowser_libfat_open,
		  fileBrowser_libfatROM_readFile, fileBrowser_libfat_seekFile, fileBrowser_libfat_init,
		  fileBrowser_libfatROM_deinit },
#ifdef WII
		{ &topLevel_libfat_USB, fileBrowser_libfat_readDir, fileBrowser_libfat_open,
		  fileBrowser_libfatROM_readFile, fileBrowser_libfat_seekFile, fileBrowser_libfat_init,
		  fileBrowser_libfatROM_deinit },
#else
		{ 0 },
#endif
		{ &topLevel_DVD, fileBrowser_DVD_readDir, fileBrowser_DVD_open, fileBrowser_DVD_readFile,
		  fileBrowser_DVD_seekFile, fileBrowser_DVD_init, fileBrowser_DVD_deinit },
#ifdef HW_RVL
		{ &topLevel_SMB, fileBrowser_SMB_readDir, fileBrowser_SMB_open, fileBrowser_SMB_readFile,
		  fileBrowser_SMB_seekFile, fileBrowser_SMB_init, fileBrowser_SMB_deinit },
#else
		{ 0 },
#endif
	};
	if (src < 0 || src > 3 || !OPS[src].top)
		return;
	const Ops &o = OPS[src];
	if (isoFile_deinit) isoFile_deinit( &isoFile );		/* the old source's state */
	isoFile_topLevel = o.top;
	isoFile_readDir  = o.readDir;
	isoFile_open     = o.open;
	isoFile_readFile = o.readFile;
	isoFile_seekFile = o.seekFile;
	isoFile_init     = o.init;
	isoFile_deinit   = o.deinit;
	isoFile_init( isoFile_topLevel );	/* mounts it (DVD: as "dvd:"; a failure is said by the browser) */
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

/* Opens source src in the explorer: its top level, or the RomDir list of romDirs ("sd:" ...; NULL
 * for the DVD, which has none). Autoboot opens AutobootPath instead. */
static void openSource(int src, const char *romDirs)
{
	explorerSource = src;
#if !defined(WII) || !defined(HW_RVL)
	if (src == 1 || src == 3)
	{
		menu::MessageBox::getInstance().setMessage("Available only for Wii");
		return;
	}
#endif
	loadRomUseSource(src);
	if(Autoboot)
	{   /* autoboot.txt / a chain naming dvd:/... or smb:/...: MenuContext::Autoboot waited for the share */
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "%s", AutobootPath);
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);
		return;
	}
	if (src == 2)
		snprintf(isoFile_topLevel->name, sizeof(isoFile_topLevel->name), "dvd:/");
	pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER,fileBrowserMode);
	if (romDirs && (src != 3 || loadRomSourceReady(3)))
		fileBrowserFrame_OpenRomFolders(isoFile_topLevel, romDirs);   /* RomDirSD / RomDirUSB / RomDirSMB */
	else
		fileBrowserFrame_OpenDirectory(isoFile_topLevel);   /* the share: says why (settings, network, share) */
}

void Func_LoadFromSD()    { openSource(0, "sd:"); }
void Func_LoadFromDVD()   { openSource(2, NULL); }
void Func_LoadFromUSB()   { openSource(1, "usb:"); }
void Func_LoadFromSamba() { openSource(3, "smb:"); }
