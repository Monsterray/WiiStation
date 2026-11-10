/**
 * WiiStation - PlayersFrame.cpp
 * The Players menu (Docs/ACHIEVEMENTS.md, Players and Miis, layout D): a card per PlayStation port on a band the
 * gray of the console's plastic, and below it a paged strip of Mii coins (players.c). A Wii
 * Remote holds A+B on a coin and drops it on a card; a pad picks a coin with A, chooses a
 * card and seats it with A. A Co-Op port's card holds all its players: opening it (A, a
 * click, or a coin dropped on it) shows the same screen with a card per Co-Op player. A coin
 * dropped on the Edit slot (or chosen, then Edit) opens that Mii's player settings: its
 * RetroAchievements account (the file browser lists them) and Official. One
 * component, the board, draws all of it and takes all input: the pointer's held buttons are
 * read each frame (the drag), presses come through updateFocus from the pointer and the pads.
**/

#include <stdio.h>
#include <string.h>
#include "MenuContext.h"
#include "PlayersFrame.h"
#include "MenuPageView.h"
#include "FileBrowserFrame.h"
#include "../libgui/Image.h"
#include "../libgui/IPLFont.h"
#include "../libgui/InputManager.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
extern "C" {
#include "../gc_input/controller.h"
#include "../wiiSXconfig.h"
#include "../players.h"
}

extern MenuContext *pMenuContext;

bool writePlayers(void);   /* GamecubeMain.cpp: players.ini */
void accountFrame_Open(const char *key, const char *mii);   /* AccountFrame.cpp */

namespace {

const int PER_PAGE = 16, COLS = 8, COIN = 56, STRIP_X = 22, STRIP_Y = 278, STRIP_DX = 66, STRIP_DY = 74;
const int EDIT_X = 592, EDIT_Y = STRIP_Y + (STRIP_DY + COIN) / 2, EDIT_R = COIN / 2;   /* the Edit slot, right of the coins */
const int BAND_Y = 56, BAND_H = 206;
const GXColor DARK = {62, 62, 62, 255}, MID = {90, 90, 90, 255}, WHITE = {255, 255, 255, 255};
const GXColor RED = {184, 49, 47, 255}, GOLD = {255, 210, 58, 255};
/* the bottom of the page is the dark end of the background (63 to 100 of 255 on the left): DARK text there
 * was as dark as the picture, and the hints vanished along it */
const GXColor PALE = {236, 236, 236, 255};

struct Rect { int x, y, w, h; bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

using menu::glue::fill;
using menu::glue::text;

void ring(menu::Graphics &gfx, int x, int y, int inner, int outer, GXColor c)
{
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(c);
	gfx.enableBlending(true);
	gfx.fillRing(x, y, inner, outer, 48);
}

/* Text centered on (x, y), capitals included: measured, a centered string's capitals sit
 * about 6 x scale above the y it is given. (drawString centers the ink horizontally now; this
 * used to add 5 x scale to x for the spacing the width counts, which put the ink right of x.) */
void textMid(int x, int y, const char *s, float scale, GXColor c, int maxw = 0)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	int w = font.getStringWidth((char *)s, scale);
	if (maxw > 0 && w > maxw)
		scale = scale * maxw / w;
	font.drawInit(c);
	font.drawString(x, y + (int)(6 * scale), (char *)s, scale, true);
}

/* A round well for a coin of radius r: shadowed at the top, lit at the bottom lip. The lip is the
 * lower part of a disc one step below the well, cut below the well's middle (a lip that thins out to nothing at the sides is a few lone pixels): drawn whole, its
 * rim came within a pixel of the well's own at the top, and pixels of it showed through there
 * as specks (the rim of a 48-sided disc is not the same pixels for two discs). */
void slot(menu::Graphics &gfx, int x, int y, int r)
{
	gfx.enableScissor(x - r - 6 > 0 ? x - r - 6 : 0, y + r / 2, 2 * r + 12, r + 8);
	ring(gfx, x, y + 2, 0, r + 2, (GXColor){196, 196, 196, 255});
	gfx.disableScissor();
	ring(gfx, x, y, 0, r + 2, (GXColor){118, 118, 118, 255});
	ring(gfx, x, y + 1, 0, r, (GXColor){146, 146, 146, 255});
}

/* A PS1 controller socket, front on: three chambers with their pins, lit when in use. */
void socket(menu::Graphics &gfx, int x, int y, int w, int h, bool lit)
{
	const GXColor rim = {143, 143, 143, 255}, hole = {38, 38, 38, 255}, wall = {163, 163, 163, 255};
	const GXColor on = {255, 210, 58, 255}, off = {74, 74, 74, 255};
	static const int pins[3] = {2, 3, 3};
	int cw = (w - 16) / 3, ph = h * 56 / 100, pin = h > 14 ? 3 : 2;
	fill(gfx, x, y, w, h, rim);
	fill(gfx, x + 2, y + 2, w - 4, h - 4, hole);
	for (int j = 0; j < 3; j++) {
		int cx = x + 5 + j * (cw + 3), cy = y + (h - ph) / 2;
		fill(gfx, cx, cy, cw, ph, wall);
		for (int p = 0; p < pins[j]; p++)
			fill(gfx, cx + cw * (p + 1) / (pins[j] + 1) - pin / 2, y + h / 2 - pin / 2, pin, pin, lit ? on : off);
	}
}

/* The Official button: lit (gold) on the Mii that plays Official, no On/Off text. */
void officialButton(menu::Graphics &gfx, const Rect &b, bool on, bool enabled, float scale)
{
	fill(gfx, b.x - 1, b.y - 1, b.w + 2, b.h + 2, enabled ? (GXColor){96, 96, 96, 255} : (GXColor){150, 150, 150, 255});
	fill(gfx, b.x, b.y, b.w, b.h, on ? GOLD : enabled ? (GXColor){120, 120, 120, 255} : (GXColor){172, 172, 172, 255});
	textMid(b.x + b.w / 2, b.y + b.h / 2 - (int)(6 * scale), "Official", scale,
	        on ? DARK : enabled ? (GXColor){214, 214, 214, 255} : (GXColor){146, 146, 146, 255});
}

int seatPort(int seat) { return seat < 2 ? seat : seat < 10 ? (seat - 2) / 4 : (seat - 10) / COOP_MAX; }
bool isGroup(int seat) { return seat < 2 && padType[seat] == PADTYPE_COOP; }
int coopCount(int port) { int n = coopPlayers[port]; return n < 1 ? 1 : n > COOP_MAX ? COOP_MAX : n; }

const char *padName(int seat, bool small)
{
	int port = seatPort(seat);
	if (padType[port] == PADTYPE_NONE)
		return "Off";
	if (isGroup(seat))
		return "Co-Op";
	virtualControllers_t &v = virtualControllers[seat];
	if (!v.inUse || !v.control)
		return "No pad";
	switch (v.control->identifier) {
	case 'G': return small ? "GameCube" : "GameCube pad";
	case 'H': return small ? "USB" : "USB pad";
	case 'C': return small ? "Classic" : "Classic Controller";
	case 'N': return small ? "Nunchuk" : "Wii Remote + Nunchuk";
	case 'W': return "Wii Remote";
	case 'P': return small ? "Pro" : "Wii U Pro";
	case 'D': return small ? "GamePad" : "Wii U GamePad";
	}
	return "Pad";
}

bool padConnected(int seat)
{
	if (isGroup(seat)) {
		for (int k = 0; k < coopCount(seat); k++)
			if (virtualControllers[COOP_VC(seat, k)].inUse)
				return true;
		return false;
	}
	return virtualControllers[seat].inUse && virtualControllers[seat].control;
}

int seatOf(int mii)
{
	const player_mii *m = players_get(mii);
	for (int s = 0; m && s < PLAYERS_SEATS; s++)
		if (!strcmp(playerSeat[s], m->key))
			return s;
	return -1;
}

void Func_ReturnFromPlayers()
{
	writePlayers();
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

} // namespace

class PlayersBoard : public menu::glue::Board
{
public:
	PlayersBoard() { turn = 0; memset(img, 0, sizeof img); memset(serial, 0, sizeof serial); memset(imgSrc, 0, sizeof imgSrc); reset(); }
	~PlayersBoard() { for (int i = 0; i < PLAYERS_MAX; i++) delete img[i]; }
	void reset() { page = 0; focus = 0; onCards = false; onEdit = false; card = 0; held = -1; dragging = false; pointer = false; coop = -1; checked = false; editKey[0] = 0; editRow = 0; }
	menu::Component *updateFocus(int direction, int buttonsPressed);

private:
	void onInput(const menu::glue::PageInput &in) { turn = in.turn; }
	void paint(menu::Graphics &gfx);
	int turn;                        /* the page turn of this frame's input step */
	menu::Image *img[PLAYERS_MAX];
	unsigned serial[PLAYERS_MAX];
	const void *imgSrc[PLAYERS_MAX];   /* the coin pixels each img was made from: the Miis' order changes (players_sort) */
	int page, focus, card, held;     /* held: the Mii picked up (pad, or a pointer click), -1 none */
	int coop;                        /* the port whose Co-Op players are shown, -1 the ports */
	bool onCards, onEdit, dragging, pointer;
	char editKey[PLAYERS_KEY];       /* the Mii whose player settings show, "" the board */
	int editRow;                     /* the settings' focus: 0 the account, 1 Official */
	bool checked;                    /* the seats were checked against the Miis on this Wii */
	int gone, goneFrames;            /* seats whose Mii is gone, and how long the notice still shows */
	int px, py;
	int seats[COOP_MAX], nSeats;
	Rect cards[COOP_MAX];

	void layout();
	int coinAt(int x, int y) const;
	int cardAt(int x, int y) const;
	int pages() const { int n = players_count(); return n > 0 ? (n + PER_PAGE - 1) / PER_PAGE : 1; }
	int onPage() const { int n = players_count() - page * PER_PAGE; return n < 0 ? 0 : n < PER_PAGE ? n : PER_PAGE; }
	void seatOn(int c, int mii);
	void openCoop(int port);
	void leaveCoop() { card = coop == 1 && padType[0] == PADTYPE_MULTITAP ? 4 : coop; coop = -1; }
	void drawCoin(menu::Graphics &gfx, int i, int x, int y, int size, u8 alpha);
	void drawCard(menu::Graphics &gfx, int c);
	void readPointer();
	void turnPage(int d) { int p = page + d; if (p >= 0 && p < pages()) { page = p; focus = 0; } }
	bool editAt(int x, int y) const { int dx = x - EDIT_X, dy = y - EDIT_Y; return players_count() > 0 && dx * dx + dy * dy <= (EDIT_R + 4) * (EDIT_R + 4); }
	void openEdit(int mii);
	bool linkedOn(int c) const { int seat = seats[c]; return !isGroup(seat) && playerSeat[seat][0] && *players_account(playerSeat[seat]); }
	Rect officialRect(int c) const;
	int officialAt(int x, int y) const;
	void drawEdit(menu::Graphics &gfx);
	menu::Component *updateEdit(int direction, bool a, bool b);
};

/* The settings page's two controls: the account, and the Official button. */
const Rect EDIT_ACCOUNT = { 330, 296, 270, 38 }, EDIT_OFFICIAL = { 330, 358, 120, 38 };

/* The seats shown: a card per port (four for a port with a multitap), or a card per player
 * of the open Co-Op port. */
void PlayersBoard::layout()
{
	nSeats = 0;
	if (coop >= 0 && padType[coop] != PADTYPE_COOP)
		coop = -1;
	if (coop >= 0)
		for (int k = 0; k < coopCount(coop); k++)
			seats[nSeats++] = COOP_VC(coop, k);
	else
		for (int p = 0; p < 2; p++)
			if (padType[p] == PADTYPE_MULTITAP)
				for (int k = 0; k < 4; k++)
					seats[nSeats++] = 2 + p * 4 + k;
			else
				seats[nSeats++] = p;
	bool big = coop < 0 && nSeats == 2;
	for (int j = 0; j < nSeats; j++)
		cards[j] = big ? (Rect){ 40 + j * 290, 66, 270, 186 }
		               : (Rect){ 20 + (j % 4) * 152, 66 + (j / 4) * 96, 144, 90 };
	if (card >= nSeats)
		card = nSeats - 1;
}

int PlayersBoard::coinAt(int x, int y) const
{
	for (int k = 0; k < onPage(); k++) {
		int cx = STRIP_X + (k % COLS) * STRIP_DX, cy = STRIP_Y + (k / COLS) * STRIP_DY;
		if (x >= cx && x < cx + COIN && y >= cy && y < cy + COIN)
			return k;
	}
	return -1;
}

void PlayersBoard::openEdit(int mii)
{
	const player_mii *m = players_get(mii);
	if (m) {
		snprintf(editKey, sizeof editKey, "%s", m->key);
		editRow = 0;
	}
	held = -1;
	dragging = false;
	onEdit = onCards = false;
}

/* A linked Mii's card: the button at its top right. */
Rect PlayersBoard::officialRect(int c) const
{
	const Rect &r = cards[c];
	return r.w > 200 ? (Rect){ r.x + r.w - 86, r.y + 8, 78, 22 } : (Rect){ r.x + r.w - 56, r.y + 5, 50, 15 };
}

int PlayersBoard::officialAt(int x, int y) const
{
	for (int c = 0; c < nSeats; c++)
		if (linkedOn(c) && officialRect(c).in(x, y))
			return c;
	return -1;
}

int PlayersBoard::cardAt(int x, int y) const
{
	for (int j = 0; j < nSeats; j++)
		if (cards[j].in(x, y))
			return j;
	return -1;
}

/* Seats a Mii on card c; one already there goes to the seat the new one left, if any. */
void PlayersBoard::seatOn(int c, int mii)
{
	const player_mii *m = players_get(mii);
	if (!m)
		return;
	int seat = seats[c], from = seatOf(mii);
	char was[PLAYERS_KEY];
	snprintf(was, sizeof was, "%s", playerSeat[seat]);
	players_seat_mii(seat, m->key);
	if (from >= 0 && from != seat && was[0])
		players_seat_mii(from, was);
}

/* Shows a Co-Op port's players; a Mii in hand stays there, the focus on its seat or the first
 * Guest. */
void PlayersBoard::openCoop(int port)
{
	coop = port;
	onCards = true;
	dragging = false;
	layout();
	card = 0;
	int from = held >= 0 ? seatOf(held) : -1;
	for (int c = nSeats - 1; c >= 0; c--)
		if (seats[c] == from || (from < 0 && !playerSeat[seats[c]][0]))
			card = c;
}

void PlayersBoard::drawCoin(menu::Graphics &gfx, int i, int x, int y, int size, u8 alpha)
{
	const player_mii *m = players_get(i);
	if (!m || !m->coin)
		return;
	if (!img[i] || imgSrc[i] != m->coin || serial[i] != m->coin_serial) {   /* i is a place in the order, not a Mii */
		delete img[i];
		img[i] = new menu::Image(m->coin, PLAYERS_COIN, PLAYERS_COIN, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		serial[i] = m->coin_serial;
		imgSrc[i] = m->coin;
		GX_InvalidateTexAll();
	}
	gfx.setColor((GXColor){255, 255, 255, alpha});
	img[i]->activateImage(GX_TEXMAP0);
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, x, y, size, size, 0, 1, 0, 1);
	if (!m->face) {   /* no head yet, or no face models: the initial */
		char s[2] = { m->name[0] & 0x80 ? '?' : m->name[0], 0 };
		bool light = m->fav_color == 2 || m->fav_color == 10;   /* yellow, white */
		GXColor c = light ? (GXColor){51, 51, 51, alpha} : (GXColor){255, 255, 255, alpha};
		textMid(x + size / 2, y + size / 2, s, size / 64.0f, c);
	}
}

void PlayersBoard::drawCard(menu::Graphics &gfx, int c)
{
	const Rect &r = cards[c];
	int seat = seats[c], mii = players_find(playerSeat[seat]);
	bool big = r.w > 200, group = isGroup(seat), pad = padConnected(seat);
	bool focused = pointer ? held >= 0 && cardAt(px, py) == c : onCards && card == c;

	if (focused)
		fill(gfx, r.x - 3, r.y - 3, r.w + 6, r.h + 6, RED);
	fill(gfx, r.x, r.y, r.w, r.h, (GXColor){146, 146, 146, 255});
	fill(gfx, r.x + 1, r.y + 1, r.w - 2, r.h - 2, pad ? (GXColor){169, 169, 169, 255} : (GXColor){158, 158, 158, 255});

	if (big) {
		text(r.x + 14, r.y + 6, PLAYERS_SEAT_NAMES[seat], 1.3f, DARK);
		text(r.x + 46, r.y + 10, "CONTROLLER", 0.45f, MID);
		text(r.x + 46, r.y + 24, padName(seat, false), 0.55f, MID);
		socket(gfx, r.x + (r.w - 62) / 2, r.y + r.h - 26, 62, 18, pad && (group || mii >= 0));
	} else {
		text(r.x + 8, r.y + 4, PLAYERS_SEAT_NAMES[seat], 0.65f, DARK);
		socket(gfx, r.x + r.w - 46, r.y + r.h - 18, 38, 12, pad && (group || mii >= 0));
	}
	if (linkedOn(c))
		officialButton(gfx, officialRect(c), !strcmp(players_official(), playerSeat[seat]), true, big ? 0.55f : 0.4f);

	if (group) {   /* a coin well per Co-Op player, and the button that opens them */
		int n = coopCount(seat), d = big ? 28 : 14, gap = big ? 4 : 7, per = big ? 8 : 4;
		int x0 = big ? r.x + (r.w - (n < per ? n : per) * (d + gap) + gap) / 2 : r.x + 12;
		int y0 = big ? r.y + 72 : r.y + 30;
		for (int k = 0; k < n; k++) {
			int x = x0 + (k % per) * (d + gap), y = y0 + (k / per) * (d + gap);
			int m = players_find(playerSeat[COOP_VC(seat, k)]);
			slot(gfx, x + d / 2, y + d / 2, d / 2);
			if (m >= 0)
				drawCoin(gfx, m, x, y, d, 255);
		}
		char line[24];
		int bw = big ? 40 : 26, bh = big ? 22 : 15, bx = r.x + r.w - bw - 8, by = r.y + 8;
		snprintf(line, sizeof line, big ? "%d players" : "Co-Op, %d", n);
		if (big)
			textMid(r.x + r.w / 2, r.y + 134, line, 0.7f, DARK);
		else
			text(r.x + 30, r.y + 8, line, 0.45f, MID, bx - r.x - 36);
		fill(gfx, bx, by, bw, bh, (GXColor){90, 90, 90, 255});
		textMid(bx + bw / 2, by + bh / 2 - (big ? 4 : 3), "...", big ? 0.8f : 0.6f, WHITE);
		return;
	}

	int rad = big ? 39 : 23, sx = big ? r.x + r.w / 2 : r.x + 33, sy = big ? r.y + 86 : r.y + 52;
	int show = held >= 0 && !dragging && focused ? held : mii;
	slot(gfx, sx, sy, rad);
	if (show >= 0)
		drawCoin(gfx, show, sx - rad, sy - rad, rad * 2, show == mii ? 255 : 140);
	else
		textMid(sx, sy, "Guest", big ? 0.6f : 0.42f, DARK);

	if (big) {
		const char *name = mii >= 0 ? players_get(mii)->name : pad ? "" : "Connect a pad";
		textMid(r.x + r.w / 2, r.y + 144, name, 0.75f, DARK, r.w - 20);
	} else {
		if (mii >= 0)
			text(r.x + 64, r.y + 26, players_get(mii)->name, 0.55f, DARK, r.w - 70);
		text(r.x + 64, r.y + 46, padName(seat, true), 0.45f, MID, r.w - 70);
	}
}

/* The pointer this frame: where, and A+B held to drag. */
void PlayersBoard::readPointer()
{
	WPADData *w = menu::Input::getInstance().getWpad();
	int chan = -1;
	for (int i = 0; i < 4 && chan < 0; i++)
		if (w[i].ir.valid && w[i].err == WPAD_ERR_NONE)
			chan = i;
	pointer = chan >= 0;
	if (!pointer) {
		dragging = false;
		return;
	}
	px = screenMode ? (int)(w[chan].ir.x * 848 / 640 - 104) : (int)w[chan].ir.x;
	py = (int)w[chan].ir.y;
	if (editKey[0]) {   /* the settings page: no drag */
		dragging = false;
		return;
	}
	bool ab = (w[chan].btns_h & WPAD_BUTTON_A) && (w[chan].btns_h & WPAD_BUTTON_B);
	if (!dragging && ab) {
		int k = coinAt(px, py), c = cardAt(px, py);
		int mii = k >= 0 ? page * PER_PAGE + k : c >= 0 && !isGroup(seats[c]) ? players_find(playerSeat[seats[c]]) : -1;
		if (mii >= 0) {
			held = mii;
			dragging = true;
		}
	} else if (dragging && !ab) {   /* let go: on a card seats it, on a Co-Op card opens it, on the strip unseats it */
		int c = cardAt(px, py);
		dragging = false;
		if (editAt(px, py)) {
			openEdit(held);
			return;
		}
		if (c >= 0 && isGroup(seats[c])) {
			openCoop(seats[c]);
			return;
		}
		if (c >= 0)
			seatOn(c, held);
		else if (py >= BAND_Y + BAND_H && seatOf(held) >= 0)
			playerSeat[seatOf(held)][0] = 0;
		held = -1;
	}
	int k = coinAt(px, py);
	if (k >= 0 && !dragging) {
		focus = k;
		onCards = false;
	}
}

/* A Mii's player settings: its RetroAchievements account and Official. */
void PlayersBoard::drawEdit(menu::Graphics &gfx)
{
	int mii = players_find(editKey), seat = seatOf(mii);
	const player_mii *m = players_get(mii);
	if (!m) {
		editKey[0] = 0;
		return;
	}
	char line[96];
	text(20, 14, "Players", 1.0f, DARK);
	text(120, 22, "A Mii's player settings", 0.55f, MID);
	fill(gfx, 0, BAND_Y, 640, BAND_H, (GXColor){180, 180, 180, 255});
	fill(gfx, 0, BAND_Y, 640, 3, (GXColor){207, 207, 207, 255});
	fill(gfx, 0, BAND_Y + BAND_H - 2, 640, 2, (GXColor){154, 154, 154, 255});
	slot(gfx, 130, 159, 62);
	drawCoin(gfx, mii, 70, 99, 120, 255);
	text(222, 104, m->name, 1.1f, DARK, 390);
	if (seat >= 0)
		snprintf(line, sizeof line, "Port %s", PLAYERS_SEAT_NAMES[seat]);
	text(224, 152, seat >= 0 ? line : "Not seated", 0.6f, MID);

	const char *acc = players_account(editKey);
	bool linked = *acc, on = linked && !strcmp(players_official(), editKey);
	int row = pointer ? (EDIT_ACCOUNT.in(px, py) ? 0 : EDIT_OFFICIAL.in(px, py) ? 1 : -1) : editRow;
	const Rect &f = row == 0 ? EDIT_ACCOUNT : EDIT_OFFICIAL;
	if (row >= 0)
		fill(gfx, f.x - 3, f.y - 3, f.w + 6, f.h + 6, RED);

	text(40, EDIT_ACCOUNT.y + 9, "RetroAchievements account", 0.65f, DARK, 280);
	fill(gfx, EDIT_ACCOUNT.x, EDIT_ACCOUNT.y, EDIT_ACCOUNT.w, EDIT_ACCOUNT.h, (GXColor){146, 146, 146, 255});
	fill(gfx, EDIT_ACCOUNT.x + 1, EDIT_ACCOUNT.y + 1, EDIT_ACCOUNT.w - 2, EDIT_ACCOUNT.h - 2, (GXColor){200, 200, 200, 255});
	text(EDIT_ACCOUNT.x + 12, EDIT_ACCOUNT.y + 9, !linked ? "None: earns Local" : !strcmp(acc, "*") ? "This Wii's account" : acc,
	     0.65f, linked ? DARK : MID, EDIT_ACCOUNT.w - 24);

	text(40, EDIT_OFFICIAL.y + 9, "Plays Official", 0.65f, DARK);
	bool plays = seat >= 0 && players_seat_active(seat);
	officialButton(gfx, EDIT_OFFICIAL, on, linked && plays, 0.65f);
	if (!linked || !plays)
		text(EDIT_OFFICIAL.x + EDIT_OFFICIAL.w + 14, EDIT_OFFICIAL.y + 11, !linked ? "Link an account first" : "Seat this Mii first", 0.5f, MID, 150);

	text(20, 448, pointer ? "Click a setting   B: back to Players" : "A: change   Up/Down: choose   B: back to Players", 0.5f, PALE, 600);
	gfx.setTEV(GX_PASSCLR);
}

menu::Component *PlayersBoard::updateEdit(int direction, bool a, bool b)
{
	if (b) {
		editKey[0] = 0;
		return this;
	}
	if (pointer)
		editRow = EDIT_ACCOUNT.in(px, py) ? 0 : EDIT_OFFICIAL.in(px, py) ? 1 : -1;
	else if (direction == menu::Focus::DIRECTION_UP)
		editRow = 0;
	else if (direction == menu::Focus::DIRECTION_DOWN)
		editRow = 1;
	int mii = players_find(editKey);
	if (!a || mii < 0)
		return this;
	if (editRow == 0) {   /* the accounts, on their own list page; it comes back here */
		pMenuContext->setActiveFrame(MenuContext::FRAME_ACCOUNT, 0);
		accountFrame_Open(editKey, players_get(mii)->name);
	} else if (editRow == 1 && *players_account(editKey) && seatOf(mii) >= 0 && players_seat_active(seatOf(mii)))
		snprintf(playersOfficial, sizeof playersOfficial, "%s", editKey);
	return this;
}

void PlayersBoard::paint(menu::Graphics &gfx)
{
	layout();
	readPointer();
	if (editKey[0]) {
		drawEdit(gfx);
		return;
	}
	/* +/- (Wii Remote, Classic) and L/R (GameCube) turn the page */
	if (turn)
		turnPage(turn);

	char line[96];
	text(20, 14, "Players", 1.0f, DARK);
	if (coop >= 0)
		snprintf(line, sizeof line, "Port %d Co-Op: seat a Mii on each player", coop + 1);
	text(120, 22, coop >= 0 ? line : "Seat a Mii on each port that plays", 0.55f, MID);

	fill(gfx, 0, BAND_Y, 640, BAND_H, (GXColor){180, 180, 180, 255});
	fill(gfx, 0, BAND_Y, 640, 3, (GXColor){207, 207, 207, 255});
	fill(gfx, 0, BAND_Y + BAND_H - 2, 640, 2, (GXColor){154, 154, 154, 255});
	for (int c = 0; c < nSeats; c++)
		drawCard(gfx, c);

	int n = players_count();
	if (n < 0)
		textMid(320, 330, "Reading the Miis...", 0.7f, DARK);
	else if (n == 0)
		textMid(320, 330, "No Miis on this Wii: make some in the Mii Channel", 0.7f, DARK);
	for (int k = 0; k < onPage(); k++) {
		int i = page * PER_PAGE + k, x = STRIP_X + (k % COLS) * STRIP_DX, y = STRIP_Y + (k / COLS) * STRIP_DY;
		if (dragging && held == i)
			continue;
		int seat = seatOf(i);
		if (!onCards && !onEdit && focus == k && (held < 0 || dragging))
			ring(gfx, x + COIN / 2, y + COIN / 2, COIN / 2 + 1, COIN / 2 + 5, RED);
		drawCoin(gfx, i, x, y, COIN, seat >= 0 && held != i ? 115 : 255);
		if (*players_account(players_get(i)->key)) {   /* linked to a RetroAchievements account */
			fill(gfx, x + COIN - 18, y - 2, 22, 13, DARK);
			textMid(x + COIN - 7, y + 4, "RA", 0.45f, WHITE);
		}
		if (seat >= 0) {   /* where it sits, under the coin */
			snprintf(line, sizeof line, "Port %s", PLAYERS_SEAT_NAMES[seat]);
			int tw = menu::IplFont::getInstance().getStringWidth(line, 0.5f) + 10;
			fill(gfx, x + COIN / 2 - tw / 2, y + COIN + 2, tw, 14, DARK);
			textMid(x + COIN / 2, y + COIN + 9, line, 0.5f, WHITE);
		}
	}
	if (n > 0) {   /* the Edit slot: a Mii dropped here opens its player settings */
		if (onEdit || (pointer && held >= 0 && editAt(px, py)))
			ring(gfx, EDIT_X, EDIT_Y, EDIT_R + 1, EDIT_R + 5, RED);
		slot(gfx, EDIT_X, EDIT_Y, EDIT_R - 2);
		if (onEdit && held >= 0)
			drawCoin(gfx, held, EDIT_X - EDIT_R + 2, EDIT_Y - EDIT_R + 2, 2 * EDIT_R - 4, 140);
		else
			textMid(EDIT_X, EDIT_Y, "Edit", 0.6f, DARK);
	}
	if (n > 0 && !checked) {   /* a seated Mii deleted in the Mii Channel: its seat is Guest */
		checked = true;
		gone = 0;
		for (int s = 0; s < PLAYERS_SEATS; s++)
			if (playerSeat[s][0] && players_find(playerSeat[s]) < 0) {
				playerSeat[s][0] = 0;
				gone++;
			}
		goneFrames = gone ? 6 * 60 : 0;
	}
	if (goneFrames > 0) {   /* its unlocks stay in the journal (Docs/ACHIEVEMENTS.md, Players and Miis) */
		goneFrames--;
		if (gone == 1)
			snprintf(line, sizeof line, "A seated Mii is no longer on this Wii: its seat is Guest now");
		else
			snprintf(line, sizeof line, "%d seated Miis are no longer on this Wii: their seats are Guest now", gone);
		text(120, 40, line, 0.5f, RED, 500);
	}
	else if (n > 0 && players_faces() < 0) {
		snprintf(line, sizeof line, "No Mii faces (code %d): in HBC, HOME > HBC > Save Mii faces", players_faces());
		text(120, 40, line, 0.5f, DARK, 500);
	}
	const player_mii *fm = players_get(page * PER_PAGE + focus);
	if (onEdit)
		textMid(320, 434, "Edit: a Mii's player settings", 0.7f, PALE);
	else if (fm && !onCards) {
		snprintf(line, sizeof line, "%s%s", fm->name, fm->favorite ? "  (favorite)" : "");
		textMid(320, 434, line, 0.7f, PALE);
	}
	if (dragging && held >= 0)
		drawCoin(gfx, held, px - 30, py - 30, 60, 240);

	const char *hint;
	if (held >= 0 && !dragging && onEdit)
		hint = "A: its player settings   Up: the seats   B: put back";
	else if (held >= 0 && !dragging)
		hint = "Choose a seat or Edit (Down), A: seat   B: put back";
	else if (onEdit)
		hint = "A on a Mii, then Edit: its settings   Left: the Miis   B: back";
	else if (onCards)
		hint = coop >= 0 ? "A: pick up   B: Guest, then back   Down: the Miis"
		                 : "A: pick up / open   B: back to Guest   Down: the Miis";
	else if (pointer)
		hint = "Hold A+B on a Mii, drop it on a seat or on Edit   B: back";
	else
		hint = "A: pick a Mii   Up: the seats   B: back";
	text(20, 448, hint, 0.5f, PALE, 470);
	if (n > 0) {   /* the coins' page, bottom right */
		snprintf(line, sizeof line, "Miis  %d / %d", page + 1, pages());
		text(620 - menu::IplFont::getInstance().getStringWidth(line, 0.55f), 447, line, 0.55f, PALE);
	}
	gfx.setTEV(GX_PASSCLR);
}

/* Presses, from the pointer (direction 0) and from the pads. Always this: B never falls
 * through to the frame unless it means Back. */
menu::Component *PlayersBoard::updateFocus(int direction, int buttonsPressed)
{
	bool a = buttonsPressed & menu::Focus::ACTION_SELECT, b = buttonsPressed & menu::Focus::ACTION_BACK;
	int n = onPage();
	if (editKey[0])
		return updateEdit(direction, a, b);

	if (pointer) {
		WPADData *w = menu::Input::getInstance().getWpad();
		bool aHeld = false;
		for (int i = 0; i < 4; i++)
			aHeld |= (w[i].btns_h & WPAD_BUTTON_A) != 0;
		int k = coinAt(px, py), c = cardAt(px, py);
		if (b && !aHeld && !dragging && k < 0 && c < 0) {   /* B over nothing: put back, leave Co-Op, Back */
			if (held >= 0) held = -1;
			else if (coop >= 0) { leaveCoop(); onCards = false; }
			else Func_ReturnFromPlayers();
		} else if (a && !b) {
			int o = officialAt(px, py);
			if (o >= 0 && held < 0) snprintf(playersOfficial, sizeof playersOfficial, "%s", playerSeat[seats[o]]);   /* one is on */
			else if (editAt(px, py) && held >= 0) openEdit(held);
			else if (c >= 0 && isGroup(seats[c])) openCoop(seats[c]);
			else if (k >= 0 && held < 0) { held = page * PER_PAGE + k; onCards = true; card = 0; }   /* click, then click a seat */
			else if (c >= 0 && held >= 0) { seatOn(c, held); held = -1; onCards = false; }
			else if (c >= 0) held = players_find(playerSeat[seats[c]]);
			else held = -1;
		}
		return this;
	}

	if (onEdit) {   /* the Edit slot: a Mii in hand opens its settings */
		if (direction == menu::Focus::DIRECTION_LEFT && held < 0) { onEdit = false; focus = n < COLS ? n - 1 : COLS - 1; }
		else if (direction == menu::Focus::DIRECTION_RIGHT && held < 0 && page + 1 < pages()) { onEdit = false; turnPage(1); }
		else if (direction == menu::Focus::DIRECTION_UP) { onEdit = false; onCards = true; card = nSeats - 1; }
		else if (a && held >= 0) openEdit(held);
		else if (b) {
			if (held >= 0) held = -1;
			else if (coop >= 0) leaveCoop();
			else Func_ReturnFromPlayers();
		}
		return this;
	}

	if (!onCards) {
		if (direction == menu::Focus::DIRECTION_RIGHT) { if (focus % COLS == COLS - 1 || focus == n - 1) onEdit = n > 0; else focus++; }
		else if (direction == menu::Focus::DIRECTION_LEFT) { if (focus % COLS == 0) { if (page > 0) { turnPage(-1); focus = COLS - 1; } } else focus--; }
		else if (direction == menu::Focus::DIRECTION_DOWN) { if (focus + COLS < n) focus += COLS; }
		else if (direction == menu::Focus::DIRECTION_UP) { if (focus >= COLS) focus -= COLS; else { onCards = true; card = 0; } }
		else if (a && n > 0) {
			held = page * PER_PAGE + focus;
			onCards = true;
			card = 0;
			int from = seatOf(held);
			for (int c = 0; c < nSeats; c++)   /* where it sits (its Co-Op port's card), else the first */
				if (seats[c] == from || (from >= 10 && seats[c] == seatPort(from))) { card = c; break; }
		}
		else if (b) {
			if (coop >= 0) leaveCoop();
			else Func_ReturnFromPlayers();
		}
		return this;
	}

	int row = nSeats > 2 ? 4 : 2;
	bool group = isGroup(seats[card]);
	if (direction == menu::Focus::DIRECTION_RIGHT) { if (card + 1 < nSeats) card++; }
	else if (direction == menu::Focus::DIRECTION_LEFT) { if (card > 0) card--; }
	else if (direction == menu::Focus::DIRECTION_UP) { if (card >= row) card -= row; }
	else if (direction == menu::Focus::DIRECTION_DOWN) { if (card + row < nSeats && nSeats > 2) card += row; else { onCards = false; onEdit = held >= 0; } }
	else if (a) {
		if (group) openCoop(seats[card]);
		else if (held >= 0) { seatOn(card, held); held = -1; onCards = false; }
		else held = players_find(playerSeat[seats[card]]);
	}
	else if (b) {
		if (held >= 0) { held = -1; onCards = false; }
		else if (!group && playerSeat[seats[card]][0]) playerSeat[seats[card]][0] = 0;
		else if (coop >= 0) leaveCoop();
	}
	return this;
}

PlayersFrame::PlayersFrame()
{
	board = new PlayersBoard();
	add(board);
	menu::Cursor::getInstance().addComponent(this, board, 0, 640, 0, 480);
	setDefaultFocus(board);
	setBackFunc(Func_ReturnFromPlayers);
	setEnabled(true);
}

PlayersFrame::~PlayersFrame()
{
	menu::Cursor::getInstance().removeComponent(this, board);
	delete board;
}

void PlayersFrame::activateSubmenu(int submenu)
{
	players_start();
	if (submenu == 1)   /* back from the account browser: the Mii's settings again */
		return;
	players_sort();
	board->reset();
}

void PlayersFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;
	menu::ComponentList::const_iterator it;
	for (it = componentList.begin(); it != componentList.end(); ++it)
		(*it)->draw(gfx);
}
