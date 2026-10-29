/**
 * WiiStation - AchievementsFrame.cpp
 *
 * Current ISO > Achievements: the book. The loaded game's set (achievements.h), in three tabs,
 * In progress, Locked and Earned, and a fourth for the leaderboards when the set has some
 * (Official mode). A header shows the player's coin and name, the game and the total progress
 * ("12 / 89 earned", "95 / 610 points", a bar). A row is a badge, the title, the description,
 * the points and, for a measured achievement, a bar with its count ("12 / 50").
 *
 * A opens a row's detail: a large badge, the title, the description, the progress, the points,
 * the rarity (Official), the type (progression, win condition, missable), when it was earned
 * and the picture taken at the unlock (ach_shot.c), large, when there is one. B goes back to
 * the list; Left and Right in the detail go to the previous and the next achievement.
 *
 * Controls: Up and Down choose, Left and Right turn the tabs, A opens, B goes back (and leaves
 * the book from the list). The menu combination, the one that stops a game for the menu, goes
 * straight back into the game when one has been started (MenuContext.cpp). L and R (GameCube), + and - (Wii Remote, Classic) switch the player
 * when Miis are seated on connected pads (Players menu): the list, the counts and the detail are
 * that Mii's. The Wii Remote's pointer clicks tabs and rows.
 *
 * When the player opened the menu while a pop-up was on screen (an unlock card, the progress
 * panel, a challenge icon), the menu opens the book on that achievement's detail
 * (Func_AchievementsOpenOn, ach_toast_take_jump).
 *
 * The rows are drawn from the engine each frame; nothing is cached but the lists of indexes
 * (rebuilt when the player changes) and one picture's texture.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "MenuContext.h"
#include "AchievementsFrame.h"
#include "../libgui/Component.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/Image.h"
#include "../libgui/InputManager.h"
#include "../gc_input/controller.h"
#include "../players.h"
#include "../ach_shot.h"
#include "../ach_picture.h"
#include "../../achievements.h"
#ifdef HW_RVL
#include "../ach_badge.h"
#endif

extern MenuContext *pMenuContext;
extern GXTexRegion texCacheRegionS[8];
namespace menu { extern u16 menu_script_keys; }	/* InputManager.cpp: an input script's "menukeys" */

namespace {

enum { TAB_PROGRESS, TAB_LOCKED, TAB_EARNED, TAB_BOARDS, TABS };
const char *const TAB_NAMES[TABS] = { "In progress", "Locked", "Earned", "Leaderboards" };

const int MAXN = 512;			/* the most achievements one list holds (Official: MAX_LISTED) */
const int ROW_Y0 = 126, ROW_H = 62, ROWS_VIS = 5;
const int LIST_X = 20, LIST_W = 584;
const int BADGE_X = 28, BADGE_S = 48, TEXT_X = 88, RIGHT_X = 596, TEXT_W = 408;	/* title and description end at 496 */
const int TAB_Y = 94, TAB_H = 26, TAB_W = 150, TAB_DX = 152;
const int TOP_SCORES = 10;
const int SHOT_X = 316, SHOT_Y = 134, SHOT_W = 296, SHOT_H = 222;	/* the detail's picture, 4:3 */

const GXColor DARK = {62, 62, 62, 255}, MID = {90, 90, 90, 255}, WHITE = {255, 255, 255, 255};
const GXColor RED = {184, 49, 47, 255}, GREEN = {44, 120, 58, 255}, BLUE = {60, 110, 190, 255}, FOOT = {52, 52, 52, 255};

struct Rect { int x, y, w, h; bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

struct Item { unsigned short i; unsigned when; unsigned char percent; };

typedef char Line[100];

void fill(menu::Graphics &gfx, int x, int y, int w, int h, GXColor c)
{
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(c);
	gfx.enableBlending(true);
	gfx.fillRect(x, y, w, h);
}

void frame(menu::Graphics &gfx, int x, int y, int w, int h, int t, GXColor c)
{
	fill(gfx, x, y, w, t, c);
	fill(gfx, x, y + h - t, w, t, c);
	fill(gfx, x, y + t, t, h - 2 * t, c);
	fill(gfx, x + w - t, y + t, t, h - 2 * t, c);
}

/* Text from its top left at scale. */
void text(int x, int y, const char *s, float scale, GXColor c)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, false);
}

int width(const char *s, float scale) { return menu::IplFont::getInstance().getStringWidth((char *)s, scale); }

/* Text that ends at x (right aligned) */
void textRight(int x, int y, const char *s, float scale, GXColor c)
{
	text(x - width(s, scale), y, s, scale, c);
}

/* Text centered on x */
void textCenter(int x, int y, const char *s, float scale, GXColor c)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, true);
}

/* s cut so that it ends in "..." within maxw pixels at scale; unless always, a string that fits is left */
void clip(char *s, float scale, int maxw, bool always = false)
{
	if (!always && width(s, scale) <= maxw)
		return;
	size_t n = strlen(s);
	while (n > 0 && width(s, scale) + width("...", scale) > maxw)
		s[--n] = '\0';
	strcat(s, "...");
}

/* The scale at which s fits maxw: scale itself, shrunk but never below minScale (a name that
 * still does not fit is cut with clip()) */
float fitScale(const char *s, float scale, float minScale, int maxw)
{
	int w = width(s, scale);
	if (w <= maxw)
		return scale;
	float f = scale * maxw / w;
	return f < minScale ? minScale : f;
}

/* Text that shrinks to fit maxw (down to minScale), then is cut */
void textFit(int x, int y, const char *s, float scale, float minScale, int maxw, GXColor c)
{
	char buf[160];
	snprintf(buf, sizeof buf, "%s", s);
	scale = fitScale(buf, scale, minScale, maxw);
	clip(buf, scale, maxw);
	text(x, y, buf, scale, c);
}

/* s in lines of at most maxw pixels at scale, split at spaces; when the text does not fit
 * maxLines the last line ends in "...". Returns the line count. */
int wrap(const char *s, float scale, int maxw, Line *lines, int maxLines)
{
	int n = 0;
	const char *p = s;
	while (*p == ' ')
		p++;
	while (*p && n < maxLines)
	{
		char cur[96] = "";
		while (*p)
		{
			const char *e = p;
			while (*e && *e != ' ')
				e++;
			char test[200];
			snprintf(test, sizeof test, "%s%s%.*s", cur, cur[0] ? " " : "", (int)(e - p), p);
			if (cur[0] && width(test, scale) > maxw)
				break;
			snprintf(cur, sizeof cur, "%s", test);
			for (p = e; *p == ' '; )
				p++;
		}
		snprintf(lines[n++], sizeof(Line), "%s", cur);
	}
	if (*p && n > 0)	/* more text than lines: the last one is cut */
	{
		char last[sizeof(Line) + 4];
		snprintf(last, sizeof last, "%s", lines[n - 1]);
		clip(last, scale, maxw, true);
		snprintf(lines[n - 1], sizeof(Line), "%s", last);
	}
	for (int i = 0; i < n; i++)
		clip(lines[i], scale, maxw);
	return n;
}

/* "12/50" as "12 / 50" */
void spaced(char *out, size_t size, const char *progress)
{
	size_t n = 0;
	for (const char *p = progress; *p && n + 3 < size; p++)
	{
		if (*p == '/' && p > progress && p[1] != ' ' && p[-1] != ' ')
		{
			out[n++] = ' ';
			out[n++] = '/';
			out[n++] = ' ';
		}
		else
			out[n++] = *p;
	}
	out[n] = '\0';
}

void dateText(unsigned t, char *out, size_t size)
{
	time_t tt = (time_t)t;
	struct tm tmv;
	if (!t || !gmtime_r(&tt, &tmv))
	{
		out[0] = '\0';
		return;
	}
	snprintf(out, size, "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

void Func_ReturnFromAchievementsFrame();

/* ---- the state ---- */

Item lists[3][MAXN];
int counts[3];
int tab, sel[TABS], topRow[TABS];
bool detail;
int board;				/* the leaderboard shown */
unsigned total_earned, total_points, earned_points, jumpId;
bool pointer;
int px, py;
menu::Image *coinImg;		/* the shown player's coin (players.c keeps the pixels) */
unsigned coinSerial;
int coinMii = -1;
GXTexObj shotTex;
bool shotOk;
unsigned shotFor;		/* the achievement whose picture was looked for, 0 none */
char shotDate[16];

int cmpProgress(const void *a, const void *b)
{
	const Item *x = (const Item *)a, *y = (const Item *)b;
	return x->percent != y->percent ? (int)y->percent - (int)x->percent : (int)x->i - (int)y->i;
}

int cmpEarned(const void *a, const void *b)
{
	const Item *x = (const Item *)a, *y = (const Item *)b;
	return x->when != y->when ? (x->when < y->when ? 1 : -1) : (int)x->i - (int)y->i;
}

/* The lists of the viewed player: what it has, what is on its way and what is left */
void build()
{
	unsigned count = achievements_count();
	achievement_entry_t e;
	counts[0] = counts[1] = counts[2] = 0;
	total_earned = total_points = earned_points = 0;
	for (unsigned i = 0; i < count && i < (unsigned)MAXN; i++)
	{
		if (achievements_entry(i, &e) < 0)
			continue;
		int t = e.earned ? TAB_EARNED : e.progress[0] && e.percent > 0 ? TAB_PROGRESS : TAB_LOCKED;
		Item &it = lists[t][counts[t]++];
		it.i = (unsigned short)i;
		it.when = e.when;
		it.percent = e.percent;
		total_points += e.points;
		if (e.earned)
		{
			total_earned++;
			earned_points += e.points;
		}
	}
	qsort(lists[TAB_PROGRESS], counts[TAB_PROGRESS], sizeof(Item), cmpProgress);
	qsort(lists[TAB_EARNED], counts[TAB_EARNED], sizeof(Item), cmpEarned);
}

void clampSel()
{
	if (tab == TAB_BOARDS)
		return;
	int n = counts[tab];
	if (sel[tab] >= n) sel[tab] = n - 1;
	if (sel[tab] < 0) sel[tab] = 0;
	if (sel[tab] < topRow[tab]) topRow[tab] = sel[tab];
	if (sel[tab] >= topRow[tab] + ROWS_VIS) topRow[tab] = sel[tab] - ROWS_VIS + 1;
	if (topRow[tab] > n - ROWS_VIS) topRow[tab] = n - ROWS_VIS;
	if (topRow[tab] < 0) topRow[tab] = 0;
}

/* The first tab that has anything, In progress first */
int firstTab()
{
	for (int t = 0; t < 3; t++)
		if (counts[t])
			return t;
	return TAB_PROGRESS;
}

void freeShot()
{
	if (shotFor)
		ach_picture_free();
	shotFor = 0;
	shotOk = false;
}

/* The detail's picture: the file the unlock made, when there is one */
void loadShot(unsigned id)
{
	char path[128];
	if (shotFor == id)
		return;
	freeShot();
	shotFor = id;
	shotDate[0] = '\0';
	if (!ach_shot_path(achievements_set_id(), id, path, sizeof path))
		return;
	struct stat st;
	if (stat(path, &st) == 0)
		dateText((unsigned)st.st_mtime, shotDate, sizeof shotDate);
	shotOk = ach_picture_load(path, &shotTex) != 0;
}

void openDetail()
{
	if (tab == TAB_BOARDS || !counts[tab])
		return;
	detail = true;
}

void leaveDetail()
{
	detail = false;
	freeShot();
}

/* The detail of the achievement with this id: its tab, its place in the list */
bool jumpTo(unsigned id)
{
	achievement_entry_t e;
	for (int t = 0; t < 3; t++)
		for (int k = 0; k < counts[t]; k++)
			if (achievements_entry(lists[t][k].i, &e) >= 0 && e.id == id)
			{
				tab = t;
				sel[t] = k;
				topRow[t] = k;
				clampSel();
				openDetail();
				return true;
			}
	return false;
}

/* ---- the picture of the page ---- */

/* The shown player's coin, size px square at x, y; nothing if the Mii database is not loaded */
void drawCoin(menu::Graphics &gfx, const char *key, int x, int y, int size)
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
	gfx.setColor(WHITE);
	coinImg->activateImage(GX_TEXMAP0);
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, x, y, size, size, 0, 1, 0, 1);
}

/* A texture over (x, y, w, h), tinted */
void drawTex(menu::Graphics &gfx, GXTexObj *obj, int x, int y, int w, int h, GXColor tint)
{
	GX_InvalidateTexRegion(&texCacheRegionS[0]);	/* the font's region: other texels */
	GX_LoadTexObjPreloaded(obj, &texCacheRegionS[0], GX_TEXMAP0);	/* outside Begin/End */
	gfx.setColor(tint);
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, x, y, w, h, 0, 1, 0, 1);
}

/* An achievement's badge, size px square: its picture when it is in memory, else a plain tile
 * with the title's first letter (a Local set has no badges). earned: in colour; else dimmed. */
void drawBadge(menu::Graphics &gfx, const achievement_entry_t &e, int x, int y, int size, bool earned)
{
	GXColor edge = {120, 120, 120, 255};
#ifdef HW_RVL
	GXTexObj obj;
	if (e.badge && e.badge[0] && ach_badge_get(e.badge, 0, &obj) == 1)
	{
		GX_InitTexObjFilterMode(&obj, GX_LINEAR, GX_LINEAR);	/* a copy: shown at another size than 64 px */
		fill(gfx, x - 2, y - 2, size + 4, size + 4, edge);
		drawTex(gfx, &obj, x, y, size, size, earned ? WHITE : (GXColor){150, 150, 150, 255});
		return;
	}
#endif
	fill(gfx, x - 2, y - 2, size + 4, size + 4, edge);
	fill(gfx, x, y, size, size, earned ? (GXColor){92, 112, 150, 255} : (GXColor){140, 140, 140, 255});
	char letter[2] = { e.title && e.title[0] ? e.title[0] : '?', 0 };
	float s = size / 44.0f;
	text(x + (size - width(letter, 1.1f * s)) / 2, y + size / 2 - (int)(15 * s), letter, 1.1f * s, WHITE);
}

void drawBar(menu::Graphics &gfx, int x, int y, int w, int h, int percent, GXColor c)
{
	fill(gfx, x, y, w, h, (GXColor){150, 150, 150, 255});
	fill(gfx, x + 1, y + 1, w - 2, h - 2, (GXColor){206, 206, 206, 255});
	if (percent > 0)
	{
		int n = (w - 2) * (percent > 100 ? 100 : percent) / 100;
		fill(gfx, x + 1, y + 1, n < 1 ? 1 : n, h - 2, c);
	}
}

/* The hint line: white on a dark band (the menu's lower half is dark, and dark text was lost on it) */
void footer(menu::Graphics &gfx, const char *hint)
{
	fill(gfx, 0, 442, 640, 38, FOOT);
	text(20, 449, hint, 0.5f, WHITE);
}

const char *typeName(unsigned char type)
{
	return type == ACH_TYPE_PROGRESSION ? "Progression" : type == ACH_TYPE_WIN ? "Win condition" :
	       type == ACH_TYPE_MISSABLE ? "Missable" : "";
}

/* L and R (GameCube), + and - (Wii Remote, Classic): the next or the previous player */
bool switchPlayer(void)
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
	/* the Wii Remote's menu combination is + and - together: it is not a switch (MenuContext.cpp) */
	if (step && (pad_menu_combo_held() || (menu::menu_script_keys & MENU_SCRIPT_COMBO)))
		step = 0;
	if (n > 1 && step)
	{
		achievements_view_player((v + n + step) % n);
		return true;
	}
	return false;
}

} // namespace

class AchievementsBoard : public menu::Component
{
public:
	AchievementsBoard() { setType(TYPE_OTHER); }
	void drawComponent(menu::Graphics &gfx);
	menu::Component *updateFocus(int direction, int buttonsPressed);
	void enter(unsigned jump);

private:
	void readPointer();
	Rect tabRect(int t) const { return (Rect){ LIST_X + t * TAB_DX, TAB_Y, TAB_W, TAB_H }; }
	int tabAt(int x, int y) const;
	int rowAt(int x, int y) const;
	int tabCount() const { return achievements_board_count() ? TABS : TABS - 1; }
	void setTab(int t) { if (t != tab) { tab = t; board = 0; clampSel(); } }
	void step(int d);
	void drawHeader(menu::Graphics &gfx);
	void drawTabs(menu::Graphics &gfx);
	void drawList(menu::Graphics &gfx);
	void drawRow(menu::Graphics &gfx, int v, int idx);
	void drawDetail(menu::Graphics &gfx);
	void drawBoards(menu::Graphics &gfx);
};

void AchievementsBoard::enter(unsigned jump)
{
	for (int t = 0; t < TABS; t++)
		sel[t] = topRow[t] = 0;
	detail = false;
	board = 0;
	pointer = false;
	freeShot();
	achievements_players_changed();	/* a Mii seated in the Players menu since: the set loads again for it, as it would on resume */
	achievements_view_player(0);
	if (achievements_player_key(0)[0])
		players_start();	/* the Miis' coins (players.c; nothing happens when it is running already) */
	achievements_list_refresh();	/* Official mode: what was earned since it was last open */
	build();
	tab = firstTab();
	if (jump)
		jumpTo(jump);
	clampSel();
}

/* A pointer's place on the picture: the same coordinates as the menu's (widescreen shifts them) */
void AchievementsBoard::readPointer()
{
	WPADData *w = menu::Input::getInstance().getWpad();
	int chan = -1;
	for (int i = 0; i < 4 && chan < 0; i++)
		if (w[i].ir.valid && w[i].err == WPAD_ERR_NONE)
			chan = i;
	pointer = chan >= 0;
	if (!pointer)
		return;
	px = screenMode ? (int)(w[chan].ir.x * 848 / 640 - 104) : (int)w[chan].ir.x;
	py = (int)w[chan].ir.y;
}

int AchievementsBoard::tabAt(int x, int y) const
{
	for (int t = 0; t < tabCount(); t++)
		if (tabRect(t).in(x, y))
			return t;
	return -1;
}

int AchievementsBoard::rowAt(int x, int y) const
{
	if (tab == TAB_BOARDS || x < LIST_X || x >= LIST_X + LIST_W || y < ROW_Y0 || y >= ROW_Y0 + ROWS_VIS * ROW_H)
		return -1;
	int k = topRow[tab] + (y - ROW_Y0) / ROW_H;
	return k < counts[tab] ? k : -1;
}

/* One step back or on in the list (or the boards) */
void AchievementsBoard::step(int d)
{
	if (tab == TAB_BOARDS)
	{
		int n = (int)achievements_board_count();
		if (board + d >= 0 && board + d < n)
			board += d;
		return;
	}
	if (sel[tab] + d >= 0 && sel[tab] + d < counts[tab])
	{
		sel[tab] += d;
		clampSel();
	}
}

menu::Component *AchievementsBoard::updateFocus(int direction, int buttonsPressed)
{
	bool a = buttonsPressed & menu::Focus::ACTION_SELECT, b = buttonsPressed & menu::Focus::ACTION_BACK;
	if (b)
	{
		if (detail)
			leaveDetail();
		else
			Func_ReturnFromAchievementsFrame();
		return this;
	}
	if (detail)
	{
		if (direction == menu::Focus::DIRECTION_LEFT || direction == menu::Focus::DIRECTION_UP) step(-1);
		else if (direction == menu::Focus::DIRECTION_RIGHT || direction == menu::Focus::DIRECTION_DOWN) step(1);
		return this;
	}
	if (direction == menu::Focus::DIRECTION_LEFT) setTab(tab > 0 ? tab - 1 : tab);
	else if (direction == menu::Focus::DIRECTION_RIGHT) setTab(tab + 1 < tabCount() ? tab + 1 : tab);
	else if (direction == menu::Focus::DIRECTION_UP) step(-1);
	else if (direction == menu::Focus::DIRECTION_DOWN) step(1);
	else if (a)
	{
		int t = pointer ? tabAt(px, py) : -1, r = pointer ? rowAt(px, py) : -1;
		if (t >= 0)
			setTab(t);
		else
		{
			if (r >= 0)
				sel[tab] = r;
			clampSel();
			openDetail();
		}
	}
	return this;
}

void AchievementsBoard::drawHeader(menu::Graphics &gfx)
{
	unsigned who = achievements_viewed_player(), count = achievements_count(), players = achievements_player_count();
	const char *name = achievements_player_name(who), *key = achievements_player_key(who);
	char line[128];
	int x = 20;
	if (name[0])
	{
		fill(gfx, 16, 6, 52, 52, (GXColor){196, 196, 196, 255});
		drawCoin(gfx, key, 20, 10, 44);
		x = 76;
	}
	textFit(x, 6, name[0] ? name : "Guest", 0.9f, 0.5f, 320, DARK);
	textFit(x, 36, achievements_title(), 0.6f, 0.5f, 330, MID);

	snprintf(line, sizeof line, "%u / %u earned", total_earned, count);
	textRight(620, 6, line, 0.75f, DARK);
	if (total_points)
		snprintf(line, sizeof line, "%u / %u points", earned_points, total_points);
	else
		snprintf(line, sizeof line, "no points in this set");
	textRight(620, 36, line, 0.6f, MID);
	drawBar(gfx, 20, 62, 600, 10, count ? (int)(total_earned * 100 / count) : 0, GREEN);

	snprintf(line, sizeof line, "%s", achievements_player_note(who));	/* why this Mii earns Local (its account did not work) */
	if (!line[0] && !who)	/* the Official Mii's list is the server's: its rich presence; the Local Miis' is their own */
		snprintf(line, sizeof line, "%s", achievements_presence());
	if (!line[0] && players > 1)
		snprintf(line, sizeof line, "Player %u of %u", who + 1, players);
	if (line[0])
		textFit(20, 74, line, 0.5f, 0.5f, 600, MID);
}

void AchievementsBoard::drawTabs(menu::Graphics &gfx)
{
	char line[48];
	for (int t = 0; t < tabCount(); t++)
	{
		Rect r = tabRect(t);
		bool on = t == tab;
		fill(gfx, r.x, r.y, r.w, r.h, on ? (GXColor){120, 120, 120, 255} : (GXColor){160, 160, 160, 255});
		fill(gfx, r.x + 1, r.y + 1, r.w - 2, r.h - 1, on ? (GXColor){238, 238, 238, 255} : (GXColor){190, 190, 190, 255});
		if (t < 3)
			snprintf(line, sizeof line, "%s (%d)", TAB_NAMES[t], counts[t]);
		else
			snprintf(line, sizeof line, "%s", TAB_NAMES[t]);
		textFit(r.x + 8, r.y + 3, line, 0.6f, 0.5f, r.w - 16, on ? DARK : MID);
	}
}

void AchievementsBoard::drawRow(menu::Graphics &gfx, int v, int idx)
{
	achievement_entry_t e;
	char line[128], count[32];
	int y = ROW_Y0 + v * ROW_H, ph = ROW_H - 4;
	if (achievements_entry(lists[tab][idx].i, &e) < 0)
		return;
	if (idx == sel[tab])
		frame(gfx, LIST_X - 3, y - 3, LIST_W + 6, ph + 6, 3, RED);
	fill(gfx, LIST_X, y, LIST_W, ph, e.earned ? (GXColor){208, 226, 208, 255} : (GXColor){222, 222, 222, 255});
	drawBadge(gfx, e, BADGE_X, y + (ph - BADGE_S) / 2, BADGE_S, e.earned);

	/* the right column: the points, then when it was earned, how rare it is, or that it is Unofficial */
	if (e.points)
	{
		snprintf(line, sizeof line, "%u pts", e.points);
		textRight(RIGHT_X, y + 4, line, 0.7f, DARK);
	}
	if (e.earned && e.when)
		dateText(e.when, line, sizeof line);
	else if (e.rarity_x100 >= 0)
		snprintf(line, sizeof line, "%d.%d%% have it", e.rarity_x100 / 100, e.rarity_x100 % 100 / 10);
	else if (e.unofficial)
		snprintf(line, sizeof line, "Unofficial");
	else
		line[0] = '\0';
	if (line[0])
		textRight(RIGHT_X, y + 30, line, 0.5f, MID);

	textFit(TEXT_X, y + 3, e.title, 0.7f, 0.5f, TEXT_W - 10, DARK);
	if (e.progress[0] && !e.earned)
	{
		/* a measured one: one line of description, then the bar and its count */
		snprintf(line, sizeof line, "%s", e.desc);
		clip(line, 0.5f, TEXT_W);
		text(TEXT_X, y + 26, line, 0.5f, MID);
		spaced(count, sizeof count, e.progress);
		int cw = width(count, 0.5f);
		drawBar(gfx, TEXT_X, y + 44, TEXT_W - cw - 12, 8, e.percent, BLUE);
		text(TEXT_X + TEXT_W - cw, y + 39, count, 0.5f, DARK);
	}
	else
	{
		Line lines[2];
		int n = wrap(e.desc, 0.5f, TEXT_W, lines, 2);
		for (int k = 0; k < n; k++)
			text(TEXT_X, y + 26 + k * 15, lines[k], 0.5f, MID);
	}
}

void AchievementsBoard::drawList(menu::Graphics &gfx)
{
	int n = counts[tab];
	if (!n)
	{
		static const char *const EMPTY[3] = { "Nothing is in progress", "Nothing is locked", "Nothing earned yet" };
		textCenter(320, 250, EMPTY[tab], 0.8f, MID);
		return;
	}
	if (pointer)	/* the pointer chooses what it is over */
	{
		int r = rowAt(px, py);
		if (r >= 0 && r != sel[tab])
			sel[tab] = r;
	}
	for (int v = 0; v < ROWS_VIS && topRow[tab] + v < n; v++)
		drawRow(gfx, v, topRow[tab] + v);
	if (n > ROWS_VIS)	/* a scroll bar at the right edge */
	{
		int h = ROWS_VIS * ROW_H - 4, th = h * ROWS_VIS / n;
		if (th < 16) th = 16;
		int ty = ROW_Y0 + (h - th) * topRow[tab] / (n - ROWS_VIS);
		fill(gfx, 612, ROW_Y0, 6, h, (GXColor){196, 196, 196, 255});
		fill(gfx, 612, ty, 6, th, (GXColor){110, 110, 110, 255});
	}
}

void AchievementsBoard::drawDetail(menu::Graphics &gfx)
{
	achievement_entry_t e;
	char line[160];
	Line lines[6];
	int n = counts[tab];
	if (!n || achievements_entry(lists[tab][sel[tab]].i, &e) < 0)
	{
		leaveDetail();
		return;
	}
	loadShot(e.id);
	const char *name = achievements_player_name(achievements_viewed_player());

	/* the big badge, the title, the points, the kind and how rare it is */
	drawBadge(gfx, e, 24, 18, 96, e.earned);
	textFit(140, 14, e.title, 1.0f, 0.5f, 476, DARK);
	if (e.points)
	{
		snprintf(line, sizeof line, "%u points", e.points);
		text(140, 50, line, 0.8f, DARK);
	}
	snprintf(line, sizeof line, "%s", typeName(e.type));
	if (e.rarity_x100 >= 0)
		snprintf(line + strlen(line), sizeof line - strlen(line), "%s%d.%d%% of players have it",
		         line[0] ? "   " : "", e.rarity_x100 / 100, e.rarity_x100 % 100 / 10);
	if (e.unofficial)
		snprintf(line + strlen(line), sizeof line - strlen(line), "%sUnofficial", line[0] ? "   " : "");
	textFit(140, 84, line, 0.6f, 0.5f, 476, MID);
	fill(gfx, 20, 124, 600, 2, (GXColor){170, 170, 170, 255});

	/* the left column: the description, the progress, who earned it and when */
	int y = SHOT_Y;
	int nl = wrap(e.desc, 0.65f, 276, lines, 6);
	for (int k = 0; k < nl; k++)
		text(24, y + k * 24, lines[k], 0.65f, DARK);
	y += nl * 24 + 14;
	if (e.progress[0] && !e.earned)
	{
		char count[32];
		spaced(count, sizeof count, e.progress);
		text(24, y, count, 0.8f, DARK);
		drawBar(gfx, 24, y + 30, 270, 12, e.percent, BLUE);
		y += 56;
	}
	if (e.earned)
	{
		char d[16];
		dateText(e.when, d, sizeof d);
		if (name[0])
			snprintf(line, sizeof line, "%s earned it%s%s", name, d[0] ? " on " : "", d);
		else
			snprintf(line, sizeof line, "Earned%s%s", d[0] ? " " : "", d);
		textFit(24, y, line, 0.65f, 0.5f, 276, GREEN);
	}
	else
		text(24, y, "Not earned yet", 0.65f, MID);

	/* the picture of the unlock, 4:3 */
	fill(gfx, SHOT_X - 3, SHOT_Y - 3, SHOT_W + 6, SHOT_H + 6, (GXColor){90, 90, 90, 255});
	if (shotOk)
	{
		drawTex(gfx, &shotTex, SHOT_X, SHOT_Y, SHOT_W, SHOT_H, WHITE);
		if (shotDate[0])
		{
			snprintf(line, sizeof line, "Taken %s", shotDate);
			fill(gfx, SHOT_X - 3, SHOT_Y + SHOT_H + 3, SHOT_W + 6, 22, FOOT);
			text(SHOT_X + 4, SHOT_Y + SHOT_H + 6, line, 0.5f, WHITE);
		}
	}
	else
	{
		fill(gfx, SHOT_X, SHOT_Y, SHOT_W, SHOT_H, (GXColor){196, 196, 196, 255});
		textCenter(SHOT_X + SHOT_W / 2, SHOT_Y + SHOT_H / 2 - 28, "No picture", 0.7f, MID);
		textCenter(SHOT_X + SHOT_W / 2, SHOT_Y + SHOT_H / 2 + 2, "It is taken when the", 0.5f, MID);
		textCenter(SHOT_X + SHOT_W / 2, SHOT_Y + SHOT_H / 2 + 18, "achievement unlocks", 0.5f, MID);
	}
	snprintf(line, sizeof line, "%d of %d", sel[tab] + 1, n);
	textRight(620, 449, line, 0.5f, WHITE);
}

void AchievementsBoard::drawBoards(menu::Graphics &gfx)
{
	unsigned count = achievements_board_count();
	achievement_score_t best[TOP_SCORES], mine;
	unsigned n = TOP_SCORES;
	const char *title, *desc;
	char line[128];
	int r;

	if (!count || achievements_board(board, &title, &desc) < 0)
	{
		textCenter(320, 250, "No leaderboards for this game", 0.8f, MID);
		return;
	}
	textFit(20, 126, title, 0.8f, 0.5f, 500, DARK);
	textFit(20, 152, desc, 0.5f, 0.5f, 500, MID);
	snprintf(line, sizeof line, "%d of %u", board + 1, count);
	textRight(620, 130, line, 0.6f, MID);

	r = achievements_board_scores(board, best, &n, &mine);
	if (r <= 0)
	{
		textCenter(320, 270, r ? "No answer from RetroAchievements" : "Asking RetroAchievements...", 0.8f, MID);
		return;
	}
	if (!n)
		textCenter(320, 270, "No scores yet", 0.8f, MID);
	for (unsigned i = 0; i < n && i < 8; i++)
	{
		int y = 176 + i * 28;
		snprintf(line, sizeof line, "%u", best[i].rank);
		text(40, y, line, 0.7f, DARK);
		textFit(100, y, best[i].user, 0.7f, 0.5f, 300, DARK);
		textRight(600, y, best[i].score, 0.7f, DARK);
	}
	if (mine.rank)
		snprintf(line, sizeof line, "You: %u   %s", mine.rank, mine.score);
	else
		snprintf(line, sizeof line, "You: no score on this board");
	textCenter(320, 176 + 8 * 28 + 4, line, 0.7f, DARK);
}

void AchievementsBoard::drawComponent(menu::Graphics &gfx)
{
	char hint[128];
	unsigned count = achievements_count();
	readPointer();
	if (!count)
	{
		snprintf(hint, sizeof hint, "%s", achievements_status());	/* why: off, a .exe, no set, Official ... */
		textCenter(320, 190, hint, 1.0f, DARK);
		footer(gfx, "B: back");
		return;
	}
	if (switchPlayer())
	{
		leaveDetail();
		for (int t = 0; t < TABS; t++)
			sel[t] = topRow[t] = 0;
		build();
		if (tab != TAB_BOARDS)
			tab = firstTab();
		clampSel();
	}
	if (detail)
		drawDetail(gfx);
	else
	{
		drawHeader(gfx);
		drawTabs(gfx);
		if (tab == TAB_BOARDS)
			drawBoards(gfx);
		else
			drawList(gfx);
	}
	bool players = achievements_player_count() > 1;
	if (detail)
		snprintf(hint, sizeof hint, "%s", "Left/Right: other achievements   B: back to the list");
	else if (tab == TAB_BOARDS)
		snprintf(hint, sizeof hint, "%s", "Up/Down: boards   Left/Right: tabs   B: back");
	else
		snprintf(hint, sizeof hint, "%s   Left/Right: tabs%s   B: back", pointer ? "Click an achievement" : "A: details",
		         players ? "   L/R, +/-: player" : "");
	footer(gfx, hint);
	gfx.setTEV(GX_PASSCLR);
}

/* ---- the frame ---- */

static AchievementsBoard *theBoard;

namespace {
void Func_ReturnFromAchievementsFrame()
{
	freeShot();
	pMenuContext->setActiveFrame(MenuContext::FRAME_CURRENTROM);
}
}

void Func_AchievementsList()
{
	jumpId = 0;
	pMenuContext->setActiveFrame(MenuContext::FRAME_ACHIEVEMENTS, 0);
}

void Func_AchievementsOpenOn(unsigned id)
{
	jumpId = id;
	pMenuContext->setActiveFrame(MenuContext::FRAME_ACHIEVEMENTS, 1);
}

AchievementsFrame::AchievementsFrame()
{
	theBoard = new AchievementsBoard();
	add(theBoard);
	menu::Cursor::getInstance().addComponent(this, theBoard, 0, 640, 0, 480);
	setDefaultFocus(theBoard);
	setBackFunc(Func_ReturnFromAchievementsFrame);
	setEnabled(true);
}

AchievementsFrame::~AchievementsFrame()
{
	menu::Cursor::getInstance().removeComponent(this, theBoard);
	delete theBoard;
	theBoard = NULL;
	freeShot();
	delete coinImg;
	coinImg = NULL;
	coinMii = -1;
}

void AchievementsFrame::activateSubmenu(int submenu)
{
	unsigned jump = submenu == 1 ? jumpId : 0;
	jumpId = 0;
	theBoard->enter(jump);
}

void AchievementsFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;

	menu::ComponentList::const_iterator it;
	for (it = componentList.begin(); it != componentList.end(); ++it)
		(*it)->draw(gfx);
}
