/**
 * WiiSX - ExplorerSources.h (was LoadRomFrame.h)
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

#ifndef EXPLORERSOURCES_H
#define EXPLORERSOURCES_H

/* The explorer's device sources (ExplorerSources.cpp). The Load ROM page is retired. */
// For autoboot (plugin)
void Func_LoadFromSD();
void Func_LoadFromDVD();
void Func_LoadFromUSB();
void Func_LoadFromSamba();
void loadRomUseSource(int src);      /* 0 SD, 1 USB, 2 DVD, 3 network: points isoFile at it and mounts, no browser */
int  loadRomSourceReady(int src);    /* is that device there now (a probe, mounts nothing) */

#endif
