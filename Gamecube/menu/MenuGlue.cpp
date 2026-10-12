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

namespace menu {
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

void appendBounded(char *dst, size_t dstSize, const char *src)
{
	size_t used = strlen(dst);
	if (used < dstSize - 1)
		strncat(dst, src, dstSize - used - 1);
}

} /* namespace glue */
} /* namespace menu */
