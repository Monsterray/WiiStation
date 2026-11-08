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

int tabShoulder(void)
{
	enum { PREV = 1, NEXT = 2, SOURCES = 5 };
	static int last[4][SOURCES];   /* what each controller held last frame: a press is a new hold */
	int press = 0;

	for (int i = 0; i < 4; i++)
	{
		int held[SOURCES] = { 0 };
		u16 gc = PAD_ButtonsHeld(i) | (i == 0 ? menu_script_keys : 0);
		held[0] = (gc & PAD_TRIGGER_L ? PREV : 0) | (gc & PAD_TRIGGER_R ? NEXT : 0);
#ifdef HW_RVL
		{
			u32 w = Input::getInstance().getWpad()[i].btns_h;
			u32 u = WUPC_ButtonsHeld(i);
			u16 h = hidControllerConnected ? ((PADStatus*)HID_MEM2_PAD_BUFF)[i].button : 0;
			held[1] = (w & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L) ? PREV : 0) |
			          (w & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R) ? NEXT : 0);
			held[2] = (u & WPAD_CLASSIC_BUTTON_FULL_L ? PREV : 0) | (u & WPAD_CLASSIC_BUTTON_FULL_R ? NEXT : 0);
			held[3] = (h & PAD_TRIGGER_L ? PREV : 0) | (h & PAD_TRIGGER_R ? NEXT : 0);
			if (i == 0)
			{
				u16 d = WiiDRC_ButtonsHeld();
				held[4] = (d & WIIDRC_BUTTON_ZL ? PREV : 0) | (d & WIIDRC_BUTTON_ZR ? NEXT : 0);
			}
		}
#endif
		for (int s = 0; s < SOURCES; s++)
		{
			press |= held[s] & ~last[i][s];
			last[i][s] = held[s];
		}
	}
	return press == PREV ? -1 : press == NEXT ? 1 : 0;
}

int tabShoulderStep(int cur, int n)
{
	return engine::tabStep(cur, n, tabShoulder(), tabWrap != 0);
}

PageInput readInput(void)
{
	PageInput in;
	in.turn = tabShoulder();
	return in;
}

/*  ListView  */

static char emptyText[1] = "";

ListView::ListView(Frame *owner, char *title, ItemFn item, PickFn pick, void *userData, ButtonFunc back)
	: frame(owner), pool(owner, BUTTONS), itemFn(item), pickFn(pick), user(userData)
{
	memset(&spec, 0, sizeof spec);
	state.count = 0; state.slots = SLOTS; state.top = 0; state.focus = -1;
	spec.state = &state;
	spec.mode = engine::LIST_PAGED;
	spec.prev.text = "^";
	spec.next.text = "v";
	memset(&row, 0, sizeof row);
	row.kind = engine::ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.rows = &row;
	page.nRows = 1;
	page.logo = engine::LOGO_SPOT_NONE;
	page.focus = engine::FOCUS_ROWS;
	page.standard = engine::DENSITY_EXPLORER;
	page.title = title;			/* centered, no tabs */
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

int ListView::control(int i) const
{
	if (i == 0) return lay.slot[lay.list][0];
	if (i == 1) return lay.slot[lay.list][1];
	return lay.listItem[i - 2];
}

Button *ListView::buttonOf(const engine::Control &c) const
{
	const int id = int(&c - lay.c);
	for (int i = 0; i < BUTTONS; i++)
		if (ctl[i] == id)
			return button[i];
	return NULL;
}

void ListView::layout(void)
{
	engine::layoutPage(page, METRICS, lay);
	engine::linkFocus(page, lay);
}

void ListView::link(void) const
{
	static const int DIRS[engine::DIR_COUNT] = {
		Focus::DIRECTION_UP, Focus::DIRECTION_DOWN,
		Focus::DIRECTION_LEFT, Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < BUTTONS; i++)
	{
		const engine::Control &c = lay.c[ctl[i]];
		for (int d = 0; d < engine::DIR_COUNT; d++)
		{
			const int to = c.link[d];	/* a link of -1 is no link */
			button[i]->setNextFocus(DIRS[d], to >= 0 ? buttonOf(lay.c[to]) : NULL);
		}
	}
}

/* The layout onto the buttons: boxes, text, active and the focus links. */
void ListView::apply(void)
{
	for (int i = 0; i < BUTTONS; i++)
		ctl[i] = control(i);
	for (int i = 0; i < BUTTONS; i++)
	{
		const engine::Control &c = lay.c[ctl[i]];
		Button *b = button[i];
		placeButton(frame, b, c, true);
		if (i < 2)
			continue;							/* the arrows have no text */
		if (!c.focusable)
			b->setVisible(false);				/* a row past the end of the list is not drawn at all */
		text[i] = (char*)c.text;
		b->setLabelColor((GXColor) {255,255,255,255});
		b->setLabelScissor(c.full ? (int)(c.textX - c.rect.x) : 0);
		b->setLabelInsetRight(c.full ? (int)(c.rect.x + c.rect.w - c.textR) : -1);
	}
	link();
}

void ListView::setCount(int n)
{
	state.count = n;
	state.top = 0;
	state.focus = state.top;
	layout();
	apply();
	if (lay.defaultFocus >= 0)
		if (Button *def = buttonOf(lay.c[lay.defaultFocus]))
			frame->setDefaultFocus(def);
}

void ListView::click(void *self, int tag)
{
	ListView *v = (ListView*)self;
	if (tag >= 2)
		v->pickFn(v->user, v->state.top + tag - 2);
	else
	{
		engine::listTurn(v->state, tag ? 1 : -1, false);
		v->state.focus = v->state.top;
		v->layout();
		v->apply();
	}
}

void ListView::update(void)
{
	const PageInput in = readInput();
	if (in.turn)
	{
		engine::listTurn(state, in.turn, true);
		state.focus = state.top;
		layout();
		apply();
		Focus::getInstance().clearPrimaryFocus();
	}
	/* the focused row scrolls its whole name, the others show the cut one */
	for (int i = 2; i < BUTTONS; i++)
	{
		const engine::Control &c = lay.c[ctl[i]];
		text[i] = (char*)(c.full && button[i]->getFocus() ? c.full : c.text);
	}
}

void ListView::drawOverlay(Graphics &gfx) const
{
	drawExplorerOverlay(gfx, lay, state);
}

Button *ListView::firstItem(void) const
{
	return button[2];
}

void appendBounded(char *dst, size_t dstSize, const char *src)
{
	size_t used = strlen(dst);
	if (used < dstSize - 1)
		strncat(dst, src, dstSize - used - 1);
}

} /* namespace glue */
} /* namespace menu */
