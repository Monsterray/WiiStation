/**
 * WiiStation - AchievementsFrame.cpp
 *
 * Current ISO > Achievements: the loaded game's set (achievements.h), ten to a page. A header
 * ("3 of 12 earned") with the game's rich presence under it, then one row per achievement:
 * Earned or its measured progress ("3/10"), the title, the points and how rare it is.
 * The Leaderboards button turns the page into the set's leaderboards, one to a page: the ten
 * best scores and the player's own, fetched when shown (Official mode; a local set has none).
 * Prev and Next turn the page, where the file browser has them; B goes back to Current ISO.
 * The rows are text, read from the engine each frame.
 *
 * Local mode with Miis seated on connected pads (Players menu): the page shows one of them at
 * a time, its coin and name in the header, with L and R (GameCube) or + and - (Wii Remote,
 * Classic) to switch; the earned marks and the count are that Mii's.
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
#include "MenuContext.h"
#include "AchievementsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/Image.h"
#include "../libgui/InputManager.h"
#include "../players.h"
#include "../../achievements.h"

extern MenuContext *pMenuContext;
namespace menu { extern u16 menu_script_keys; }	/* InputManager.cpp: an input script's "menukeys" */

#define PER_PAGE	10
#define ROW_Y0		82		/* the first row's top; rows are ROW_DY apart */
#define ROW_DY		34
#define MARK_X		120		/* "Earned" or "3/10", in the file browser's column (Prev is left of it) */
#define TITLE_X		210
#define TITLE_W		232		/* to x = 442 */
#define RIGHT_X		528		/* points and rarity end here: clear of Next */
#define ROW_SCALE	0.8f
#define SMALL_SCALE	0.6f
#define BOARD_DY	28
#define TOP_SCORES	10

static int top;		/* the first achievement shown */
static int board;	/* the leaderboard shown */
static bool boards;	/* the page shows the leaderboards */
static char prevText[] = "Prev", nextText[] = "Next";
static char boardsText[] = "Leaderboards", listText[] = "Achievements";
static char *prevString = prevText, *nextString = nextText, *modeString = boardsText;
static menu::Button *prevButton, *nextButton, *modeButton;
static menu::Image *coinImg;	/* the shown player's coin (players.c keeps the pixels) */
static unsigned coinSerial;
static int coinMii = -1;

static void Func_AchPrev()
{
	if (boards) { if (board > 0) board--; }
	else if (top >= PER_PAGE) top -= PER_PAGE;
}

static void Func_AchNext()
{
	if (boards) { if (board + 1 < (int)achievements_board_count()) board++; }
	else if (top + PER_PAGE < (int)achievements_count()) top += PER_PAGE;
}

static void Func_AchMode()
{
	boards = !boards;
	modeString = boards ? listText : boardsText;
}

static void Func_ReturnFromAchievementsFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_CURRENTROM);
}

void Func_AchievementsList()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_ACHIEVEMENTS, 0);
}

static menu::Button *button(menu::Frame *frame, char **text, void (*fn)(), float x, float y, float w)
{
	menu::Button *b = new menu::Button(BTN_A_NRM, text, x, y, w, 40.0f);
	b->setActive(true);
	b->setClicked(fn);
	b->setReturn(Func_ReturnFromAchievementsFrame);
	frame->add(b);
	menu::Cursor::getInstance().addComponent(frame, b, x, x + w, y, y + 40.0f);
	return b;
}

AchievementsFrame::AchievementsFrame()
{
	/* Prev and Next at the file browser's place for them (FileBrowserFrame.cpp FB_SPEC) */
	prevButton = button(this, &prevString, Func_AchPrev, 35.0f, 220.0f, 70.0f);
	nextButton = button(this, &nextString, Func_AchNext, 535.0f, 220.0f, 70.0f);
	modeButton = button(this, &modeString, Func_AchMode, 230.0f, 420.0f, 180.0f);
	/* Left and Right go to the other one, either way round; Down from either is the mode */
	prevButton->setNextFocus(menu::Focus::DIRECTION_LEFT, nextButton);
	prevButton->setNextFocus(menu::Focus::DIRECTION_RIGHT, nextButton);
	nextButton->setNextFocus(menu::Focus::DIRECTION_LEFT, prevButton);
	nextButton->setNextFocus(menu::Focus::DIRECTION_RIGHT, prevButton);
	prevButton->setNextFocus(menu::Focus::DIRECTION_DOWN, modeButton);
	nextButton->setNextFocus(menu::Focus::DIRECTION_DOWN, modeButton);
	modeButton->setNextFocus(menu::Focus::DIRECTION_UP, nextButton);
	modeButton->setNextFocus(menu::Focus::DIRECTION_LEFT, prevButton);
	modeButton->setNextFocus(menu::Focus::DIRECTION_RIGHT, nextButton);
	setDefaultFocus(nextButton);
	setBackFunc(Func_ReturnFromAchievementsFrame);
	setEnabled(true);
}

AchievementsFrame::~AchievementsFrame()
{
	menu::Cursor::getInstance().removeComponent(this, prevButton);
	menu::Cursor::getInstance().removeComponent(this, nextButton);
	menu::Cursor::getInstance().removeComponent(this, modeButton);
	delete prevButton;
	delete nextButton;
	delete modeButton;
	delete coinImg;
	coinImg = NULL;
	coinMii = -1;
}

void AchievementsFrame::activateSubmenu(int submenu)
{
	top = 0;
	board = 0;
	boards = false;
	modeString = boardsText;
	achievements_view_player(0);
	if (achievements_player_key(0)[0])
		players_start();	/* the Miis' coins (players.c; nothing happens when it is running already) */
	achievements_list_refresh();	/* Official mode: what was earned since it was last open */
}

/* s cut to `width` pixels at `scale` */
static void cut(char *s, float scale, int width)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	for (size_t n = strlen(s); n && font.getStringWidth(s, scale) > width; )
		s[--n] = '\0';
}

static void drawRight(int right, int y, char *s, float scale)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	font.drawString(right - font.getStringWidth(s, scale), y, s, scale, false);
}

/* L and R (GameCube), + and - (Wii Remote, Classic): the next or the previous player */
static void switchPlayer(void)
{
	unsigned n = achievements_player_count(), v = achievements_viewed_player();
	int step = 0;
#ifdef HW_RVL
	WPADData *w = menu::Input::getInstance().getWpad();
	for (int i = 0; i < 4; i++)
	{
		if (w[i].btns_d & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R)) step = 1;
		if (w[i].btns_d & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L)) step = -1;
	}
#endif
	static u16 before[4];	/* an input script's menukeys count as pad 1's (FocusManager.cpp) */
	for (int i = 0; i < 4; i++)
	{
		u16 held = PAD_ButtonsHeld(i) | (i == 0 ? menu::menu_script_keys : 0);
		u16 gc = held & ~before[i];
		before[i] = held;
		if (gc & PAD_TRIGGER_R) step = 1;
		if (gc & PAD_TRIGGER_L) step = -1;
	}
	if (n > 1 && step)
		achievements_view_player((v + n + step) % n);
}

/* The shown player's coin, size px square at x, y; nothing if the Mii database is not loaded */
static void drawCoin(menu::Graphics &gfx, const char *key, int x, int y, int size)
{
	int i = key[0] ? players_find(key) : -1;
	const player_mii *m = players_get(i);
	if (!m || !m->coin)
		return;
	if (!coinImg || coinMii != i || coinSerial != m->coin_serial)
	{
		delete coinImg;
		coinImg = new menu::Image(m->coin, PLAYERS_COIN, PLAYERS_COIN, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		coinMii = i;
		coinSerial = m->coin_serial;
		GX_InvalidateTexAll();
	}
	gfx.setColor((GXColor){255, 255, 255, 255});
	coinImg->activateImage(GX_TEXMAP0);
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, x, y, size, size, 0, 1, 0, 1);
}

static void drawList(menu::Graphics &gfx)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	unsigned count = achievements_count();
	unsigned players = achievements_player_count(), who = achievements_viewed_player();
	const char *name = achievements_player_name(who);
	char line[128];

	if (!count)
	{
		snprintf(line, sizeof line, "%s", achievements_status());   /* why: off, a .exe, no set, Official ... */
		font.drawString(320, 150, line, 1.0f, true);	/* below the logo, above Prev and Next */
		return;
	}
	if (name[0])	/* a Mii: its name, count and coin */
		snprintf(line, sizeof line, "%s: %u of %u earned", name, achievements_player_earned(who), count);
	else
		snprintf(line, sizeof line, "%u of %u earned", achievements_player_earned(who), count);
	cut(line, 0.9f, 400);
	if (name[0])
	{
		int w = font.getStringWidth(line, 0.9f);
		drawCoin(gfx, achievements_player_key(who), 320 - w / 2 - 32, 18, 26);
		GXColor ink = {56, 56, 56, 255};
		font.drawInit(ink);	/* the coin left the GX state as a picture's */
	}
	font.drawString(320, 30, line, 0.9f, true);
	snprintf(line, sizeof line, "%s", achievements_presence());
	if (players > 1 && !line[0])
		snprintf(line, sizeof line, "L / R: the other players (%u of %u)", who + 1, players);
	cut(line, SMALL_SCALE, 440);
	if (line[0])
		font.drawString(320, 56, line, SMALL_SCALE, true);

	for (int i = 0; i < PER_PAGE; i++)
	{
		achievement_entry_t e;
		if (achievements_entry(top + i, &e) < 0)
			break;
		int y = ROW_Y0 + i * ROW_DY;
		/* the state: Earned, the measured progress (smaller: "1699/2500"), or Unofficial */
		snprintf(line, sizeof line, "%s", e.earned ? "Earned" : e.progress[0] ? e.progress : e.unofficial ? "Unoff." : "");
		float scale = e.earned || !e.progress[0] ? ROW_SCALE : SMALL_SCALE;
		cut(line, scale, TITLE_X - MARK_X - 8);
		if (line[0])
			font.drawString(MARK_X, scale == ROW_SCALE ? y : y + 4, line, scale, false);
		snprintf(line, sizeof line, "%s", e.title);
		cut(line, ROW_SCALE, TITLE_W);
		font.drawString(TITLE_X, y, line, ROW_SCALE, false);
		/* the points, and the share of players who have it */
		if (e.rarity_x100 >= 0)
			snprintf(line, sizeof line, "%u  %d.%d%%", e.points, e.rarity_x100 / 100, e.rarity_x100 % 100 / 10);
		else
			snprintf(line, sizeof line, e.points ? "%u" : "", e.points);
		if (line[0])
			drawRight(RIGHT_X, y + 4, line, SMALL_SCALE);
	}
}

static void drawBoards(void)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	unsigned count = achievements_board_count();
	achievement_score_t best[TOP_SCORES], mine;
	unsigned n = TOP_SCORES;
	const char *title, *desc;
	char line[128];
	int y, r;

	if (!count || achievements_board(board, &title, &desc) < 0)
	{
		font.drawString(320, 150, (char *)"No leaderboards for this game", 1.0f, true);
		return;
	}
	snprintf(line, sizeof line, "%s", title);
	cut(line, 0.9f, 440);
	font.drawString(320, 30, line, 0.9f, true);
	snprintf(line, sizeof line, "%s", desc);
	cut(line, SMALL_SCALE, 440);
	font.drawString(320, 56, line, SMALL_SCALE, true);

	r = achievements_board_scores(board, best, &n, &mine);
	if (r <= 0)
	{
		font.drawString(320, 150, (char *)(r ? "No answer from RetroAchievements" : "Asking RetroAchievements..."), 0.8f, true);
		return;
	}
	if (!n)
		font.drawString(320, 150, (char *)"No scores yet", 0.8f, true);
	for (unsigned i = 0; i < n; i++)
	{
		y = ROW_Y0 + i * BOARD_DY;
		snprintf(line, sizeof line, "%u", best[i].rank);
		font.drawString(MARK_X, y, line, ROW_SCALE, false);
		snprintf(line, sizeof line, "%s", best[i].user);
		cut(line, ROW_SCALE, 220);
		font.drawString(MARK_X + 60, y, line, ROW_SCALE, false);
		snprintf(line, sizeof line, "%s", best[i].score);
		drawRight(RIGHT_X, y, line, ROW_SCALE);
	}
	y = ROW_Y0 + TOP_SCORES * BOARD_DY + 6;
	if (mine.rank)
		snprintf(line, sizeof line, "You: %u   %s", mine.rank, mine.score);
	else
		snprintf(line, sizeof line, "You: no score on this board");
	font.drawString(320, y, line, ROW_SCALE, true);
	snprintf(line, sizeof line, "%d of %u", board + 1, count);
	drawRight(RIGHT_X, 56, line, SMALL_SCALE);
}

void AchievementsFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;

	menu::ComponentList::const_iterator it;
	for (it = componentList.begin(); it != componentList.end(); ++it)
		(*it)->draw(gfx);

	GXColor text = {56, 56, 56, 255};	/* TextBox's: a lighter one was lost on the darker lower half */
	menu::IplFont::getInstance().drawInit(text);
	if (boards)
		drawBoards();
	else
	{
		switchPlayer();
		drawList(gfx);
	}
}
