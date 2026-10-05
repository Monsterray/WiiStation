/**
 * WiiStation - CustomizeCoopFrame.h
 *
 * Which PlayStation buttons one Co-Op player works (Gamecube/coop.c). The submenu number
 * is the player: port * COOP_MAX + player.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#ifndef CUSTOMIZECOOPFRAME_H
#define CUSTOMIZECOOPFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class CustomizeCoopFrame : public menu::Frame
{
public:
	CustomizeCoopFrame();
	~CustomizeCoopFrame();
	void activateSubmenu(int submenu);
	void drawChildren(menu::Graphics& gfx);
};

#endif
