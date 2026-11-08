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

extern "C" void SysPrintf(const char *fmt, ...);

namespace menu {
namespace glue {

static char emptyStr[1] = "";

/* A button's tag: a row's button or segment is (row + 1) * 256 + slot + 1; a tab is -(tab + 1). */
static int rowTag(int row, int slot) { return (row + 1) * 256 + slot + 1; }

PageView::PageView(Frame *owner, int rows, int slots, int texts, int tabs, int help, ActFn actFn, void *userData,
                   ButtonFunc back, const char *title)
	: frame(owner), pool(new Pool(owner, tabs + rows * slots)),
	  nRows(rows), nSlots(slots), nTexts(texts), nTabs(tabs), nHelp(help), act(actFn), user(userData),
	  titleString((char*)title), footerRow(-1)
{
	rowW = new RowW[rows];
	tabW = new Button*[tabs];
	tabString = new char*[tabs];
	helpTermBox = new TextBox*[help];  helpBox = new TextBox*[help];
	helpTermString = new char*[help];  helpString = new char*[help];
	footerBox = new TextBox*[STD_HELP_LINES];
	footerString = new char*[STD_HELP_LINES];

	titleBox = new TextBox(&titleString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
	frame->add(titleBox);

	for (int t = 0; t < tabs; t++)
	{
		tabString[t] = emptyStr;
		/* BTN_A_SEL, as the Settings tabs: the current one stays lit. The pointer's box is set by show(). */
		tabW[t] = pool->button(engine::ROLE_TAB, -1, t, BTN_A_SEL, &tabString[t], 0.0f, 0.0f, 0.0f, 0.0f, 0, back);
		tabW[t]->setClickedCtx(click, this, -(t + 1));
	}

	for (int i = 0; i < rows; i++)
	{
		RowW &r = rowW[i];
		r.btn = new Button*[slots];
		r.btnString = new char*[slots];
		r.marks = new unsigned char[slots];
		r.valueBox = new TextBox*[texts];
		r.valueString = new char*[texts];
		r.labelString = emptyStr;
		r.value[0] = '\0';
		/* The label lives in two places depending on the row kind, so it gets two boxes; the page
		 * makes the one it needs visible. */
		r.text     = new TextBox(&r.labelString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
		r.rlabel   = new TextBox(&r.labelString, 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
		frame->add(r.text);
		frame->add(r.rlabel);
		for (int c = 0; c < texts; c++)
		{
			r.valueString[c] = r.value;
			r.valueBox[c] = new TextBox(&r.valueString[c], 0.0f, 0.0f, engine::grid::TEXT_SCALE, true);
			frame->add(r.valueBox[c]);
		}
		for (int c = 0; c < slots; c++)
		{
			r.btnString[c] = emptyStr;
			r.marks[c] = 0;
			/* The art is set when the page is shown: it follows the button's role there. */
			r.btn[c] = pool->button(engine::ROLE_BUTTON, i, c, BTN_A_NRM, &r.btnString[c], 0.0f, 0.0f, 0.0f, 0.0f, 0, back);
			r.btn[c]->setClickedCtx(click, this, rowTag(i, c));
		}
	}

	for (int i = 0; i < help; i++)
	{
		helpTermString[i] = emptyStr;
		helpString[i] = emptyStr;
		helpTermBox[i] = new TextBox(&helpTermString[i], 0.0f, 0.0f, engine::grid::HELP_SCALE, false);
		helpTermBox[i]->setRightAligned(true);
		helpTermBox[i]->setBold(true);
		helpBox[i] = new TextBox(&helpString[i], 0.0f, 0.0f, engine::grid::HELP_SCALE, false);
		frame->add(helpTermBox[i]);
		frame->add(helpBox[i]);
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
		delete[] rowW[i].valueBox;
		delete[] rowW[i].valueString;
		delete[] rowW[i].btn;
		delete[] rowW[i].btnString;
		delete[] rowW[i].marks;
	}
	for (int i = 0; i < nHelp; i++)
	{
		delete helpTermBox[i];
		delete helpBox[i];
	}
	for (int i = 0; i < STD_HELP_LINES; i++)
		delete footerBox[i];
	delete[] rowW; delete[] tabW; delete[] tabString;
	delete[] helpTermBox; delete[] helpBox; delete[] helpTermString; delete[] helpString;
	delete[] footerBox; delete[] footerString;
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
	footerRow = -1;
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
	for (int i = 0; i < nHelp; i++)
	{
		helpTermString[i] = emptyStr;
		helpString[i] = emptyStr;
		helpTermBox[i]->setVisible(false);
		helpBox[i]->setVisible(false);
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
	const bool standard = page.standard != engine::DENSITY_OFF;
	if (engine::layoutPage(page, METRICS, lay) < 0)
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
			/* BTN_A_SEL, as the Settings tabs: the current one stays lit. */
			tabW[c.slot]->setStyle(standard ? BTN_STD : BTN_A_SEL);
			tabW[c.slot]->setPlain(false);
			placeButton(frame, tabW[c.slot], c, false);
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
			/* a toggle or cycle, or an action. On the standard an action is plain art; a flag keeps the
			 * select art (it is lit when on), and a cycle has an arrow at each end. */
			Button *b = rowW[c.row].btn[c.slot];
			const int marks = rowW[c.row].marks[c.slot];
			rowW[c.row].btnString[c.slot] = (char*)c.text;
			b->setStyle(standard ? BTN_STD : BTN_A_NRM);
			b->setPlain(standard && !(marks & TOGGLE));
			b->setArrows(standard && (page.rows[c.row].kind == engine::ROW_CYCLE || (marks & CYCLE)));
			placeButton(frame, b, c, engineLit);
			break;
		}
		case engine::ROLE_CHOICE:
		{
			/* BTN_A_SEL is the style a one-of-several choice uses: the selected one stays lit. */
			Button *b = rowW[c.row].btn[c.slot];
			rowW[c.row].btnString[c.slot] = (char*)c.text;
			b->setStyle(standard ? BTN_STD : BTN_A_SEL);
			b->setPlain(standard && (rowW[c.row].marks[c.slot] & FLAT));
			b->setArrows(false);
			placeButton(frame, b, c, engineLit);
			break;
		}
		case engine::ROLE_HELP_TERM:
			helpTermString[c.slot] = (char*)c.text;
			helpTermBox[c.slot]->setPosition(c.ax, c.ay);
			helpTermBox[c.slot]->setVisible(true);
			break;
		case engine::ROLE_HELP_TEXT:
			helpString[c.slot] = (char*)c.text;
			helpBox[c.slot]->setPosition(c.ax, c.ay);
			helpBox[c.slot]->setVisible(true);
			break;
		}
	}
	linkButtons();
	return true;
}

/* The engine's focus links, on the buttons. */
void PageView::linkButtons(void) const
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < lay.n; i++)
	{
		const engine::Control &c = lay.c[i];
		if (!c.focusable)
			continue;
		Button *b = buttonFor(c);
		if (!b)
			continue;
		for (int d = 0; d < engine::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			b->setNextFocus(DIRS[d], to >= 0 ? buttonFor(lay.c[to]) : NULL);
		}
	}
}

Button *PageView::startButton(void) const
{
	return lay.defaultFocus >= 0 ? buttonFor(lay.c[lay.defaultFocus]) : NULL;
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
	if (row >= 0)
		footerRow = row;
	else
	{
		for (int t = 0; t < nTabs; t++)
			if (tabW[t]->getFocus())
				footerRow = -1;
	}
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
	int rowCount[engine::MAX_ROWS] = { 0 };
	memset(acts, 0, sizeof acts);
	for (int k = 0; k < def.nActs; k++)
	{
		const ActionDef &a = def.acts[k];
		engine::ActionButton &b = acts[a.row][a.slot];
		snprintf(label[k], LABEL_LEN, "%s", a.text);
		labelPtr[k] = label[k];
		b.text  = label[k];
		b.lit   = false;
		b.width = a.width;
		b.dy    = a.dy;
		if (a.slot + 1 > rowCount[a.row])
			rowCount[a.row] = a.slot + 1;	/* a row holds slots 0 .. its last one */
	}
	memset(rows, 0, sizeof rows);
	for (int r = 0; r < def.nRows; r++)
	{
		rows[r].kind    = engine::ROW_BUTTONS;
		rows[r].count   = rowCount[r];
		rows[r].actions = acts[r];
		rows[r].x0      = def.rows[r].x0;
		rows[r].gap     = def.rows[r].gap;
	}
	memset(&page, 0, sizeof page);
	page.rows       = rows;
	page.nRows      = def.nRows;
	page.logo       = def.logo;
	page.focus      = engine::FOCUS_ROWS;
	page.rowY0      = def.rowY0;
	page.overrides  = def.links;
	page.nOverrides = def.nLinks;
	engine::layoutPage(page, METRICS, lay);
	engine::linkFocus(page, lay);
#ifdef PERF_PROF
	logProblems("ActionMenu", def.name, page, lay);
#endif

	/* Each button's box, and so its Cursor range, is the engine's rectangle for its place. */
	pool = new Pool(owner, def.nActs);
	for (int k = 0; k < def.nActs; k++)
	{
		const ActionDef &a = def.acts[k];
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
	return pool->at(engine::ROLE_BUTTON, def.acts[i].row, def.acts[i].slot);
}

#ifdef PERF_PROF
void logProblems(const char *frameName, const char *pageName, const engine::Page &page, const engine::Layout &lay)
{
	static engine::Problem probs[engine::MAX_PROBLEMS];
	const int np = engine::checkLayout(page, METRICS, lay, probs, engine::MAX_PROBLEMS);
	if (np > 0)
		SysPrintf("%s: page '%s' has %d layout problem(s)\n", frameName, pageName, np);
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
