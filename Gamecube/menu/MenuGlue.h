/**
 * WiiStation - MenuGlue.h
 *
 * The Wii side of the menu engine. Every frame needs these three things, and they are here once:
 * the IplFont measures for the engine's Metrics, the focus links on the libgui buttons, and a
 * bounded string append. MenuEngine.h stays free of GX and libogc, so the host test builds
 * without this file.
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

#ifndef MENUGLUE_H
#define MENUGLUE_H

#include <stddef.h>

#include "MenuEngine.h"
#include "../libgui/Button.h"

namespace menu {
namespace glue {

/* The text measures of IplFont, for every page's engine Metrics. */
extern const engine::Metrics METRICS;

/* The button a frame draws for a layout control, or NULL. */
typedef Button *(*ButtonOfControl)(const engine::Control &c);

/* Sets the four focus links of button b, which is drawn for control c of lay. A link of -1 is
 * no link. buttonOf gives the button for the control a link points at. */
void linkFocus(Button *b, const engine::Layout &lay, const engine::Control &c, ButtonOfControl buttonOf);

/* Appends src to dst, which is dstSize bytes long. Text past the end is cut. */
void appendBounded(char *dst, size_t dstSize, const char *src);

} /* namespace glue */
} /* namespace menu */

#endif
