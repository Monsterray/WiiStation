/**
 * WiiStation - MenuGlue.cpp
 *
 * The Wii side of the menu engine (see MenuGlue.h).
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

#include <string.h>

#include "MenuGlue.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/InputManager.h"
#include "../../HidController/KernelHID.h"

namespace menu {
extern u16 menu_script_keys;   /* InputManager.cpp: an input script's "menukeys" */

namespace glue {

static int textWidth(const char *s, float scale, void *)
{
	return IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int textHeight(const char *s, float scale, void *)
{
	return IplFont::getInstance().getStringHeight((char*)s, scale);
}

const engine::Metrics METRICS = { textWidth, textHeight, NULL };

void linkFocus(Button *b, const engine::Layout &lay, const engine::Control &c, ButtonOfControl buttonOf)
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	for (int d = 0; d < engine::DIR_COUNT; d++)
	{
		const int to = c.link[d];	/* a link of -1 is no link */
		b->setNextFocus(DIRS[d], to >= 0 ? buttonOf(lay.c[to]) : NULL);
	}
}

int tabShoulder(void)
{
	enum { PREV = 1, NEXT = 2, SOURCES = 5 };
	static int last[4][SOURCES];   /* what each controller held last frame: a press is a new hold */
	int press = 0;

	for (int i = 0; i < 4; i++)
	{
		int held[SOURCES] = { 0 };
		u16 gc = PAD_ButtonsHeld(i) | (i == 0 ? menu_script_keys : 0);
		held[0] = (gc & PAD_TRIGGER_L ? PREV : 0) | (gc & PAD_TRIGGER_R ? NEXT : 0);
#ifdef HW_RVL
		{
			u32 w = Input::getInstance().getWpad()[i].btns_h;
			u32 u = WUPC_ButtonsHeld(i);
			u16 h = hidControllerConnected ? ((PADStatus*)HID_MEM2_PAD_BUFF)[i].button : 0;
			held[1] = (w & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L) ? PREV : 0) |
			          (w & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R) ? NEXT : 0);
			held[2] = (u & WPAD_CLASSIC_BUTTON_FULL_L ? PREV : 0) | (u & WPAD_CLASSIC_BUTTON_FULL_R ? NEXT : 0);
			held[3] = (h & PAD_TRIGGER_L ? PREV : 0) | (h & PAD_TRIGGER_R ? NEXT : 0);
			if (i == 0)
			{
				u16 d = WiiDRC_ButtonsHeld();
				held[4] = (d & WIIDRC_BUTTON_ZL ? PREV : 0) | (d & WIIDRC_BUTTON_ZR ? NEXT : 0);
			}
		}
#endif
		for (int s = 0; s < SOURCES; s++)
		{
			press |= held[s] & ~last[i][s];
			last[i][s] = held[s];
		}
	}
	return press == PREV ? -1 : press == NEXT ? 1 : 0;
}

int tabShoulderStep(int cur, int n)
{
	return engine::tabStep(cur, n, tabShoulder(), tabWrap != 0);
}

void appendBounded(char *dst, size_t dstSize, const char *src)
{
	size_t used = strlen(dst);
	if (used < dstSize - 1)
		strncat(dst, src, dstSize - used - 1);
}

} /* namespace glue */
} /* namespace menu */
