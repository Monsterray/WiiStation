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
 * two controls per row plus a row of up to MAX_SLOTS buttons, and two per help line. */
typedef char engine_controls_fit
	[(MAX_TABS + 1 + MAX_ROWS * (2 + MAX_SLOTS) + 2 * MAX_HELP) <= MAX_CONTROLS ? 1 : -1];

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

static int rowSlots(const Row &r)
{
	switch (r.kind) {
	case ROW_TOGGLE:
	case ROW_CYCLE:   return 1;
	case ROW_CHOICE:
	case ROW_BUTTONS: return r.count;
	default:          return 0;
	}
}

static bool rowValid(const Row &r)
{
	switch (r.kind) {
	case ROW_TOGGLE:
	case ROW_CYCLE:   return r.count >= 1 && r.names != NULL;
	case ROW_CHOICE:  return r.count >= 1 && r.count <= MAX_SLOTS && r.names != NULL;
	case ROW_BUTTONS: return r.count >= 1 && r.count <= MAX_SLOTS && r.actions != NULL;
	case ROW_READOUT: return true;
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
                     float x, float y, float w, float h, const char *text, bool lit)
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
	c.focusable = true;
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

	y = ROW_Y0 + p.nRows * ROW_DY;
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

	y = ROW_Y0 + p.nRows * ROW_DY;
	for (int i = 0; i < p.nHelp; i++) {
		const HelpLine &h = p.help[i];
		if (i > 0 && h.term) y += HELP_CELL_GAP;
		if (h.term)
			addText(out, m, ROLE_HELP_TERM, -1, i, termR, y, ALIGN_RIGHT_TOP, HELP_SCALE, h.term);
		addText(out, m, ROLE_HELP_TEXT, -1, i, textX, y, ALIGN_LEFT_TOP, HELP_SCALE, h.text);
		y += HELP_LINE_DY;
	}
}

/* A choice row's width, from its first button's left edge to its last one's right edge. */
static float choiceRowWidth(const Metrics &m, const Row &r)
{
	float total = 0;
	for (int c = 0; c < r.count; c++) {
		total += r.fit ? (float)buttonFitWidth(m, r.names[c]) : CHOICE_W;
		if (c + 1 < r.count)
			total += r.fit ? CHOICE_GAP : CHOICE_DX - CHOICE_W;
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

int layoutPage(const Page &p, const Metrics &m, Layout &out)
{
	if (p.nRows < 0 || p.nRows > MAX_ROWS || p.nHelp < 0 || p.nHelp > MAX_HELP ||
	    p.nTabs < 0 || p.nTabs > MAX_TABS || (p.nTabs > 0 && (!p.tabs || p.curTab < 0 || p.curTab >= p.nTabs)))
		return -1;
	for (int i = 0; i < p.nRows; i++)
		if (!rowValid(p.rows[i]))
			return -1;

	out.n = 0;
	out.title = -1;
	out.defaultFocus = -1;
	for (int t = 0; t < MAX_TABS; t++)
		out.tab[t] = -1;
	for (int r = 0; r < MAX_ROWS; r++)
		for (int s = 0; s < MAX_SLOTS; s++)
			out.slot[r][s] = -1;

	/* The choice labels' shared centre: RLABEL_CX, moved left only when a row of buttons had
	 * to move left, so that its label still clears them by RLABEL_GAP. */
	float rlabelCx = RLABEL_CX;
	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		if (r.kind != ROW_CHOICE || !r.label)
			continue;
		{
			const float x0 = choiceStart(m, r, ROW_Y0 + i * ROW_DY, p.logo);
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
			                       p.tabs[t].text, t == p.curTab);
			x += p.tabs[t].width + TAB_ROW_GAP;
		}
	} else if (p.title) {
		out.title = addText(out, m, ROLE_TITLE, -1, 0, TITLE_X, TITLE_Y, ALIGN_CENTRE, TEXT_SCALE, p.title);
	}

	for (int i = 0; i < p.nRows; i++) {
		const Row &r = p.rows[i];
		const float y = ROW_Y0 + i * ROW_DY;

		switch (r.kind) {
		case ROW_TOGGLE:
		case ROW_CYCLE: {
			if (r.label)
				addText(out, m, ROLE_LABEL, i, 0, LABEL_CX, y + LABEL_DY, ALIGN_CENTRE, TEXT_SCALE, r.label);
			out.slot[i][0] = addButton(out, ROLE_BUTTON, i, 0, CYCLE_X, y, CYCLE_W, BUTTON_H,
			                           r.names[shownValue(r) - r.lo], false);
			break;
		}
		case ROW_READOUT: {
			/* The text is centred on VALUE_CX, unless its label would run into it: then it
			 * starts VALUE_GAP after the label's right edge, as TextBox would draw it. */
			float vx = VALUE_CX;
			if (r.label) {
				const int lw = measure(m, r.label, TEXT_SCALE);
				const int vw = measure(m, r.text, TEXT_SCALE);
				const int need = (int)LABEL_CX - lw / 2 + lw + (int)VALUE_GAP;
				if ((int)vx - vw / 2 < need)
					vx = (float)(need + vw / 2);
			}
			if (r.label)
				addText(out, m, ROLE_LABEL, i, 0, LABEL_CX, y + LABEL_DY, ALIGN_CENTRE, TEXT_SCALE, r.label);
			addText(out, m, ROLE_VALUE, i, 0, vx, y + LABEL_DY, ALIGN_CENTRE, TEXT_SCALE, r.text);
			break;
		}
		case ROW_CHOICE: {
			const int lit = shownValue(r) - r.lo;
			float cx = choiceStart(m, r, y, p.logo);
			if (r.label)
				addText(out, m, ROLE_RLABEL, i, 0, rlabelCx, y + LABEL_DY, ALIGN_CENTRE, TEXT_SCALE, r.label);
			for (int c = 0; c < r.count; c++) {
				float w = r.fit ? (float)buttonFitWidth(m, r.names[c]) : CHOICE_W;
				out.slot[i][c] = addButton(out, ROLE_CHOICE, i, c, cx, y, w, BUTTON_H, r.names[c], c == lit);
				cx += r.fit ? w + CHOICE_GAP : CHOICE_DX;
			}
			break;
		}
		case ROW_BUTTONS: {
			float total = TAB_GAP * (r.count - 1);
			float x;
			for (int k = 0; k < r.count; k++)
				total += r.actions[k].width;
			x = (MENU_W - total) / 2;
			for (int k = 0; k < r.count; k++) {
				out.slot[i][k] = addButton(out, ROLE_BUTTON, i, k, x, y, r.actions[k].width, BUTTON_H,
				                           r.actions[k].text, false);
				x += r.actions[k].width + TAB_GAP;
			}
			break;
		}
		}
	}

	if (p.nHelp > 0)
		layoutHelp(p, m, out);
	return out.n;
}

/*  Focus  */

/* OptionsFrame's rule. Rows run top to bottom and wrap, a row's buttons go left and right
 * and wrap, and Up/Down keep the column (a shorter row takes its last column). On a tabbed
 * page the tab row is the top of the loop: the last row goes down to the lit tab, the first
 * row up to it, and every tab goes down to the first row and up to the last. */
static void linkRows(const Page &p, Layout &lay)
{
	int F[MAX_ROWS], nf = 0;
	const bool tabbed = p.nTabs > 0;
	const int cur = tabbed ? lay.tab[p.curTab] : -1;

	for (int i = 0; i < p.nRows; i++)
		if (rowSlots(p.rows[i]) > 0)
			F[nf++] = i;

	for (int k = 0; k < nf; k++) {
		const int i = F[k], n = rowSlots(p.rows[i]);
		if (n > 1)
			for (int c = 0; c < n; c++) {
				lay.c[lay.slot[i][c]].link[DIR_LEFT]  = lay.slot[i][(c + n - 1) % n];
				lay.c[lay.slot[i][c]].link[DIR_RIGHT] = lay.slot[i][(c + 1) % n];
			}
	}

	for (int k = 0; k < nf; k++) {
		const bool wrap = (k == nf - 1);
		int A, B, nA, nB;
		if (wrap && tabbed)
			continue;		/* the tab row takes the wrap, below */
		A = F[k];
		B = F[wrap ? 0 : k + 1];
		nA = rowSlots(p.rows[A]);
		nB = rowSlots(p.rows[B]);
		for (int c = 0; c < nA; c++)
			lay.c[lay.slot[A][c]].link[DIR_DOWN] = lay.slot[B][c < nB ? c : nB - 1];
		for (int c = 0; c < nB; c++)
			lay.c[lay.slot[B][c]].link[DIR_UP] = lay.slot[A][c < nA ? c : nA - 1];
	}

	if (tabbed) {
		for (int t = 0; t < p.nTabs; t++) {
			Control &tc = lay.c[lay.tab[t]];
			tc.link[DIR_LEFT]  = lay.tab[(t + p.nTabs - 1) % p.nTabs];
			tc.link[DIR_RIGHT] = lay.tab[(t + 1) % p.nTabs];
			if (nf) {
				tc.link[DIR_DOWN] = lay.slot[F[0]][0];
				tc.link[DIR_UP]   = lay.slot[F[nf - 1]][0];
			}
		}
		if (nf) {
			const int first = F[0], last = F[nf - 1];
			for (int c = 0; c < rowSlots(p.rows[last]); c++)
				lay.c[lay.slot[last][c]].link[DIR_DOWN] = cur;
			for (int c = 0; c < rowSlots(p.rows[first]); c++)
				lay.c[lay.slot[first][c]].link[DIR_UP] = cur;
		}
	}
}

/* CustomizeCoopFrame's rule: from each focusable control, the nearest other one in each
 * direction, scored as along + 2 x the sideways offset, so a straight line wins. Controls
 * further than one pixel behind the origin are not candidates. */
static void linkGeometry(Layout &lay)
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
				switch (d) {
				case DIR_LEFT:  along = -dx; side = dy; break;
				case DIR_RIGHT: along =  dx; side = dy; break;
				case DIR_UP:    along = -dy; side = dx; break;
				default:        along =  dy; side = dx; break;
				}
				if (along <= 1.0f)
					continue;
				cost = along + 2.0f * fabsf(side);
				if (cost < bestCost) {
					bestCost = cost;
					best = cand[b];
				}
			}
			lay.c[cand[a]].link[d] = best;
		}
	}
}

/* Where focus starts: the first row's first button, as OptionsFrame does, or the lit tab on a
 * page with no focusable row. Shared by both focus modes. */
void linkFocus(const Page &p, Layout &lay)
{
	int def = -1;

	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++)
			lay.c[i].link[d] = -1;

	if (p.focus == FOCUS_GEOMETRY)
		linkGeometry(lay);
	else
		linkRows(p, lay);

	for (int i = 0; i < p.nRows && def < 0; i++)
		if (rowSlots(p.rows[i]) > 0)
			def = lay.slot[i][0];
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
		    (c.role == ROLE_TAB || c.role == ROLE_BUTTON || c.role == ROLE_CHOICE)) {
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
