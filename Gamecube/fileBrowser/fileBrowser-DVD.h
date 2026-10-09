/**
 * WiiStation - fileBrowser-DVD.h
 * Load from DVD: a data DVD mounted as "dvd:" (fileBrowser-DVD.c).
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#ifndef FILE_BROWSER_DVD_H
#define FILE_BROWSER_DVD_H

#include "fileBrowser.h"

/* Why the disc could not be mounted: readDir returns one, and the browser says it in words
 * (FileBrowserFrame.cpp). Apart from the SMB_* codes. */
#define DVD_ERR_NO_ACCESS  -120   /* no AHBPROT: not started by the Homebrew Channel */
#define DVD_ERR_NO_DRIVE   -121   /* IOS would not open /dev/di */
#define DVD_ERR_NO_DISC    -122   /* the drive is empty */
#define DVD_ERR_UNREADABLE -123   /* not ISO9660, a CD, or a DVD-R this drive cannot read */

extern fileBrowser_file topLevel_DVD;

int fileBrowser_DVD_readDir(fileBrowser_file*, fileBrowser_file**);
int fileBrowser_DVD_open(fileBrowser_file* file);
int fileBrowser_DVD_readFile(fileBrowser_file*, void*, unsigned int);
int fileBrowser_DVD_seekFile(fileBrowser_file*, unsigned int, unsigned int);
int fileBrowser_DVD_init(fileBrowser_file* file);
int fileBrowser_DVD_deinit(fileBrowser_file* file);

#endif
