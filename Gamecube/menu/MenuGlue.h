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
#include "../libgui/FocusManager.h"

namespace menu {
namespace glue {

/* The text measures of IplFont, for every page's engine Metrics. */
extern const engine::Metrics METRICS;

/* The engine's four focus links on the buttons: for each control of lay that has a button
 * (of(control) is the button, or NULL), and is a focus target or inactiveToo, the buttons its
 * links point at. A link of -1 is no link. One loop for every frame. */
template <class Of>
void linkControls(const engine::Layout &lay, Of of, bool inactiveToo = false)
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < lay.n; i++)
	{
		const engine::Control &c = lay.c[i];
		if (!c.focusable && !inactiveToo)
			continue;
		Button *b = of(c);
		if (!b)
			continue;
		for (int d = 0; d < engine::DIR_COUNT; d++)
		{
			const int to = c.link[d];
			b->setNextFocus(DIRS[d], to >= 0 ? of(lay.c[to]) : NULL);
		}
	}
}

/* A filled rectangle in a flat color, blended. */
void fill(Graphics &gfx, int x, int y, int w, int h, GXColor c);

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

/* A button at the engine's rectangle, shown (the Cursor hits a button by its own bounds). With
 * engineState the engine also decides what is active (a focus target) and lit (selected);
 * without it the button is active and its lit state is the caller's. */
void placeButton(Button *b, const engine::Control &c, bool engineState);

/* Appends src to dst, which is dstSize bytes long. Text past the end is cut. */
void appendBounded(char *dst, size_t dstSize, const char *src);

/* One pass over the pads for the page keys of a frame, once per drawn frame: the page turn (the
 * shoulders: -1 / 0 / +1, as tabShoulder) and the sort key (GC Z, Wiimote 1, Classic ZR, HID R1).
 * Shares tabShoulder's edge state: a frame calls one of the two per drawn frame. */
struct PageInput { int turn; bool sort; };
PageInput readInput(void);

/* A list page (Prev / Next arrows and STD_LIST_ROWS item buttons, explorer density): the engine
 * page, the layout, the buttons, their focus links and the click handlers, written once. The frame
 * supplies the items, what a pick does and what B does, and calls update() before its components
 * draw and drawOverlay() after them. A titled list (Account) needs nothing more; the explorer adds
 * source tabs (setTabs), its own list state (useState), a hook that sets the page's texts and lit
 * tab before each layout, and a hook that turns the page its own way (setHooks). */
class ListView {
public:
	typedef void (*ItemFn)(void *user, int index, engine::ListItem &out);
	typedef void (*PickFn)(void *user, int index);
	typedef void (*TabFn)(void *user, int tab);
	typedef void (*PrepareFn)(void *user, engine::Page &page);	/* before every layout */
	typedef void (*TurnFn)(void *user, int dir, bool wrap);		/* an arrow (wrap false) or a shoulder (true) */

	ListView(Frame *owner, char *title, ItemFn item, PickFn pick, void *user, ButtonFunc back);
	~ListView();

	enum { SLOTS = STD_LIST_ROWS, BUTTONS = 2 + SLOTS, MAX_TABS = 6 };	/* 0 Prev, 1 Next, then the item rows */

	/* Set up once, after the constructor. Tabs are BTN_A_SEL buttons after the item buttons; the
	 * page's curTab is lit. useState: the list's top, count and focus live in the frame's state. */
	void setTabs(const engine::Tab *tabs, int n, TabFn fn);
	void setHooks(PrepareFn prepare, TurnFn turn);
	void useState(engine::ListState *s);

	/* New contents: n items, the window back at the top. Lays out and updates the buttons. */
	void setCount(int n);
	/* Lays out the list as its state is now and puts the buttons on it (no focus change). */
	void relayout(void);
	/* relayout, then the frame's default focus: the layout's, or with no item the lit tab. */
	void refresh(void);
	/* Per drawn frame, before the components draw: the page turn and the focused row's full name.
	 * Returns the input, so the frame can act on the keys the view does not (sort). */
	PageInput update(void);
	/* After the components: folder icons, arrows, scroll thumb, texts. */
	void drawOverlay(Graphics &gfx) const;

	engine::ListState &list(void) { return *st; }
	const engine::Layout &laid(void) const { return lay; }
	/* Item row k's button (k = 0 .. SLOTS-1), and the row of a button (-1 for none). */
	Button *item(int k) const { return button[2 + k]; }
	int itemRow(const Button *b) const;
	/* The first item's button, for the frame's default focus. */
	Button *firstItem(void) const { return button[2]; }
	int top(void) const { return st->top; }

private:
	void layout(void);
	void apply(void);
	int control(int i) const;
	Button *buttonOf(const engine::Control &c) const;
	static void click(void *self, int tag);
	void turn(int dir, bool wrap);
	void link(void) const;

	Frame *frame;
	Pool pool;
	engine::ListState own, *st;
	engine::ListSpec spec;
	engine::Row row;
	engine::Page page;
	engine::Layout lay;
	ItemFn itemFn;
	PickFn pickFn;
	TabFn tabFn;
	PrepareFn prepareFn;
	TurnFn turnFn;
	void *user;
	ButtonFunc backFn;
	Button *button[BUTTONS];
	int ctl[BUTTONS];
	char *text[BUTTONS];
	Button *tabB[MAX_TABS];
	char *tabText[MAX_TABS];
	int nTabs;
};

} /* namespace glue */
} /* namespace menu */

#endif
