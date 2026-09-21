/**
 * WiiSX - fileBrowser.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 * 
 * Actual declarations of all the fileBrowser function pointers, etc
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                emukidid@gmail.com
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

#include "fileBrowser.h"
#include "fileBrowser-libfat.h"
#include "fileBrowser-CARD.h"
#include "../wiiSXconfig.h"

#define NULL 0

fileBrowser_file* isoFile_topLevel;
fileBrowser_file* saveFile_dir;
fileBrowser_file* biosFile_dir;

int (*isoFile_init)(fileBrowser_file*) = NULL;
int (*isoFile_open)(fileBrowser_file*) = NULL;
int (*isoFile_readDir)(fileBrowser_file*, fileBrowser_file**) = NULL;
int (*isoFile_readFile)(fileBrowser_file*, void*, unsigned int) = NULL;
int (*isoFile_seekFile)(fileBrowser_file*, unsigned int, unsigned int) = NULL;
int (*isoFile_deinit)(fileBrowser_file*) = NULL;

int (*saveFile_init)(fileBrowser_file*) = NULL;
//int (*saveFile_exists)(fileBrowser_file*) = NULL;
int (*saveFile_readFile)(fileBrowser_file*, void*, unsigned int) = NULL;
int (*saveFile_writeFile)(fileBrowser_file*, void*, unsigned int) = NULL;
int (*saveFile_deinit)(fileBrowser_file*) = NULL;

/* Point the saveFile_* calls at the device the player chose. Memory cards and button
 * configurations both go through these, and four places used to repeat the same switch
 * before every save and every load. Call this once before saveFile_init(). */
void setSaveDevice(void)
{
	switch (nativeSaveDevice)
	{
	case NATIVESAVEDEVICE_CARDA:
	case NATIVESAVEDEVICE_CARDB:
		saveFile_dir       = (nativeSaveDevice == NATIVESAVEDEVICE_CARDA)
		                   ? &saveDir_CARD_SlotA : &saveDir_CARD_SlotB;
		saveFile_readFile  = fileBrowser_CARD_readFile;
		saveFile_writeFile = fileBrowser_CARD_writeFile;
		saveFile_init      = fileBrowser_CARD_init;
		saveFile_deinit    = fileBrowser_CARD_deinit;
		break;
	case NATIVESAVEDEVICE_SD:
	case NATIVESAVEDEVICE_USB:
	default:
		saveFile_dir       = (nativeSaveDevice == NATIVESAVEDEVICE_USB)
		                   ? &saveDir_libfat_USB : &saveDir_libfat_Default;
		saveFile_readFile  = fileBrowser_libfat_readFile;
		saveFile_writeFile = fileBrowser_libfat_writeFile;
		saveFile_init      = fileBrowser_libfat_init;
		saveFile_deinit    = fileBrowser_libfat_deinit;
		break;
	}
}

int (*biosFile_init)(fileBrowser_file*) = NULL;
int (*biosFile_open)(fileBrowser_file*) = NULL;
int (*biosFile_readFile)(fileBrowser_file*, void*, unsigned int) = NULL;
int (*biosFile_deinit)(fileBrowser_file*) = NULL;
