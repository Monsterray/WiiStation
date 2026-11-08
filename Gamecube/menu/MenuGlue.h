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
#include "../libgui/Frame.h"

namespace menu {
namespace glue {

/* The text measures of IplFont, for every page's engine Metrics. */
extern const engine::Metrics METRICS;

/* The button a frame draws for a layout control, or NULL. */
typedef Button *(*ButtonOfControl)(const engine::Control &c);

/* Sets the four focus links of button b, which is drawn for control c of lay. A link of -1 is
 * no link. buttonOf gives the button for the control a link points at. */
void linkFocus(Button *b, const engine::Layout &lay, const engine::Control &c, ButtonOfControl buttonOf);

/* The tab buttons, for every tabbed screen: -1 for a new press of the previous-tab button on any
 * controller, +1 for the next-tab button, 0 for neither. Previous / next: GameCube and HID pads
 * L / R, Classic and Wii U Pro L / R, a Wiimote's - / +, the GamePad's ZL / ZR, and an input
 * script's '<' / '>' (menukeys). Call it once per drawn frame of the screen on show. */
int tabShoulder(void);

/* The tab a shoulder press turns to from tab cur of n (engine::tabStep with the TabWrap
 * setting), or cur. */
int tabShoulderStep(int cur, int n);

/* The libgui buttons of one frame, made, added to the frame, given a Cursor box and focus-linked
 * in one place. Every frame used to repeat that loop and its own xxxButtonOf(control). A button is
 * kept under the (role, row, slot) of the engine control it draws (a free button:
 * ROLE_FREE, row -1, slot = its index; a tab: ROLE_TAB, row -1), which is how link() finds the neighbour a focus link points at.
 * The frame owns the Pool; the destructor takes every button out of the Cursor and deletes it. */
class Pool {
public:
	Pool(Frame *owner, int capacity);
	~Pool();

	/* Makes a button (new Button with exactly these arguments, active, click and return set),
	 * adds it to the frame and gives the Cursor the same box. Returns it. The frame's component
	 * list order is the order of these calls. A button of style A is made 56 high: a caller that
	 * wants another height calls setBounds on the result. */
	Button *button(engine::CtrlRole role, int row, int slot, int style, char **text, float x, float y, float w, float h,
	               ButtonFunc click, ButtonFunc ret);

	/* The button kept for (role, row, slot), or NULL. */
	Button *at(engine::CtrlRole role, int row, int slot) const;
	Button *at(const engine::Control &c) const { return at(c.role, c.row, c.slot); }

	/* The four focus links of every focusable control of lay that has a button. A frame whose
	 * controls change buttons from page to page (Settings' tabs) passes its own resolver; the
	 * default is at(control). inactiveToo links the controls that are drawn but not focus
	 * targets as well. */
	typedef Button *(*Resolver)(const engine::Control &c);
	void link(const engine::Layout &lay, Resolver of = NULL, bool inactiveToo = false) const;

private:
	Frame *frame;
	struct Entry { Button *b; engine::CtrlRole role; int row, slot; };
	Entry *e;
	int n, cap;
};

/* A button at the engine's rectangle, shown, and the Cursor's hit box moved with it. With
 * engineState the engine also decides what is active (a focus target) and lit (selected);
 * without it the button is active and its lit state is the caller's. */
void placeButton(Frame *frame, Button *b, const engine::Control &c, bool engineState);

/* Appends src to dst, which is dstSize bytes long. Text past the end is cut. */
void appendBounded(char *dst, size_t dstSize, const char *src);

/* One pass over the pads for the page keys of a frame, once per drawn frame. Today: the page turn
 * (tabShoulder's -1 / 0 / +1). The other frames' own pad polling moves in here as they are migrated. */
struct PageInput { int turn; };
PageInput readInput(void);

/* A titled list page (Prev / Next arrows and STD_LIST_ROWS item buttons, explorer density): the
 * engine page, the layout, the buttons, their focus links and the click handlers, written once.
 * The frame supplies the items, what a pick does and what B does; it keeps calling
 * drawOverlay() after its components. Used by AccountFrame; the explorer is the next user. */
class ListView {
public:
	typedef void (*ItemFn)(void *user, int index, engine::ListItem &out);
	typedef void (*PickFn)(void *user, int index);

	ListView(Frame *owner, char *title, ItemFn item, PickFn pick, void *user, ButtonFunc back);
	~ListView();

	/* New contents: n items, the window back at the top. Lays out and updates the buttons. */
	void setCount(int n);
	/* Per drawn frame, before the components draw: L/R page turn, the focused row's full name. */
	void update(void);
	/* After the components: folder icons, arrows, scroll thumb, texts. */
	void drawOverlay(Graphics &gfx) const;
	/* The first item's button, for the frame's default focus. */
	Button *firstItem(void) const;
	int top(void) const { return state.top; }

	enum { SLOTS = STD_LIST_ROWS, BUTTONS = 2 + SLOTS };	/* 0 Prev, 1 Next, then the item rows */

private:
	void layout(void);
	void apply(void);
	int control(int i) const;
	Button *buttonOf(const engine::Control &c) const;
	static void click(void *self, int tag);
	void link(void) const;

	Frame *frame;
	Pool pool;
	engine::ListState state;
	engine::ListSpec spec;
	engine::Row row;
	engine::Page page;
	engine::Layout lay;
	ItemFn itemFn;
	PickFn pickFn;
	void *user;
	Button *button[BUTTONS];
	int ctl[BUTTONS];
	char *text[BUTTONS];
};

} /* namespace glue */
} /* namespace menu */

#endif
