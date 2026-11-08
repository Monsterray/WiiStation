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
 * the focus (updateFooter).
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

	/* rows x (label boxes + `texts` text boxes + `slots` buttons), `tabs` tab buttons, `help` help-line
	 * pairs and the footer lines, added to owner in this order. title is the first title text. */
	PageView(Frame *owner, int rows, int slots, int texts, int tabs, int help, ActFn act, void *user,
	         ButtonFunc back, const char *title);
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

	/* The footer help of the row that has focus; kept while nothing has focus; blank on a tab. Per frame. */
	void updateFooter(const engine::Page &page);

private:
	struct RowW {
		Button **btn;
		char   **btnString;
		unsigned char *marks;
		TextBox *text, *rlabel;
		TextBox **valueBox;
		char    *labelString;
		char   **valueString;
		char     value[VALUE_LEN];
	};
	static void click(void *self, int tag);
	void linkButtons(void) const;

	Frame *frame;
	Pool *pool;
	int nRows, nSlots, nTexts, nTabs, nHelp;
	ActFn act;
	void *user;
	TextBox *titleBox;
	char *titleString;
	RowW *rowW;
	Button **tabW;
	char **tabString;
	TextBox **helpTermBox, **helpBox, **footerBox;
	char **helpTermString, **helpString, **footerString;
	engine::Layout lay;
	int footerRow;
};

#ifdef PERF_PROF
/* The engine's checks, in debug builds: logs the problems of a laid-out page (a label wider than its button, a
 * readout over its label, a control in the logo's corner) with SysPrintf. A finding, not a crash. */
void logProblems(const char *frameName, const char *pageName, const engine::Page &page, const engine::Layout &lay);
#endif

} /* namespace glue */
} /* namespace menu */

#endif
