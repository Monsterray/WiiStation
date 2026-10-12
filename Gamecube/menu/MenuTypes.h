/**
 * Wii64 - MenuTypes.h
 * Copyright (C) 2009 sepp256
 *
 * Wii64 homepage: http://www.emulatemii.com
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

#ifndef MENUDEFS_H
#define MENUDEFS_H

class MenuContext;
class MainFrame;
class LoadRomFrame;

//extern MenuContext *pMenuContext;

/* One row of the settings table, OPTIONS in GamecubeMain.cpp. A section row has no value.
 * The menu text (label, choices[value - min], help) lives only here. */
struct SettingOption {
	const char* key;			/* a section row (value NULL) has the key of its [name] */
	char* value;
	char min, max;
	const char* note;			/* a section row: the comment under its [name] in settings.ini */
	const char* label;			/* NULL: no menu row (file-only, grid or trigger) */
	const char* const* choices;
	const char* help;
};

/* The table row for a setting key, or NULL. Menus call it once when a page is built. */
const SettingOption* settingByKey(const char* key);

#endif
