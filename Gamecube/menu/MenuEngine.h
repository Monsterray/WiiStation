/**
 * WiiStation - MenuEngine.h
 *
 * The menu framework's core (Docs/MENU_PLAN.md, step 1). A page is described as
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
 * state is a ListState the caller owns, changed by listTurn() (pages), and the page is laid out again after each change -- never per frame.
 *
 * Free buttons (step 5) are buttons at their own rectangles, for a picture rather than a grid:
 * Customize Co-Op's presets and controller buttons. Page::freeButtons holds them, after the rows;
 * they take part in FOCUS_GEOMETRY links, and Layout::freeCtl gives each one's control.
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
	MAX_ROWS     = 9,		/* rows from ROW_Y0 down; a page sets its pitch (Memory: 46, eight rows) */
	MAX_SLOTS    = 4,		/* focusable buttons in one row: choices, or action buttons */
	MAX_TABS     = 6,
	MAX_HELP     = 8,
	MAX_CONTROLS = 96,
	MAX_PROBLEMS = 64,
	MAX_LIST_SLOTS = 16,	/* item buttons in one list's window */
	MAX_FREE     = 56		/* free buttons on one page (Configure Input: 2 + 4 + 2 x 4 slots x 2 + 2 x 8 x 3) */
};

/* The numbers of OptionsFrame.cpp, under their names. Three disagree with MenuLayout.h and are
 * kept as OptionsFrame has them, so that a page ported here looks the same:
 *   LABEL_CX    160  -- MenuLayout's TAB_LABEL_CX is 150.
 *   CHOICE_GAP   12  -- MenuLayout's TAB_GAP is 10. The Buttons row uses TAB_GAP.
 *   TAB_ROW_GAP  20  -- the gap between two tabs; TAB_GAP is for buttons in a row.
 */
namespace grid {
	const float TAB_Y         =  30.0f;	/* the tab row: 56 high, centred */
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
	/* Page::columns == 2: one grid for the page. A cell is a right-aligned label, RLABEL_GAP, then a
	 * button; a column's label width is its widest label, its button width its widest fitted button
	 * (so every line shares the edges). The columns are COL2_GAP apart, the grid centred once. */
	const float COL2_GAP      =  24.0f;
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
 * engine changes top and focus through listTurn(). Nothing is kept per item. */
struct ListState {
	int count;		/* items in the list, of any length */
	int slots;		/* items shown at once: the window, 1..MAX_LIST_SLOTS */
	int top;		/* the first item shown; a multiple of slots in LIST_PAGED */
	int focus;		/* the item focus lands on when the page is shown; -1 for the first shown */
};

enum ListMode {
	LIST_PAGED		/* the file browser: Prev and Next turn pages (clamped); focus wraps round the
	                 * window, as the links say; listTurn() with wrap also turns pages by L/R */
};

/* What item i is: its text, and whether it is a folder (drawn in the folder colour). The text must
 * stay valid while the page is shown: the caller's own storage, nothing copied. */
struct ListItem {
	const char *text;
	bool        folder;
	bool        dim;		/* a list item shown dimmed (its source is not there now): grey label */
	const char *tag;	/* DENSITY_EXPLORER: the format tag at the row's right end, or NULL */
	const char *note;	/* DENSITY_EXPLORER: a short text left of the tag (disc count, date), or NULL */
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
	int                 plain;		/* Choice: the last `plain` buttons are actions (lit only as a flag, like a Buttons row) */
	/* Bit k set: button k of the row is shown but inactive (This Game with no game loaded).
	 * It is drawn, but it is never a focus target and no link points at it. A Toggle or Cycle
	 * has one button, bit 0. Zero, the default, makes every button active. */
	unsigned            inactive;
	/* Added for lists (step 4). ROW_LIST only. */
	const ListSpec     *list;
	/* Added for the standard (step 3b). Buttons on a standard page: when set, action k is a cycle
	 * with a text in front of it (Memcard Type: "1" and "2"): prefix[k], then a STD_CYCLES_W button. */
	const char *const  *prefix;
};

/*  Free buttons (step 5)  */

/* A button at its own rectangle, for a screen that is a picture rather than a grid: Customize Co-Op's
 * presets and controller buttons, Configure Buttons' controller. The caller owns the array and sets
 * lit (and inactive) before layoutPage. Its links come from FOCUS_GEOMETRY only, like every button of a
 * geometry page, so a page with free buttons is laid out with that focus mode. */
/* A button that steps through names[] and wraps (ROW_CYCLE is the same rule for a row). The
 * label is always names[index]. A screen does not edit the text of a cycle button by hand. */
struct Cycle {
	const char *const *names;
	unsigned    count;
	unsigned    index;
};
inline const char *cycleText(const Cycle &c) { return c.names[c.index % c.count]; }
inline const char *cycleNext(Cycle &c) { c.index = (c.index + 1) % c.count; return cycleText(c); }

struct FreeButton {
	const char *text;
	float       x, y, w, h;
	bool        lit;
	bool        inactive;		/* drawn, but not a focus target (as Row::inactive) */
};

enum Dir { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_COUNT };

/* A text at its own anchor, for a free page's labels: centred on (ax, ay) or with its top left there,
 * drawn at scale. The caller owns the text and keeps it valid while the page is shown. */
struct FreeText {
	const char *text;
	float       ax, ay;
	float       scale;
	bool        centred;
	bool        right;		/* the anchor is the top-right corner (standard pages; not with centred) */
};

/* A focus link the caller sets by hand on a free page, after the rule (linkFocus): from free button
 * `from` (its index in Page::freeButtons), the direction dir goes to free button `to`; to < 0 means
 * no link. Applied only when `from` is a focus target, and when `to` is one too (or to < 0), so a
 * link never points at a hidden or inactive control: the rule's link stands otherwise. */
struct FreeLink {
	int   from;
	Dir   dir;
	int   to;
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


/* A focus link the page sets by hand, after the rule has made the links: from the button at
 * (row, slot), the direction dir goes to the button at (toRow, toSlot). The override stands only
 * when both are focus targets (shown and active). Otherwise the rule's link is kept, so an
 * override never points at a hidden or inactive control. */
struct LinkOverride {
	int   row, slot;
	Dir   dir;
	int   toRow, toSlot;
};

/* How a standard page is laid out (STD_* in MenuLayout.h). */
enum Density {
	DENSITY_OFF = 0,	/* not the standard: the page keeps the grid:: layout */
	DENSITY_REGULAR,	/* buttons 50 high, pitch 56, 6 rows */
	DENSITY_COMPACT,	/* buttons 40 high, pitch 46, 7 rows */
	DENSITY_TEXT,		/* text rows: 28 high, pitch 32, 10 rows (readout pages) */
	DENSITY_EXPLORER	/* one ROW_LIST: STD_LIST_ROWS (8) rows 34 high, pitch 34 (touching), from y 126, tag and note at the row's
	                     * right end, names cut to fit; the ListSpec's own geometry is ignored, only its
	                     * state, item function and the page arrows' texts are used (STD_LIST_*) */
};

/* The help of one row, for the footer: up to two lines. */
struct FooterHelp {
	const char *line[2];	/* NULL = none */
};

struct Page {
	const char      *title;	/* shown only when there is no tab row */
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
	/* Added for Customize Co-Op (step 5). Zero means the default for each. */
	const FreeButton *freeButtons;	/* buttons at their own rectangles, after the rows */
	int              nFreeButtons;
	float            titleY;		/* the title's centre y, not TITLE_Y (Customize Co-Op: 112) */
	/* Added for Configure Input and Configure Buttons (step 5). Zero means none. */
	const FreeText   *freeTexts;	/* labels at their own anchors, after the free buttons */
	int              nFreeTexts;
	const FreeLink   *freeLinks;	/* focus links set by hand, after the rule and overrides */
	int              nFreeLinks;
	/* Edge wrap. FOCUS_GEOMETRY only; FOCUS_ROWS always wraps. Off by default.
	 * A direction with no target wraps to the farthest focusable control on its row or column.
	 * With none there, it wraps to the farthest focusable control on that side, nearest in the cross axis. */
	bool             wrap;
	/* The compact (four-column) layout, for any page with many short settings: 2 = two cells of
	 * label + button per line, on one grid for the page (shared label edges and button x; each
	 * button as wide as its widest name, so cycling never moves it). Toggle and Cycle rows fill
	 * the cells in reading order (left, then right, then the next line); any other row takes a
	 * whole line, and a choice row starts on the grid (label edge and first button of column 1).
	 * Zero or 1 is one row per line, as every page was. Help runs under the last line.
	 * FOCUS_ROWS: Left and Right run along a line, Up and Down keep the column.
	 * Use: columns = 2 in the page (eng::Page, or PG_COLS(2) in OptionsPages.h); nothing else changes.
	 * A standard page does the same with Cycle rows only (a Toggle is a whole line there): each cell is its
	 * own Row, so its focus, footer help, hit box and menuclick key are the row's; Up and Down keep the column. */
	int              columns;
	/* The menu standard (MenuLayout.h STD_*, Docs/MENU_PLAN.md 1.1-1.3). Zero, the default, is every
	 * page as it was; a non-zero Density opts the page in and layoutPage uses layoutStandard's
	 * geometry instead of grid:: (the old path is untouched). The grid's own fields (labelCx, labelDy,
	 * rowDy, rowY0, titleY, tab widths, Row widths and x0) are then ignored: the label edge,
	 * the rows' y and every width come from the standard and the text. */
	Density          standard;
	/* Footer help: footer[i] is the help of row i (0-2 lines, either NULL), shown for the focused row
	 * only, see layoutFooter. NULL = the page has none. Standard pages only. */
	const FooterHelp *footer;
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
	ROLE_LIST_PAGE,	/* a list's Prev or Next button */
	ROLE_FREE,		/* a free button: at its own rectangle, not on the grid (FreeButton) */
	ROLE_FREE_TEXT,	/* a free text: at its own anchor (FreeText) */
	ROLE_LIST_TAG,	/* an explorer row's format tag: right-aligned text, row = list row, slot = window slot */
	ROLE_LIST_NOTE,	/* an explorer row's note, left of the tag */
	ROLE_FOOTER		/* a line of footer help (layoutFooter); never part of a Layout */
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
	bool        dim;			/* a list item drawn dimmed (Recent: its device is not there) */
	/* A DENSITY_EXPLORER list item: text is the name cut to fit (the caller's own text when it fits),
	 * full the whole name, textX and textR the x range the name is drawn in (a focused row scrolls
	 * its full name inside it). Zero in every other control. */
	const char *full;
	float       textX, textR;
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
	int     freeCtl[MAX_FREE];		/* control index of each free button, or -1 */
	int     defaultFocus;			/* where focus lands when the page is shown, or -1 */
	char    cut[MAX_LIST_SLOTS][STD_CUT_LEN];	/* the cut names of an explorer list (Control::text points here) */
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

/*  The letter strip and the scroll thumb (DENSITY_EXPLORER)
 *
 * Cell 0 is "#" (a name that starts with anything but a letter), cells 1-26 are A-Z. A name's cell
 * is its first character, upper-cased (ASCII only); "", "." and ".." (the up row) have none (-1). */
const int STRIP_CELLS = 27;
typedef const char *(*NameFn)(void *ctx, int index);	/* the name of item index; may return NULL */

int stripLetter(const char *name);				/* the cell of a name, -1 for none */
char stripChar(int cell);						/* '#', 'A'..'Z' */
unsigned stripMask(int count, NameFn name, void *ctx);	/* bit k set when some name has cell k */
/* The first item of cell k in list order (-1 when none). In a list that is sorted by name this is
 * the letter's first row; a list that puts folders first has a run of folders, then of files. */
int stripFirst(int count, NameFn name, void *ctx, int cell);
/* The next cell with items after cur (dir +1) or before it (dir -1); -1 at the end. cur -1 (nothing
 * lit) starts from the edge. No wrap. */
int stripStep(unsigned mask, int cur, int dir);
/* Cell k's box: x, y, w, h (y and h are the strip's). Cells never touch (STD_STRIP_GAP) and lie inside
 * STD_STRIP_X0..STD_STRIP_X1. The cell under a point, or -1: the gap belongs to the cell before it. */
void stripCell(int k, int *x, int *w);
int stripHit(int x, int y);

/* The scroll thumb between track ends y0 and y1: it is drawn by page (top / slots), so a drag maps back
 * exactly. live is false when everything fits (the thumb then fills the track). */
struct Thumb { int y, h; bool live; };
Thumb scrollThumb(const ListState &s, int y0, int y1);
/* The page whose thumb is nearest to a thumb top at thumbY (a drag: pointer y minus where it grabbed). */
int scrollDragPage(const ListState &s, int y0, int y1, int thumbY);

/* The facts strip (STD_FACTS_*): the panel, the title line and six fact cells (0-2 the first column,
 * 3-5 the second). Every y is a text top; keyR is the key's right edge, valX the value's left edge,
 * valRoom the width the value may take. Pure numbers: the frame draws. */
struct FactsCell { int keyR, valX, y, valRoom; };
struct FactsBox {
	int x0, y0, x1, y1;				/* the panel */
	int titleX, titleY, titleRoom;	/* the title line's left, top and room */
	FactsCell cell[STD_FACTS_N];
};
void factsLayout(FactsBox &out);
/* `text` cut with "..." to room px at scale (the text itself when it fits; else buf, bufSize bytes). */
const char *fitText(const Metrics &m, const char *text, float scale, int room, char *buf, int bufSize);
/* `text` into out (size bytes): itself when it fits room px, else "..." and the longest tail that fits
 * (cut at a character boundary; at least the first character goes). */
void cutTextLeft(const Metrics &m, const char *text, float scale, int room, char *out, int size);

/* The explorer's count text: "42 games, A-Z", "3 folders, 6 games, folders first", on Recent
 * "9 games, newest first" and, with a letter filter on (filterCell >= 0), "3 games, C". Too wide
 * for room: the hint goes, then the text is cut with "...". `hint` is a COUNT_HINT_*. */
enum { COUNT_HINT_NONE, COUNT_HINT_AZ, COUNT_HINT_FOLDERS, COUNT_HINT_NEWEST };
void countText(const Metrics &m, float scale, int room, int folders, int games, int hint, int filterCell,
               char *out, int size);

/* Tabs. The shoulder buttons (menu::glue::tabShoulder) turn the tabs of every tabbed screen:
 * delta -1 or +1 from tab cur of n. wrap: past the last tab is the first (the TabWrap setting);
 * without it the ends stop. Returns the new tab, or cur when there is none to go to. */
int tabStep(int cur, int n, int delta, bool wrap);

/* The width a fitted choice is drawn at: its label plus 16 px each side, rounded up to an
 * even width (Button::fitToLabel). An empty label keeps the choice width. */
int buttonFitWidth(const Metrics &m, const char *text);

/*  Widths of the standard (Docs/MENU_PLAN.md 1.2: "widths come from the text")
 *
 * No page types a width. Each width is the measured text (the menu font, scale 1) plus a padding,
 * rounded up to a multiple of STD_STEP (8). An empty text measures 0. */

/* text + pad, rounded up to a multiple of 8. */
int stdStepWidth(const Metrics &m, const char *text, int pad);

/* A separate button (an action, a flag, a sub-page button): text + 32. */
int stdButtonWidth(const Metrics &m, const char *text);

/* The segments of one choice (they touch). Writes n widths to out and returns their sum.
 * Equal widths, the widest text + 32, when n of them fit in `room`; otherwise each segment its own
 * text + 16. The sum may still exceed room: that is a layout error the caller reports (a row that
 * does not fit is never squeezed). `equal` (may be NULL) is set to whether the equal rule was used. */
int stdSegmentWidths(const Metrics &m, const char *const *names, int n, int room, int *out, bool *equal);

/* A tab strip's widths (n values to out; returns the strip's total width with the gaps). Equal widths,
 * the widest text + 16 rounded up to 8 (STD_TAB_PAD), when the strip fits between the screen margins;
 * otherwise each tab its own text + 16 (STD_TAB_PAD_OWN). The gap between tabs is STD_TAB_GAP. */
int stdTabWidths(const Metrics &m, const Tab *tabs, int n, int *out);

/*  Footer help
 *
 * The help of the focused row (page.footer[row]): up to STD_HELP_LINES lines of STD_HELP_PX px text,
 * left-aligned at STD_HELP_X0, from STD_HELP_Y0 down, STD_HELP_DY apart. Writes the lines to out
 * (ROLE_FOOTER texts, ALIGN_LEFT_TOP, not part of any Layout) and returns how many (0 when the page has
 * no footer or the row no help). -1 when a line is wider than STD_HELP_X1 - STD_HELP_X0. The caller
 * draws them for the focused row only, and clears them when focus leaves. */
int layoutFooter(const Page &page, const Metrics &m, int row, Control out[STD_HELP_LINES]);

/* The number of rows of a standard page whose footer help is invalid (too wide); 0 = all fit. */
int checkFooters(const Page &page, const Metrics &m);

enum ProblemKind {
	PROB_OVERLAP,		/* two shown controls share a pixel */
	PROB_IN_LOGO,		/* a control reaches the logo's box */
	PROB_PAST_EDGE,		/* a control comes within MENU_EDGE of an edge, or is off screen */
	PROB_TEXT_WIDE,		/* a button's label is wider than the button */
	PROB_UNREACHABLE,	/* a focusable control cannot be reached from the default focus */
	PROB_BAD_LINK,		/* a link points at a control that is not focusable */
	/* Standard pages only (Page::standard != DENSITY_OFF); the old checks above apply to every page,
	 * except IN_LOGO, which uses the standard's own box there. */
	PROB_LABEL_WIDE,	/* a label is wider than STD_LABEL_MAX_W (224 px) */
	PROB_PAST_RIGHT,	/* a body control reaches past x STD_CTRL_X1 (616) */
	PROB_IN_STD_LOGO,	/* a control enters the standard's logo box (STD_LOGO_*) */
	PROB_BTN_TOUCH,		/* two action buttons, or an action and a segment, closer than STD_BTN_GAP */
	PROB_ARROW_TIGHT	/* a Cycle button too narrow for its widest value plus its two arrows (STD_CELL_PAD) */
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
