/**
 * WiiStation - fileBrowser-DVD.c
 * Load from DVD. The first version was WiiSX's (C) 2007-2009 emu_kidid.
 *
 * Games on a data DVD (ISO9660, with or without Joliet names), such as a DVD-R burned on a
 * PC. libiso9660 mounts the disc as the device "dvd:" over libdi's __io_wiidvd, and from
 * there it is the code the SD card uses: the browser lists dvd:/ with opendir(), and
 * cdriso.c opens the game with fopen(), so .cue/.bin, CHD, PBP and multi-track images work
 * as they do from the card. (The version before 5.9.0 browsed the disc with its own
 * ISO9660 reader and kept only a file's name, which cdriso.c's fopen() could not open: no
 * game ever started from a disc.)
 *
 * What a Wii can read: data DVDs, on a Wii whose drive reads DVD-R (most made until late
 * 2008; not the Wii Family Edition, the Wii mini or a Wii U). No Wii reads CDs. libdi reads
 * the drive's registers directly, which needs the full hardware access the Homebrew Channel
 * gives an app whose meta.xml asks for it (<ahb_access/>); no cIOS is needed.
 *
 * The disc is mounted afresh each time Load from DVD opens, so a disc changed in between is
 * read -- but not while a game from the disc is loaded: its open file points into the mount,
 * which ISO9660_Unmount() frees (a disc changed meanwhile is read once a game from another
 * device has been loaded). libiso9660 has no lock, so only one thread may read the
 * disc: cdriso.c starts no CD read-ahead thread for a game here.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <string.h>
#include <gccore.h>
#include <iso9660.h>
#ifdef HW_RVL
#include <di/di.h>
#define DVD_DISC (&__io_wiidvd)
#else
#include <ogc/dvd.h>
#define DVD_DISC (&__io_gcdvd)
#endif
#include "fileBrowser.h"
#include "fileBrowser-libfat.h"
#include "fileBrowser-DVD.h"

fileBrowser_file topLevel_DVD =
	{ "dvd:/",   // file name
	  0ULL,      // discoffset (u64)
	  0,         // offset
	  0,         // size
	  FILE_BROWSER_ATTR_DIR
	};

extern BOOL hasLoadedISO;    /* GamecubeMain.cpp: a game is loaded, from isoFile */

static int mounted;
static int mount_error;      /* why the last mount failed: a DVD_ERR_*, or 0 */

/* Mount the disc in the drive as "dvd:". Load from DVD calls it each time it opens. */
int fileBrowser_DVD_init(fileBrowser_file* file)
{
	(void)file;
	if (mounted && hasLoadedISO && !strncmp(isoFile.name, "dvd:", 4))
		return 0;   /* the loaded game's file is open in this mount: keep it */
	if (mounted)
		ISO9660_Unmount("dvd");
	mounted = 0;

#ifdef HW_RVL
	if (*(vu32 *)0xCD800064 != 0xFFFFFFFF) {   /* HW_AHBPROT: no direct access to the drive */
		mount_error = DVD_ERR_NO_ACCESS;
		return mount_error;
	}
	if (DI_Init() < 0) {
		mount_error = DVD_ERR_NO_DRIVE;
		return mount_error;
	}
#else
	DVD_Init();
#endif
	/* Spins the disc up and reads its directories: a few seconds */
	mounted = ISO9660_Mount("dvd", DVD_DISC);
	if (mounted)
		mount_error = 0;
	else
		mount_error = DVD_DISC->isInserted() ? DVD_ERR_UNREADABLE : DVD_ERR_NO_DISC;
	return mount_error;
}

int fileBrowser_DVD_readDir(fileBrowser_file* file, fileBrowser_file** dir)
{
	if (!mounted)
		return mount_error ? mount_error : DVD_ERR_NO_DISC;
	return fileBrowser_libfat_readDir(file, dir);
}

int fileBrowser_DVD_open(fileBrowser_file* file)
{
	return fileBrowser_libfat_open(file);
}

int fileBrowser_DVD_readFile(fileBrowser_file* file, void* buffer, unsigned int length)
{
	return fileBrowser_libfatROM_readFile(file, buffer, length);
}

int fileBrowser_DVD_seekFile(fileBrowser_file* file, unsigned int where, unsigned int type)
{
	return fileBrowser_libfat_seekFile(file, where, type);
}

int fileBrowser_DVD_deinit(fileBrowser_file* file)
{
	/* The mount stays until the next Load from DVD: the game reads through it */
	return fileBrowser_libfatROM_deinit(file);
}
