/**
 * WiiSX - Button.h
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

#ifndef BUTTON_H
#define BUTTON_H

//#include "GuiTypes.h"
#include "Component.h"

#define BTN_DEFAULT menu::Button::BUTTON_DEFAULT
#define BTN_A_NRM menu::Button::BUTTON_STYLEA_NORMAL
#define BTN_A_SEL menu::Button::BUTTON_STYLEA_SELECT
#define BTN_STD menu::Button::BUTTON_STANDARD

typedef void (*ButtonFunc)( void );
/* A click that says which button it came from: ctx is what the page registered, tag the button's number in it. */
typedef void (*ButtonCtxFunc)( void *ctx, int tag );

namespace menu {

class Button : public Component
{
public:
	Button(int style, char** label, float x, float y, float width, float height);
	~Button();
	void setActive(bool active);
	bool getActive();
	void setSelected(bool selected);
	/* BUTTON_STANDARD: a cycle button shows an arrow at each end (Docs/MENU_PLAN.md 1.3). */
	void setArrows(bool arrows) { this->arrows = arrows; }
	/* BUTTON_STANDARD: draw the style A normal-button art (a cycle, an action) instead of the selectable one. */
	void setPlain(bool plain) { this->plain = plain; }
	/* Changes the style after construction, between BUTTON_STYLEA_NORMAL, BUTTON_STYLEA_SELECT and
	 * BUTTON_STANDARD (the images the constructor sets for each). For one widget that is shown
	 * on a standard page and on an older one (OptionsFrame). Not for BUTTON_DEFAULT. */
	void setStyle(int style);
	void setReturn(ButtonFunc returnFn);
	void doReturn();
	void setClicked(ButtonFunc clickedFn);
	void doClicked();
	/* Click with a context: replaces setClicked's function; one handler then serves every row of a page. */
	void setClickedCtx(ButtonCtxFunc fn, void *ctx, int tag) { ctxFunc = fn; ctxData = ctx; ctxTag = tag; }
	void setText(char** strPtr);

	/* Let the button take its size from its own label, on either axis or both. The label
	 * is a char** the caller may change at any time, so the fit is applied by setBounds()
	 * and by fitToLabel(); it is not redone every frame. A dimension the flags do not
	 * name keeps whatever it was given.
	 *
	 *   b->setAutoSize(BTN_FIT_WIDTH);
	 *   b->setBounds(x, y, 0, 40);     // width comes from the text, height stays 40
	 *
	 * The fit is the label plus the padding, given as in CSS: setPadding(all),
	 * setPadding(top/bottom, left/right) or setPadding(top, right, bottom, left). The
	 * label is centred in what the padding leaves. setMinWidth()/setMaxWidth() bound a
	 * fitted width as CSS min-width/max-width do; a label too wide for the maximum is
	 * drawn smaller to fit (0 = no bound). A fitted width is a whole, even number of
	 * pixels: the image is drawn as two mirrored halves, which a fraction pulls apart.
	 */
	enum
	{
		BTN_FIT_NONE   = 0,
		BTN_FIT_WIDTH  = 1,
		BTN_FIT_HEIGHT = 2
	};
	void setAutoSize(int flags);
	void setPadding(float all);
	void setPadding(float vertical, float horizontal);
	void setPadding(float top, float right, float bottom, float left);
	void setMinWidth(float minWidth);
	void setMaxWidth(float maxWidth);
	void fitToLabel();

	/* Move and resize after construction; the auto-size flags are applied afterwards, so
	 * an auto-sized dimension may come back different from what was passed. */
	void setBounds(float x, float y, float width, float height);
	float getX() const { return x; }
	float getY() const { return y; }
	float getWidth() const { return width; }
	float getHeight() const { return height; }
	void setFontSize(float size);
	void setLabelMode(int mode);
	void setLabelScissor(int scissor);
	void setLabelInsetRight(int inset);	/* -1 (default): the same as the left scissor */
	void setNormalImage(Image *image);
	void setFocusImage(Image *image);
	void setSelectedImage(Image *image);
	void setSelectedFocusImage(Image *image);
	void updateTime(float deltaTime);
	void drawComponent(Graphics& gfx);
	Component* updateFocus(int direction, int buttonsPressed);
	/* A button takes focus only when it is shown and active (Component::isVisible, getActive). */
	bool canTakeFocus();
	void setButtonColors(GXColor *colors);
	void setLabelColor(GXColor color);
	/* A label colour for while the button has focus (the explorer's dimmed Recent row on the focus bar);
	 * clearLabelFocusColor goes back to the one colour. */
	void setLabelFocusColor(GXColor color) { labelFocusColor = color; labelFocusSet = true; }
	void clearLabelFocusColor() { labelFocusSet = false; }
	/* A hook for a direction press while this button has focus (the explorer's rows: Down on the last
	 * visible row scrolls the list). It runs before the focus links; true means the press was used and
	 * focus stays. NULL (the default) leaves every other button as it was. */
	typedef bool (*NavFunc)(Button *self, int direction);
	void setNavFunc(NavFunc fn) { navFunc = fn; }
	enum LabelMode
	{
		LABEL_CENTER=0,
		LABEL_LEFT,
		LABEL_SCROLL,
		LABEL_SCROLLONFOCUS
	};

	enum ButtonStyle
	{
		BUTTON_DEFAULT=0,
		BUTTON_STYLEA_NORMAL,
		BUTTON_STYLEA_SELECT,
		/* The menu standard (Docs/MENU_PLAN.md 1.2): today's style A art (the same images, colors and
		 * label), square corners and any height (the constructor keeps the height it is given; style A
		 * forces 56). The images are the select set, or the normal set after setPlain(true). */
		BUTTON_STANDARD
	};

private:
	void setImages();
	Component* followFocus(int direction, int buttonsPressed);
	bool active, selected, arrows, plain;
	Image	*plainImage;
	Image	*plainFocusImage;
	Image	*normalImage;
	Image	*focusImage;
	Image	*selectedImage;
	Image	*selectedFocusImage;
	char** buttonText;
	int buttonStyle, labelMode, labelScissor, labelInsetRight;
	bool labelFocusSet;		/* setLabelFocusColor was called: labelFocusColor shows while focused */
	GXColor labelFocusColor;
	NavFunc navFunc;
	int labelInsetR() const { return labelInsetRight < 0 ? labelScissor : labelInsetRight; }
	unsigned long StartTime;
	unsigned scrollKey;   /* hash of the text the scroll time runs for, and whether it was scrolling */
	bool scrollOn;
	int scrollShift(bool scrolling, float room);
	float x, y, width, height, fontSize;
	int autoSizeFlags;
	float padTop, padRight, padBottom, padLeft;
	float minWidth, maxWidth, labelScale;   /* labelScale < 1: the label shrunk to fit maxWidth */
	GXColor	focusColor, inactiveColor, activeColor, selectedColor, labelColor;
	ButtonFunc clickedFunc, returnFunc;
	ButtonCtxFunc ctxFunc;
	void *ctxData;
	int ctxTag;

};

} //namespace menu 

#endif
