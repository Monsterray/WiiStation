/**
 * WiiStation - MenuEngine.h
 *
 * The menu framework's core (Docs/MENU_FRAMEWORK_PLAN.md, step 1). A page is described as
 * data -- tabs, rows, help lines, where the logo is -- and this code works out where every
 * control goes and where D-pad focus goes. It is plain C++ with no GX or libogc calls, so a
 * host build (tests/menu_engine_test.cpp) lays out the real pages and checks them.
 *
 * The arithmetic is OptionsFrame.cpp's, moved here under its own names. layoutPage() gives
 * the rectangles and text positions OptionsFrame gives today; the test re-derives them from
 * OptionsFrame's code and compares. Where OptionsFrame and MenuLayout.h disagree, the numbers
 * kept here are OptionsFrame's (the look must not change); see the notes on grid:: below.
 *
 *   Metrics m = { fontWidth, fontHeight, NULL };   // IplFont on the Wii, a file stub on the host
 *   Layout  lay;                                    // caller-owned; nothing is allocated here
 *   layoutPage(page, m, lay);                       // once, when the page is shown
 *   linkFocus(page, lay);                           // once, after layoutPage
 *   checkLayout(page, m, lay, problems, max);       // host checks; optional on the Wii
 *
 * Focus is either FOCUS_ROWS (OptionsFrame's rule: Up/Down keep the column, the tab row is the
 * top of the loop) or FOCUS_GEOMETRY (CustomizeCoopFrame's rule: the nearest control in each
 * direction, scored along + 2 x sideways). Both use only controls that are shown.
 *
 * A page may also set some links by hand (Page::overrides, LinkOverride): applied after the rule,
 * each one only when both of its buttons are focus targets, so one never points at a hidden or
 * inactive control. Current ROM uses four of them to keep Save State and Cycle State together.
 *
 * A list (ROW_LIST, step 4) is a window of N item buttons over a list of any length, plus Prev and
 * Next buttons where the page turns. The engine lays out only the window: the caller gives the
 * count, and a callback that says what item i is, called for the shown items only. The scrolling
 * state is a ListState the caller owns, changed by listTurn() (pages) or listScroll() (focus at
 * the ends), and the page is laid out again after each change -- never per frame.
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

#ifndef MENUENGINE_H
#define MENUENGINE_H

#include "MenuLayout.h"

namespace menu {
namespace engine {

/* Fixed sizes: a page is laid out in these arrays and nothing else is allocated. */
enum {
	MAX_ROWS     = 6,		/* rows from ROW_Y0 down; a seventh could not fit the screen */
	MAX_SLOTS    = 4,		/* focusable buttons in one row: choices, or action buttons */
	MAX_TABS     = 6,
	MAX_HELP     = 8,
	MAX_CONTROLS = 96,
	MAX_PROBLEMS = 64,
	MAX_LIST_SLOTS = 16		/* item buttons in one list's window */
};

/* The numbers of OptionsFrame.cpp, under their names. Three disagree with MenuLayout.h and are
 * kept as OptionsFrame has them, so that a page ported here looks the same:
 *   LABEL_CX    160  -- MenuLayout's TAB_LABEL_CX is 150.
 *   CHOICE_GAP   12  -- MenuLayout's TAB_GAP is 10. The Buttons row uses TAB_GAP.
 *   TAB_ROW_GAP  20  -- the gap between two tabs; TAB_GAP is for buttons in a row.
 */
namespace grid {
	const float TAB_Y         =  20.0f;	/* the tab row: 56 high, centred */
	const float TAB_ROW_GAP   =  20.0f;
	const float TITLE_X       = 320.0f;
	const float TITLE_Y       =  44.0f;
	const float ROW_Y0        = 100.0f;	/* the Settings tabs' first row */
	const float ROW_DY        =  60.0f;	/* row pitch */
	const float BUTTON_H      =  56.0f;
	const float LABEL_CX      = 160.0f;	/* left-hand label, centred */
	const float LABEL_DY      =  20.0f;	/* a label's centre is this far below its row top */
	const float CYCLE_X       = 295.0f;	/* == TAB_BUTTON_X */
	const float CYCLE_W       = 200.0f;
	const float VALUE_CX      = 395.0f;	/* a readout's text: CYCLE_X + CYCLE_W / 2, unless that
	                                         * would run into its label (VALUE_GAP after it) */
	const float VALUE_GAP     =  10.0f;	/* between a readout's label and its text */
	const float RLABEL_CX     = 110.0f;	/* label of a row of choices, centred; a page's choice
	                                         * labels share one centre, moved left only if a row
	                                         * of buttons had to move left (layoutPage) */
	const float CHOICE_X0     = 205.0f;	/* a choice row starts here, or further left (layoutPage) */
	const float CHOICE_W      = 135.0f;	/* a choice's width unless its row fits the words */
	const float CHOICE_DX     = 147.0f;	/* CHOICE_W + 12 */
	const float CHOICE_GAP    =  12.0f;	/* between two fitted choices */
	const float RLABEL_GAP    =  10.0f;	/* between a choice row's label and its first button */
	const float BUTTON_PAD_X  =  16.0f;	/* Button's default padding, left and right */
	const float TEXT_SCALE    =   1.0f;
	/* Help: two columns under the rows. The term is drawn right-aligned in its column. */
	const float HELP_TERM_R   = 152.0f;	/* defaults; a page moves them to centre its block */
	const float HELP_TEXT_X   = 162.0f;
	const float HELP_COL_GAP  =  10.0f;
	const float HELP_SCALE    =  0.62f;
	const float HELP_LINE_DY  =  19.0f;	/* lines of one explanation */
	const float HELP_CELL_GAP =   9.0f;	/* extra before a line that starts a new term */
	const float FONT_H        =  24.0f;	/* the glyph tile: CH_FONT_HEIGHT in IPLFont.cpp */
}

/* Text measurement. The Wii passes IplFont's getStringWidth and getStringHeight; the host
 * passes a file-backed stub. Both return whole pixels, as IplFont does. */
typedef int (*TextWidthFn)(const char *text, float scale, void *ctx);
typedef int (*TextHeightFn)(const char *text, float scale, void *ctx);

struct Metrics {
	TextWidthFn  width;
	TextHeightFn height;
	void        *ctx;
};

/*  Lists  */

/* The scrolling state of one list. The caller owns it and sets count, slots and focus; the
 * engine changes top and focus through listTurn() and listScroll(). Nothing is kept per item. */
struct ListState {
	int count;		/* items in the list, of any length */
	int slots;		/* items shown at once: the window, 1..MAX_LIST_SLOTS */
	int top;		/* the first item shown; a multiple of slots in LIST_PAGED */
	int focus;		/* the item focus lands on when the page is shown; -1 for the first shown */
};

enum ListMode {
	LIST_PAGED,		/* the file browser: Prev and Next turn pages (clamped); focus wraps round the
	                 * window, as the links say; listTurn() with wrap also turns pages by L/R */
	LIST_SCROLL		/* focus moving off either end of the window scrolls it by one item, clamped at
	                 * the list's own ends (listScroll); the links stop at the window's ends */
};

/* What item i is: its text, and whether it is a folder (drawn in the folder colour). The text must
 * stay valid while the page is shown: the caller's own storage, nothing copied. */
struct ListItem {
	const char *text;
	bool        folder;
};

/* Called by the engine for each shown item, when the window is laid out. index is absolute. */
typedef void (*ListItemFn)(void *ctx, int index, ListItem &out);

/* A Prev or Next button: its text and its box, which need not be on the grid. */
struct ListButton {
	const char *text;
	float       x, y, w, h;
};

struct ListSpec {
	ListState  *state;		/* caller-owned */
	ListMode    mode;
	ListItemFn  item;
	void       *ctx;
	float       x0, y0;		/* the first item's top left */
	float       w, h;		/* an item's size */
	float       pitch;		/* item to item; 0 = h */
	ListButton  prev, next;	/* LIST_PAGED's page buttons; drawn in both modes, active only when paged */
};

/*  Rows  */

enum RowKind {
	ROW_TOGGLE,		/* label + one button reading names[value]: two names, e.g. Off / On */
	ROW_CYCLE,		/* label + one button stepping through names[] and wrapping */
	ROW_CHOICE,		/* a row of 2..MAX_SLOTS buttons, the current one lit */
	ROW_READOUT,	/* label + text on the right, not focusable */
	ROW_BUTTONS,	/* no label: action buttons centred on the screen */
	ROW_LIST		/* a ListSpec: a window of item buttons (and Prev/Next); at most one per page */
};

struct ActionButton {
	const char *text;
	float       width;
	bool        lit;		/* Buttons: a toggle that is on (drawn lit); an action is never lit */
	float       dy;			/* Buttons: this far below the row's top; 0 = the row's top (Current ROM's Slot) */
};

struct Row {
	RowKind             kind;
	const char         *label;		/* left-hand text; NULL for none */
	const char *const  *names;		/* Toggle/Cycle/Choice: names[value - lo] */
	int                 lo;			/* Toggle/Cycle/Choice: the value of names[0] */
	int                 count;		/* Toggle/Cycle/Choice: values; Choice and Buttons: buttons */
	int                 value;		/* Toggle/Cycle/Choice: the current value; the host keeps it */
	const char         *text;		/* Readout: the right-hand text; the host refreshes it */
	bool                fit;		/* Choice: each button as wide as its own word */
	const ActionButton *actions;	/* Buttons */
	/* Added for the Settings tabs (step 2). Zero means the default for each. */
	float               x0;			/* Choice, Buttons: the first button's left edge; 0 = where the row goes */
	float               gap;		/* Choice with widths, Buttons: between two buttons; 0 = TAB_GAP */
	const float        *widths;		/* Choice: each button's width (the fixed grid is CHOICE_W); NULL = the default */
	int                 plain;		/* Choice: the last `plain` buttons are actions, never lit */
	/* Bit k set: button k of the row is shown but inactive (This Game with no game loaded).
	 * It is drawn, but it is never a focus target and no link points at it. A Toggle or Cycle
	 * has one button, bit 0. Zero, the default, makes every button active. */
	unsigned            inactive;
	/* Added for lists (step 4). ROW_LIST only. */
	const ListSpec     *list;
};

/*  Pages  */

struct Tab {
	const char *text;
	float       width;
};

struct HelpLine {
	const char *term;	/* NULL: continues the explanation above */
	const char *text;
};

enum LogoSpot {
	LOGO_SPOT_NONE,		/* no logo drawn: both corners are free */
	LOGO_SPOT_TOP,		/* top right, the logo's usual place */
	LOGO_SPOT_BOTTOM	/* bottom right: a page with a tab row, or a row reaching x=634 */
};

enum FocusMode {
	FOCUS_ROWS,			/* OptionsFrame's rule: row by row, the column kept */
	FOCUS_GEOMETRY		/* CustomizeCoopFrame's rule: the nearest control in each direction */
};

enum Dir { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_COUNT };

/* A focus link the page sets by hand, after the rule has made the links: from the button at
 * (row, slot), the direction dir goes to the button at (toRow, toSlot). The override stands only
 * when both are focus targets (shown and active). Otherwise the rule's link is kept, so an
 * override never points at a hidden or inactive control. */
struct LinkOverride {
	int   row, slot;
	Dir   dir;
	int   toRow, toSlot;
};

struct Page {
	const char      *title;		/* shown only when there is no tab row */
	const Tab       *tabs;		/* the tab row, or NULL */
	int              nTabs;
	int              curTab;	/* the lit tab */
	Row             *rows;
	int              nRows;
	const HelpLine  *help;
	int              nHelp;
	LogoSpot         logo;
	FocusMode        focus;
	/* Added for the Settings tabs (step 2). Zero means the default for each. */
	float            labelCx;	/* every label centred here, not LABEL_CX (and RLABEL_CX for a choice's) */
	float            labelDy;	/* a label's centre this far below its row's top, not LABEL_DY */
	float            rowDy;		/* the row pitch, not ROW_DY (the Settings Audio and Saves tabs: 70) */
	float            rowY0;		/* the first row's top, not ROW_Y0 (Main: 60); 0 = ROW_Y0 */
	/* Added for Current ROM (step 3). Zero means none. */
	const LinkOverride *overrides;	/* focus links set by hand, after the rule (linkFocus) */
	int              nOverrides;
};

/*  Layout output  */

enum CtrlKind { CTRL_BUTTON, CTRL_TEXT };

enum CtrlRole {
	ROLE_TAB,
	ROLE_TITLE,
	ROLE_LABEL,		/* left-hand label of a cycle, toggle or readout row */
	ROLE_RLABEL,	/* left-hand label of a choice row (further left) */
	ROLE_VALUE,		/* a readout's text */
	ROLE_BUTTON,	/* a toggle or cycle button, or an action button */
	ROLE_CHOICE,	/* one button of a choice row */
	ROLE_HELP_TERM,	/* bold, right-aligned in the term column */
	ROLE_HELP_TEXT,
	ROLE_LIST_ITEM,	/* one item of a list's window; inactive past the end of the list */
	ROLE_LIST_PAGE	/* a list's Prev or Next button */
};

enum TextAlign {
	ALIGN_LEFT_TOP,		/* the anchor is the top-left corner */
	ALIGN_CENTRE,		/* the anchor is the centre: TextBox(..., centred = true) */
	ALIGN_RIGHT_TOP		/* the anchor is the top-right corner: setRightAligned(true) */
};

struct Rect {
	float x, y, w, h;
};

struct Control {
	CtrlKind    kind;
	CtrlRole    role;
	int         row;			/* the row, or -1 for tabs, title and help */
	int         slot;			/* tab, choice or action index; help: the line */
	Rect        rect;			/* a button's box; a text's drawn box, from its anchor */
	float       ax, ay;			/* text: the anchor the page gives (the TextBox position) */
	TextAlign   align;
	float       scale;			/* text */
	const char *text;
	bool        focusable;		/* a focus target: a shown, active button (or a tab) */
	bool        selected;		/* lit: the current choice or the current tab */
	bool        folder;			/* a list item that is a folder: drawn in the folder colour */
	int         link[DIR_COUNT];	/* index into Layout::c, or -1 */
};

struct Layout {
	Control c[MAX_CONTROLS];		/* shown controls only: a hidden one is not in the list;
	                                 * an inactive button is in it, but not focusable */
	int     n;
	int     tab[MAX_TABS];			/* index of each tab, -1 when there is no tab row */
	int     title;					/* index, or -1 */
	int     slot[MAX_ROWS][MAX_SLOTS];	/* focusable control of each row slot, or -1; a list row's
	                                 * slots 0 and 1 are its Prev and Next buttons */
	int     list;					/* the list row's index, or -1 */
	int     listItem[MAX_LIST_SLOTS];	/* control index of each window slot, or -1 */
	int     defaultFocus;			/* where focus lands when the page is shown, or -1 */
};

/* Returns the number of controls, or -1 if the page does not fit the fixed arrays or has a
 * malformed row (a Choice of more than MAX_SLOTS buttons, a Cycle with no names). */
int layoutPage(const Page &page, const Metrics &m, Layout &out);

/* Sets Up/Down/Left/Right on every focusable control, by page.focus, then applies the page's
 * overrides. Call after layoutPage. */
void linkFocus(const Page &page, Layout &lay);

/* Lists. listTurn() turns a page: LIST_PAGED's Prev and Next clamp (wrap false), L and R wrap
 * (wrap true), and focus lands on the page's first item. Lays out nothing: the caller lays the
 * page out again. */
void listTurn(ListState &s, int delta, bool wrap);

/* LIST_SCROLL: focus moves by delta (+1 or -1), clamped at the list's ends. Where focus leaves the
 * window, the window scrolls by one item to keep it in. Returns true when the window moved. */
bool listScroll(ListState &s, int delta);

/* The width a fitted choice is drawn at: its label plus 16 px each side, rounded up to an
 * even width (Button::fitToLabel). An empty label keeps the choice width. */
int buttonFitWidth(const Metrics &m, const char *text);

enum ProblemKind {
	PROB_OVERLAP,		/* two shown controls share a pixel */
	PROB_IN_LOGO,		/* a control reaches the logo's box */
	PROB_PAST_EDGE,		/* a control comes within MENU_EDGE of an edge, or is off screen */
	PROB_TEXT_WIDE,		/* a button's label is wider than the button */
	PROB_UNREACHABLE,	/* a focusable control cannot be reached from the default focus */
	PROB_BAD_LINK		/* a link points at a control that is not focusable */
};

struct Problem {
	int kind;	/* ProblemKind */
	int a, b;	/* control indices; b is -1 when there is only one */
};

/* Writes up to maxOut problems and returns how many there are. out may be NULL. */
int checkLayout(const Page &page, const Metrics &m, const Layout &lay, Problem *out, int maxOut);

const char *problemName(int kind);

} /* namespace engine */
} /* namespace menu */

#endif
