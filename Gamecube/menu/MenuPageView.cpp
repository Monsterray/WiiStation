/**
 * WiiStation - MenuPageView.cpp
 *
 * The widgets of a Form page and their placement (see MenuPageView.h).
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

#include <stdio.h>
#include <string.h>

#include "MenuPageView.h"
#include "MenuLayout.h"
#include "../libgui/FocusManager.h"
#include "../libgui/resources.h"
#include "../libgui/IPLFont.h"
#include "../libgui/GraphicsGX.h"

extern "C" void SysPrintf(const char *fmt, ...);

namespace menu {
namespace glue {

static char emptyStr[1] = "";

enum { nRows = engine::MAX_ROWS, nSlots = engine::MAX_SLOTS, nTexts = engine::MAX_SLOTS,
       nTabs = engine::MAX_TABS };

/* A button's tag: a row's button or segment is (row + 1) * 256 + slot + 1; a tab is -(tab + 1). */
static int rowTag(int row, int slot) { return (row + 1) * 256 + slot + 1; }

PageView::PageView(Frame *owner, ActFn actFn, void *userData, ButtonFunc back, const char *title)
	: frame(owner), pool(new Pool(owner, nTabs + nRows * nSlots)),
	  act(actFn), user(userData), titleString((char*)title), footerRow(-1), footerShown(-1)
{
	titleBox = new TextBox(&titleString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
	frame->add(titleBox);

	for (int t = 0; t < nTabs; t++)
	{
		tabString[t] = emptyStr;
		/* BTN_A_SEL, as the Settings tabs: the current one stays lit. The pointer's box is set by show(). */
		tabW[t] = pool->button(engine::ROLE_TAB, -1, t, BTN_A_SEL, &tabString[t], 0.0f, 0.0f, 0.0f, 0.0f, 0, back);
		tabW[t]->setClickedCtx(click, this, -(t + 1));
	}

	for (int i = 0; i < nRows; i++)
	{
		RowW &r = rowW[i];
		r.labelString = emptyStr;
		r.value[0] = '\0';
		/* The label lives in two places depending on the row kind, so it gets two boxes; the page
		 * makes the one it needs visible. */
		r.text     = new TextBox(&r.labelString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
		r.rlabel   = new TextBox(&r.labelString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
		frame->add(r.text);
		frame->add(r.rlabel);
		for (int c = 0; c < nTexts; c++)
		{
			r.valueString[c] = r.value;
			r.valueBox[c] = new TextBox(&r.valueString[c], 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
			frame->add(r.valueBox[c]);
		}
		for (int c = 0; c < nSlots; c++)
		{
			r.btnString[c] = emptyStr;
			r.marks[c] = 0;
			/* The art is set when the page is shown: it follows the button's role there. */
			r.btn[c] = pool->button(engine::ROLE_BUTTON, i, c, BTN_A_NRM, &r.btnString[c], 0.0f, 0.0f, 0.0f, 0.0f, 0, back);
			r.btn[c]->setClickedCtx(click, this, rowTag(i, c));
		}
	}

	for (int i = 0; i < STD_HELP_LINES; i++)
	{
		footerString[i] = emptyStr;
		footerBox[i] = new TextBox(&footerString[i], 0.0f, 0.0f, (float)STD_HELP_PX / engine::grid::FONT_H, false);
		GXColor helpColor = STD_HELP_COLOR;
		footerBox[i]->setColor(&helpColor);
		footerBox[i]->setVisible(false);
		frame->add(footerBox[i]);
	}
	memset(&lay, 0, sizeof lay);
	lay.defaultFocus = -1;
}

PageView::~PageView()
{
	delete titleBox;
	delete pool;
	for (int i = 0; i < nRows; i++)
	{
		delete rowW[i].text;
		delete rowW[i].rlabel;
		for (int c = 0; c < nTexts; c++)
			delete rowW[i].valueBox[c];
	}
	for (int i = 0; i < STD_HELP_LINES; i++)
		delete footerBox[i];
}

void PageView::click(void *self, int tag)
{
	PageView *v = (PageView*)self;
	if (tag < 0)
		v->act(v->user, -1, -tag - 1);
	else
		v->act(v->user, tag / 256 - 1, tag % 256 - 1);
}

void PageView::setTabText(int t, const char *text)
{
	tabString[t] = (char*)text;
}

void PageView::setLit(int row, int slot, bool lit)
{
	rowW[row].btn[slot]->setSelected(lit);
}

void PageView::hideAll(void)
{
	footerRow = footerShown = -1;
	titleBox->setVisible(false);
	for (int t = 0; t < nTabs; t++)
	{
		tabW[t]->setVisible(false);
		tabW[t]->setActive(false);
		tabW[t]->setSelected(false);
	}
	for (int i = 0; i < nRows; i++)
	{
		RowW &r = rowW[i];
		r.text->setVisible(false);
		r.rlabel->setVisible(false);
		for (int c = 0; c < nTexts; c++)
			r.valueBox[c]->setVisible(false);
		for (int c = 0; c < nSlots; c++)
		{
			r.btn[c]->setVisible(false);
			r.btn[c]->setActive(false);
			r.btn[c]->setSelected(false);
			r.marks[c] = 0;
		}
	}
	for (int i = 0; i < STD_HELP_LINES; i++)
		footerBox[i]->setVisible(false);
}

Button *PageView::buttonFor(const engine::Control &c) const
{
	switch (c.role)
	{
	case engine::ROLE_TAB:    return tabW[c.slot];
	case engine::ROLE_BUTTON:
	case engine::ROLE_CHOICE: return rowW[c.row].btn[c.slot];
	default:                  return NULL;
	}
}

bool PageView::show(const engine::Page &page, bool engineLit)
{
	if (page.standard == engine::DENSITY_OFF || engine::layoutPage(page, METRICS, lay) < 0)
	{
		/* The tables are fixed, so this is a programming error. Nothing is shown rather than a half-placed page. */
		lay.n = 0;
		lay.defaultFocus = -1;
		return false;
	}
	engine::linkFocus(page, lay);

	/* Each shown control at its place. Text takes the engine's anchor: a TextBox draws itself from
	 * there (centered, or right-aligned for the help terms). A button's lit state is the caller's. */
	for (int i = 0; i < lay.n; i++)
	{
		const engine::Control &c = lay.c[i];
		switch (c.role)
		{
		case engine::ROLE_TAB:
			tabW[c.slot]->setStyle(BTN_STD);
			tabW[c.slot]->setPlain(false);
			placeButton(tabW[c.slot], c, false);
			tabW[c.slot]->setSelected(c.selected);
			break;
		case engine::ROLE_TITLE:
			titleString = (char*)c.text;
			titleBox->setPosition(c.ax, c.ay);
			titleBox->setVisible(true);
			break;
		case engine::ROLE_LABEL:
		case engine::ROLE_RLABEL:
		{
			TextBox *box = c.role == engine::ROLE_LABEL ? rowW[c.row].text : rowW[c.row].rlabel;
			rowW[c.row].labelString = (char*)c.text;
			box->setPosition(c.ax, c.ay);
			box->setVisible(true);
			break;
		}
		case engine::ROLE_VALUE:	/* a readout's text, or a small cycle's prefix (slot = the cycle) */
			rowW[c.row].valueString[c.slot] = (char*)c.text;
			rowW[c.row].valueBox[c.slot]->setPosition(c.ax, c.ay);
			rowW[c.row].valueBox[c.slot]->setVisible(true);
			break;
		case engine::ROLE_BUTTON:
		{
			/* a toggle or cycle, or an action. An action is plain art; a flag keeps the select art (it is
			 * lit when on), and a cycle has an arrow at each end. */
			Button *b = rowW[c.row].btn[c.slot];
			const int marks = rowW[c.row].marks[c.slot];
			rowW[c.row].btnString[c.slot] = (char*)c.text;
			b->setStyle(BTN_STD);
			b->setPlain(!(marks & TOGGLE));
			b->setArrows(page.rows[c.row].kind == engine::ROW_CYCLE || (marks & CYCLE));
			placeButton(b, c, engineLit);
			break;
		}
		case engine::ROLE_CHOICE:
		{
			Button *b = rowW[c.row].btn[c.slot];
			rowW[c.row].btnString[c.slot] = (char*)c.text;
			b->setStyle(BTN_STD);
			b->setPlain(rowW[c.row].marks[c.slot] & FLAT);
			b->setArrows(false);
			placeButton(b, c, engineLit);
			break;
		}
		}
	}
	linkButtons();
	return true;
}

/* The engine's focus links, on the buttons. */
void PageView::linkButtons(void) const
{
	linkControls(lay, [this](const engine::Control &c) { return buttonFor(c); });
}

Button *PageView::startButton(void) const
{
	return lay.defaultFocus >= 0 ? buttonFor(lay.c[lay.defaultFocus]) : NULL;
}

void PageView::update(const engine::Page &page)
{
	const int cur = page.curTab, tab = page.nTabs ? tabShoulderStep(cur, page.nTabs) : cur;
	if (tab != cur)
	{
		act(user, -1, tab);	/* the new tab sets its own default focus; clearing it here showed it a frame late */
	}
	updateFooter(page);
}

void PageView::updateFooter(const engine::Page &page)
{
	int row = -1;
	for (int i = 0; i < lay.n && row < 0; i++)
	{
		const engine::Control &c = lay.c[i];
		if ((c.role == engine::ROLE_BUTTON || c.role == engine::ROLE_CHOICE) && buttonFor(c)->getFocus())
			row = c.row;
	}
	/* A focused row shows its help; a focused tab shows none (rows without help show none either);
	 * anything else (the back button) keeps what was shown. */
	if (row >= 0)
		footerRow = row;
	else
	{
		for (int t = 0; t < nTabs; t++)
			if (tabW[t]->getFocus())
				footerRow = -1;
	}
	if (footerRow == footerShown)
		return;
	footerShown = footerRow;
	engine::Control out[STD_HELP_LINES];
	int n = 0;
	if (page.footer && footerRow >= 0)
		n = engine::layoutFooter(page, METRICS, footerRow, out);
	for (int i = 0; i < STD_HELP_LINES; i++)
	{
		const bool on = i < n;
		if (on)
		{
			footerString[i] = (char*)out[i].text;
			footerBox[i]->setPosition(out[i].ax, out[i].ay);
		}
		footerBox[i]->setVisible(on);
	}
}

ActionMenu::ActionMenu(Frame *owner, const ActionMenuDef &d, ButtonFunc back)
	: def(d), pool(NULL)
{
	memset(&lay, 0, sizeof lay);
	for (int k = 0; k < def.nActs && k < MAX_ACTIONS; k++)
	{
		snprintf(label[k], LABEL_LEN, "%s", def.acts[k].text);
		labelPtr[k] = label[k];
	}
	/* A table that does not fit the engine, or a page it refuses, makes no buttons (a programming error). */
	if (!buildActionPage(def, labelPtr, ap) || engine::layoutPage(ap.page, METRICS, lay) < 0)
	{
		pool = new Pool(owner, 0);
		return;
	}
	engine::linkFocus(ap.page, lay);
#ifdef PERF_PROF
	logProblems("ActionMenu", def.name, ap.page, lay);
#endif

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its place. */
	pool = new Pool(owner, def.nActs);
	for (int k = 0; k < def.nActs; k++)
	{
		const ActionDef &a = def.acts[k];
		if (lay.slot[a.row][a.slot] < 0)
			continue;
		const engine::Control &c = lay.c[lay.slot[a.row][a.slot]];
		pool->button(engine::ROLE_BUTTON, a.row, a.slot, BTN_A_NRM, &labelPtr[k],
		             c.rect.x, c.rect.y, c.rect.w, c.rect.h, a.click, back);
	}
	pool->link(lay);	/* the engine's links, handed to the buttons */
}

ActionMenu::~ActionMenu()
{
	delete pool;
}

void ActionMenu::setText(int i, const char *text)
{
	snprintf(label[i], LABEL_LEN, "%s", text);
}

Button *ActionMenu::button(int i) const
{
	if (i < 0 || i >= def.nActs)
		return NULL;
	return pool->at(engine::ROLE_BUTTON, def.acts[i].row, def.acts[i].slot);
}

Editor::Editor(Frame *owner, int nButtons, int nLabels, ClickFn clickF, void *userData, ButtonFunc back)
	: frame(owner), pool(new Pool(owner, nButtons)), clickFn(clickF), user(userData), backFn(back),
	  it(new Item[nButtons]), lb(new Label[nLabels > 0 ? nLabels : 1]), nBtn(nButtons), nLbl(nLabels),
	  links(new Link[nButtons * engine::DIR_COUNT]), nLinks(0),
	  fb(new engine::FreeButton[nButtons]), fbId(new int[nButtons]),
	  ft(new engine::FreeText[nLabels > 0 ? nLabels : 1]), ftId(new int[nLabels > 0 ? nLabels : 1]),
	  fl(new engine::FreeLink[nButtons * engine::DIR_COUNT]),
	  wrap(false), titleBuf(NULL), titlePtr(NULL), titleY(0), titleBox(NULL)
{
	memset(it, 0, sizeof(Item) * nButtons);
	memset(lb, 0, sizeof(Label) * (nLabels > 0 ? nLabels : 1));
}

Editor::~Editor()
{
	delete titleBox;
	for (int i = 0; i < nLbl; i++)
		delete lb[i].box;
	delete pool;	/* takes the buttons out of the Cursor and deletes them */
	delete[] it; delete[] lb; delete[] links; delete[] fb; delete[] fbId; delete[] ft; delete[] ftId; delete[] fl;
}

void Editor::click(void *self, int tag)
{
	Editor *e = (Editor*)self;
	e->clickFn(e->user, tag);
}

Button *Editor::add(int id, int style, char **text, float x, float y, float w, float h)
{
	Item &i = it[id];
	i.text = text; i.x = x; i.y = y; i.w = w; i.h = h;
	i.shown = i.active = true; i.lit = false;
	i.b = pool->button(engine::ROLE_FREE, -1, id, style, text, x, y, w, h, NULL, backFn);
	i.b->setClickedCtx(click, this, id);
	/* style A makes itself 56 high; the author's rectangle is the one */
	i.b->setBounds(x, y, w, h);
	return i.b;
}

TextBox *Editor::addLabel(int id, char **text, float ax, float ay, float scale, bool centred)
{
	Label &l = lb[id];
	l.text = text; l.ax = ax; l.ay = ay; l.scale = scale; l.centred = centred; l.shown = true;
	l.box = new TextBox(text, ax, ay, scale, centred);
	frame->add(l.box);
	return l.box;
}

void Editor::setTitle(char *buf, float y)
{
	titleBuf = buf; titlePtr = buf; titleY = y;
}

void Editor::link(int from, int up, int down, int left, int right)
{
	const int to[engine::DIR_COUNT] = { up, down, left, right };
	static const engine::Dir dirs[engine::DIR_COUNT] = { engine::DIR_UP, engine::DIR_DOWN, engine::DIR_LEFT, engine::DIR_RIGHT };
	for (int d = 0; d < engine::DIR_COUNT && nLinks < nBtn * engine::DIR_COUNT; d++)
	{
		links[nLinks].from = from; links[nLinks].dir = dirs[d]; links[nLinks].to = to[d];
		nLinks++;
	}
}

void Editor::relayout(bool withState)
{
	int nf = 0, nt = 0, nl = 0;
	int freeOf[nBtn];

	for (int i = 0; i < nBtn; i++)
	{
		freeOf[i] = -1;
		if (!it[i].shown)
			continue;
		engine::FreeButton &b = fb[nf];
		b.text = *it[i].text;
		b.x = it[i].x; b.y = it[i].y; b.w = it[i].w; b.h = it[i].h;
		b.lit = it[i].lit;
		b.inactive = !it[i].active;
		fbId[nf] = i;
		freeOf[i] = nf++;
	}
	for (int t = 0; t < nLbl; t++)
	{
		if (!lb[t].shown)
			continue;
		engine::FreeText &x = ft[nt];
		x.text = *lb[t].text;
		x.ax = lb[t].ax; x.ay = lb[t].ay; x.scale = lb[t].scale; x.centred = lb[t].centred;
		x.right = false;
		ftId[nt++] = t;
	}
	for (int k = 0; k < nLinks; k++)
	{
		const Link &l = links[k];
		const int to = l.to < 0 ? -1 : freeOf[l.to];
		if (freeOf[l.from] < 0 || (l.to >= 0 && to < 0))
			continue;
		fl[nl].from = freeOf[l.from]; fl[nl].dir = l.dir; fl[nl].to = to;
		nl++;
	}

	memset(&page, 0, sizeof page);
	page.title = titleBuf;
	page.titleY = titleY;
	page.logo = engine::LOGO_SPOT_NONE;
	page.focus = engine::FOCUS_GEOMETRY;
	page.wrap = wrap;
	page.freeButtons = fb; page.nFreeButtons = nf;
	page.freeTexts = ft; page.nFreeTexts = nt;
	page.freeLinks = fl; page.nFreeLinks = nl;
	if (engine::layoutPage(page, METRICS, lay) < 0)
		return;		/* the tables are fixed: a page that does not fit is a programming error */
	engine::linkFocus(page, lay);

	if (titleBuf && lay.title >= 0)
	{
		const engine::Control &t = lay.c[lay.title];
		if (!titleBox)
		{
			titleBox = new TextBox(&titlePtr, t.ax, t.ay, 1.0f, true);
			frame->add(titleBox);
		}
		else
			titleBox->setPosition(t.ax, t.ay);
	}
	if (withState)
	{
		for (int i = 0; i < nBtn; i++)
		{
			it[i].b->setVisible(it[i].shown);
			it[i].b->setActive(it[i].shown && it[i].active);
			if (it[i].shown)
				it[i].b->setSelected(it[i].lit);
		}
		for (int t = 0; t < nLbl; t++)
			lb[t].box->setVisible(lb[t].shown);
	}
	linkControls(lay, [this](const engine::Control &c) -> Button* {
		return c.role == engine::ROLE_FREE ? it[fbId[c.slot]].b : NULL; }, true);
}

Button *Editor::defaultButton(void) const
{
	if (lay.defaultFocus < 0)
		return NULL;
	const engine::Control &c = lay.c[lay.defaultFocus];
	return c.role == engine::ROLE_FREE ? it[fbId[c.slot]].b : NULL;
}

/*  Board  */

void Board::drawComponent(Graphics &gfx)
{
	onInput(readInput());
	paint(gfx);
}

void text(int x, int y, const char *s, float scale, GXColor c, int maxw)
{
	IplFont &font = IplFont::getInstance();
	if (maxw > 0)
	{
		int w = font.getStringWidth((char *)s, scale);
		if (w > maxw)
			scale = scale * maxw / w;
	}
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, false);
}

int width(const char *s, float scale) { return IplFont::getInstance().getStringWidth((char *)s, scale); }

void textCenter(int x, int y, const char *s, float scale, GXColor c)
{
	IplFont &font = IplFont::getInstance();
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, true);
}

void hintLine(Graphics &gfx, const char *hint)
{
	fill(gfx, 0, 442, 640, 38, (GXColor){52, 52, 52, 255});
	text(20, 449, hint, 0.5f, (GXColor){255, 255, 255, 255});
}

#ifdef PERF_PROF
void logProblems(const char *frameName, const char *pageName, const engine::Page &page, const engine::Layout &lay)
{
	static engine::Problem probs[engine::MAX_PROBLEMS];
	const int np = engine::checkLayout(page, METRICS, lay, probs, engine::MAX_PROBLEMS);
	if (np > 0)
		SysPrintf("%s: page '%s' has %d layout problem(s)\n", frameName, pageName, np);
	const int nf = engine::checkFooters(page, METRICS);
	if (nf > 0)
		SysPrintf("%s: page '%s' has %d row(s) whose help line does not fit\n", frameName, pageName, nf);
	for (int k = 0; k < np && k < engine::MAX_PROBLEMS; k++)
	{
		const engine::Problem &p = probs[k];
		if (p.b >= 0)
			SysPrintf("%s:   %s: '%s' and '%s'\n", frameName, engine::problemName(p.kind), lay.c[p.a].text, lay.c[p.b].text);
		else
			SysPrintf("%s:   %s: '%s'\n", frameName, engine::problemName(p.kind), lay.c[p.a].text);
	}
}
#endif

} /* namespace glue */
} /* namespace menu */
