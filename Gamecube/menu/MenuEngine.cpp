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
#include <stdio.h>
#include <string.h>

#include "MenuEngine.h"

namespace menu {
namespace engine {

using namespace grid;

/* Every control the worst legal page can need must fit in Layout::c: the tabs, the title,
 * two controls per row plus a row of up to MAX_SLOTS buttons, and a list's window with its Prev and Next. */
typedef char engine_controls_fit
	[(MAX_TABS + 1 + MAX_ROWS * (2 + MAX_SLOTS) + MAX_LIST_SLOTS + 2) <= MAX_CONTROLS ? 1 : -1];

/* The text of a measured string. OptionsFrame's helpWidth() gives 0 for an empty string; the
 * Button fit and TextBox drawing use IplFont directly, which gives 5 for an empty one. */
static int measure(const Metrics &m, const char *s, float scale)
{
	if (!s || !*s)
		return 0;
	return m.width(s, scale, m.ctx);
}

/*  The standard's widths: text + pad, rounded up to a step of STD_STEP  */

int stdStepWidth(const Metrics &m, const char *text, int pad)
{
	const int tw = measure(m, text, 1.0f);
	return ((tw + pad + STD_STEP - 1) / STD_STEP) * STD_STEP;
}

/* A cycle button: wide enough for the widest of its values (and of `first`, the text shown) with the
 * arrows' room, so no value ever touches an arrow. */
int stdCycleWidth(const Metrics &m, const char *const *values, int n, const char *first)
{
	int w = stdStepWidth(m, first, STD_CELL_PAD);
	for (int k = 0; k < n; k++) {
		const int v = stdStepWidth(m, values[k], STD_CELL_PAD);
		if (v > w)
			w = v;
	}
	return w > (int)STD_CYCLE_MIN ? w : (int)STD_CYCLE_MIN;
}

int stdButtonWidth(const Metrics &m, const char *text)
{
	return stdStepWidth(m, text, STD_SEG_PAD_EQ);
}

int stdSegmentWidths(const Metrics &m, const char *const *names, int n, int room, int *out, bool *equal)
{
	int eq = 0, sum = 0;
	for (int k = 0; k < n; k++) {
		const int w = stdStepWidth(m, names[k], STD_SEG_PAD_EQ);
		if (w > eq)
			eq = w;
	}
	if (equal)
		*equal = n * eq <= room;
	for (int k = 0; k < n; k++) {
		out[k] = n * eq <= room ? eq : stdStepWidth(m, names[k], STD_SEG_PAD_OWN);
		sum += out[k];
	}
	/* a row that still does not fit is a layout error (PROB_PAST_RIGHT): shorten the words */
	return sum;
}

int stdTabWidths(const Metrics &m, const Tab *tabs, int n, int *out)
{
	const float room = (float)MENU_W - 2.0f * (float)MENU_EDGE;
	int w = 0, sum = 0;
	bool equal = false;

	for (int t = 0; t < n; t++) {
		const int tw = stdStepWidth(m, tabs[t].text, STD_TAB_PAD);
		if (tw > w)
			w = tw;
	}
	/* equal widths when the strip fits between the screen margins, otherwise each tab its own text + 16 */
	if (n > 0 && (float)(w * n) + STD_TAB_GAP * (float)(n - 1) <= room)
		equal = true;
	for (int t = 0; t < n; t++) {
		out[t] = equal ? w : stdStepWidth(m, tabs[t].text, STD_TAB_PAD_OWN);
		sum += out[t];
	}
	return sum + (n > 1 ? (int)STD_TAB_GAP * (n - 1) : 0);
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

/* A row that takes a cell of a two-column page: a Cycle (a Toggle is a segmented control as wide as the
 * control column, so it is a line of its own). */
static bool isCell(const Page &p, const Row &r)
{
	return p.columns >= 2 && r.kind == ROW_CYCLE;
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
		const bool cell = isCell(p, p.rows[k]);
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

/* The two-column grid. Column c has one label width (the widest label in it) and one button width
 * (the widest fitted button, over every name of every row in it), so every line shares the same
 * edges. A cell is a right-aligned label, RLABEL_GAP, then the button; the two columns are
 * COL2_GAP apart and the whole grid is centred once. */
static void gridPlace(const Page &p, const Metrics &m, int col, float &btnX, float &btnW, float &labelRight)
{
	float lw[2] = { 0, 0 }, bw[2] = { 0, 0 }, x;
	int cols = 1;

	for (int k = 0; k < p.nRows; k++) {
		const Row &r = p.rows[k];
		int line, c;
		if (!isCell(p, r))
			continue;
		rowCell(p, k, line, c);
		cols = c ? 2 : cols;
		if ((float)measure(m, r.label, TEXT_SCALE) > lw[c])
			lw[c] = (float)measure(m, r.label, TEXT_SCALE);
		for (int n = 0; n < r.count; n++) {
			/* a cell is text + the arrows (STD_CELL_PAD); the column takes its widest value */
			const float w = (float)stdStepWidth(m, r.names[n], STD_CELL_PAD);
			if (w > bw[c])
				bw[c] = w;
		}
	}
	{
		/* the grid is pushed left when it would pass the control column's right edge */
		const float lg = (float)STD_CELL_LGAP;
		const float cg = (float)STD_CELL_CGAP;
		const float total = lw[0] + lg + bw[0] + (cols > 1 ? cg + lw[1] + lg + bw[1] : 0);
		x = (MENU_W - total) / 2;
		if (x + total > (float)STD_CTRL_X1)
			x = (float)STD_CTRL_X1 - total;
		if (col)
			x += lw[0] + lg + bw[0] + cg;
		btnX = x + lw[col] + lg;
		btnW = bw[col];
		labelRight = btnX - lg;
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

/* `text` cut to at most `room` px with "..." at the end, written to buf; the text itself when it fits.
 * Cut at a character boundary (not inside a UTF-8 sequence). */
static const char *cutText(const Metrics &m, const char *text, float scale, int room, char *buf, int bufSize)
{
	int lo = 0, hi, len;
	char probe[STD_CUT_LEN];

	if (measure(m, text, scale) <= room)
		return text;
	len = (int)strlen(text);
	hi = len < bufSize - 4 ? len : bufSize - 4;	/* the prefix, before the three dots and the NUL */
	/* the longest prefix whose width with "..." is within the room (the width only grows with it) */
	while (lo < hi) {
		const int mid = (lo + hi + 1) / 2;
		int n = mid;
		while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80)
			n--;
		memcpy(probe, text, n);
		strcpy(probe + n, "...");
		if (measure(m, probe, scale) <= room)
			lo = mid;
		else
			hi = mid - 1;
	}
	while (lo > 0 && ((unsigned char)text[lo] & 0xC0) == 0x80)
		lo--;
	memcpy(buf, text, lo);
	strcpy(buf + lo, "...");
	return buf;
}

/* The explorer's list (DENSITY_EXPLORER): the page arrows at the two ends of the scroll column, then
 * the window's rows, each with its name (cut), and the tag and note texts when the item has them. */
static void layoutListStd(Layout &out, const Metrics &m, const ListSpec &L, int row)
{
	const ListState &s = *L.state;
	const bool paged = L.mode == LIST_PAGED;
	const bool canPrev = paged && s.top > 0;
	const bool canNext = paged && s.top + s.slots < s.count;
	const float tagScale = STD_LIST_PX_TAG / FONT_H;
	const float yEnd = (float)STD_LIST_Y0 + (float)(s.slots - 1) * (float)STD_LIST_PITCH + (float)STD_LIST_H;

	out.slot[row][0] = addButton(out, ROLE_LIST_PAGE, row, 0, (float)STD_SCROLL_X, (float)STD_LIST_Y0,
	                             (float)STD_SCROLL_W, (float)STD_SCROLL_ARROW_H, L.prev.text, false, canPrev);
	out.slot[row][1] = addButton(out, ROLE_LIST_PAGE, row, 1, (float)STD_SCROLL_X, yEnd - (float)STD_SCROLL_ARROW_H,
	                             (float)STD_SCROLL_W, (float)STD_SCROLL_ARROW_H, L.next.text, false, canNext);
	for (int k = 0; k < s.slots; k++) {
		const int index = s.top + k;
		const bool shown = index < s.count;
		const float y = (float)STD_LIST_Y0 + (float)k * (float)STD_LIST_PITCH;
		ListItem it;
		int c;

		it.text = "";
		it.folder = false;
		it.dim = false;
		it.tag = it.note = NULL;
		if (shown)
			L.item(L.ctx, index, it);
		if (!it.text)
			it.text = "";
		c = addButton(out, ROLE_LIST_ITEM, row, k, (float)STD_LIST_X0, y, (float)STD_LIST_W, (float)STD_LIST_H,
		              "", false, shown);
		out.c[c].folder = shown && it.folder;
		out.c[c].dim = shown && it.dim;
		out.listItem[k] = c;
		if (!shown)
			continue;

		{
			const float tagR = (float)STD_LIST_TAG_R;
			const bool hasTag = it.tag && *it.tag, hasNote = it.note && *it.note;
			/* the note sits right against its own tag (a gap of its own, not the widest tag's room) */
			const float noteR = hasTag ? tagR - (float)measure(m, it.tag, tagScale) - (float)STD_LIST_NOTE_GAP
			                           : (float)STD_LIST_NOTE_R;
			/* no text on the right: the name's room is as far from the row's right end as its start from the left */
			float rightEdge = (float)STD_LIST_X0 + (float)STD_LIST_W - ((float)STD_LIST_NAME_X - (float)STD_LIST_X0);
			const float textX = it.folder ? (float)STD_LIST_FOLDER_X : (float)STD_LIST_NAME_X;
			int room;

			/* the name ends before the leftmost text on its right */
			if (hasTag)
				rightEdge = tagR - (float)measure(m, it.tag, tagScale) - (float)STD_LIST_NAME_GAP;
			if (hasNote) {
				const float e = noteR - (float)measure(m, it.note, tagScale) - (float)STD_LIST_NAME_GAP;
				if (e < rightEdge)
					rightEdge = e;
			}
			room = (int)(rightEdge - textX);
			out.c[c].full = it.text;
			out.c[c].text = cutText(m, it.text, TEXT_SCALE, room, out.cut[k], STD_CUT_LEN);
			out.c[c].textX = textX;
			out.c[c].textR = rightEdge;

			{
				const float ty = y + ((float)STD_LIST_H - (float)m.height("Ag", tagScale, m.ctx)) / 2;
				if (hasTag)
					addText(out, m, ROLE_LIST_TAG, row, k, tagR, ty, ALIGN_RIGHT_TOP, tagScale, it.tag);
				if (hasNote)
					addText(out, m, ROLE_LIST_NOTE, row, k, noteR, ty, ALIGN_RIGHT_TOP, tagScale, it.note);
			}
		}
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
		case ROW_LIST:    n += 2 + r.list->state->slots * 3; break;	/* the item, its tag and its note */
		}
	}
	return n + p.nFreeButtons + p.nFreeTexts;
}

/*  The menu standard (Page::standard; MenuLayout.h STD_*, Docs/MENU_PLAN.md 1.1-1.3)
 *
 * A page that opts in is laid out here and nowhere else: tab strip or centred title in the header
 * band, the rows in the body from STD_BODY_Y0 at the density's pitch, every label right-aligned to
 * STD_LABEL_EDGE, every control in the column from STD_CTRL_X0, every width from the text.
 *
 *   Toggle, Choice  touching segments (Toggle = its names as segments); the last `plain` of a
 *                   Choice are separate action buttons after a gap of STD_BTN_GAP
 *   Cycle           one button reading the current name, as wide as its own widest value (stdCycleWidth)
 *   Buttons         separate buttons (text + 32, gap 12); the actions' `lit` is "on" (a flag).
 *                   With a label they start at STD_CTRL_X0, without they are centred on the screen
 *   Readout         label, and the text at STD_CTRL_X0
 *
 * Row::x0 and gap and the Page's titleY are ignored (rowY0 > 0 moves the first row down: the ISO Info disc band); ActionButton::width is used on a Buttons row
 * when it is above 0 (a cycle: its widest value, stdCycleWidth), else the width comes from the text. A row that
 * does not fit is not squeezed: checkLayout reports it. A page with more rows than the density holds,
 * or a List row, is refused (-1): the list is not laid out by the standard yet.
 * Toggle segments are ROLE_CHOICE controls, slot k = value k, so a host's click on slot k sets it. */
static int layoutStandard(const Page &p, const Metrics &m, Layout &out)
{
	float h, pitch;
	int maxRows;

	switch (p.standard) {
	case DENSITY_REGULAR: h = (float)STD_REG_H; pitch = (float)STD_REG_PITCH; maxRows = STD_REG_ROWS; break;
	case DENSITY_COMPACT: h = (float)STD_CMP_H; pitch = (float)STD_CMP_PITCH; maxRows = STD_CMP_ROWS; break;
	case DENSITY_TEXT:    h = (float)STD_TXT_H; pitch = (float)STD_TXT_PITCH; maxRows = STD_TXT_ROWS; break;
	case DENSITY_EXPLORER: h = (float)STD_LIST_H; pitch = (float)STD_LIST_PITCH; maxRows = 1; break;
	default: return -1;
	}
	if (pageLines(p) > maxRows)
		return -1;
	if (p.standard == DENSITY_EXPLORER && (p.nRows != 1 || p.rows[0].kind != ROW_LIST))
		return -1;
	if (p.standard != DENSITY_EXPLORER && p.nRows > 0 && p.rows[0].kind == ROW_LIST)
		return -1;

	if (p.nTabs > 0) {
		int tws[MAX_TABS];
		if (p.nTabs > MAX_TABS)
			return -1;
		const int total = stdTabWidths(m, p.tabs, p.nTabs, tws);
		int x = ((int)MENU_W - total) / 2;
		for (int t = 0; t < p.nTabs; t++) {
			out.tab[t] = addButton(out, ROLE_TAB, -1, t, (float)x, (float)STD_TAB_Y, (float)tws[t], (float)STD_TAB_H,
			                       p.tabs[t].text, t == p.curTab, true);
			x += tws[t] + (int)STD_TAB_GAP;
		}
	} else if (p.title) {
		out.title = addText(out, m, ROLE_TITLE, -1, 0, MENU_W / 2, (float)STD_TITLE_Y, ALIGN_CENTRE, TEXT_SCALE, p.title);
	}

	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		int line, col;
		rowCell(p, i, line, col);
		const float y = (p.rowY0 > 0 ? p.rowY0 : (float)STD_BODY_Y0) + (float)line * pitch;
		const float cy = y + h / 2;
		/* a cell of a two-column page (Page::columns): its label edge, button x and width from gridPlace */
		float bx = (float)STD_CTRL_X0, bw = 0, edge = (float)STD_LABEL_EDGE;
		if (isCell(p, r))
			gridPlace(p, m, col, bx, bw, edge);
		else if (r.kind == ROW_CYCLE)
			bw = (float)stdCycleWidth(m, r.names, r.count, r.names[shownValue(r) - r.lo]);

		if (r.label && r.kind != ROW_LIST) {
			/* right-aligned to the edge: centred on an anchor that puts the right end there */
			const int lw = measure(m, r.label, TEXT_SCALE);
			addText(out, m, r.kind == ROW_CHOICE ? ROLE_RLABEL : ROLE_LABEL, i, 0,
			        edge - (float)(lw - lw / 2), cy, ALIGN_CENTRE, TEXT_SCALE, r.label);
		}

		switch (r.kind) {
		case ROW_TOGGLE:
		case ROW_CHOICE: {
			const int plain = r.kind == ROW_CHOICE ? r.plain : 0;
			const int nseg = r.count - plain;
			int w[MAX_SLOTS], aw[MAX_SLOTS], room = (int)STD_CTRL_W;
			float x = (float)STD_CTRL_X0;
			if (nseg < 1 || nseg + plain > MAX_SLOTS)
				return -1;
			for (int k = 0; k < plain; k++) {
				aw[k] = stdButtonWidth(m, r.names[nseg + k]);
				room -= aw[k] + (int)STD_BTN_GAP;
			}
			{
				/* equal widths when the whole row fits with them; if not, every part of the row (the
				 * segments and the separate buttons after them) takes its own width: text + 16 */
				bool equal = true;
				stdSegmentWidths(m, r.names, nseg, room, w, &equal);
				if (!equal) {
					/* the room was taken with the buttons at text + 32; they now shrink to text + 16, so the
					 * segments get that room back. Own widths stay unless the row still does not fit. */
					int room2 = (int)STD_CTRL_W, ownSum = 0;
					for (int k = 0; k < plain; k++) {
						aw[k] = stdStepWidth(m, r.names[nseg + k], STD_SEG_PAD_OWN);
						room2 -= aw[k] + (int)STD_BTN_GAP;
					}
					for (int k = 0; k < nseg; k++) {
						w[k] = stdStepWidth(m, r.names[k], STD_SEG_PAD_OWN);
						ownSum += w[k];
					}
					if (ownSum > room2) {
						bool eq2 = true;
						int tw[MAX_SLOTS];
						stdSegmentWidths(m, r.names, nseg, room2, tw, &eq2);
						if (!eq2)
							for (int k = 0; k < nseg; k++)
								w[k] = tw[k];
					}
				}
			}
			for (int k = 0; k < nseg; k++) {
				out.slot[i][k] = addButton(out, ROLE_CHOICE, i, k, x, y, (float)w[k], h, r.names[k],
				                           k == shownValue(r) - r.lo, !isInactive(r, k));
				x += (float)w[k];
			}
			for (int k = 0; k < plain; k++) {
				x += (float)STD_BTN_GAP;
				out.slot[i][nseg + k] = addButton(out, ROLE_BUTTON, i, nseg + k, x, y, (float)aw[k], h,
				                                  r.names[nseg + k], r.actions && r.actions[nseg + k].lit, !isInactive(r, nseg + k));
				x += (float)aw[k];
			}
			break;
		}
		case ROW_CYCLE:
			out.slot[i][0] = addButton(out, ROLE_BUTTON, i, 0, bx, y, bw, h,
			                           r.names[shownValue(r) - r.lo], false, !isInactive(r, 0));
			break;
		case ROW_READOUT: {
			const int vw = measure(m, r.text, TEXT_SCALE);
			addText(out, m, ROLE_VALUE, i, 0, (float)((int)STD_CTRL_X0 + vw / 2), cy, ALIGN_CENTRE, TEXT_SCALE, r.text);
			break;
		}
		case ROW_BUTTONS: {
			int w[MAX_SLOTS], total = (int)STD_BTN_GAP * (r.count - 1);
			float x;
			if (r.count > MAX_SLOTS)
				return -1;
			if (r.prefix) {
				/* small cycles, each after its own prefix text: prefix, 8, 140 wide, 16 to the next */
				x = (float)STD_CTRL_X0;
				for (int k = 0; k < r.count; k++) {
					const int pw = measure(m, r.prefix[k], TEXT_SCALE);
					addText(out, m, ROLE_VALUE, i, k, x + (float)(pw / 2), cy, ALIGN_CENTRE, TEXT_SCALE, r.prefix[k]);
					x += (float)pw + (float)STD_CYCLES_PRE;
					out.slot[i][k] = addButton(out, ROLE_BUTTON, i, k, x, y, (float)STD_CYCLES_W, h, r.actions[k].text,
					                           false, !isInactive(r, k));
					x += (float)STD_CYCLES_W + (float)STD_CYCLES_GAP;
				}
				break;
			}
			for (int k = 0; k < r.count; k++) {
				/* a cycle: its widest value; else a width the host gives is kept; else the text's */
				w[k] = r.actions[k].nValues > 0 ? stdCycleWidth(m, r.actions[k].values, r.actions[k].nValues, r.actions[k].text)
				     : r.actions[k].width > 0 ? (int)r.actions[k].width : stdButtonWidth(m, r.actions[k].text);
				total += w[k];
			}
			x = r.label ? (float)STD_CTRL_X0 : (float)(((int)MENU_W - total) / 2);
			for (int k = 0; k < r.count; k++) {
				out.slot[i][k] = addButton(out, ROLE_BUTTON, i, k, x, y, (float)w[k], h, r.actions[k].text,
				                           r.actions[k].lit, !isInactive(r, k));
				x += (float)w[k] + (float)STD_BTN_GAP;
			}
			break;
		}
		case ROW_LIST:
			if (p.standard != DENSITY_EXPLORER || r.list->state->slots > STD_LIST_ROWS)
				return -1;
			out.list = i;
			layoutListStd(out, m, *r.list, i);
			break;
		}
	}

	for (int k = 0; k < p.nFreeButtons; k++) {
		const FreeButton &b = p.freeButtons[k];
		out.freeCtl[k] = addButton(out, ROLE_FREE, -1, k, b.x, b.y, b.w, b.h, b.text, b.lit, !b.inactive);
	}
	for (int k = 0; k < p.nFreeTexts; k++) {
		const FreeText &t = p.freeTexts[k];
		addText(out, m, ROLE_FREE_TEXT, -1, k, t.ax, t.ay,
		        t.centred ? ALIGN_CENTRE : t.right ? ALIGN_RIGHT_TOP : ALIGN_LEFT_TOP, t.scale, t.text);
	}
	return out.n;
}

int layoutFooter(const Page &p, const Metrics &m, int row, Control out[STD_HELP_LINES])
{
	int n = 0;
	if (!p.footer || row < 0 || row >= p.nRows)
		return 0;
	for (int k = 0; k < STD_HELP_LINES; k++) {
		Control &c = out[n];
		if (!p.footer[row].line[k])
			continue;
		memset(&c, 0, sizeof c);
		c.kind = CTRL_TEXT;
		c.role = ROLE_FOOTER;
		c.row = row;
		c.slot = n;
		c.ax = (float)STD_HELP_X0;
		c.ay = (float)STD_HELP_Y0 + (float)n * (float)STD_HELP_DY;
		c.align = ALIGN_LEFT_TOP;
		c.scale = (float)STD_HELP_PX / FONT_H;
		c.text = p.footer[row].line[k];
		for (int d = 0; d < DIR_COUNT; d++)
			c.link[d] = -1;
		placeText(c, m);
		/* a page with the logo at the bottom (drawn in the legacy box, not STD_LOGO_*) keeps its help left of it */
		if (c.rect.x + c.rect.w > (float)STD_HELP_X1 ||
		    (p.logo == LOGO_SPOT_BOTTOM && c.rect.x + c.rect.w > LOGO_BOX_L(LOGO_PAGE_X)))
			return -1;
		n++;
	}
	return n;
}

int checkFooters(const Page &p, const Metrics &m)
{
	int bad = 0;
	for (int i = 0; i < p.nRows; i++) {
		Control c[STD_HELP_LINES];
		if (layoutFooter(p, m, i, c) < 0)
			bad++;
	}
	return bad;
}

int layoutPage(const Page &p, const Metrics &m, Layout &out)
{
	if (p.nRows < 0 || p.nRows > MAX_ROWS ||
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

	if (p.standard != DENSITY_OFF)
		return layoutStandard(p, m, out);

	/* A page that has not opted in is one of two kinds: an action menu (rows of buttons from their own x0,
	 * ActionMenu) or a free page (free buttons and texts under a title, Editor). Nothing else is laid out
	 * here: a tab strip, a label, a cycle, a choice, a readout or a list needs the standard. */
	if (p.nTabs > 0)
		return -1;
	for (int i = 0; i < p.nRows; i++)
		if (p.rows[i].kind != ROW_BUTTONS)
			return -1;

	if (p.title)
		out.title = addText(out, m, ROLE_TITLE, -1, 0, TITLE_X, p.titleY > 0 ? p.titleY : TITLE_Y,
		                    ALIGN_CENTRE, TEXT_SCALE, p.title);

	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		const float y = p.rowY0 + (float)i * ROW_DY;
		/* Centred on the screen, or from x0 */
		const float gap = r.gap > 0 ? r.gap : TAB_GAP;
		float total = gap * (r.count - 1);
		float x;
		for (int k = 0; k < r.count; k++)
			total += r.actions[k].width;
		x = r.x0 > 0 ? r.x0 : (MENU_W - total) / 2;
		for (int k = 0; k < r.count; k++) {
			out.slot[i][k] = addButton(out, ROLE_BUTTON, i, k, x, y + r.actions[k].dy, r.actions[k].width, BUTTON_H,
			                           r.actions[k].text, r.actions[k].lit, !isInactive(r, k));
			x += r.actions[k].width + gap;
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
 * row up to it, and every tab goes down to the first row and up to the last. A standard page
 * (Page::standard) does not keep the column: Up and Down land on the leftmost button. Inactive
 * buttons are left out of every row, so the columns are those of the active buttons. */
static void linkRows(const Page &p, Layout &lay)
{
	int F[MAX_ROWS], nf = 0;
	int A[MAX_ROWS][MAX_SLOTS], nA[MAX_ROWS];
	const bool tabbed = p.nTabs > 0;
	const int cur = tabbed ? lay.tab[p.curTab] : -1;
	const bool left = p.standard != DENSITY_OFF && p.columns < 2;	/* two columns keep the column */

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
		/* a standard page: Up and Down always land on the row's leftmost button */
		for (int c = 0; c < nA[a]; c++)
			lay.c[A[a][c]].link[DIR_DOWN] = A[b][left ? 0 : (c < nA[b] ? c : nA[b] - 1)];
		for (int c = 0; c < nA[b]; c++)
			lay.c[A[b][c]].link[DIR_UP] = A[a][left ? 0 : (c < nA[a] ? c : nA[a] - 1)];
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
 * the inactive ones, as the file browser has always wrapped. Left and Right go to Prev and Next, and each of those goes back
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
		if (p.standard == DENSITY_EXPLORER)
			c.link[DIR_DOWN] = c.link[DIR_LEFT] = c.link[DIR_RIGHT];	/* "^": down and left to the first row; its Up (the lit tab) is linkListTabs' */
	}
	if (lay.slot[lay.list][1] >= 0) {
		Control &c = lay.c[lay.slot[lay.list][1]];
		c.link[DIR_UP] = c.link[DIR_DOWN] = c.link[DIR_RIGHT] = -1;
		c.link[DIR_LEFT] = n > 0 ? A[0] : -1;
		if (p.standard == DENSITY_EXPLORER && n > 0) {
			c.link[DIR_UP] = A[n - 1];			/* the explorer's "v": up to the last row, and left to it */
			c.link[DIR_LEFT] = A[n - 1];
		}
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
		if (from < 0 || !lay.c[from].focusable || l.dir < 0 || l.dir >= DIR_COUNT || (l.to >= 0 && to < 0))
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

/* A standard page with tabs over a list (the explorer): Down from any tab lands on the list's default
 * item, and Up from the list's first item goes to the lit tab (the item's Down from the last item
 * still wraps to the first, as linkList made it). Left and Right on the tabs are linkRows'. */
static void linkListTabs(const Page &p, Layout &lay)
{
	int first = -1, def;

	if (p.standard == DENSITY_OFF || p.nTabs <= 0 || lay.list < 0)
		return;
	def = listDefault(p, lay);
	for (int k = 0; k < p.rows[lay.list].list->state->slots && first < 0; k++) {
		const int c = lay.listItem[k];
		if (c >= 0 && lay.c[c].focusable)
			first = c;
	}
	if (def < 0)
		def = first;

	for (int t = 0; t < p.nTabs && t < MAX_TABS; t++) {
		const int c = lay.tab[t];
		if (c < 0 || !lay.c[c].focusable)
			continue;
		lay.c[c].link[DIR_UP] = -1;
		lay.c[c].link[DIR_DOWN] = def;
	}
	if (first >= 0 && lay.tab[p.curTab] >= 0 && lay.c[lay.tab[p.curTab]].focusable)
		lay.c[first].link[DIR_UP] = lay.tab[p.curTab];
	/* the "^" arrow's Up goes to the lit tab as well */
	if (lay.slot[lay.list][0] >= 0 && lay.tab[p.curTab] >= 0 && lay.c[lay.tab[p.curTab]].focusable)
		lay.c[lay.slot[lay.list][0]].link[DIR_UP] = lay.tab[p.curTab];
}

void linkFocus(const Page &p, Layout &lay)
{
	int def = -1;

	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++)
			lay.c[i].link[d] = -1;
	if (p.nTabs > 0 && (p.curTab < 0 || p.curTab >= p.nTabs || p.nTabs > MAX_TABS)) {
		lay.defaultFocus = -1;		/* a page layoutPage would refuse: no links, nothing to index lay.tab with */
		return;
	}

	if (p.focus == FOCUS_GEOMETRY)
		linkGeometry(lay, p.wrap);
	else
		linkRows(p, lay);
	linkList(p, lay);
	linkListTabs(p, lay);
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

		if (p.standard != DENSITY_OFF) {
			if ((c.role == ROLE_LABEL || c.role == ROLE_RLABEL) && r.w > (float)STD_LABEL_MAX_W)
				note(out, maxOut, np, PROB_LABEL_WIDE, i, -1);
			if ((c.role == ROLE_BUTTON || c.role == ROLE_CHOICE || c.role == ROLE_VALUE || c.role == ROLE_TAB ||
			     c.role == ROLE_FREE || c.role == ROLE_LIST_PAGE || c.role == ROLE_LIST_ITEM) &&
			    r.x + r.w > (float)STD_CTRL_X1)
				note(out, maxOut, np, PROB_PAST_RIGHT, i, -1);
			if (p.logo == LOGO_SPOT_BOTTOM &&
			    r.x < (float)(STD_LOGO_X + STD_LOGO_W) && (float)STD_LOGO_X < r.x + r.w &&
			    r.y < (float)(STD_LOGO_Y + STD_LOGO_H) && (float)STD_LOGO_Y < r.y + r.h)
				note(out, maxOut, np, PROB_IN_STD_LOGO, i, -1);
		}

		if (p.logo != LOGO_SPOT_NONE && !(p.standard != DENSITY_OFF && p.logo == LOGO_SPOT_BOTTOM)) {
			const float lx = p.logo == LOGO_SPOT_TOP ? LOGO_MAIN_X : LOGO_PAGE_X;
			const float ly = p.logo == LOGO_SPOT_TOP ? LOGO_MAIN_Y : LOGO_PAGE_Y;
			if (r.x < LOGO_BOX_R(lx) && LOGO_BOX_L(lx) < r.x + r.w &&
			    r.y < LOGO_BOX_B(ly) && LOGO_BOX_T(ly) < r.y + r.h)
				note(out, maxOut, np, PROB_IN_LOGO, i, -1);
		}

		/* the explorer's page arrows are 12 px glyph buttons: their label is not measured against the box */
		if (c.kind == CTRL_BUTTON && !(c.role == ROLE_LIST_PAGE && p.standard == DENSITY_EXPLORER) &&
		    (c.role == ROLE_TAB || c.role == ROLE_BUTTON || c.role == ROLE_CHOICE ||
		     c.role == ROLE_LIST_PAGE || c.role == ROLE_FREE)) {
			/* a standard page's narrowest width is text + 16 (STD_SEG_PAD_OWN), not + 32 */
			const float pad = p.standard != DENSITY_OFF ? (float)STD_SEG_PAD_OWN : 2.0f * BUTTON_PAD_X;
			if (m.width(c.text, TEXT_SCALE, m.ctx) + pad > r.w)
				note(out, maxOut, np, PROB_TEXT_WIDE, i, -1);
		}
		/* a cycle button in a row of buttons: its width is its widest value's (stdCycleWidth); checked the same way */
		if (p.standard != DENSITY_OFF && c.role == ROLE_BUTTON && c.row >= 0 && c.row < p.nRows &&
		    p.rows[c.row].kind == ROW_BUTTONS && p.rows[c.row].actions && p.rows[c.row].actions[c.slot].nValues > 0) {
			const ActionButton &ab = p.rows[c.row].actions[c.slot];
			for (int n = 0; n < ab.nValues; n++)
				if (m.width(ab.values[n], TEXT_SCALE, m.ctx) + (float)STD_CELL_PAD > r.w) {
					note(out, maxOut, np, PROB_ARROW_TIGHT, i, -1);
					break;
				}
		}
		/* a standard Cycle draws an arrow at each end: every value it can show must clear them */
		if (p.standard != DENSITY_OFF && c.role == ROLE_BUTTON && c.row >= 0 && c.row < p.nRows &&
		    p.rows[c.row].kind == ROW_CYCLE) {
			const Row &cr = p.rows[c.row];
			for (int n = 0; n < cr.count; n++)
				if (m.width(cr.names[n], TEXT_SCALE, m.ctx) + (float)STD_CELL_PAD > r.w) {
					note(out, maxOut, np, PROB_ARROW_TIGHT, i, -1);
					break;
				}
		}
	}

	for (int i = 0; i < lay.n; i++)
		for (int j = i + 1; j < lay.n; j++) {
			const Control &a = lay.c[i], &b = lay.c[j];
			/* an explorer row's tag and note are drawn on their own row: they may overlap its item */
			const bool ownRow = a.row == b.row && a.slot == b.slot && a.row >= 0 &&
			    ((a.role == ROLE_LIST_ITEM && (b.role == ROLE_LIST_TAG || b.role == ROLE_LIST_NOTE)) ||
			     (b.role == ROLE_LIST_ITEM && (a.role == ROLE_LIST_TAG || a.role == ROLE_LIST_NOTE)));
			if (!ownRow && meets(a.rect, b.rect))
				note(out, maxOut, np, PROB_OVERLAP, i, j);
		}

	/* Standard pages: buttons that act alone never touch anything (the segments of one choice do). */
	if (p.standard != DENSITY_OFF) {
		for (int i = 0; i < lay.n; i++) {
			const Control &a = lay.c[i];
			if (a.kind != CTRL_BUTTON || a.row < 0 || (a.role != ROLE_BUTTON && a.role != ROLE_CHOICE))
				continue;
			for (int j = i + 1; j < lay.n; j++) {
				const Control &b = lay.c[j];
				float gap;
				if (b.kind != CTRL_BUTTON || b.row != a.row || (b.role != ROLE_BUTTON && b.role != ROLE_CHOICE) ||
				    (a.role == ROLE_CHOICE && b.role == ROLE_CHOICE))
					continue;
				if (!(a.rect.y < b.rect.y + b.rect.h && b.rect.y < a.rect.y + a.rect.h))
					continue;
				gap = b.rect.x >= a.rect.x ? b.rect.x - (a.rect.x + a.rect.w) : a.rect.x - (b.rect.x + b.rect.w);
				if (gap < (float)STD_BTN_GAP)
					note(out, maxOut, np, PROB_BTN_TOUCH, i, j);
			}
		}
	}

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

int tabStep(int cur, int n, int delta, bool wrap)
{
	int t = cur + delta;
	if (n <= 0 || cur < 0 || cur >= n)
		return cur;
	if (wrap)
		return ((t % n) + n) % n;
	return t < 0 || t >= n ? cur : t;
}

int stripLetter(const char *name)
{
	if (!name || !name[0])
		return -1;
	if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
		return -1;
	const int ch = (unsigned char)name[0];
	if (ch >= 'a' && ch <= 'z') return ch - 'a' + 1;
	if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 1;
	return 0;
}

char stripChar(int cell)
{
	return cell <= 0 ? '#' : (char)('A' + (cell - 1) % 26);
}

unsigned stripMask(int count, NameFn name, void *ctx)
{
	unsigned m = 0;
	for (int i = 0; i < count; i++) {
		const int k = stripLetter(name(ctx, i));
		if (k >= 0)
			m |= 1u << k;
	}
	return m;
}

int stripFirst(int count, NameFn name, void *ctx, int cell)
{
	for (int i = 0; i < count; i++)
		if (stripLetter(name(ctx, i)) == cell)
			return i;
	return -1;
}

int stripStep(unsigned mask, int cur, int dir)
{
	if (dir > 0) {
		for (int k = cur < 0 ? 0 : cur + 1; k < STRIP_CELLS; k++)
			if (mask & (1u << k)) return k;
	} else if (dir < 0) {
		for (int k = cur < 0 ? STRIP_CELLS - 1 : cur - 1; k >= 0; k--)
			if (mask & (1u << k)) return k;
	}
	return -1;
}

static int stripEdge(int k)	/* the left edge of cell k's slot: 27 slots over the strip's width */
{
	return (int)STD_STRIP_X0 + k * ((int)STD_STRIP_X1 - (int)STD_STRIP_X0) / STRIP_CELLS;
}

void stripCell(int k, int *x, int *w)
{
	*x = stripEdge(k);
	*w = stripEdge(k + 1) - *x - STD_STRIP_GAP;
}

int stripHit(int x, int y)
{
	if (y < (int)STD_STRIP_Y || y >= (int)(STD_STRIP_Y + STD_STRIP_H) ||
	    x < (int)STD_STRIP_X0 || x >= (int)STD_STRIP_X1)
		return -1;
	for (int k = 0; k < STRIP_CELLS; k++)
		if (x < stripEdge(k + 1))
			return k;
	return -1;
}

static int listPages(const ListState &s)
{
	const int slots = s.slots > 0 ? s.slots : 1;
	return (s.count + slots - 1) / slots;
}

Thumb scrollThumb(const ListState &s, int y0, int y1)
{
	Thumb t;
	const int track = y1 - y0, pages = listPages(s), slots = s.slots > 0 ? s.slots : 1;
	t.y = y0;
	t.h = track;
	t.live = pages > 1 && track > 0;
	if (t.live) {
		t.h = track * slots / s.count;
		if (t.h < 16) t.h = 16;
		if (t.h > track) t.h = track;
		int page = s.top / slots;
		if (page > pages - 1) page = pages - 1;
		t.y = y0 + (track - t.h) * page / (pages - 1);
	}
	return t;
}

int scrollDragPage(const ListState &s, int y0, int y1, int thumbY)
{
	const int pages = listPages(s);
	const Thumb t = scrollThumb(s, y0, y1);
	const int room = (y1 - y0) - t.h;
	if (pages <= 1 || room <= 0)
		return 0;
	int d = thumbY - y0;
	if (d < 0) d = 0;
	if (d > room) d = room;
	return (d * (pages - 1) + room / 2) / room;
}

void factsLayout(FactsBox &out)
{
	out.x0 = STD_FACTS_X0;	out.y0 = STD_FACTS_Y0;
	out.x1 = STD_FACTS_X1;	out.y1 = STD_FACTS_Y1;
	out.titleX = STD_FACTS_TITLE_X;
	out.titleY = STD_FACTS_TITLE_Y;
	out.titleRoom = STD_FACTS_X1 - 6 - STD_FACTS_TITLE_X;
	for (int k = 0; k < STD_FACTS_N; k++) {
		const int colX = STD_FACTS_COL_X + (k / 3) * STD_FACTS_COL_DX;
		out.cell[k].keyR = colX + STD_FACTS_KEY_R;
		out.cell[k].valX = colX + STD_FACTS_VAL_X;
		out.cell[k].y = STD_FACTS_ROW_Y0 + (k % 3) * STD_FACTS_PITCH;
		out.cell[k].valRoom = STD_FACTS_VAL_W;
	}
}

const char *fitText(const Metrics &m, const char *text, float scale, int room, char *buf, int bufSize)
{
	return cutText(m, text, scale, room, buf, bufSize);
}

void cutTextLeft(const Metrics &m, const char *text, float scale, int room, char *out, int size)
{
	int lo = 1, hi = (int)strlen(text);
	snprintf(out, size, "%s", text);
	if (hi == 0 || measure(m, out, scale) <= room)
		return;
	while (lo < hi && ((unsigned char)text[lo] & 0xC0) == 0x80)	/* at least one character goes */
		lo++;
	/* the smallest character boundary whose tail, with "..." in front, fits (the width only shrinks with it);
	 * lo and hi are always boundaries, and when nothing fits the result is "..." alone */
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		while (mid > lo && ((unsigned char)text[mid] & 0xC0) == 0x80)
			mid--;
		snprintf(out, size, "...%s", text + mid);
		if (measure(m, out, scale) <= room)
			hi = mid;
		else {
			lo = mid + 1;
			while (lo < hi && ((unsigned char)text[lo] & 0xC0) == 0x80)
				lo++;
		}
	}
	snprintf(out, size, "...%s", text + lo);
}

void countText(const Metrics &m, float scale, int room, int folders, int games, int hint, int filterCell,
               char *out, int size)
{
	char head[40], tail[24];
	if (folders == 0)
		snprintf(head, sizeof head, games == 1 ? "%d game" : "%d games", games);
	else if (games == 0)
		snprintf(head, sizeof head, folders == 1 ? "%d folder" : "%d folders", folders);
	else
		snprintf(head, sizeof head, "%d folder%s, %d game%s",
		         folders, folders == 1 ? "" : "s", games, games == 1 ? "" : "s");
	tail[0] = 0;
	if (filterCell >= 0)
		snprintf(tail, sizeof tail, ", %c", stripChar(filterCell));
	else if (hint == COUNT_HINT_AZ)
		snprintf(tail, sizeof tail, ", A-Z");
	else if (hint == COUNT_HINT_FOLDERS)
		snprintf(tail, sizeof tail, ", folders first");
	else if (hint == COUNT_HINT_NEWEST)
		snprintf(tail, sizeof tail, ", newest first");
	snprintf(out, size, "%s%s", head, tail);
	if (tail[0] && m.width(out, scale, m.ctx) > room)	/* too wide: the hint goes first */
		snprintf(out, size, "%s", head);
	if (m.width(out, scale, m.ctx) > room) {
		char buf[80];
		snprintf(buf, sizeof buf, "%s", out);
		cutText(m, buf, scale, room, out, size);
	}
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
	case PROB_LABEL_WIDE:  return "label wider than 224 px";
	case PROB_PAST_RIGHT:  return "control past x 616";
	case PROB_IN_STD_LOGO: return "in the standard's logo box";
	case PROB_ARROW_TIGHT: return "a cycle value touches its arrows";
	case PROB_BTN_TOUCH:   return "action buttons closer than 12 px";
	}
	return "?";
}

} /* namespace engine */
} /* namespace menu */
