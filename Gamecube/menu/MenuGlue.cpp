/**
 * WiiStation - MenuGlue.cpp
 *
 * The Wii side of the menu engine (see MenuGlue.h).
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

#include <string.h>

#include "MenuGlue.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/InputManager.h"
#include "../../HidController/KernelHID.h"

/* FileBrowserFrame.cpp: the folder icons, arrow glyphs, scroll thumb and texts of an explorer-density page */
void drawExplorerOverlay(menu::Graphics &gfx, const menu::engine::Layout &L, const menu::engine::ListState &S);

namespace menu {
extern u16 menu_script_keys;   /* InputManager.cpp: an input script's "menukeys" */

namespace glue {

static int textWidth(const char *s, float scale, void *)
{
	return IplFont::getInstance().getStringWidth((char*)s, scale);
}

static int textHeight(const char *s, float scale, void *)
{
	return IplFont::getInstance().getStringHeight((char*)s, scale);
}

const engine::Metrics METRICS = { textWidth, textHeight, NULL };

void linkFocus(Button *b, const engine::Layout &lay, const engine::Control &c, ButtonOfControl buttonOf)
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	for (int d = 0; d < engine::DIR_COUNT; d++)
	{
		const int to = c.link[d];	/* a link of -1 is no link */
		b->setNextFocus(DIRS[d], to >= 0 ? buttonOf(lay.c[to]) : NULL);
	}
}

Pool::Pool(Frame *owner, int capacity) : frame(owner), e(new Entry[capacity]), n(0), cap(capacity)
{
}

Pool::~Pool()
{
	for (int i = 0; i < n; i++)
	{
		Cursor::getInstance().removeComponent(frame, e[i].b);
		delete e[i].b;
	}
	delete[] e;
}

Button *Pool::button(engine::CtrlRole role, int row, int slot, int style, char **text, float x, float y, float w, float h,
                     ButtonFunc click, ButtonFunc ret)
{
	if (n >= cap)
		return NULL;
	Button *b = new Button(style, text, x, y, w, h);
	b->setActive(true);
	if (click) b->setClicked(click);
	if (ret) b->setReturn(ret);
	frame->add(b);
	Cursor::getInstance().addComponent(frame, b, x, x + w, y, y + h);
	e[n].b = b; e[n].role = role; e[n].row = row; e[n].slot = slot;
	n++;
	return b;
}

Button *Pool::at(engine::CtrlRole role, int row, int slot) const
{
	for (int i = 0; i < n; i++)
		if (e[i].role == role && e[i].row == row && e[i].slot == slot)
			return e[i].b;
	return NULL;
}

void Pool::link(const engine::Layout &lay, Resolver of, bool inactiveToo) const
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
		Button *b = of ? of(c) : at(c);
		if (!b)
			continue;
		for (int d = 0; d < engine::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			b->setNextFocus(DIRS[d], to >= 0 ? (of ? of(lay.c[to]) : at(lay.c[to])) : NULL);
		}
	}
}

void placeButton(Frame *frame, Button *b, const engine::Control &c, bool engineState)
{
	b->setBounds(c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	b->setVisible(true);
	b->setActive(engineState ? c.focusable : true);
	if (engineState)
		b->setSelected(c.selected);
	Cursor::getInstance().moveComponent(frame, b,
		c.rect.x, c.rect.x + c.rect.w, c.rect.y, c.rect.y + c.rect.h);
}

/* The page keys pressed this frame (a press is a new hold): bits PREV, NEXT, SORT. */
enum { KEY_PREV = 1, KEY_NEXT = 2, KEY_SORT = 4 };
static int pageKeys(void)
{
	enum { SOURCES = 5 };
	static int last[4][SOURCES];   /* what each controller held last frame */
	int press = 0;

	for (int i = 0; i < 4; i++)
	{
		int held[SOURCES] = { 0 };
		u16 gc = PAD_ButtonsHeld(i) | (i == 0 ? menu_script_keys : 0);
		held[0] = (gc & PAD_TRIGGER_L ? KEY_PREV : 0) | (gc & PAD_TRIGGER_R ? KEY_NEXT : 0) | (gc & PAD_TRIGGER_Z ? KEY_SORT : 0);
#ifdef HW_RVL
		{
			u32 w = Input::getInstance().getWpad()[i].btns_h;
			u32 u = WUPC_ButtonsHeld(i);
			u16 h = hidControllerConnected ? ((PADStatus*)HID_MEM2_PAD_BUFF)[i].button : 0;
			held[1] = (w & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L) ? KEY_PREV : 0) |
			          (w & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R) ? KEY_NEXT : 0) |
			          (w & (WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_ZR) ? KEY_SORT : 0);
			held[2] = (u & WPAD_CLASSIC_BUTTON_FULL_L ? KEY_PREV : 0) | (u & WPAD_CLASSIC_BUTTON_FULL_R ? KEY_NEXT : 0) |
			          (u & WPAD_CLASSIC_BUTTON_ZR ? KEY_SORT : 0);
			held[3] = (h & PAD_TRIGGER_L ? KEY_PREV : 0) | (h & PAD_TRIGGER_R ? KEY_NEXT : 0) | (h & PAD_TRIGGER_R1 ? KEY_SORT : 0);
			if (i == 0)
			{
				u16 d = WiiDRC_ButtonsHeld();
				held[4] = (d & WIIDRC_BUTTON_ZL ? KEY_PREV : 0) | (d & WIIDRC_BUTTON_ZR ? KEY_NEXT : 0);
			}
		}
#endif
		for (int s = 0; s < SOURCES; s++)
		{
			press |= held[s] & ~last[i][s];
			last[i][s] = held[s];
		}
	}
	return press;
}

static int turnOf(int keys)
{
	keys &= KEY_PREV | KEY_NEXT;
	return keys == KEY_PREV ? -1 : keys == KEY_NEXT ? 1 : 0;
}

int tabShoulder(void)
{
	return turnOf(pageKeys());
}

int tabShoulderStep(int cur, int n)
{
	return engine::tabStep(cur, n, tabShoulder(), tabWrap != 0);
}

PageInput readInput(void)
{
	const int keys = pageKeys();
	PageInput in;
	in.turn = turnOf(keys);
	in.sort = (keys & KEY_SORT) != 0;
	return in;
}

/*  ListView  */

static char emptyText[1] = "";

ListView::ListView(Frame *owner, char *title, ItemFn item, PickFn pick, void *userData, ButtonFunc back)
	: frame(owner), pool(owner, BUTTONS + MAX_TABS), st(&own), itemFn(item), pickFn(pick), tabFn(NULL),
	  prepareFn(NULL), turnFn(NULL), user(userData), backFn(back), nTabs(0)
{
	memset(&spec, 0, sizeof spec);
	own.count = 0; own.slots = SLOTS; own.top = 0; own.focus = -1;
	spec.state = st;
	spec.mode = engine::LIST_PAGED;
	spec.prev.text = "^";
	spec.next.text = "v";
	memset(&row, 0, sizeof row);
	row.kind = engine::ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.rows = &row;
	page.nRows = 1;
	page.logo = engine::LOGO_SPOT_NONE;		/* the rows and the tabs use the logo's corners */
	page.focus = engine::FOCUS_ROWS;
	page.standard = engine::DENSITY_EXPLORER;
	page.title = title;			/* centered; NULL with tabs */
	spec.item = itemFn;
	spec.ctx = user;

	layout();					/* an empty list: every button is drawn inactive, at its box */
	for (int i = 0; i < BUTTONS; i++)
	{
		ctl[i] = control(i);
		text[i] = emptyText;
		const engine::Control &c = lay.c[ctl[i]];
		button[i] = pool.button(engine::ROLE_FREE, -1, i, BTN_A_NRM, &text[i],
		                        c.rect.x, c.rect.y, c.rect.w, c.rect.h, 0, back);
		button[i]->setClickedCtx(click, this, i);
		if (i >= 2)
			button[i]->setLabelMode(Button::LABEL_SCROLLONFOCUS);
	}
	apply();
	frame->setDefaultFocus(button[2]);
	frame->setBackFunc(back);
	frame->setEnabled(true);
}

ListView::~ListView()
{
}

void ListView::setTabs(const engine::Tab *tabs, int n, TabFn fn)
{
	nTabs = n < MAX_TABS ? n : MAX_TABS;
	tabFn = fn;
	page.tabs = tabs;
	page.nTabs = nTabs;
	for (int t = 0; t < nTabs; t++)
	{
		/* BTN_A_SEL, as the Settings tabs: the current one stays lit. A tab acts on A; moving
		 * across them only moves the focus. The box comes from the layout (apply). */
		tabText[t] = (char*)tabs[t].text;
		tabB[t] = pool.button(engine::ROLE_TAB, -1, t, BTN_A_SEL, &tabText[t], 0.0f, 0.0f, 0.0f, 0.0f, 0, backFn);
		tabB[t]->setClickedCtx(click, this, BUTTONS + t);
	}
	relayout();
}

void ListView::setHooks(PrepareFn prepare, TurnFn turn)
{
	prepareFn = prepare;
	turnFn = turn;
}

void ListView::useState(engine::ListState *s)
{
	st = s;
	spec.state = s;
}

int ListView::control(int i) const
{
	if (i == 0) return lay.slot[lay.list][0];
	if (i == 1) return lay.slot[lay.list][1];
	return lay.listItem[i - 2];
}

Button *ListView::buttonOf(const engine::Control &c) const
{
	if (c.role == engine::ROLE_TAB)
		return c.slot >= 0 && c.slot < nTabs ? tabB[c.slot] : NULL;
	const int id = int(&c - lay.c);
	for (int i = 0; i < BUTTONS; i++)
		if (ctl[i] == id)
			return button[i];
	return NULL;
}

int ListView::itemRow(const Button *b) const
{
	for (int k = 0; k < SLOTS; k++)
		if (button[2 + k] == b)
			return k;
	return -1;
}

void ListView::layout(void)
{
	if (prepareFn)
		prepareFn(user, page);
	engine::layoutPage(page, METRICS, lay);
	engine::linkFocus(page, lay);
}

void ListView::link(void) const
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	/* every button, shown or not, and the tabs */
	for (int i = 0; i < BUTTONS + nTabs; i++)
	{
		Button *b;
		const engine::Control *c;
		if (i < BUTTONS)
		{
			b = button[i];
			c = &lay.c[ctl[i]];
		}
		else
		{
			const int k = lay.tab[i - BUTTONS];
			if (k < 0)
				continue;
			b = tabB[i - BUTTONS];
			c = &lay.c[k];
		}
		for (int d = 0; d < engine::DIR_COUNT; d++)
		{
			const int to = c->link[d];	/* a link of -1 is no link */
			b->setNextFocus(DIRS[d], to >= 0 ? buttonOf(lay.c[to]) : NULL);
		}
	}
}

/* The layout onto the buttons: boxes, text, label colour and room, active, and the focus links. */
void ListView::apply(void)
{
	for (int i = 0; i < BUTTONS; i++)
		ctl[i] = control(i);
	for (int i = 0; i < BUTTONS; i++)
	{
		const engine::Control &c = lay.c[ctl[i]];
		Button *b = button[i];
		placeButton(frame, b, c, true);			/* box, active (a shown item or a live arrow) */
		if (i < 2)
			continue;							/* the arrows have no text */
		if (!c.focusable)
			b->setVisible(false);				/* a row past the end of the list is not drawn at all */
		text[i] = (char*)c.text;
		b->clearLabelFocusColor();
		if (c.dim)
		{
			b->setLabelColor((GXColor) {160,160,160,255});		/* Recent: its device is not there */
			b->setLabelFocusColor((GXColor) {235,210,210,255});	/* on the rose focus bar 160 grey did not read */
		}
		else if (c.folder)
			b->setLabelColor((GXColor) {254,213,95,255});
		else
			b->setLabelColor((GXColor) {255,255,255,255});
		/* the name's room: from textX to textR, so a scrolling name stays left of the tag */
		b->setLabelScissor(c.full ? (int)(c.textX - c.rect.x) : 0);
		b->setLabelInsetRight(c.full ? (int)(c.rect.x + c.rect.w - c.textR) : -1);
	}
	for (int t = 0; t < nTabs; t++)
	{
		const int k = lay.tab[t];
		if (k >= 0)
			placeButton(frame, tabB[t], lay.c[k], true);
		else
		{	/* no tab for this one (the layout always has them: kept for safety) */
			tabB[t]->setVisible(false);
			tabB[t]->setActive(false);
			Cursor::getInstance().moveComponent(frame, tabB[t], 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
	link();
}

void ListView::relayout(void)
{
	layout();
	apply();
}

void ListView::refresh(void)
{
	relayout();
	Button *def = lay.defaultFocus >= 0 ? buttonOf(lay.c[lay.defaultFocus]) : NULL;
	if (!def && nTabs > 0 && page.curTab >= 0 && page.curTab < nTabs && lay.tab[page.curTab] >= 0)
		def = tabB[page.curTab];		/* an empty list: the lit tab */
	if (def)
		frame->setDefaultFocus(def);
}

void ListView::setCount(int n)
{
	st->count = n;
	st->top = 0;
	st->focus = st->top;
	refresh();
}

void ListView::turn(int dir, bool wrap)
{
	if (turnFn)
		turnFn(user, dir, wrap);
	else
	{
		engine::listTurn(*st, dir, wrap);
		st->focus = st->top;
		relayout();
	}
}

void ListView::click(void *self, int tag)
{
	ListView *v = (ListView*)self;
	if (tag >= BUTTONS)
	{
		if (v->tabFn)
			v->tabFn(v->user, tag - BUTTONS);
	}
	else if (tag >= 2)
		v->pickFn(v->user, v->st->top + tag - 2);
	else
		v->turn(tag ? 1 : -1, false);
}

PageInput ListView::update(void)
{
	const PageInput in = readInput();
	if (in.turn && !in.sort)
	{
		turn(in.turn, true);
		Focus::getInstance().clearPrimaryFocus();
	}
	/* the focused row scrolls its whole name, the others show the cut one */
	for (int i = 2; i < BUTTONS; i++)
	{
		const engine::Control &c = lay.c[ctl[i]];
		text[i] = (char*)(c.full && button[i]->getFocus() ? c.full : c.text);
	}
	return in;
}

void ListView::drawOverlay(Graphics &gfx) const
{
	drawExplorerOverlay(gfx, lay, *st);
}

void appendBounded(char *dst, size_t dstSize, const char *src)
{
	size_t used = strlen(dst);
	if (used < dstSize - 1)
		strncat(dst, src, dstSize - used - 1);
}

} /* namespace glue */
} /* namespace menu */
