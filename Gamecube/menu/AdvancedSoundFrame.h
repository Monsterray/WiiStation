/**
 * WiiStation - AdvancedSoundFrame.h
 *
 * The "Advanced" page under Settings -> Audio. See the .cpp for how to add a row.
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

#ifndef ADVANCEDSOUNDFRAME_H
#define ADVANCEDSOUNDFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class AdvancedSoundFrame : public menu::Frame
{
public:
	AdvancedSoundFrame();
	~AdvancedSoundFrame();
	void activateSubmenu(int submenu);

	enum AdvancedSoundSubmenus
	{
		SUBMENU_NONE=0,
		SUBMENU_REINIT
	};

private:

};

#endif
