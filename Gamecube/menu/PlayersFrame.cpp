/**
 * WiiStation - PlayersFrame.cpp
 * The Players menu (Docs/MII_PLAN.md, layout D): a card per PlayStation port on a band the
 * gray of the console's plastic, and below it a paged strip of Mii coins (players.c). A Wii
 * Remote holds A+B on a coin and drops it on a card; a pad picks a coin with A, chooses a
 * card and seats it with A. A Co-Op port's card holds all its players: opening it (A, a
 * click, or a coin dropped on it) shows the same screen with a card per Co-Op player. One
 * component, the board, draws all of it and takes all input: the pointer's held buttons are
 * read each frame (the drag), presses come through updateFocus from the pointer and the pads.
**/

#include <stdio.h>
#include <string.h>
#include "MenuContext.h"
#include "PlayersFrame.h"
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

namespace {

const int PER_PAGE = 16, COLS = 8, COIN = 56, STRIP_X = 66, STRIP_Y = 278, STRIP_DX = 66, STRIP_DY = 74;
const int BAND_Y = 56, BAND_H = 206;
const GXColor DARK = {62, 62, 62, 255}, MID = {90, 90, 90, 255}, WHITE = {255, 255, 255, 255};
const GXColor RED = {184, 49, 47, 255};

struct Rect { int x, y, w, h; bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

void fill(menu::Graphics &gfx, int x, int y, int w, int h, GXColor c)
{
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(c);
	gfx.fillRect(x, y, w, h);
}

void ring(menu::Graphics &gfx, int x, int y, int inner, int outer, GXColor c)
{
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(c);
	gfx.enableBlending(true);
	gfx.fillRing(x, y, inner, outer, 48);
}

/* Text from its top left, scaled down to fit maxw when given. */
void text(int x, int y, const char *s, float scale, GXColor c, int maxw = 0)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	int w = font.getStringWidth((char *)s, scale);
	if (maxw > 0 && w > maxw)
		scale = scale * maxw / w;
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, false);
}

/* Text centered on (x, y), capitals included: measured, a centered string's capitals sit
 * about 6 x scale above the y it is given, and its ink about 5 x scale left of x (the font's
 * width counts the spacing after the last letter). */
void textMid(int x, int y, const char *s, float scale, GXColor c, int maxw = 0)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	int w = font.getStringWidth((char *)s, scale);
	if (maxw > 0 && w > maxw)
		scale = scale * maxw / w;
	font.drawInit(c);
	font.drawString(x + (int)(5 * scale + 0.5f), y + (int)(6 * scale), (char *)s, scale, true);
}

/* A round well for a coin of radius r: shadowed at the top, lit at the bottom lip. */
void slot(menu::Graphics &gfx, int x, int y, int r)
{
	ring(gfx, x, y + 1, 0, r + 3, (GXColor){196, 196, 196, 255});
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

class PlayersBoard : public menu::Component
{
public:
	PlayersBoard() { setType(TYPE_OTHER); memset(img, 0, sizeof img); memset(serial, 0, sizeof serial); reset(); }
	~PlayersBoard() { for (int i = 0; i < PLAYERS_MAX; i++) delete img[i]; }
	void reset() { page = 0; focus = 0; onCards = false; card = 0; held = -1; dragging = false; pointer = false; coop = -1; }
	void drawComponent(menu::Graphics &gfx);
	menu::Component *updateFocus(int direction, int buttonsPressed);

private:
	menu::Image *img[PLAYERS_MAX];
	unsigned serial[PLAYERS_MAX];
	int page, focus, card, held;     /* held: the Mii picked up (pad, or a pointer click), -1 none */
	int coop;                        /* the port whose Co-Op players are shown, -1 the ports */
	bool onCards, dragging, pointer;
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
};

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
	if (!img[i] || serial[i] != m->coin_serial) {
		delete img[i];
		img[i] = new menu::Image(m->coin, PLAYERS_COIN, PLAYERS_COIN, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		serial[i] = m->coin_serial;
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

void PlayersBoard::drawComponent(menu::Graphics &gfx)
{
	layout();
	readPointer();
	/* +/- (Wii Remote, Classic) and L/R (GameCube) turn the page */
	WPADData *w = menu::Input::getInstance().getWpad();
	for (int i = 0; i < 4; i++) {
		if (w[i].btns_d & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R)) turnPage(1);
		if (w[i].btns_d & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L)) turnPage(-1);
		u16 gc = PAD_ButtonsDown(i);
		if (gc & PAD_TRIGGER_R) turnPage(1);
		if (gc & PAD_TRIGGER_L) turnPage(-1);
	}

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
		if (!onCards && focus == k && (held < 0 || dragging))
			ring(gfx, x + COIN / 2, y + COIN / 2, COIN / 2 + 1, COIN / 2 + 5, RED);
		drawCoin(gfx, i, x, y, COIN, seat >= 0 && held != i ? 115 : 255);
		if (seat >= 0) {   /* where it sits, under the coin */
			snprintf(line, sizeof line, "Port %s", PLAYERS_SEAT_NAMES[seat]);
			int tw = menu::IplFont::getInstance().getStringWidth(line, 0.42f) + 10;
			fill(gfx, x + COIN / 2 - tw / 2, y + COIN + 2, tw, 14, DARK);
			textMid(x + COIN / 2, y + COIN + 9, line, 0.42f, WHITE);
		}
	}
	if (n > 0 && players_faces() < 0) {
		snprintf(line, sizeof line, "No Mii faces (%d): in HBC, HOME > HBC > Save Mii faces", players_faces());
		textMid(320, 418, line, 0.5f, MID);
	}
	const player_mii *fm = players_get(page * PER_PAGE + focus);
	if (fm && !onCards) {
		snprintf(line, sizeof line, "%s%s", fm->name, fm->favorite ? "  (favorite)" : "");
		textMid(320, 434, line, 0.7f, DARK);
	}
	if (dragging && held >= 0)
		drawCoin(gfx, held, px - 30, py - 30, 60, 240);

	const char *hint;
	if (held >= 0 && !dragging)
		hint = "Choose a seat, A: seat   B: put back";
	else if (onCards)
		hint = coop >= 0 ? "A: pick up   B: Guest, then back   Down: the Miis"
		                 : "A: pick up / open   B: back to Guest   Down: the Miis";
	else if (pointer)
		hint = "Hold A+B on a Mii and drop it on a seat   B: back";
	else
		hint = "A: pick a Mii   Up: the seats   B: back";
	text(20, 448, hint, 0.5f, DARK, 470);
	if (n > 0) {   /* the coins' page, bottom right */
		snprintf(line, sizeof line, "Miis  %d / %d", page + 1, pages());
		text(620 - menu::IplFont::getInstance().getStringWidth(line, 0.55f), 447, line, 0.55f, DARK);
	}
	gfx.setTEV(GX_PASSCLR);
}

/* Presses, from the pointer (direction 0) and from the pads. Always this: B never falls
 * through to the frame unless it means Back. */
menu::Component *PlayersBoard::updateFocus(int direction, int buttonsPressed)
{
	bool a = buttonsPressed & menu::Focus::ACTION_SELECT, b = buttonsPressed & menu::Focus::ACTION_BACK;
	int n = onPage();

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
			if (c >= 0 && isGroup(seats[c])) openCoop(seats[c]);
			else if (k >= 0 && held < 0) { held = page * PER_PAGE + k; onCards = true; card = 0; }   /* click, then click a seat */
			else if (c >= 0 && held >= 0) { seatOn(c, held); held = -1; onCards = false; }
			else if (c >= 0) held = players_find(playerSeat[seats[c]]);
			else held = -1;
		}
		return this;
	}

	if (!onCards) {
		if (direction == menu::Focus::DIRECTION_RIGHT) { if (focus % COLS == COLS - 1 || focus == n - 1) turnPage(1); else focus++; }
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
	else if (direction == menu::Focus::DIRECTION_DOWN) { if (card + row < nSeats && nSeats > 2) card += row; else if (held < 0) onCards = false; }
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
	(void)submenu;
	players_start();
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
