/**
 * WiiStation - MenuPageView.h
 *
 * The Wii side of a Form page: the widgets of a page of rows (title, tabs, label + button rows,
 * choice segments, readouts, help lines, the footer help), made once, and the code that puts them
 * on the rectangles the menu engine gives back. A frame that is a table of rows (OptionsFrame; the
 * Settings tabs next) keeps its tables and its settings, builds an engine::Page, and calls show().
 *
 * The frame supplies one callback for every click: (row, slot), where slot is the row's button (a
 * cycle, an action or a choice segment, counted from 0 as the engine counts them) and row is -1 for a
 * tab (slot = the tab). A row's buttons are one set of widgets: which art and arrows a button gets
 * follows its role in the layout (a choice segment, a button) and the marks the frame gave it
 * (mark()). The view owns the layout, so nothing is computed per frame; only the footer help follows
 * the focus (update).
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

#ifndef MENUPAGEVIEW_H
#define MENUPAGEVIEW_H

#include "MenuGlue.h"
#include "pages/ActionPages.h"
#include "../libgui/TextBox.h"

namespace menu {
namespace glue {

class PageView {
public:
	enum { VALUE_LEN = 32 };
	typedef void (*ActFn)(void *user, int row, int slot);

	/* Marks of a button, given before show(). TOGGLE: a flag that is lit when on (the select art, though the
	 * engine calls it a button). CYCLE: an arrow at each end. A ROW_CYCLE row's button has the arrows anyway.
	 * FLAT: a choice segment drawn with the plain art (actions that sit in a choice row, never lit). */
	enum { TOGGLE = 1, CYCLE = 2, FLAT = 4 };

	/* Widgets for the engine's maxima: MAX_ROWS x (label boxes + MAX_SLOTS text boxes + MAX_SLOTS buttons),
	 * MAX_TABS tab buttons, MAX_HELP help-line pairs and the footer lines, added to owner in this order.
	 * The unused ones stay hidden. title is the first title text. */
	PageView(Frame *owner, ActFn act, void *user, ButtonFunc back, const char *title);
	~PageView();

	/* A tab's text; the view keeps the pointer. */
	void setTabText(int tab, const char *text);
	/* The text of a readout: the frame writes it, the engine lays it out and the box shows it. */
	char *value(int row) { return rowW[row].value; }
	/* The text of a row's button; the view keeps the pointer. A change shows at once (the button width is the layout's). */
	void setText(int row, int slot, const char *text) { rowW[row].btnString[slot] = (char*)text; }
	/* A button lit or not. */
	void setLit(int row, int slot, bool lit);
	void mark(int row, int slot, int bits) { rowW[row].marks[slot] = (unsigned char)bits; }

	/* Every widget off and unlit, the help lines blank, the footer reset. First step of a page change. */
	void hideAll(void);
	/* Lays page out (once, here), places every widget, links focus. False when it does not fit: nothing is shown.
	 * engineLit: take each button's lit and inactive state from the layout (the Settings tabs); else the frame
	 * sets them (setLit). */
	bool show(const engine::Page &page, bool engineLit = false);
	/* Where focus starts: the page's default control's button, or NULL. */
	Button *startButton(void) const;
	Button *tab(int t) const { return tabW[t]; }
	Button *rowButton(int row) const { return rowW[row].btn[0]; }
	Button *buttonFor(const engine::Control &c) const;
	const engine::Layout &layout(void) const { return lay; }

	/* Once per drawn frame, for a page of tabs and rows: the shoulder buttons turn the tab (the same as a click
	 * on it: act(user, -1, tab); the new page's default control takes focus) and the footer follows the focus. */
	void update(const engine::Page &page);

private:
	/* The footer help of the row that has focus; kept while nothing has focus; blank on a tab. Lays out only when the row changes. */
	void updateFooter(const engine::Page &page);
	struct RowW {
		Button *btn[engine::MAX_SLOTS];
		char   *btnString[engine::MAX_SLOTS];
		unsigned char marks[engine::MAX_SLOTS];
		TextBox *text, *rlabel;
		TextBox *valueBox[engine::MAX_SLOTS];
		char    *labelString;
		char    *valueString[engine::MAX_SLOTS];
		char     value[VALUE_LEN];
	};
	static void click(void *self, int tag);
	void linkButtons(void) const;

	Frame *frame;
	Pool *pool;
	ActFn act;
	void *user;
	TextBox *titleBox;
	char *titleString;
	RowW rowW[engine::MAX_ROWS];
	Button *tabW[engine::MAX_TABS];
	char *tabString[engine::MAX_TABS];
	TextBox *helpTermBox[engine::MAX_HELP], *helpBox[engine::MAX_HELP], *footerBox[STD_HELP_LINES];
	char *helpTermString[engine::MAX_HELP], *helpString[engine::MAX_HELP], *footerString[STD_HELP_LINES];
	engine::Layout lay;
	int footerRow;		/* the row whose help should show, -1 none */
	int footerShown;	/* the row laid out now; updateFooter skips the layout when equal */
};

/* A page of buttons that each do one thing (Main, Current ISO): the table is an ActionMenuDef
 * (pages/ActionPages.h). The menu lays itself out once, makes its buttons once (a button's A and B
 * are the table's click and `back`) and links the focus; the frame adds its other widgets before it,
 * sets the default focus on button(0) and B (setBackFunc) itself. */
class ActionMenu {
public:
	enum { MAX_ACTIONS = ACTION_MAX, LABEL_LEN = 24 };
	ActionMenu(Frame *owner, const ActionMenuDef &def, ButtonFunc back);
	~ActionMenu();

	/* A button's text, copied into the menu's own label (the button keeps pointing at it). */
	void setText(int i, const char *text);
	Button *button(int i) const;

private:
	ActionMenu(const ActionMenu&);
	ActionMenu &operator=(const ActionMenu&);

	const ActionMenuDef &def;
	Pool *pool;
	ActionPage ap;
	engine::Layout lay;
	char label[MAX_ACTIONS][LABEL_LEN];
	char *labelPtr[MAX_ACTIONS];
};

/* A page of free buttons and labels at the author's rectangles (Configure Input, Configure Buttons, Customize
 * Co-Op): the frame adds each button once (add: its place, art and text; the buttons are made here, once, and a
 * click is click(user, id)), then sets each one's state and links and calls relayout() when the state changes
 * and never per frame. The form owns the focus: the engine's geometry rule over the buttons that are shown,
 * with the author's links (link()) over it; the frame keeps its picture and its own drawChildren. */
class Editor {
public:
	typedef void (*ClickFn)(void *user, int id);
	struct Item { Button *b; char **text; float x, y, w, h; bool shown, active, lit; };
	struct Label { TextBox *box; char **text; float ax, ay, scale; bool centred, shown; };

	Editor(Frame *owner, int nButtons, int nLabels, ClickFn click, void *user, ButtonFunc back);
	~Editor();

	/* Button id (0..nButtons-1) at its rectangle, shown and active; text is the frame's own pointer, read at each layout. */
	Button *add(int id, int style, char **text, float x, float y, float w, float h);
	TextBox *addLabel(int id, char **text, float ax, float ay, float scale, bool centred);
	/* A title the engine centres at y (Customize Co-Op); buf is kept and may change in place. */
	void setTitle(char *buf, float y);

	Item &item(int id) { return it[id]; }
	Label &label(int id) { return lb[id]; }
	Button *button(int id) const { return it[id].b; }
	/* The links of the state, replacing the geometry rule's where both ends are shown: -1 none. */
	void clearLinks(void) { nLinks = 0; }
	void link(int from, int up, int down, int left, int right);
	/* The focus wraps at the screen edges (Configure Buttons). */
	void setWrap(bool w) { wrap = w; }
	/* Lays out the shown buttons and labels and links focus. withState also puts each button's visible, active and lit
	 * state and each label's visibility on its widget; Configure Buttons sets those itself and passes false. */
	void relayout(bool withState);
	/* The button the layout's default focus is, or NULL. */
	Button *defaultButton(void) const;

private:
	Editor(const Editor&);
	Editor &operator=(const Editor&);
	static void click(void *self, int tag);

	struct Link { int from; engine::Dir dir; int to; };
	Frame *frame;
	Pool *pool;
	ClickFn clickFn;
	void *user;
	ButtonFunc backFn;
	Item *it;
	Label *lb;
	int nBtn, nLbl;
	Link *links;
	int nLinks;
	engine::FreeButton *fb;
	int *fbId;
	engine::FreeText *ft;
	int *ftId;
	engine::FreeLink *fl;
	engine::Page page;
	engine::Layout lay;
	bool wrap;
	char *titleBuf, *titlePtr;
	float titleY;
	TextBox *titleBox;
};

#ifdef PERF_PROF
/* The engine's checks, in debug builds: logs the problems of a laid-out page (a label wider than its button, a
 * readout over its label, a control in the logo's corner) with SysPrintf. A finding, not a crash. */
void logProblems(const char *frameName, const char *pageName, const engine::Page &page, const engine::Layout &lay);
#endif

} /* namespace glue */
} /* namespace menu */

#endif
