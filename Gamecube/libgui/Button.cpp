/**
 * WiiSX - Button.cpp
 * Copyright (C) 2009, 2010 sepp256
 *
 * WiiSX homepage: http://www.emulatemii.com
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

#include "Button.h"
#include "GuiResources.h"
#include "GraphicsGX.h"
#include "IPLFont.h"
#include "Image.h"
#include "FocusManager.h"
#include <math.h>

namespace menu {

Button::Button(int style, char** label, float x, float y, float width, float height)
		: active(false),
		  selected(false),
		  arrows(false),
		  plain(false),
		  plainImage(0),
		  plainFocusImage(0),
		  normalImage(0),
		  focusImage(0),
		  selectedImage(0),
		  selectedFocusImage(0),
		  buttonText(label),
		  buttonStyle(style),
		  labelMode(LABEL_CENTER),
		  labelScissor(0),
		  labelInsetRight(-1),
		  labelFocusSet(false),
		  navFunc(0),
		  StartTime(0),
		  scrollKey(0),
		  scrollOn(false),
		  x(x),
		  y(y),
		  width(width),
		  autoSizeFlags(BTN_FIT_NONE),
		  padTop(10.0f),      /* a few pixels more than the 8 and 14 it used to be */
		  padRight(16.0f),
		  padBottom(10.0f),
		  padLeft(16.0f),
		  minWidth(0.0f),
		  maxWidth(0.0f),
		  labelScale(1.0f),
		  height(height),
		  fontSize(1.0),
		  clickedFunc(0),
		  returnFunc(0),
		  ctxFunc(0),
		  ctxData(0),
		  ctxTag(0)
{
						//Focus color			Inactive color		  Active color			Selected color		  Label color
	GXColor colors[5] = {{255, 100, 100, 255}, {255, 255, 255,  70}, {255, 255, 255, 130}, {255, 255, 255, 255}, {255, 255, 255, 255}};

	setType(TYPE_BUTTON);
	setImages();
	if (buttonStyle != BUTTON_DEFAULT)
	{
		colors[0] = (GXColor) {255, 255, 255, 255};
		colors[1] = (GXColor) {200, 200, 200, 255};
		colors[2] = (GXColor) {255, 255, 255, 255};
	}

	setButtonColors(colors);
}

Button::~Button()
{
}

/* The images (and the 56 high) of the current style. */
void Button::setImages()
{
	switch(buttonStyle)
	{
	case BUTTON_DEFAULT:
		setNormalImage(Resources::getInstance().getImage(Resources::IMAGE_DEFAULT_BUTTON));
		setFocusImage(Resources::getInstance().getImage(Resources::IMAGE_DEFAULT_BUTTONFOCUS));
		setSelectedImage(Resources::getInstance().getImage(Resources::IMAGE_DEFAULT_BUTTONFOCUS));
		break;
	case BUTTON_STYLEA_NORMAL:
		setNormalImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTON));
		setFocusImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONFOCUS));
		this->height = 56;
		break;
	case BUTTON_STYLEA_SELECT:
		setNormalImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTOFF));
		setFocusImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTOFFFOCUS));
		setSelectedImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTON));
		setSelectedFocusImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTONFOCUS));
		this->height = 56;
		break;
	case BUTTON_STANDARD:
		/* today's art: the select set, and the normal set for a button that is not a choice */
		setNormalImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTOFF));
		setFocusImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTOFFFOCUS));
		setSelectedImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTON));
		setSelectedFocusImage(Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONSELECTONFOCUS));
		plainImage = Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTON);
		plainFocusImage = Resources::getInstance().getImage(Resources::IMAGE_STYLEA_BUTTONFOCUS);
		break;
	}
}

void Button::setStyle(int style)
{
	if (style == buttonStyle || style == BUTTON_DEFAULT)
		return;
	buttonStyle = style;
	setImages();
}

void Button::setActive(bool activeBool)
{
	active = activeBool;
}

bool Button::getActive()
{
	return active;
}

void Button::setSelected(bool selectedBool)
{
	selected = selectedBool;
}

void Button::setReturn(ButtonFunc newReturnFunc)
{
	returnFunc = newReturnFunc;
}

void Button::doReturn()
{
	if (returnFunc) returnFunc();
}

void Button::setClicked(ButtonFunc newClickedFunc)
{
	clickedFunc = newClickedFunc;
	ctxFunc = 0;		/* the last one set wins: doClicked prefers a context click */
}

void Button::doClicked()
{
	if (ctxFunc) ctxFunc(ctxData, ctxTag);
	else if (clickedFunc) clickedFunc();
}

void Button::setAutoSize(int flags)
{
	autoSizeFlags = flags;
	fitToLabel();
}

void Button::setPadding(float all)
{
	setPadding(all, all, all, all);
}

void Button::setPadding(float vertical, float horizontal)
{
	setPadding(vertical, horizontal, vertical, horizontal);
}

void Button::setPadding(float top, float right, float bottom, float left)
{
	padTop = top;
	padRight = right;
	padBottom = bottom;
	padLeft = left;
	fitToLabel();
}

void Button::setMinWidth(float w)
{
	minWidth = w;
	fitToLabel();
}

void Button::setMaxWidth(float w)
{
	maxWidth = w;
	fitToLabel();
}

/* The text plus the padding. Called when the size is set and when the caller says the
 * label changed; a button with no flags set is left alone. */
void Button::fitToLabel()
{
	if (!autoSizeFlags || !buttonText || !*buttonText) return;

	labelScale = 1.0f;
	if (autoSizeFlags & BTN_FIT_WIDTH)
	{
		float text = (float)IplFont::getInstance().getStringWidth(*buttonText, fontSize);
		width = 2.0f * ceilf((text + padLeft + padRight) / 2.0f);
		if (width < minWidth)
			width = minWidth;
		if (maxWidth > 0.0f && width > maxWidth)
		{
			width = maxWidth;
			if (text > 0.0f && maxWidth > padLeft + padRight)
				labelScale = (maxWidth - padLeft - padRight) / text;
		}
	}
	if (autoSizeFlags & BTN_FIT_HEIGHT)
		height = (float)IplFont::getInstance().getStringHeight(*buttonText, fontSize * labelScale) + padTop + padBottom;
}

void Button::setBounds(float newX, float newY, float newWidth, float newHeight)
{
	x = newX;
	y = newY;
	width = newWidth;
	height = newHeight;
	fitToLabel();
}

void Button::setText(char** strPtr)
{
	buttonText = strPtr;
}

void Button::setFontSize(float size)
{
	fontSize = size;
}

#include "ogc/lwp_watchdog.h"

void Button::setLabelMode(int mode)
{
	labelMode = mode;
	if(labelMode >= LABEL_SCROLL) StartTime = ticks_to_microsecs(gettick());
}

void Button::setLabelScissor(int scissor)
{
	labelScissor = scissor;
}

/* The room kept free at the right of the label: the left scissor unless set (-1), so every other
 * button keeps its symmetric box. The explorer's rows set it to keep a scrolling name left of the tag. */
void Button::setLabelInsetRight(int inset)
{
	labelInsetRight = inset;
}

void Button::setNormalImage(Image *image)
{
	normalImage = image;
}

void Button::setFocusImage(Image *image)
{
	focusImage = image;
}

void Button::setSelectedImage(Image *image)
{
	selectedImage = image;
}

void Button::setSelectedFocusImage(Image *image)
{
	selectedFocusImage = image;
}

#define SCROLL_PAUSE 1.5f   /* seconds the label stays at its beginning, and at its end */
#define SCROLL_SPEED 45.0f  /* pixels a second (of the 640 wide menu) */

/* A button's picture is its image mirrored into quarters (or halves) that meet at the middle.
 * Each edge is cut to a whole pixel once and both quads share it: with a float x (a row of
 * three 165 wide buttons is centered on a half pixel) the quads' own truncations
 * (int)(x + width / 2) and (int)x + (int)(width / 2) differ by one, which left a column of
 * the background showing through the middle of the button. */
static void drawQuarters(Graphics& gfx, float x, float y, float width, float height)
{
	const int x0 = (int)x, xm = (int)(x + width / 2), x1 = (int)(x + width);
	const int y0 = (int)y, ym = (int)(y + height / 2), y1 = (int)(y + height);
	gfx.drawImage(0, x0, y0, xm - x0, ym - y0, 0.0, width/16.0, 0.0, height/16.0);
	gfx.drawImage(0, xm, y0, x1 - xm, ym - y0, width/16.0, 0.0, 0.0, height/16.0);
	gfx.drawImage(0, x0, ym, xm - x0, y1 - ym, 0.0, width/16.0, height/16.0, 0.0);
	gfx.drawImage(0, xm, ym, x1 - xm, y1 - ym, width/16.0, 0.0, height/16.0, 0.0);
}

static void drawHalves(Graphics& gfx, float x, float y, float width, float height)
{
	const int x0 = (int)x, xm = (int)(x + width / 2), x1 = (int)(x + width);
	const int y0 = (int)y, y1 = (int)(y + height);
	gfx.drawImage(0, x0, y0, xm - x0, y1 - y0, 0.0, width/8.0, 0.0, 1.0);
	gfx.drawImage(0, xm, y0, x1 - xm, y1 - y0, width/8.0, 0.0, 0.0, 1.0);
}

/* BUTTON_STANDARD: today's style A picture with square corners, cut into a 9-slice at one texel to one
 * pixel. The texture is 8 x 56: texels 0-1 are a faint glow, texel 2 is the 1 px dark border, texel 3 the
 * light edge, and from texel 4 on every column is the middle; row 0 is the top border, row 1 the light
 * edge, rows 2..50 the face (row 50 holds the cyan underline of a lit button), row 51 the bottom border and
 * rows 52..55 the soft shadow. The old picture drew 4 px to the texture width, 2 texels to a pixel, so the
 * half-pixel border fell between two pixels or fell out when the picture was scaled (16:9), and a button lost
 * a border side depending on where it stood. Here the edge strips are 2 px (texels 2-3) and the top and
 * bottom bands 2 px and 7 px (rows 0-1, rows 49-55), each at exactly one texel to a pixel, sampled at
 * the texel centers; only the face between them is stretched. The outer pixel of each strip repeats one
 * border texel, so the corner pixels are as dark as the sides (the texture's corner texel is rounded). Whole pixels throughout, no overlap
 * (an overlap would blend the border over the face instead of over the background), so touching
 * segments meet exactly. */
static void drawSquare(Graphics& gfx, float x, float y, float width, float height)
{
	const int x0 = (int)x, x1 = (int)(x + width), y0 = (int)y, y1 = (int)(y + height);
	const int w = x1 - x0, h = y1 - y0;
	if (w < 8 || h < 14)
	{
		drawHalves(gfx, x, y, width, height);
		return;
	}
	const float sB = 2.5f / 8.0f, sL = 3.5f / 8.0f;    /* the border column and the light edge column */
	const float T = 1.0f / 56.0f;
	/* a band: tex rows r0..r1 over py..py+ph; ts is a row to repeat for the border column, or -1 */
	struct Band { int r0, r1, py, ph; };
	const Band bands[3] = {
		{ 0, 2, y0, 2 },
		{ 2, 49, y0 + 2, h - 9 },
		{ 49, 56, y1 - 7, 7 },
	};
	for (int b = 0; b < 3; b++)
	{
		const Band &B = bands[b];
		const float ta = B.r0 * T, tb = B.r1 * T;
		for (int side = 0; side < 2; side++)
		{
			const int bx = side ? x1 - 1 : x0, lx = side ? x1 - 2 : x0 + 1;   /* border column, light column */
			if (b == 0)
				gfx.drawImage(0, bx, B.py, 1, B.ph, sB, sB, 1.5f * T, 1.5f * T);
			else if (b == 1)
				gfx.drawImage(0, bx, B.py, 1, B.ph, sB, sB, ta, tb);
			else
			{
				gfx.drawImage(0, bx, B.py, 1, 3, sB, sB, 50.5f * T, 50.5f * T);   /* rows 49-51: border */
				gfx.drawImage(0, bx, B.py + 3, 1, 4, sB, sB, 52.0f * T, 56.0f * T);
			}
			gfx.drawImage(0, lx, B.py, 1, B.ph, sL, sL, ta, tb);
		}
		gfx.drawImage(0, x0 + 2, B.py, w - 4, B.ph, 1.0, 1.0, ta, tb);
	}
}

static const GXColor ARROW_COLOR = { 225, 225, 225, 255 };

/* The cycle arrows: a filled triangle at each end pointing outward, as in the mock-ups (render.py): centred
 * 14 px in from the end, the tip 4 px outward of that centre and the base 3 px inward, 12 px high. Each is
 * ONE triangle (not 1 px columns: a 1 px column can fall between two pixels when the picture is scaled, as
 * in 16:9, which left holes in the arrow). The two are exact mirrors: tip and base x are the same distance
 * from their end. The color is the caller's. */
static void drawArrows(Graphics& gfx, float x, float y, float width, float height, GXColor color)
{
	const int x0 = (int)x, y0 = (int)y, w = (int)(x + width) - x0, h = (int)(y + height) - y0;
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(color);
	const int cy = y0 + h / 2;
	const int lt = x0 + 10, lb = x0 + 17;		/* left arrow: tip x, base x */
	const int rt = x0 + w - 10, rb = x0 + w - 17;	/* right arrow: the same, mirrored */
	gfx.fillTriangle(lt, cy, lb, cy - 6, lb, cy + 6);
	gfx.fillTriangle(rt, cy, rb, cy + 6, rb, cy - 6);
}

/* How far a label wider than its button is moved left, in pixels: it shows its beginning for
 * SCROLL_PAUSE seconds, moves at SCROLL_SPEED until its end shows, stays there SCROLL_PAUSE
 * seconds and starts over from its beginning. The time starts again when the button is
 * focused and when its text changes (the file browser reuses its buttons for other names),
 * so a name always shows its beginning first. A label that fits, or one not scrolling
 * (scrolling false: focus is the usual reason), is not moved. */
int Button::scrollShift(bool scrolling, float room)
{
	unsigned key = 5381;
	for (const char *c = *buttonText; c && *c; c++)
		key = key * 33 + (unsigned char)*c;
	if (key != scrollKey || scrolling != scrollOn)
	{
		scrollKey = key;
		scrollOn = scrolling;
		StartTime = ticks_to_microsecs(gettick());
	}
	if (!scrolling)
		return 0;
	const float over = IplFont::getInstance().getStringWidth(*buttonText, fontSize) - room + labelScissor + labelInsetR();
	if (over <= 0.0f)
		return 0;
	const float travel = over / SCROLL_SPEED;
	float t = (float)((unsigned long)ticks_to_microsecs(gettick()) - StartTime) / 1000000.0f;
	t = fmodf(t, SCROLL_PAUSE + travel + SCROLL_PAUSE);
	if (t < SCROLL_PAUSE)
		return 0;
	if (t < SCROLL_PAUSE + travel)
		return (int)(over * (t - SCROLL_PAUSE) / travel);
	return (int)over;
}

void Button::drawComponent(Graphics& gfx)
{
//	printf("Button drawComponent\n");

	gfx.setColor(inactiveColor);

	//activate relevant texture
	if(active)
	{
		//draw normalImage with/without gray mask and with alpha test on
		//printf("Button Active\n");
		gfx.setColor(activeColor);
	}
	if(getFocus())
	{
		//draw focus indicator (extra border for button?)
		//printf("Button in Focus\n");
		gfx.setColor(focusColor);
	}
	//draw buttonLabel?

	gfx.enableBlending(true);
//	gfx.setTEV(GX_PASSCLR);
	gfx.setTEV(GX_MODULATE);

//	gfx.setColor(focusColor);
	gfx.setDepth(-10.0f);
	gfx.newModelView();
	gfx.loadModelView();
	gfx.loadOrthographic();

	switch (buttonStyle)
	{
	case BUTTON_DEFAULT:
//	gfx.fillRect(x, y, width, height);
		normalImage->activateImage(GX_TEXMAP0);
		drawQuarters(gfx, x, y, width, height);

		if (selected)
		{
			gfx.setColor(selectedColor);
			if(selectedImage) selectedImage->activateImage(GX_TEXMAP0);
			drawQuarters(gfx, x, y, width, height);
		}
		break;
	case BUTTON_STYLEA_NORMAL:
		if (getFocus())	focusImage->activateImage(GX_TEXMAP0);
		else			normalImage->activateImage(GX_TEXMAP0);
		drawHalves(gfx, x, y, width, height);
		break;
	case BUTTON_STYLEA_SELECT:
		if (selected)
		{
			if (getFocus())	selectedFocusImage->activateImage(GX_TEXMAP0);
			else			selectedImage->activateImage(GX_TEXMAP0);
		}
		else
		{
			if (getFocus())	focusImage->activateImage(GX_TEXMAP0);
			else			normalImage->activateImage(GX_TEXMAP0);
		}
		drawHalves(gfx, x, y, width, height);
		break;
	case BUTTON_STANDARD:
		if (plain)
		{
			if (getFocus())	plainFocusImage->activateImage(GX_TEXMAP0);
			else			plainImage->activateImage(GX_TEXMAP0);
		}
		else if (selected)
		{
			if (getFocus())	selectedFocusImage->activateImage(GX_TEXMAP0);
			else			selectedImage->activateImage(GX_TEXMAP0);
		}
		else
		{
			if (getFocus())	focusImage->activateImage(GX_TEXMAP0);
			else			normalImage->activateImage(GX_TEXMAP0);
		}
		drawSquare(gfx, x, y, width, height);
		if (arrows)
			drawArrows(gfx, x, y, width, height, active ? ARROW_COLOR : inactiveColor);
		break;
	}

	if (buttonText)
	{
		//int strWidth;
		int strHeight;
		gfx.enableScissor(x + labelScissor, y, width - labelScissor - labelInsetR(), height);
		if(active)	IplFont::getInstance().drawInit(labelFocusSet && getFocus() ? labelFocusColor : labelColor);
		else		IplFont::getInstance().drawInit(inactiveColor);
		switch (labelMode)
		{
			case LABEL_CENTER:
			{
				/* centred in the box the padding leaves; the same place when it is even. A label
				 * wider than that box shrinks to fit: a fixed-size button whose text the page
				 * changes (Configure Buttons' ANALOG combo, 110 px: "L2+R2+Start" was cut at both
				 * ends, 2026-10-21) had only fitToLabel for auto-sized buttons, and other languages
				 * run longer than English. Measured here because the text changes at any time. */
				float scale = fontSize*labelScale;
				float room = width - 8.0f;   /* the whole button, not the padding: labels drew past it before */
				float text = (float)IplFont::getInstance().getStringWidth(*buttonText, scale);
				if (room > 0.0f && text > room)
					scale *= room / text;
				IplFont::getInstance().drawString((int) (x+padLeft+(width-padLeft-padRight)/2),
					(int) (y+padTop+(height-padTop-padBottom)/2), *buttonText, scale, true);
				break;
			}
			case LABEL_LEFT:
				//strWidth = IplFont::getInstance().getStringWidth(*buttonText, fontSize);
				strHeight = IplFont::getInstance().getStringHeight(*buttonText, fontSize);
				IplFont::getInstance().drawString((int) (x+labelScissor), (int) (y+(height-strHeight)/2), *buttonText, fontSize, false);
				break;
			case LABEL_SCROLL:
				strHeight = IplFont::getInstance().getStringHeight(*buttonText, fontSize);
				IplFont::getInstance().drawString((int) (x+labelScissor-scrollShift(true, width)), (int) (y+(height-strHeight)/2), *buttonText, fontSize, false);
				break;
			case LABEL_SCROLLONFOCUS:
				strHeight = IplFont::getInstance().getStringHeight(*buttonText, fontSize);
				IplFont::getInstance().drawString((int) (x+labelScissor-scrollShift(getFocus(), width)), (int) (y+(height-strHeight)/2), *buttonText, fontSize, false);
				break;
		}
		gfx.disableScissor();
	}

}

void Button::updateTime(float deltaTime)
{
	//Overload in Component class
	//Add interpolator class & update here?
}

/* Limits on focus moves. A link chain is followed past at most FOCUS_MAX_SKIP hidden or
 * inactive buttons, and focus moves nest at most FOCUS_MAX_DEPTH deep, so a loop of links
 * (A -> B -> A, or a button linked to itself) ends instead of recursing forever. */
enum {
	FOCUS_MAX_SKIP  = 32,
	FOCUS_MAX_DEPTH = 16
};
static int focusDepth = 0;

/* Anything but a button can take focus, as before; a button needs to be shown and active. */
static bool takesFocus(Component *c)
{
	if (c->getType() != Component::TYPE_BUTTON)
		return true;
	return static_cast<Button*>(c)->canTakeFocus();
}

bool Button::canTakeFocus()
{
	return isVisible() && active;
}

/* The move in a direction: the first link that can take focus, following links through the
 * buttons that cannot. NULL when there is none within the limits. */
Component* Button::followFocus(int direction, int buttonsPressed)
{
	Component* target = getNextFocus(direction);
	int skipped = 0;

	while (target && !takesFocus(target))
	{
		if (++skipped > FOCUS_MAX_SKIP)
			return NULL;
		target = target->getNextFocus(direction);
	}
	if (!target || target == this || focusDepth >= FOCUS_MAX_DEPTH)
		return NULL;

	Component* newFocus;
	focusDepth++;
	newFocus = target->updateFocus(direction, buttonsPressed);
	focusDepth--;
	return newFocus;
}

Component* Button::updateFocus(int direction, int buttonsPressed)
{
	Component* newFocus = NULL;
	if(!getFocus() && canTakeFocus() && direction == Focus::DIRECTION_NONE && buttonsPressed == Focus::ACTION_BACK)
	{
		//B alone on a button that is not lit runs its Back and leaves the highlight where it is:
		//on the main menu (no Back) it used to light the button that last had focus.
		doReturn();
		return this;
	}
	if(!getFocus() && canTakeFocus())
	{
		setFocus(true);
		newFocus = this;
	}
	else if (!getFocus())
	{
		//not a focus target: try to move in the direction, and take the first one that is
		newFocus = followFocus(direction, buttonsPressed);
	}
	else if (navFunc && direction != Focus::DIRECTION_NONE && navFunc(this, direction))
	{
		newFocus = this;		/* the hook used the press: focus stays here */
	}
	else
	{
		newFocus = followFocus(direction, buttonsPressed);
		if (newFocus)
			setFocus(false);
		else
			newFocus = this;
	}
	if (newFocus == this)
	{
		//finally update button behavior based on buttons pressed
		if(buttonsPressed & Focus::ACTION_BACK)
			doReturn();
		else if (buttonsPressed & Focus::ACTION_SELECT)
			doClicked();
	}
	return newFocus;
}

void Button::setButtonColors(GXColor *colors)
{
	focusColor.r = colors[0].r;
	focusColor.g = colors[0].g;
	focusColor.b = colors[0].b;
	focusColor.a = colors[0].a;
	inactiveColor.r = colors[1].r;
	inactiveColor.g = colors[1].g;
	inactiveColor.b = colors[1].b;
	inactiveColor.a = colors[1].a;
	activeColor.r = colors[2].r;
	activeColor.g = colors[2].g;
	activeColor.b = colors[2].b;
	activeColor.a = colors[2].a;
	selectedColor.r = colors[3].r;
	selectedColor.g = colors[3].g;
	selectedColor.b = colors[3].b;
	selectedColor.a = colors[3].a;
	labelColor.r = colors[4].r;
	labelColor.g = colors[4].g;
	labelColor.b = colors[4].b;
	labelColor.a = colors[4].a;
}

void Button::setLabelColor(GXColor color)
{
	labelColor.r = color.r;
	labelColor.g = color.g;
	labelColor.b = color.b;
	labelColor.a = color.a;
}

} //namespace menu 
