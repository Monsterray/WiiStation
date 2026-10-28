/**
 * WiiStation - PlayersFrame.cpp
 * The Players menu (Docs/MII_PLAN.md, layout D): a card per PlayStation port on a band the
 * grey of the console's plastic, and below it a paged strip of Mii coins (players.c). A Wii
 * Remote holds A+B on a coin and drops it on a card; a pad picks a coin with A, chooses a
 * card and seats it with A. One component, the board, draws all of it and takes all input:
 * the pointer's held buttons are read each frame (the drag), presses come through
 * updateFocus from both the pointer and the pads.
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

void text(int x, int y, const char *s, float scale, GXColor c, bool centered = false)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	font.drawInit(c);
	font.drawString(x, y, (char *)s, scale, centered);
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

const char *padName(int seat)
{
	int port = seat < 2 ? seat : (seat - 2) / 4;
	if (padType[port] == PADTYPE_NONE)
		return "Off";
	if (padType[port] == PADTYPE_COOP)
		return "Co-Op";
	virtualControllers_t &v = virtualControllers[seat];
	if (!v.inUse || !v.control)
		return "No pad";
	switch (v.control->identifier) {
	case 'G': return "GameCube pad";
	case 'H': return "USB pad";
	case 'C': return "Classic Controller";
	case 'N': return "Wii Remote + Nunchuk";
	case 'W': return "Wii Remote";
	case 'P': return "Wii U Pro";
	case 'D': return "Wii U GamePad";
	}
	return "Pad";
}

bool padConnected(int seat)
{
	int port = seat < 2 ? seat : (seat - 2) / 4;
	if (padType[port] == PADTYPE_COOP) {
		for (int k = 0; k < coopPlayers[port]; k++)
			if (virtualControllers[COOP_VC(port, k)].inUse)
				return true;
		return false;
	}
	return virtualControllers[seat].inUse && virtualControllers[seat].control;
}

void Func_ReturnFromPlayers()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

} // namespace

class PlayersBoard : public menu::Component
{
public:
	PlayersBoard() { setType(TYPE_OTHER); memset(img, 0, sizeof img); memset(serial, 0, sizeof serial); reset(); }
	~PlayersBoard() { for (int i = 0; i < PLAYERS_MAX; i++) delete img[i]; }
	void reset() { page = 0; focus = 0; onCards = false; card = 0; held = -1; dragging = false; pointer = false; }
	void drawComponent(menu::Graphics &gfx);
	menu::Component *updateFocus(int direction, int buttonsPressed);

private:
	menu::Image *img[PLAYERS_MAX];
	unsigned serial[PLAYERS_MAX];
	int page, focus, card, held;     /* held: the Mii picked up (pad, or a pointer click), -1 none */
	bool onCards, dragging, pointer;
	int px, py;
	int seats[8], nSeats;
	Rect cards[8];

	void layout();
	int coinAt(int x, int y) const;
	int cardAt(int x, int y) const;
	int pages() const { int n = players_count(); return n > 0 ? (n + PER_PAGE - 1) / PER_PAGE : 1; }
	int onPage() const { int n = players_count() - page * PER_PAGE; return n < 0 ? 0 : n < PER_PAGE ? n : PER_PAGE; }
	void seatOn(int c, int mii);
	void drawCoin(menu::Graphics &gfx, int i, int x, int y, int size, u8 alpha);
	void drawCard(menu::Graphics &gfx, int c);
	void readPointer();
	void turnPage(int d) { int p = page + d; if (p >= 0 && p < pages()) { page = p; focus = 0; } }
};

/* The seats shown: a card per port, or four for a port with a multitap. */
void PlayersBoard::layout()
{
	nSeats = 0;
	for (int p = 0; p < 2; p++)
		if (padType[p] == PADTYPE_MULTITAP)
			for (int k = 0; k < 4; k++)
				seats[nSeats++] = 2 + p * 4 + k;
		else
			seats[nSeats++] = p;
	for (int j = 0; j < nSeats; j++)
		cards[j] = nSeats == 2 ? (Rect){ 40 + j * 290, 66, 270, 186 }
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
	int seat = seats[c], from = -1;
	for (int s = 0; s < PLAYERS_SEATS; s++)
		if (!strcmp(playerSeat[s], m->key))
			from = s;
	char was[PLAYERS_KEY];
	snprintf(was, sizeof was, "%s", playerSeat[seat]);
	players_seat_mii(seat, m->key);
	if (from >= 0 && from != seat && was[0])
		players_seat_mii(from, was);
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
		float sc = size / 64.0f;
		text(x + size / 2, y + size / 2 + (int)(6 * sc), s, sc, c, true);   /* measured: a centred string's capitals sit about 6 x scale above y */
	}
}

void PlayersBoard::drawCard(menu::Graphics &gfx, int c)
{
	const Rect &r = cards[c];
	int seat = seats[c], mii = players_find(playerSeat[seat]);
	bool big = r.w > 200, pad = padConnected(seat), focused = !pointer && onCards && card == c;
	const char *name = mii >= 0 ? players_get(mii)->name : pad ? "Guest" : "Connect a pad";
	int cs = big ? 78 : 46;
	int cx = big ? r.x + (r.w - cs) / 2 : r.x + 10, cy = big ? r.y + 50 : r.y + 24;

	if (focused)
		fill(gfx, r.x - 3, r.y - 3, r.w + 6, r.h + 6, RED);
	fill(gfx, r.x, r.y, r.w, r.h, (GXColor){146, 146, 146, 255});
	fill(gfx, r.x + 1, r.y + 1, r.w - 2, r.h - 2, pad ? (GXColor){169, 169, 169, 255} : (GXColor){158, 158, 158, 255});
	fill(gfx, cx - 4, cy - 4, cs + 8, cs + 8, (GXColor){151, 151, 151, 255});   /* the recess */

	int show = held >= 0 && !dragging && focused ? held : mii;
	if (show >= 0)
		drawCoin(gfx, show, cx, cy, cs, show == mii ? 255 : 140);
	else
		text(cx + cs / 2, cy + cs / 2 - 8, "Guest", 0.55f, MID, true);

	if (big) {
		text(r.x + 14, r.y + 6, PLAYERS_SEAT_NAMES[seat], 1.3f, DARK);
		text(r.x + 46, r.y + 10, "CONTROLLER", 0.45f, MID);
		text(r.x + 46, r.y + 24, padName(seat), 0.55f, MID);
		text(r.x + r.w / 2, r.y + 138, name, 0.75f, DARK, true);
		socket(gfx, r.x + r.w - 74, r.y + r.h - 26, 62, 18, pad && mii >= 0);
	} else {
		text(r.x + 8, r.y + 4, PLAYERS_SEAT_NAMES[seat], 0.65f, DARK);
		text(r.x + 66, r.y + 22, name, 0.6f, DARK);
		text(r.x + 66, r.y + 40, padName(seat), 0.45f, MID);
		socket(gfx, r.x + r.w - 46, r.y + r.h - 18, 38, 12, pad && mii >= 0);
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
		int mii = k >= 0 ? page * PER_PAGE + k : c >= 0 ? players_find(playerSeat[seats[c]]) : -1;
		if (mii >= 0) {
			held = mii;
			dragging = true;
		}
	} else if (dragging && !ab) {   /* let go: on a card seats it, on the strip unseats it */
		int c = cardAt(px, py);
		if (c >= 0)
			seatOn(c, held);
		else if (py >= BAND_Y + BAND_H)
			for (int s = 0; s < PLAYERS_SEATS; s++)
				if (players_get(held) && !strcmp(playerSeat[s], players_get(held)->key))
					playerSeat[s][0] = 0;
		held = -1;
		dragging = false;
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

	text(20, 14, "Players", 1.0f, DARK);
	text(120, 22, "Seat a Mii on each port that plays", 0.55f, MID);
	char line[96];
	if (players_count() > 0) {
		snprintf(line, sizeof line, "%d / %d", page + 1, pages());
		text(620 - menu::IplFont::getInstance().getStringWidth(line, 0.6f), 20, line, 0.6f, MID);
	}

	fill(gfx, 0, BAND_Y, 640, BAND_H, (GXColor){180, 180, 180, 255});
	fill(gfx, 0, BAND_Y, 640, 3, (GXColor){207, 207, 207, 255});
	fill(gfx, 0, BAND_Y + BAND_H - 2, 640, 2, (GXColor){154, 154, 154, 255});
	for (int c = 0; c < nSeats; c++)
		drawCard(gfx, c);

	int n = players_count();
	const char *hint;
	if (n < 0)
		text(320, 330, "Reading the Miis...", 0.7f, DARK, true);
	else if (n == 0)
		text(320, 330, "No Miis on this Wii: make some in the Mii Channel", 0.7f, DARK, true);
	for (int k = 0; k < onPage(); k++) {
		int i = page * PER_PAGE + k, x = STRIP_X + (k % COLS) * STRIP_DX, y = STRIP_Y + (k / COLS) * STRIP_DY;
		const player_mii *m = players_get(i);
		if (dragging && held == i)
			continue;
		int seat = -1;
		for (int s = 0; s < PLAYERS_SEATS; s++)
			if (m && !strcmp(playerSeat[s], m->key))
				seat = s;
		bool f = !onCards && focus == k && (held < 0 || dragging);
		if (f) {
			gfx.setTEV(GX_PASSCLR);
			gfx.setColor(RED);
			for (int r = 0; r < 4; r++)
				gfx.drawCircle(x + COIN / 2, y + COIN / 2, COIN / 2 + 1 + r, 40);
		}
		drawCoin(gfx, i, x, y, COIN, seat >= 0 && held != i ? 115 : 255);
		if (seat >= 0) {
			fill(gfx, x + COIN - 18, y - 4, 24, 16, (GXColor){47, 47, 47, 255});
			text(x + COIN - 6, y - 3, PLAYERS_SEAT_NAMES[seat], 0.45f, WHITE, true);
		}
	}
	if (n > 0 && players_faces() < 0) {
		snprintf(line, sizeof line, "No Mii faces (%d): in HBC, HOME > HBC > Save Mii faces", players_faces());
		text(320, 412, line, 0.5f, MID, true);
	}
	const player_mii *fm = players_get(page * PER_PAGE + focus);
	if (fm && !onCards) {
		snprintf(line, sizeof line, "%s%s", fm->name, fm->favorite ? "  (favourite)" : "");
		text(320, 430, line, 0.7f, DARK, true);
	}
	if (dragging && held >= 0)
		drawCoin(gfx, held, px - 30, py - 30, 60, 240);

	if (held >= 0 && !dragging)
		hint = "Choose a port, A: seat   B: put back";
	else if (onCards)
		hint = "A: pick up   B: back to Guest   Down: the Miis";
	else if (pointer)
		hint = "Hold A+B on a Mii and drop it on a port   +/-: page   B: back";
	else
		hint = "A: pick a Mii   Up: the ports   L/R: page   B: back";
	text(320, 452, hint, 0.55f, DARK, true);
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
		if (b && !aHeld && !dragging && k < 0 && c < 0) {   /* B over nothing: Back */
			if (held >= 0) held = -1;
			else Func_ReturnFromPlayers();
		} else if (a && !(buttonsPressed & menu::Focus::ACTION_BACK)) {
			if (k >= 0 && held < 0) { held = page * PER_PAGE + k; onCards = true; card = 0; }   /* click, then click a port */
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
			for (int c = 0; c < nSeats; c++)   /* where it sits, else the first Guest seat */
				if (!strcmp(playerSeat[seats[c]], players_get(held)->key)) { card = c; break; }
		}
		else if (b) Func_ReturnFromPlayers();
		return this;
	}

	int row = nSeats > 2 ? 4 : 2;
	if (direction == menu::Focus::DIRECTION_RIGHT) { if (card + 1 < nSeats) card++; }
	else if (direction == menu::Focus::DIRECTION_LEFT) { if (card > 0) card--; }
	else if (direction == menu::Focus::DIRECTION_UP) { if (card >= row) card -= row; }
	else if (direction == menu::Focus::DIRECTION_DOWN) { if (card + row < nSeats && nSeats > 2) card += row; else if (held < 0) onCards = false; }
	else if (a) {
		if (held >= 0) { seatOn(card, held); held = -1; onCards = false; }
		else held = players_find(playerSeat[seats[card]]);
	}
	else if (b) {
		if (held >= 0) { held = -1; onCards = false; }
		else playerSeat[seats[card]][0] = 0;
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
