/**
 * Wii64 - TextBox.h
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

#ifndef TEXTBOX_H
#define TEXTBOX_H

//#include "GuiTypes.h"
#include "Component.h"

namespace menu {

class TextBox : public Component
{
public:
	TextBox(char** label, float x, float y, float scale, bool centered);
	~TextBox();
	void setColor(GXColor *labelColor);
	void setText(char** strPtr);
	void setBold(bool bold);
	/* Right-aligned text ends at x instead of starting there. The font is drawn glyph by
	 * glyph from a left edge, so this is a measure-then-offset at draw time; the string
	 * can change between frames, which is why it is not done once at construction. */
	void setRightAligned(bool rightAligned);
	void drawComponent(Graphics& gfx);

private:
	bool centered, bold, rightAligned;
	char** textBoxText;
	float x, y, scale;
	GXColor	labelColor;

};

} //namespace menu 

#endif
