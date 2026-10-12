/**
 * WiiStation - OptionsFrame.h
 *
 * The sub-pages reached from the Settings tabs: the Advanced pages (tabs CPU, GPU, SPU,
 * CD, Input), Advanced Graphics and Memory.
 * One frame class serves all of them; see the .cpp for how to add a page or a row.
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

#ifndef OPTIONSFRAME_H
#define OPTIONSFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class OptionsFrame : public menu::Frame
{
public:
	OptionsFrame();
	~OptionsFrame();
	void activateSubmenu(int submenu);
	void drawChildren(menu::Graphics& gfx);

	/* activateSubmenu() takes one of these: which page to show. The tab a page
	 * returns to on B is part of the page definition (see PAGES[] in the .cpp). */
	enum OptionsPages
	{
		PAGE_SOUND=0,		// Advanced Sound (the Audio tab's Advanced button)
		PAGE_ADVGFX,
		PAGE_PLUGINS,		// Advanced: CPU tab (the General tab's Advanced button)
		PAGE_STORAGE,		// Advanced: CD tab
		PAGE_MEMORY,
		PAGE_ADV_GPU,		// Advanced: GPU tab
		PAGE_ADV_INPUT,		// Advanced: Input tab
		PAGE_ADV_SPU		// Advanced: SPU tab
	};
	void showTab(int tab);	// a tab of the Advanced pages was pressed

};

#endif
