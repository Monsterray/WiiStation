/**
 * WiiStation - AchievementsFrame.h
 *
 * Current ISO > Achievements: the loaded game's achievement set, ten to a page.
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

#ifndef ACHIEVEMENTSFRAME_H
#define ACHIEVEMENTSFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class AchievementsFrame : public menu::Frame
{
public:
	AchievementsFrame();
	~AchievementsFrame();
	void activateSubmenu(int submenu);
	void drawChildren(menu::Graphics& gfx);
};

void Func_AchievementsList();	/* the Current ISO button: opens the list */

#endif
