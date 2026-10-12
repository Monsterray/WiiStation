/**
 * WiiStation - MenuEngine.cpp
 *
 * The menu framework's layout and focus, with no GX or libogc calls (see MenuEngine.h).
 *
 * Every number and every rounding step here is OptionsFrame.cpp's: the help block's centring
 * and logo shift, the integer pixel positions TextBox::drawComponent and IplFont::drawString
 * produce, the even widths Button::fitToLabel gives. That is what lets the host test check
 * this against OptionsFrame's own arithmetic, and lets a page be ported without its look moving.
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

#include <math.h>
#include <string.h>

#include "MenuEngine.h"

namespace menu {
namespace engine {

using namespace grid;

/* Every control the worst legal page can need must fit in Layout::c: the tabs, the title,
 * two controls per row plus a row of up to MAX_SLOTS buttons, two per help line, and a list's
 * window with its Prev and Next. */
typedef char engine_controls_fit
	[(MAX_TABS + 1 + MAX_ROWS * (2 + MAX_SLOTS) + 2 * MAX_HELP + MAX_LIST_SLOTS + 2) <= MAX_CONTROLS ? 1 : -1];

/* The text of a measured string. OptionsFrame's helpWidth() gives 0 for an empty string; the
 * Button fit and TextBox drawing use IplFont directly, which gives 5 for an empty one. */
static int measure(const Metrics &m, const char *s, float scale)
{
	if (!s || !*s)
		return 0;
	return m.width(s, scale, m.ctx);
}

int buttonFitWidth(const Metrics &m, const char *text)
{
	if (!text || !*text)
		return (int)CHOICE_W;
	{
		float tw = (float)m.width(text, 1.0f, m.ctx);
		return (int)(2.0f * ceilf((tw + 2.0f * BUTTON_PAD_X) / 2.0f));
	}
}

/* Button k of the row is shown but inactive: drawn, never a focus target (Row::inactive). */
static bool isInactive(const Row &r, int k)
{
	return k < 32 && ((r.inactive >> k) & 1u) != 0;
}

static bool rowValid(const Row &r)
{
	switch (r.kind) {
	case ROW_TOGGLE:
	case ROW_CYCLE:   return r.count >= 1 && r.names != NULL;
	case ROW_CHOICE:  return r.count >= 1 && r.count <= MAX_SLOTS && r.names != NULL;
	case ROW_BUTTONS: return r.count >= 1 && r.count <= MAX_SLOTS && r.actions != NULL;
	case ROW_READOUT: return true;
	case ROW_LIST:
		return r.list && r.list->state && r.list->item &&
		       r.list->state->slots >= 1 && r.list->state->slots <= MAX_LIST_SLOTS &&
		       r.list->state->count >= 0;
	}
	return false;
}

/* The value a row shows: OptionsFrame's refreshRow() puts an out-of-range value back at the
 * lowest one (a value the settings file parser let through). */
static int shownValue(const Row &r)
{
	int v = r.value;
	if (v < r.lo || v >= r.lo + r.count)
		v = r.lo;
	return v;
}

/* The top of line i: the page's first row (rowY0, or ROW_Y0) plus i row pitches, the page's own
 * pitch or ROW_DY. */
static float rowTop(const Page &p, int i)
{
	return (p.rowY0 > 0 ? p.rowY0 : ROW_Y0) + i * (p.rowDy > 0 ? p.rowDy : ROW_DY);
}

/* Which line and column row i is in. One column: line i, column 0. Two columns: a Toggle or
 * Cycle row takes the open right-hand cell of the line above, or opens a new line; any other
 * row is a line of its own. */
static void rowCell(const Page &p, int i, int &line, int &col)
{
	bool open = false;		/* the line has a left cell and no right one yet */
	line = -1;
	col = 0;
	for (int k = 0; k <= i; k++) {
		const bool cell = p.columns >= 2 && (p.rows[k].kind == ROW_TOGGLE || p.rows[k].kind == ROW_CYCLE);
		if (cell && open) {
			col = 1;
			open = false;
		} else {
			line++;
			col = 0;
			open = cell;
		}
	}
}

/* How many lines the rows take: where the help starts. */
static int pageLines(const Page &p)
{
	int line, col;
	if (p.nRows <= 0)
		return 0;
	rowCell(p, p.nRows - 1, line, col);
	return line + 1;
}

/*  Placing  */

static int newControl(Layout &out)
{
	int i = out.n++;
	Control &c = out.c[i];
	memset(&c, 0, sizeof c);
	c.row = -1;
	for (int d = 0; d < DIR_COUNT; d++)
		c.link[d] = -1;
	return i;
}

/* Where a text is drawn, in whole pixels, as TextBox::drawComponent and IplFont::drawString
 * compute it: the anchor is cut to an int first, then a centred string is moved back by half
 * its size (integer division), and a right-aligned one by its whole width. */
static void placeText(Control &c, const Metrics &m)
{
	const char *s = c.text ? c.text : "";
	int w = m.width(s, c.scale, m.ctx);
	int h = m.height(s, c.scale, m.ctx);
	int ax = (int)c.ax, ay = (int)c.ay;
	int x = ax, y = ay;

	if (c.align == ALIGN_RIGHT_TOP) {
		x = ax - w;
	} else if (c.align == ALIGN_CENTRE) {
		x = ax - w / 2;
		y = ay - h / 2;
	}
	c.rect.x = (float)x;
	c.rect.y = (float)y;
	c.rect.w = (float)w;
	c.rect.h = (float)h;
	if (c.role == ROLE_HELP_TERM)
		c.rect.w += 1.0f;	/* the term is drawn bold: a second copy one pixel to the right */
}

static int addText(Layout &out, const Metrics &m, CtrlRole role, int row, int slot,
                   float ax, float ay, TextAlign align, float scale, const char *text)
{
	int i = newControl(out);
	Control &c = out.c[i];
	c.kind = CTRL_TEXT;
	c.role = role;
	c.row = row;
	c.slot = slot;
	c.ax = ax;
	c.ay = ay;
	c.align = align;
	c.scale = scale;
	c.text = text ? text : "";
	c.focusable = false;
	placeText(c, m);
	return i;
}

static int addButton(Layout &out, CtrlRole role, int row, int slot,
                     float x, float y, float w, float h, const char *text, bool lit, bool focusable)
{
	int i = newControl(out);
	Control &c = out.c[i];
	c.kind = CTRL_BUTTON;
	c.role = role;
	c.row = row;
	c.slot = slot;
	c.rect.x = x;
	c.rect.y = y;
	c.rect.w = w;
	c.rect.h = h;
	c.ax = x;
	c.ay = y;
	c.align = ALIGN_CENTRE;
	c.scale = TEXT_SCALE;
	c.text = text ? text : "";
	c.focusable = focusable;
	c.selected = lit;
	return i;
}

/* The right edge a line may reach, given its lowest pixel (MenuLayout.h's MENU_ROW_RIGHT, for
 * whichever corner the logo is in). */
static float rowRight(float bottom, LogoSpot spot)
{
	if (spot == LOGO_SPOT_BOTTOM && bottom > LOGO_BOX_T(LOGO_PAGE_Y))
		return LOGO_BOX_L(LOGO_PAGE_X) - MENU_EDGE;
	if (spot == LOGO_SPOT_TOP && bottom > LOGO_BOX_T(LOGO_MAIN_Y))
		return LOGO_BOX_L(LOGO_MAIN_X) - MENU_EDGE;
	return MENU_RIGHT;
}

/* The help block: two columns, centred on the screen as a whole, then moved left far enough
 * that its lower lines clear the logo's corner, and no further. */
static void layoutHelp(const Page &p, const Metrics &m, Layout &out)
{
	const float helpLineH = FONT_H * HELP_SCALE;
	int wTerm = 0, wText = 0, shift = 0;
	float termR, textX, y;

	for (int i = 0; i < p.nHelp; i++) {
		const HelpLine &h = p.help[i];
		if (h.term) {
			int w = measure(m, h.term, HELP_SCALE);
			if (w > wTerm) wTerm = w;
		}
		{
			int w = measure(m, h.text, HELP_SCALE);
			if (w > wText) wText = w;
		}
	}
	termR = (MENU_W - (wTerm + HELP_COL_GAP + wText)) / 2 + wTerm;
	textX = termR + HELP_COL_GAP;

	y = rowTop(p, pageLines(p));
	for (int i = 0; i < p.nHelp; i++) {
		const HelpLine &h = p.help[i];
		int over;
		if (i > 0 && h.term) y += HELP_CELL_GAP;
		over = (int)(textX + measure(m, h.text, HELP_SCALE) - rowRight(y + helpLineH, p.logo));
		if (over > shift) shift = over;
		y += HELP_LINE_DY;
	}
	if (termR - wTerm - shift < MENU_EDGE)
		shift = (int)(termR - wTerm - MENU_EDGE);
	termR -= shift;
	textX -= shift;

	y = rowTop(p, pageLines(p));
	for (int i = 0; i < p.nHelp; i++) {
		const HelpLine &h = p.help[i];
		if (i > 0 && h.term) y += HELP_CELL_GAP;
		if (h.term)
			addText(out, m, ROLE_HELP_TERM, -1, i, termR, y, ALIGN_RIGHT_TOP, HELP_SCALE, h.term);
		addText(out, m, ROLE_HELP_TEXT, -1, i, textX, y, ALIGN_LEFT_TOP, HELP_SCALE, h.text);
		y += HELP_LINE_DY;
	}
}

/* A choice button's width: its own, the fitted one, or the fixed grid's. */
static float choiceWidth(const Metrics &m, const Row &r, int c)
{
	if (r.widths)
		return r.widths[c];
	return r.fit ? (float)buttonFitWidth(m, r.names[c]) : CHOICE_W;
}

/* The space between two choice buttons (or two action buttons, for Buttons rows). */
static float choiceGap(const Row &r)
{
	if (r.widths)
		return r.gap > 0 ? r.gap : TAB_GAP;
	return CHOICE_GAP;
}

/* A choice row's width, from its first button's left edge to its last one's right edge. */
static float choiceRowWidth(const Metrics &m, const Row &r)
{
	float total = 0;
	for (int c = 0; c < r.count; c++) {
		total += choiceWidth(m, r, c);
		if (c + 1 < r.count)
			total += choiceGap(r);
	}
	return total;
}

/* Where a choice row's first button goes: CHOICE_X0, or further left when the row would run
 * past the right edge its lowest pixel allows, and no further. */
static float choiceStart(const Metrics &m, const Row &r, float y, LogoSpot spot)
{
	const float right = rowRight(y + BUTTON_H, spot);
	const float total = choiceRowWidth(m, r);

	return CHOICE_X0 + total > right ? right - total : CHOICE_X0;
}

/* Where a choice or buttons row starts: its own x0 when the page gives one, else the default. */
static float rowStart(const Metrics &m, const Row &r, float y, LogoSpot spot)
{
	return r.x0 > 0 ? r.x0 : choiceStart(m, r, y, spot);
}

/* A list's window: its Prev and Next buttons, then the item buttons, slot by slot. The items past
 * the end of the list are drawn inactive, with no text, as the file browser has always drawn its
 * empty slots. Items are asked for only in the window: nothing is kept per item. */
static void layoutList(Layout &out, const ListSpec &L, int row)
{
	const ListState &s = *L.state;
	const float pitch = L.pitch > 0 ? L.pitch : L.h;
	const bool paged = L.mode == LIST_PAGED;
	const bool canPrev = paged && s.top > 0;
	const bool canNext = paged && s.top + s.slots < s.count;

	out.slot[row][0] = addButton(out, ROLE_LIST_PAGE, row, 0, L.prev.x, L.prev.y, L.prev.w, L.prev.h,
	                             L.prev.text, false, canPrev);
	out.slot[row][1] = addButton(out, ROLE_LIST_PAGE, row, 1, L.next.x, L.next.y, L.next.w, L.next.h,
	                             L.next.text, false, canNext);
	for (int k = 0; k < s.slots; k++) {
		const int index = s.top + k;
		const bool shown = index < s.count;
		ListItem it;
		int c;

		it.text = "";
		it.folder = false;
		if (shown)
			L.item(L.ctx, index, it);
		c = addButton(out, ROLE_LIST_ITEM, row, k, L.x0, L.y0 + k * pitch, L.w, L.h,
		              shown && it.text ? it.text : "", false, shown);
		out.c[c].folder = shown && it.folder;
		out.listItem[k] = c;
	}
}

/* Every control a page makes, counted before any is placed, so that a page too big for Layout::c is
 * refused here rather than written past its end. A row of buttons is its label and its buttons. */
static int controlBound(const Page &p)
{
	int n = p.nTabs > 0 ? p.nTabs : (p.title ? 1 : 0);
	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		switch (r.kind) {
		case ROW_TOGGLE:
		case ROW_CYCLE:
		case ROW_READOUT: n += 2; break;
		case ROW_CHOICE:
		case ROW_BUTTONS: n += 1 + r.count; break;
		case ROW_LIST:    n += 2 + r.list->state->slots; break;
		}
	}
	return n + p.nFreeButtons + p.nFreeTexts + 2 * p.nHelp;
}

int layoutPage(const Page &p, const Metrics &m, Layout &out)
{
	if (p.nRows < 0 || p.nRows > MAX_ROWS || p.nHelp < 0 || p.nHelp > MAX_HELP ||
	    p.nTabs < 0 || p.nTabs > MAX_TABS || (p.nTabs > 0 && (!p.tabs || p.curTab < 0 || p.curTab >= p.nTabs)) ||
	    p.nFreeButtons < 0 || p.nFreeButtons > MAX_FREE || (p.nFreeButtons > 0 && !p.freeButtons) ||
	    p.nFreeTexts < 0 || (p.nFreeTexts > 0 && !p.freeTexts) || p.nFreeLinks < 0 || (p.nFreeLinks > 0 && !p.freeLinks))
		return -1;
	int lists = 0;
	for (int i = 0; i < p.nRows; i++) {
		if (!rowValid(p.rows[i]))
			return -1;
		if (p.rows[i].kind == ROW_LIST)
			lists++;
	}
	if (lists > 1 || controlBound(p) > MAX_CONTROLS)
		return -1;

	out.n = 0;
	out.title = -1;
	out.list = -1;
	out.defaultFocus = -1;
	for (int t = 0; t < MAX_TABS; t++)
		out.tab[t] = -1;
	for (int r = 0; r < MAX_ROWS; r++)
		for (int s = 0; s < MAX_SLOTS; s++)
			out.slot[r][s] = -1;
	for (int k = 0; k < MAX_LIST_SLOTS; k++)
		out.listItem[k] = -1;
	for (int k = 0; k < MAX_FREE; k++)
		out.freeCtl[k] = -1;

	/* The choice labels' shared centre: RLABEL_CX, moved left only when a row of buttons had
	 * to move left, so that its label still clears them by RLABEL_GAP. */
	const float lcx = p.labelCx > 0 ? p.labelCx : LABEL_CX;	/* a row's label, left of its controls */
	const float ldy = p.labelDy > 0 ? p.labelDy : LABEL_DY;
	float rlabelCx = p.labelCx > 0 ? p.labelCx : RLABEL_CX;
	for (int i = 0; i < p.nRows && p.labelCx <= 0; i++) {
		const Row &r = p.rows[i];
		if (r.kind != ROW_CHOICE || !r.label)
			continue;
		{
			int line, col;
			rowCell(p, i, line, col);
			const float x0 = rowStart(m, r, rowTop(p, line), p.logo);
			const int lw = measure(m, r.label, TEXT_SCALE);
			const float cx = x0 - RLABEL_GAP - (float)(lw - lw / 2);
			if (cx < rlabelCx)
				rlabelCx = cx;
		}
	}

	/* The tab row: centred, TAB_ROW_GAP apart. */
	const bool tabbed = p.nTabs > 0;
	if (tabbed) {
		float total = TAB_ROW_GAP * (p.nTabs - 1);
		float x;
		for (int t = 0; t < p.nTabs; t++)
			total += p.tabs[t].width;
		x = (MENU_W - total) / 2;
		for (int t = 0; t < p.nTabs; t++) {
			out.tab[t] = addButton(out, ROLE_TAB, -1, t, x, TAB_Y, p.tabs[t].width, BUTTON_H,
			                       p.tabs[t].text, t == p.curTab, true);
			x += p.tabs[t].width + TAB_ROW_GAP;
		}
	} else if (p.title) {
		out.title = addText(out, m, ROLE_TITLE, -1, 0, TITLE_X, p.titleY > 0 ? p.titleY : TITLE_Y,
		                    ALIGN_CENTRE, TEXT_SCALE, p.title);
	}

	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		int line, col;
		rowCell(p, i, line, col);
		const float y = rowTop(p, line);

		switch (r.kind) {
		case ROW_TOGGLE:
		case ROW_CYCLE: {
			const bool two = p.columns >= 2;
			if (r.label)
				addText(out, m, ROLE_LABEL, i, 0, two ? COL2_LABEL_CX[col] : lcx, y + ldy, ALIGN_CENTRE, TEXT_SCALE, r.label);
			out.slot[i][0] = addButton(out, ROLE_BUTTON, i, 0, two ? COL2_BTN_X[col] : CYCLE_X, y,
			                           two ? COL2_BTN_W : CYCLE_W, BUTTON_H,
			                           r.names[shownValue(r) - r.lo], false, !isInactive(r, 0));
			break;
		}
		case ROW_READOUT: {
			/* The text is centred on VALUE_CX, unless its label would run into it: then it
			 * starts VALUE_GAP after the label's right edge, as TextBox would draw it. */
			float vx = VALUE_CX;
			if (r.label) {
				const int lw = measure(m, r.label, TEXT_SCALE);
				const int vw = measure(m, r.text, TEXT_SCALE);
				const int need = (int)lcx - lw / 2 + lw + (int)VALUE_GAP;
				if ((int)vx - vw / 2 < need)
					vx = (float)(need + vw / 2);
			}
			if (r.label)
				addText(out, m, ROLE_LABEL, i, 0, lcx, y + ldy, ALIGN_CENTRE, TEXT_SCALE, r.label);
			addText(out, m, ROLE_VALUE, i, 0, vx, y + ldy, ALIGN_CENTRE, TEXT_SCALE, r.text);
			break;
		}
		case ROW_CHOICE: {
			/* The last `plain` buttons are actions: drawn like the rest, never lit. */
			const int lit = shownValue(r) - r.lo;
			float cx = rowStart(m, r, y, p.logo);
			if (r.label)
				addText(out, m, ROLE_RLABEL, i, 0, rlabelCx, y + ldy, ALIGN_CENTRE, TEXT_SCALE, r.label);
			for (int c = 0; c < r.count; c++) {
				float w = choiceWidth(m, r, c);
				out.slot[i][c] = addButton(out, ROLE_CHOICE, i, c, cx, y, w, BUTTON_H, r.names[c],
				                           c < r.count - r.plain && c == lit, !isInactive(r, c));
				cx += w + choiceGap(r);
			}
			break;
		}
		case ROW_LIST:
			out.list = i;
			layoutList(out, *r.list, i);
			break;
		case ROW_BUTTONS: {
			/* Centred on the screen, or from x0. With a label, the label is drawn left of them. */
			const float gap = r.gap > 0 ? r.gap : TAB_GAP;
			float total = gap * (r.count - 1);
			float x;
			for (int k = 0; k < r.count; k++)
				total += r.actions[k].width;
			x = r.x0 > 0 ? r.x0 : (MENU_W - total) / 2;
			if (r.label)
				addText(out, m, ROLE_LABEL, i, 0, lcx, y + ldy, ALIGN_CENTRE, TEXT_SCALE, r.label);
			for (int k = 0; k < r.count; k++) {
				out.slot[i][k] = addButton(out, ROLE_BUTTON, i, k, x, y + r.actions[k].dy, r.actions[k].width, BUTTON_H,
				                           r.actions[k].text, r.actions[k].lit, !isInactive(r, k));
				x += r.actions[k].width + gap;
			}
			break;
		}
		}
	}

	for (int k = 0; k < p.nFreeButtons; k++) {
		const FreeButton &b = p.freeButtons[k];
		out.freeCtl[k] = addButton(out, ROLE_FREE, -1, k, b.x, b.y, b.w, b.h, b.text, b.lit, !b.inactive);
	}
	for (int k = 0; k < p.nFreeTexts; k++) {
		const FreeText &t = p.freeTexts[k];
		addText(out, m, ROLE_FREE_TEXT, -1, k, t.ax, t.ay, t.centred ? ALIGN_CENTRE : ALIGN_LEFT_TOP,
		        t.scale, t.text);
	}

	if (p.nHelp > 0)
		layoutHelp(p, m, out);
	return out.n;
}

/*  Focus  */

/* The focus targets of row i, in order: the control indices of its focusable buttons. An
 * inactive button is not one, so a row with none takes no part in the links. */
static void activeSlots(const Layout &lay, int i, int out[MAX_SLOTS], int &n)
{
	n = 0;
	for (int c = 0; c < MAX_SLOTS; c++) {
		const int k = lay.slot[i][c];
		if (k >= 0 && lay.c[k].focusable)
			out[n++] = k;
	}
}

/* OptionsFrame's rule. Rows run top to bottom and wrap, a row's buttons go left and right
 * and wrap, and Up/Down keep the column (a shorter row takes its last column). On a tabbed
 * page the tab row is the top of the loop: the last row goes down to the lit tab, the first
 * row up to it, and every tab goes down to the first row and up to the last. Inactive
 * buttons are left out of every row, so the columns are those of the active buttons. */
static void linkRows(const Page &p, Layout &lay)
{
	int F[MAX_ROWS], nf = 0;
	int A[MAX_ROWS][MAX_SLOTS], nA[MAX_ROWS];
	const bool tabbed = p.nTabs > 0;
	const int cur = tabbed ? lay.tab[p.curTab] : -1;

	/* A line is a row, or on a two-column page the one or two cells that share it: its buttons
	 * run left to right as one row. A list's links are its own (linkList). */
	const int nl = pageLines(p);
	for (int l = 0; l < nl; l++)
		nA[l] = 0;
	for (int i = 0; i < p.nRows; i++) {
		int line, col, a[MAX_SLOTS], n;
		if (i == lay.list)
			continue;
		rowCell(p, i, line, col);
		activeSlots(lay, i, a, n);
		for (int k = 0; k < n; k++)
			A[line][nA[line]++] = a[k];
	}
	for (int l = 0; l < nl; l++)
		if (nA[l] > 0)
			F[nf++] = l;

	for (int k = 0; k < nf; k++) {
		const int i = F[k], n = nA[i];
		if (n > 1)
			for (int c = 0; c < n; c++) {
				lay.c[A[i][c]].link[DIR_LEFT]  = A[i][(c + n - 1) % n];
				lay.c[A[i][c]].link[DIR_RIGHT] = A[i][(c + 1) % n];
			}
	}

	for (int k = 0; k < nf; k++) {
		const bool wrap = (k == nf - 1);
		int a, b;
		if (wrap && tabbed)
			continue;		/* the tab row takes the wrap, below */
		a = F[k];
		b = F[wrap ? 0 : k + 1];
		for (int c = 0; c < nA[a]; c++)
			lay.c[A[a][c]].link[DIR_DOWN] = A[b][c < nA[b] ? c : nA[b] - 1];
		for (int c = 0; c < nA[b]; c++)
			lay.c[A[b][c]].link[DIR_UP] = A[a][c < nA[a] ? c : nA[a] - 1];
	}

	if (tabbed) {
		for (int t = 0; t < p.nTabs; t++) {
			Control &tc = lay.c[lay.tab[t]];
			tc.link[DIR_LEFT]  = lay.tab[(t + p.nTabs - 1) % p.nTabs];
			tc.link[DIR_RIGHT] = lay.tab[(t + 1) % p.nTabs];
			if (nf) {
				tc.link[DIR_DOWN] = A[F[0]][0];
				tc.link[DIR_UP]   = A[F[nf - 1]][0];
			}
		}
		if (nf) {
			const int first = F[0], last = F[nf - 1];
			for (int c = 0; c < nA[last]; c++)
				lay.c[A[last][c]].link[DIR_DOWN] = cur;
			for (int c = 0; c < nA[first]; c++)
				lay.c[A[first][c]].link[DIR_UP] = cur;
		}
	}
}

/* A list's rule (the file browser's): LIST_PAGED's items go up and down round the window and skip
 * the inactive ones, as the file browser has always wrapped; LIST_SCROLL's stop at the window's
 * ends, where the caller scrolls. Left and Right go to Prev and Next, and each of those goes back
 * to the window's first item. Only focus targets are linked. */
static void linkList(const Page &p, Layout &lay)
{
	const ListSpec *L;
	int A[MAX_LIST_SLOTS], n = 0;
	int prev, next;

	if (lay.list < 0)
		return;
	L = p.rows[lay.list].list;
	for (int k = 0; k < L->state->slots; k++) {
		const int c = lay.listItem[k];
		if (c >= 0 && lay.c[c].focusable)
			A[n++] = c;
	}

	prev = lay.slot[lay.list][0];
	next = lay.slot[lay.list][1];
	if (prev >= 0 && !lay.c[prev].focusable)
		prev = -1;
	if (next >= 0 && !lay.c[next].focusable)
		next = -1;

	for (int j = 0; j < n; j++) {
		Control &c = lay.c[A[j]];
		if (L->mode == LIST_PAGED && n > 1) {
			c.link[DIR_UP]   = A[(j + n - 1) % n];
			c.link[DIR_DOWN] = A[(j + 1) % n];
		} else {
			c.link[DIR_UP]   = j > 0 ? A[j - 1] : -1;
			c.link[DIR_DOWN] = j + 1 < n ? A[j + 1] : -1;
		}
		c.link[DIR_LEFT]  = prev;
		c.link[DIR_RIGHT] = next;
	}

	/* A page button's own links are kept while it is inactive: focus stays on Next after the last
	 * page turns it off, and from there Left still goes to the first item, as the browser always
	 * did. No link points at it, so only focus that is already on it uses these. */
	if (lay.slot[lay.list][0] >= 0) {
		Control &c = lay.c[lay.slot[lay.list][0]];
		c.link[DIR_UP] = c.link[DIR_DOWN] = c.link[DIR_LEFT] = -1;
		c.link[DIR_RIGHT] = n > 0 ? A[0] : -1;
	}
	if (lay.slot[lay.list][1] >= 0) {
		Control &c = lay.c[lay.slot[lay.list][1]];
		c.link[DIR_UP] = c.link[DIR_DOWN] = c.link[DIR_RIGHT] = -1;
		c.link[DIR_LEFT] = n > 0 ? A[0] : -1;
	}
}

/* The distance along direction d from an offset (dx, dy), and the offset sideways. */
static void alongSide(int d, float dx, float dy, float &along, float &side)
{
	switch (d) {
	case DIR_LEFT:  along = -dx; side = dy; break;
	case DIR_RIGHT: along =  dx; side = dy; break;
	case DIR_UP:    along = -dy; side = dx; break;
	default:        along =  dy; side = dx; break;
	}
}

/* Edge wrap: the farthest focusable control on the origin's row or column, on the side opposite d.
 * Returns -1 when there is none. */
static int edgeWrap(const Layout &lay, const int cand[], int nc, int a, int d)
{
	const Control &A = lay.c[cand[a]];
	const float ax = A.rect.x + A.rect.w / 2, ay = A.rect.y + A.rect.h / 2;
	const int od = d ^ 1;		/* the enum's pairs: UP/DOWN, LEFT/RIGHT */
	const bool horizontal = d == DIR_LEFT || d == DIR_RIGHT;
	int best = -1;
	float bestAlong = 1.0f;

	for (int b = 0; b < nc; b++) {
		const Control &B = lay.c[cand[b]];
		float along, side;
		if (b == a)
			continue;
		if (horizontal ? !(B.rect.y < A.rect.y + A.rect.h && A.rect.y < B.rect.y + B.rect.h)
		               : !(B.rect.x < A.rect.x + A.rect.w && A.rect.x < B.rect.x + B.rect.w))
			continue;
		alongSide(od, (B.rect.x + B.rect.w / 2) - ax, (B.rect.y + B.rect.h / 2) - ay, along, side);
		if (along > bestAlong) {
			bestAlong = along;
			best = cand[b];
		}
	}
	return best;
}

/* Edge wrap, second stage: edgeWrap() found no control on the origin's row or column on the side
 * opposite d. Take the farthest focusable control on that side, of any row or column, and of
 * those the one nearest in the cross axis. Returns -1 when no control lies beyond the origin. */
static int farWrap(const Layout &lay, const int cand[], int nc, int a, int d)
{
	const Control &A = lay.c[cand[a]];
	const float ax = A.rect.x + A.rect.w / 2, ay = A.rect.y + A.rect.h / 2;
	const int od = d ^ 1;
	float far = 1.0f, bestSide = 1e9f;
	int best = -1;

	for (int b = 0; b < nc; b++) {
		const Control &B = lay.c[cand[b]];
		float along, side;
		if (b == a)
			continue;
		alongSide(od, (B.rect.x + B.rect.w / 2) - ax, (B.rect.y + B.rect.h / 2) - ay, along, side);
		if (along > far)
			far = along;
	}
	if (far <= 1.0f)
		return -1;
	for (int b = 0; b < nc; b++) {
		const Control &B = lay.c[cand[b]];
		float along, side;
		if (b == a)
			continue;
		alongSide(od, (B.rect.x + B.rect.w / 2) - ax, (B.rect.y + B.rect.h / 2) - ay, along, side);
		/* within one pixel of the farthest: controls in the same column or row are equally far */
		if (along >= far - 1.0f && fabsf(side) < bestSide) {
			bestSide = fabsf(side);
			best = cand[b];
		}
	}
	return best;
}

/* CustomizeCoopFrame's rule: from each focusable control, the nearest other one in each
 * direction, scored as along + 2 x the sideways offset, so a straight line wins. Controls
 * further than one pixel behind the origin are not candidates. With wrap, a direction that
 * finds none takes edgeWrap(), then farWrap(). So with wrap, a direction has a target whenever
 * another focusable control lies on the opposite side. */
static void linkGeometry(Layout &lay, bool wrap)
{
	int cand[MAX_CONTROLS], nc = 0;

	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].focusable)
			cand[nc++] = i;

	for (int a = 0; a < nc; a++) {
		const Control &A = lay.c[cand[a]];
		const float ax = A.rect.x + A.rect.w / 2, ay = A.rect.y + A.rect.h / 2;

		for (int d = 0; d < DIR_COUNT; d++) {
			int best = -1;
			float bestCost = 1e9f;
			for (int b = 0; b < nc; b++) {
				if (b == a)
					continue;
				const Control &B = lay.c[cand[b]];
				const float dx = (B.rect.x + B.rect.w / 2) - ax, dy = (B.rect.y + B.rect.h / 2) - ay;
				float along, side, cost;
				alongSide(d, dx, dy, along, side);
				if (along <= 1.0f)
					continue;
				cost = along + 2.0f * fabsf(side);
				if (cost < bestCost) {
					bestCost = cost;
					best = cand[b];
				}
			}
			if (best < 0 && wrap)
				best = edgeWrap(lay, cand, nc, a, d);
			if (best < 0 && wrap)
				best = farWrap(lay, cand, nc, a, d);
			lay.c[cand[a]].link[d] = best;
		}
	}
}

/* Where focus starts: the first row's first button, as OptionsFrame does, or the lit tab on a
 * page with no focusable row. Shared by both focus modes. */
/* The focus target at (row, slot): its control index, or -1 when there is no such row or slot,
 * or the button is hidden, inactive or not a button at all. */
static int focusTargetAt(const Page &p, const Layout &lay, int row, int slot)
{
	if (row < 0 || row >= p.nRows || slot < 0 || slot >= MAX_SLOTS)
		return -1;
	{
		const int k = lay.slot[row][slot];
		return (k >= 0 && lay.c[k].focusable) ? k : -1;
	}
}

/* LinkOverride: each override that names two focus targets replaces that one link. Any other
 * override is left out, and the link the rule made stands. */
static void applyOverrides(const Page &p, Layout &lay)
{
	for (int k = 0; p.overrides && k < p.nOverrides; k++) {
		const LinkOverride &o = p.overrides[k];
		const int from = focusTargetAt(p, lay, o.row, o.slot);
		const int to = focusTargetAt(p, lay, o.toRow, o.toSlot);
		if (from < 0 || to < 0 || o.dir < 0 || o.dir >= DIR_COUNT)
			continue;
		lay.c[from].link[o.dir] = to;
	}
}

/* FreeLink: each link that names a free button as a focus target (or no target) replaces that one
 * link, as applyOverrides does for rows. */
static void applyFreeLinks(const Page &p, Layout &lay)
{
	for (int k = 0; p.freeLinks && k < p.nFreeLinks; k++) {
		const FreeLink &l = p.freeLinks[k];
		const int from = (l.from >= 0 && l.from < p.nFreeButtons) ? lay.freeCtl[l.from] : -1;
		const int to = (l.to < 0) ? -1 : (l.to < p.nFreeButtons ? lay.freeCtl[l.to] : -2);
		if (from < 0 || !lay.c[from].focusable || l.dir < 0 || l.dir >= DIR_COUNT || to == -2)
			continue;
		if (to >= 0 && !lay.c[to].focusable)
			continue;
		lay.c[from].link[l.dir] = to;
	}
}

/* The list's focus: the item the state names when it is shown and active, else the first
 * active item, else the first active Prev or Next. */
static int listDefault(const Page &p, const Layout &lay)
{
	const ListState &s = *p.rows[lay.list].list->state;
	const int want = s.focus - s.top;

	if (s.focus >= 0 && want >= 0 && want < s.slots) {
		const int c = lay.listItem[want];
		if (c >= 0 && lay.c[c].focusable)
			return c;
	}
	for (int k = 0; k < s.slots; k++) {
		const int c = lay.listItem[k];
		if (c >= 0 && lay.c[c].focusable)
			return c;
	}
	for (int k = 0; k < 2; k++) {
		const int c = lay.slot[lay.list][k];
		if (c >= 0 && lay.c[c].focusable)
			return c;
	}
	return -1;
}

void linkFocus(const Page &p, Layout &lay)
{
	int def = -1;

	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++)
			lay.c[i].link[d] = -1;

	if (p.focus == FOCUS_GEOMETRY)
		linkGeometry(lay, p.wrap);
	else
		linkRows(p, lay);
	linkList(p, lay);
	applyOverrides(p, lay);
	applyFreeLinks(p, lay);

	if (lay.list >= 0)
		def = listDefault(p, lay);
	for (int i = 0; i < p.nRows && def < 0; i++) {
		int A[MAX_SLOTS], n;
		if (i == lay.list)
			continue;
		activeSlots(lay, i, A, n);
		if (n > 0)
			def = A[0];
	}
	for (int k = 0; def < 0 && k < p.nFreeButtons; k++) {
		const int c = lay.freeCtl[k];
		if (c >= 0 && lay.c[c].focusable)
			def = c;
	}
	if (def < 0 && p.nTabs > 0)
		def = lay.tab[p.curTab];
	lay.defaultFocus = def;
}

/*  Checking  */

static void note(Problem *out, int maxOut, int &np, int kind, int a, int b)
{
	if (out && np < maxOut) {
		out[np].kind = kind;
		out[np].a = a;
		out[np].b = b;
	}
	np++;
}

/* Strict: rectangles that only touch along an edge do not overlap. */
static bool meets(const Rect &a, const Rect &b)
{
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int checkLayout(const Page &p, const Metrics &m, const Layout &lay, Problem *out, int maxOut)
{
	int np = 0;
	bool seen[MAX_CONTROLS];
	int queue[MAX_CONTROLS], qh = 0, qt = 0;

	for (int i = 0; i < lay.n; i++) {
		const Control &c = lay.c[i];
		const Rect &r = c.rect;

		if (r.x < MENU_EDGE || r.x + r.w > MENU_RIGHT || r.y < MENU_EDGE || r.y + r.h > MENU_BOTTOM)
			note(out, maxOut, np, PROB_PAST_EDGE, i, -1);

		if (p.logo != LOGO_SPOT_NONE) {
			const float lx = p.logo == LOGO_SPOT_TOP ? LOGO_MAIN_X : LOGO_PAGE_X;
			const float ly = p.logo == LOGO_SPOT_TOP ? LOGO_MAIN_Y : LOGO_PAGE_Y;
			if (r.x < LOGO_BOX_R(lx) && LOGO_BOX_L(lx) < r.x + r.w &&
			    r.y < LOGO_BOX_B(ly) && LOGO_BOX_T(ly) < r.y + r.h)
				note(out, maxOut, np, PROB_IN_LOGO, i, -1);
		}

		if (c.kind == CTRL_BUTTON &&
		    (c.role == ROLE_TAB || c.role == ROLE_BUTTON || c.role == ROLE_CHOICE ||
		     c.role == ROLE_LIST_PAGE || c.role == ROLE_FREE)) {
			if (m.width(c.text, TEXT_SCALE, m.ctx) + 2.0f * BUTTON_PAD_X > r.w)
				note(out, maxOut, np, PROB_TEXT_WIDE, i, -1);
		}
	}

	for (int i = 0; i < lay.n; i++)
		for (int j = i + 1; j < lay.n; j++)
			if (meets(lay.c[i].rect, lay.c[j].rect))
				note(out, maxOut, np, PROB_OVERLAP, i, j);

	for (int i = 0; i < lay.n; i++) {
		if (!lay.c[i].focusable)
			continue;
		for (int d = 0; d < DIR_COUNT; d++) {
			const int t = lay.c[i].link[d];
			if (t >= 0 && (t >= lay.n || !lay.c[t].focusable))
				note(out, maxOut, np, PROB_BAD_LINK, i, t);
		}
	}

	/* Every focusable control must be reachable from the default focus along the links. */
	memset(seen, 0, sizeof seen);
	if (lay.defaultFocus >= 0 && lay.defaultFocus < lay.n && lay.c[lay.defaultFocus].focusable) {
		seen[lay.defaultFocus] = true;
		queue[qt++] = lay.defaultFocus;
	}
	while (qh < qt) {
		const int u = queue[qh++];
		for (int d = 0; d < DIR_COUNT; d++) {
			const int v = lay.c[u].link[d];
			if (v >= 0 && v < lay.n && !seen[v] && lay.c[v].focusable) {
				seen[v] = true;
				queue[qt++] = v;
			}
		}
	}
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].focusable && !seen[i])
			note(out, maxOut, np, PROB_UNREACHABLE, i, -1);

	return np;
}

void listTurn(ListState &s, int delta, bool wrap)
{
	const int slots = s.slots > 0 ? s.slots : 1;
	const int pages = (s.count + slots - 1) / slots;
	int p = s.top / slots;

	if (pages <= 0)
		return;
	if (wrap) {
		p = ((p + delta) % pages + pages) % pages;
	} else {
		p += delta;
		if (p < 0) p = 0;
		if (p >= pages) p = pages - 1;
	}
	s.top = p * slots;
	s.focus = s.top;
}

bool listScroll(ListState &s, int delta)
{
	const int top = s.top;
	int f = s.focus < 0 ? s.top : s.focus;

	if (s.count <= 0 || s.slots < 1)
		return false;
	f += delta;
	if (f < 0) f = 0;
	if (f >= s.count) f = s.count - 1;
	s.focus = f;
	if (f < s.top) s.top = f;
	if (f >= s.top + s.slots) s.top = f - s.slots + 1;
	return s.top != top;
}

const char *problemName(int kind)
{
	switch (kind) {
	case PROB_OVERLAP:     return "overlap";
	case PROB_IN_LOGO:     return "in the logo's box";
	case PROB_PAST_EDGE:   return "past MENU_EDGE";
	case PROB_TEXT_WIDE:   return "label wider than its button";
	case PROB_UNREACHABLE: return "unreachable by focus";
	case PROB_BAD_LINK:    return "focus link to a non-focusable control";
	}
	return "?";
}

} /* namespace engine */
} /* namespace menu */
