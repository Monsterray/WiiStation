/**
 * WiiStation - AchievementsFrame.cpp
 *
 * Current ISO > Achievements: a header ("3 of 12 earned") and one text row per achievement of
 * the loaded game's set (achievements.h), ten to a page. Prev and Next turn the page, where the
 * file browser has them; B goes back to Current ISO. Nothing here is focusable but the two
 * page buttons: the rows are text, read from the engine each frame.
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

#include <stdio.h>
#include <string.h>
#include "MenuContext.h"
#include "AchievementsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../../achievements.h"

extern MenuContext *pMenuContext;

#define PER_PAGE	10
#define ROW_Y0		75		/* the first row's top; rows are ROW_DY apart */
#define ROW_DY		36
#define MARK_X		120		/* "Earned", in the file browser's column (Prev is left of it) */
#define TITLE_X		210
#define TITLE_W		310		/* to x = 520: clear of Next and of the logo */
#define ROW_SCALE	0.8f

static int top;		/* the first achievement shown */
static char prevText[] = "Prev", nextText[] = "Next";
static char *prevString = prevText, *nextString = nextText;
static menu::Button *prevButton, *nextButton;

static void Func_AchPrev() { if (top >= PER_PAGE) top -= PER_PAGE; }
static void Func_AchNext() { if (top + PER_PAGE < (int)achievements_count()) top += PER_PAGE; }
static void Func_ReturnFromAchievementsFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_CURRENTROM);
}

void Func_AchievementsList()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_ACHIEVEMENTS, 0);
}

/* A page button at the file browser's place for it (FileBrowserFrame.cpp FB_SPEC). */
static menu::Button *pageButton(menu::Frame *frame, char **text, void (*fn)(), float x)
{
	menu::Button *b = new menu::Button(BTN_A_NRM, text, x, 220.0f, 70.0f, 40.0f);
	b->setActive(true);
	b->setClicked(fn);
	b->setReturn(Func_ReturnFromAchievementsFrame);
	frame->add(b);
	menu::Cursor::getInstance().addComponent(frame, b, x, x + 70.0f, 220.0f, 260.0f);
	return b;
}

AchievementsFrame::AchievementsFrame()
{
	prevButton = pageButton(this, &prevString, Func_AchPrev, 35.0f);
	nextButton = pageButton(this, &nextString, Func_AchNext, 535.0f);
	/* Left and Right go to the other one, either way round */
	prevButton->setNextFocus(menu::Focus::DIRECTION_LEFT, nextButton);
	prevButton->setNextFocus(menu::Focus::DIRECTION_RIGHT, nextButton);
	nextButton->setNextFocus(menu::Focus::DIRECTION_LEFT, prevButton);
	nextButton->setNextFocus(menu::Focus::DIRECTION_RIGHT, prevButton);
	setDefaultFocus(nextButton);
	setBackFunc(Func_ReturnFromAchievementsFrame);
	setEnabled(true);
}

AchievementsFrame::~AchievementsFrame()
{
	menu::Cursor::getInstance().removeComponent(this, prevButton);
	menu::Cursor::getInstance().removeComponent(this, nextButton);
	delete prevButton;
	delete nextButton;
}

void AchievementsFrame::activateSubmenu(int submenu)
{
	top = 0;
}

void AchievementsFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;

	menu::ComponentList::const_iterator it;
	for (it = componentList.begin(); it != componentList.end(); ++it)
		(*it)->draw(gfx);

	GXColor text = {56, 56, 56, 255};	/* TextBox's: a lighter one was lost on the darker lower half */
	menu::IplFont &font = menu::IplFont::getInstance();
	unsigned count = achievements_count();
	char line[96];

	font.drawInit(text);
	if (!count)
	{
		snprintf(line, sizeof line, "No achievement set is loaded for this game.");
		font.drawString(320, 150, line, 1.0f, true);	/* below the logo, above Prev and Next */
		return;
	}
	snprintf(line, sizeof line, "%u of %u earned", achievements_earned(), count);
	font.drawString(320, 40, line, 1.0f, true);

	for (int i = 0; i < PER_PAGE; i++)
	{
		const char *title;
		int earned = achievements_get(top + i, &title);
		if (earned < 0)
			break;
		int y = ROW_Y0 + i * ROW_DY;
		if (earned)
		{
			snprintf(line, sizeof line, "Earned");
			font.drawString(MARK_X, y, line, ROW_SCALE, false);
		}
		/* the title, cut to its column */
		snprintf(line, sizeof line, "%s", title);
		for (size_t n = strlen(line); n && font.getStringWidth(line, ROW_SCALE) > TITLE_W; )
			line[--n] = '\0';
		font.drawString(TITLE_X, y, line, ROW_SCALE, false);
	}
}
