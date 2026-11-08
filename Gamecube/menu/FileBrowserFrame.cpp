/**
 * WiiSX - FileBrowserFrame.cpp
 * Copyright (C) 2009, 2010 sepp256
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
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
#include <cstdlib>
#include <strings.h>
#include <sys/stat.h>   /* the RomDir folders */
#include <time.h>        /* the Recent record's play time */
#include <ctype.h>
#include "../../achievements.h"   /* the set result stored in the Recent record */
#ifdef EXPLORER_TIMING
#include <stdarg.h>
#include <ogc/lwp_watchdog.h>
#endif
#include <dirent.h>     /* the account files */
#include "../config_parse.h"   /* config_list_next */
#include "MenuGlue.h"
#define CONFIG_ITEM_SIZE 256
#include "MenuContext.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "FileBrowserFrame.h"
#include "ExplorerSources.h"   /* loadRomUseSource: a Recent row on another device */
#include "RomInfo.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/MessageBox.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"

#include "../../psxcommon.h"

#include "../../HidController/KernelHID.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-DVD.h"
#include "../fileBrowser/fileBrowser-CARD.h"
#include "../fileBrowser/fileBrowser-SMB.h"
#include "../fileBrowser/smb2dev.h"
#include "../fileBrowser/explorer_info.h"
#include "../fileBrowser/recent_list.h"
#include "../fileBrowser/cover_index.h"
#include "../cover_art.h"
#include "../../database.h"
#include "../players.h"
extern long ISOgetTN(unsigned char *buffer);
extern char debugInfo[256];
}


namespace eng = menu::engine;

void Func_ReturnFromFileBrowserFrame();
void Func_LoadFromSD();		/* ExplorerSources.cpp: the tabs load the sources through these */
void Func_LoadFromUSB();
void Func_LoadFromDVD();
void Func_LoadFromSamba();
static void Func_TabSD();
static void Func_TabUSB();
static void Func_TabDVD();
static void Func_TabNetwork();
static void Func_TabRecent();

static const int NUM_FILE_SLOTS = STD_LIST_ROWS;	/* the one row count (MenuLayout.h) */
static const int NUM_TABS = 5;

/* The source the explorer shows: 0 SD, 1 USB, 2 DVD, 3 Network, 4 Recent. Set by ExplorerSources.cpp's
 * Func_LoadFrom*, so the lit tab is always the last source loaded. */
int explorerSource = 0;

/* The directory listing, owned here: dir_entries holds num_entries entries. */
static fileBrowser_file* dir_entries;
static bool				romFolderList;   /* dir_entries are the RomDir folders: show whole paths */
static int				num_entries;

/* Where the explorer is. Depth 0 is the source's root: the one RomDir folder (or the device root
 * when there is none), or the list of several RomDir folders. Each folder below it adds one. Every
 * folder below the root has a real ".." row first. A source remembers its folder (tabMemory). */
static const int ROM_DIRS_MAX = 16;
static int  fbDepth;
static bool fbUpRow;					/* dir_entries[0] is the ".." row */
static bool fbRootIsList;				/* the root is the list of RomDir folders (fbRootList) */
static fileBrowser_file fbRootList[ROM_DIRS_MAX];
static int  fbRootListN;
static char fbRootDevice[16];
struct TabMemory { char path[256]; int depth; };
static TabMemory tabMemory[4];			/* by explorerSource: SD, USB, DVD, Network */
static bool explorerUp(void);			/* B: up one folder; false at a root */
static char fbCameFrom[256];			/* the folder just left by going up: the next list focuses it */
static int  fbFocusIdx = -1;			/* FillPage focuses this item (and shows its page) once */

/* What the explorer learns about a file by reading it: the disc count of a PBP or an M3U (and the
 * size, for a source whose directory read gives none). Read only for the rows shown, one per frame
 * (fbPumpInfo), and kept here, parallel to dir_entries: the entry's attr is the file handle slot of
 * the ISO reader (libfat), so it cannot carry bits. A re-sort starts it again (fbInfoClear). */
struct FbInfo { unsigned char done, discs; };
static FbInfo *fbInfo;
static char fbNote[16][28];				/* "4 discs", "yesterday, 4 discs", "USB, 2026-09-28", by row index % 16 */

/* The first game inside a folder row (ei_find_game), for the facts strip: the result is kept per
 * row index in a few slots (state 1 = found, 2 = none), looked up lazily for the focused row only,
 * and dropped whenever the list is read again or re-sorted (fbInfoClear / fbInfoReset). */
#define FB_GAME_SLOTS 8
struct FbGame { int idx; unsigned char state, discs; unsigned size; char path[FILE_BROWSER_MAX_PATH_LEN]; };
static FbGame fbGames[FB_GAME_SLOTS];
static unsigned fbGamesNext;
static void fbGamesClear(void) { memset(fbGames, 0, sizeof fbGames); }
static FbGame *fbGameFind(int idx)
{
	for (int i = 0; i < FB_GAME_SLOTS; i++)
		if (fbGames[i].state && fbGames[i].idx == idx)
			return &fbGames[i];
	return NULL;
}
static void normPath(char *p);			/* defined with showFolder */

/* The Recent record (sd:/wiistation/recent.ini): the last games loaded. Read when the explorer
 * opens (and the file again after a game is recorded); the facts strip looks the focused row up in
 * it. A file that is missing or damaged is an empty list. The saves count (stat of the game's
 * .st0-.st9) is kept for the row it was made for, so it is not redone every frame. */
#define RECENT_FILE "sd:/wiistation/recent.ini"
extern "C" char *statespath;
extern "C" unsigned int cdrIsoMultidiskCount, cdrIsoMultidiskSelect;
extern char CdromId[10];
static recent_list fbRecent;
static bool fbRecentLoaded;
static int fbRecentFilter = -1;		/* the Recent tab's letter filter: a strip cell, -1 none (Func_TabRecent clears it) */
static void fbRecentLoad(void)
{
	recent_load(&fbRecent, RECENT_FILE);
	fbRecentLoaded = true;
}
static int fbSavesIdx = -1, fbSavesN = -1;		/* the row index the count is for; the count (-1 unknown) */
static int fbLastFocus = -1;					/* the last row a button had focus on (the strip keeps it under a message box) */
static void fbSavesReset(void) { fbSavesIdx = -1; fbSavesN = -1; }
static int fbSavesCount(int idx, const char *serial)
{
	if (idx == fbSavesIdx)
		return fbSavesN;
	int n = -1;
	if (serial && serial[0])
	{
		n = 0;
		char p[256];
		struct stat st;
		for (int slot = 0; slot < 10; slot++)
			if (recent_state_path(p, sizeof p, saveStateDevice == SAVESTATEDEVICE_USB ? "usb:" : "sd:",
			                      statespath, serial, slot) == 0 && stat(p, &st) == 0)
				n++;
	}
	fbSavesIdx = idx;
	fbSavesN = n;
	return n;
}
/* A game was just loaded from the explorer: record it. Never fails loudly: no card folder, a full
 * card or a read-only card leave the old file as it was. */
static void fbRecordLoad(const char *path)
{
	recent_entry e;
	memset(&e, 0, sizeof e);
	snprintf(e.path, sizeof e.path, "%s", path);
	recent_src_of(path, e.src, sizeof e.src);
	e.disc = cdrIsoMultidiskCount > 1 ? (int)cdrIsoMultidiskSelect + 1 : 0;
	e.time = (long long)time(NULL);
	snprintf(e.serial, sizeof e.serial, "%s", CdromId);
	const char *h = achievements_disc_hash();
	if (h) snprintf(e.hash, sizeof e.hash, "%s", h);
	e.set = recent_set_result(achievementsEnabled == ACH_OFFICIAL || achievementsEnabled == ACH_LOCAL,
	                          e.hash, achievements_count(), achievements_loading());
	fbRecentLoad();							/* an unknown set keeps what an older entry knew */
	recent_keep_known(&e, &fbRecent);
	if (recent_add(&fbRecent, &e) == 0)
	{
		mkdir("sd:/wiistation", 0777);		/* exists in practice; an error here is no matter */
		recent_save(&fbRecent, RECENT_FILE);
	}
	fbSavesReset();
}

/* Recent rows. Each entry's source is one of the four tabs' devices; a row whose device is not
 * there now is dimmed. The ready flags are probed when the tab opens and after an A attempt. */
static const char *const fbSrcName[4] = { "sd", "usb", "dvd", "smb" };
static int fbSrcReady[4];
static int fbSrcIndex(const char *src)
{
	for (int s = 0; s < 4; s++)
		if (!strcasecmp(src, fbSrcName[s]))
			return s;
	return -1;
}
static void fbSrcProbe(void)
{
	for (int s = 0; s < 4; s++)
		fbSrcReady[s] = loadRomSourceReady(s);
}
/* The record of the Recent row whose path is name, or NULL. */
static const recent_entry *fbRecentOf(const char *name)
{
	const int ri = recent_find(&fbRecent, name);
	return ri < 0 ? NULL : &fbRecent.e[ri];
}
static bool fbRecentDim(int index)
{
	if (explorerSource != 4 || !dir_entries || index < 0 || index >= num_entries)
		return false;
	const recent_entry *re = fbRecentOf(dir_entries[index].name);
	if (!re)
		return false;
	const int s = fbSrcIndex(re->src);
	return s < 0 || !fbSrcReady[s];
}

static void fbInfoClear(void)
{
	fbSavesReset();
	fbGamesClear();
	if (fbInfo && num_entries > 0)
		memset(fbInfo, 0, num_entries * sizeof(FbInfo));
}
static void fbInfoReset(void)			/* a new dir_entries */
{
	fbSavesReset();
	fbGamesClear();
	fbLastFocus = -1;
	if (fbInfo) { free(fbInfo); fbInfo = NULL; }
	if (num_entries > 0)
		fbInfo = (FbInfo*)calloc(num_entries, sizeof(FbInfo));
}

/*  The list: the engine's ROW_LIST, paged (Gamecube/menu/MenuEngine.h)  */

static void fileBrowserItem(void *ctx, int index, eng::ListItem &out);
extern int fileBrowserMode;
void fileBrowserFrame_FillPage();
void fileBrowserFrame_LoadFile(int i);

/* The explorer page (DENSITY_EXPLORER), a glue::ListView: the five source tabs, the path and the
 * item count on one line, STD_LIST_ROWS rows from y 126 and a scroll bar at the right whose ends are
 * the Prev and Next arrows. The list's state is FB_LIST (the view lays out from it); the page's texts
 * and lit tab are set before each layout (fbPrepare); a page turn goes through fbTurn. */
static menu::glue::ListView *fbView;
static eng::ListState FB_LIST = { 0, NUM_FILE_SLOTS, 0, -1 };

static const eng::Tab FB_TABS[NUM_TABS] = {
	{ "SD", 0.0f }, { "USB", 0.0f }, { "DVD", 0.0f }, { "Network", 0.0f }, { "Recent", 0.0f }
};
static char fbPath[256];		/* the folder shown (the device for a list of folders) */
static char fbPathShown[256];	/* fbPath, cut from the left to the room the count leaves */
static char fbCount[64];
static eng::FreeText FB_TEXTS[2];

/* out = s, or "..." and its tail when s is wider than room pixels at scale. */
static void cutLeft(char *out, size_t size, const char *s, float scale, int room)
{
	const eng::Metrics &m = menu::glue::METRICS;
	snprintf(out, size, "%s", s);
	if (m.width(out, scale, m.ctx) <= room)
		return;
	for (const char *p = s; *p; )
	{
		p++;
		while ((*p & 0xC0) == 0x80)		/* not into the middle of a UTF-8 character */
			p++;
		snprintf(out, size, "...%s", p);
		if (m.width(out, scale, m.ctx) <= room)
			return;
	}
}

/* Before every layout: the lit tab, and the path and count line (the count changes with the list). */
static void fbPrepare(void *, eng::Page &page)
{
	page.curTab = explorerSource;

	const eng::Metrics &m = menu::glue::METRICS;
	const float scale = (float)STD_PATH_PX / 24.0f;
	/* "42 games", "3 folders", "3 folders, 6 games" (the ".." row is not an item) */
	int folders = 0, games = 0;
	for (int i = fbUpRow ? 1 : 0; dir_entries && i < num_entries; i++)
	{
		if (dir_entries[i].attr & FILE_BROWSER_ATTR_DIR) folders++;
		else games++;
	}
	if (explorerSource == 4 && games == 0)
		snprintf(fbCount, sizeof fbCount, "No games played yet");
	else	/* plus the sort hint: "A-Z" / "folders first"; Recent "newest first" or the letter filter */
		eng::countText(m, scale, (int)(STD_COUNT_R - STD_PATH_X) - 152, folders, games,
		               explorerSource == 4 ? eng::COUNT_HINT_NEWEST :
		               fileSortMode ? eng::COUNT_HINT_FOLDERS : eng::COUNT_HINT_AZ,
		               explorerSource == 4 ? fbRecentFilter : -1, fbCount, sizeof fbCount);
	cutLeft(fbPathShown, sizeof fbPathShown, fbPath, scale,
	        (int)STD_COUNT_R - m.width(fbCount, scale, m.ctx) - 16 - (int)STD_PATH_X);
	FB_TEXTS[0].text = fbPathShown;
	FB_TEXTS[0].ax = (float)STD_PATH_X;		FB_TEXTS[0].ay = (float)STD_PATH_Y;
	FB_TEXTS[0].scale = scale;				FB_TEXTS[0].centred = false;	FB_TEXTS[0].right = false;
	FB_TEXTS[1].text = fbCount;
	FB_TEXTS[1].ax = (float)STD_COUNT_R;	FB_TEXTS[1].ay = (float)STD_PATH_Y;
	FB_TEXTS[1].scale = scale;				FB_TEXTS[1].centred = false;	FB_TEXTS[1].right = true;
	page.freeTexts = FB_TEXTS;
	page.nFreeTexts = 2;
}

/* A page turn (an arrow, or L / R): the list moves and the focus goes past "." and "..". */
static void fbTurn(void *, int dir, bool wrap)
{
	eng::listTurn(FB_LIST, dir, wrap);
	fileBrowserFrame_FillPage();
}

static void fbPick(void *, int index)
{
	fileBrowserFrame_LoadFile(index);
}

static ButtonFunc fbTabFunc[NUM_TABS] = { Func_TabSD, Func_TabUSB, Func_TabDVD, Func_TabNetwork, Func_TabRecent };
static void fbTab(void *, int t)
{
	fbTabFunc[t]();
}

/* Down on the last row, or Up on the first, with more items beyond: the list moves by ONE row and
 * the focus stays on the same button (the engine's link would wrap round the window, or go to the
 * tab row). At the true ends nothing is done here, so the wrap and Up-to-tab stay as they were. */
static bool fbRowNav(menu::Button *self, int direction)
{
	const int slot = fbView ? fbView->itemRow(self) : -1;
	if (slot < 0 || !dir_entries || romFolderList)
		return false;
	const int dir = direction == menu::Focus::DIRECTION_DOWN ? 1 : direction == menu::Focus::DIRECTION_UP ? -1 : 0;
	const int top = ei_edge_top(FB_LIST.top, FB_LIST.slots, num_entries, slot, dir);
	if (top < 0)
		return false;
	FB_LIST.top = top;
	FB_LIST.focus = fbLastFocus = top + slot;
	fbView->relayout();
	return true;
}

FileBrowserFrame::FileBrowserFrame()
{
	fbView = new menu::glue::ListView(this, NULL, fileBrowserItem, fbPick, NULL, Func_ReturnFromFileBrowserFrame);
	fbView->useState(&FB_LIST);
	fbView->setHooks(fbPrepare, fbTurn);
	fbView->setTabs(FB_TABS, NUM_TABS, fbTab);	/* an empty list: every row is drawn inactive, at its box */
	for (int k = 0; k < NUM_FILE_SLOTS; k++)
		fbView->item(k)->setNavFunc(fbRowNav);
	setDefaultFocus(fbView->firstItem());
}

FileBrowserFrame::~FileBrowserFrame()
{
	delete fbView;
	fbView = NULL;
}

int fileBrowserMode = FileBrowserFrame::FILEBROWSER_LOADISO;

void FileBrowserFrame::activateSubmenu(int submenu)
{
	fileBrowserMode = submenu;
}

void fileBrowserFrame_OpenDirectory(fileBrowser_file* dir);
void fileBrowserFrame_Error(fileBrowser_file* dir, int error_code);
void fileBrowserFrame_FillPage();
void fileBrowserFrame_LoadFile(int i);
static int dir_comparator(const void* _x, const void* _y);

static void fbFill(menu::Graphics &gfx, int x, int y, int w, int h, GXColor c)
{
	gfx.setTEV(GX_PASSCLR);
	gfx.setColor(c);
	gfx.enableBlending(true);
	gfx.fillRect(x, y, w, h);
}

/* What the buttons do not draw: the folder icons, the arrow glyphs, the scroll thumb (to scale
 * between the arrows) and every text the layout holds (path, count, tag, note, title). */
void drawExplorerOverlay(menu::Graphics &gfx, const eng::Layout &L, const eng::ListState &S)	/* also AccountFrame.cpp */
{
	const GXColor gold = {254,213,95,255}, white = {255,255,255,255}, dim = {110,110,110,255};

	for (int k = 0; k < NUM_FILE_SLOTS; k++)
	{
		if (L.listItem[k] < 0)
			continue;
		const eng::Control &c = L.c[L.listItem[k]];
		if (!c.folder)
			continue;
		const int x = (int)STD_LIST_ICON_X, y = (int)c.rect.y + ((int)c.rect.h - 14) / 2;
		fbFill(gfx, x, y, 8, 4, gold);			/* the tab */
		fbFill(gfx, x, y + 3, 20, 11, gold);	/* the folder */
	}

	if (L.list >= 0 && L.slot[L.list][0] >= 0 && L.slot[L.list][1] >= 0)
	{
		const eng::Control &prev = L.c[L.slot[L.list][0]], &next = L.c[L.slot[L.list][1]];
		for (int p = 0; p < 2; p++)
		{
			const eng::Control &c = p ? next : prev;
			const int cx = (int)c.rect.x + (int)c.rect.w / 2, cy = (int)c.rect.y + (int)c.rect.h / 2;
			for (int r = 0; r < 4; r++)
			{
				const int w = 2 * (p ? 4 - r : r + 1);
				fbFill(gfx, cx - w / 2, cy - 4 + 2 * r, w, 2, c.focusable ? white : dim);
			}
		}
		const int y0 = (int)(prev.rect.y + prev.rect.h) + 2, y1 = (int)next.rect.y - 2, track = y1 - y0;
		const int x = (int)prev.rect.x;
		const eng::Thumb th = eng::scrollThumb(S, y0, y1);	/* by page: a drag maps back to it */
		fbFill(gfx, x + 5, y0, 2, track, (GXColor) {255,255,255,40});
		fbFill(gfx, x + 2, th.y, 8, th.h, th.live ? (GXColor) {200,200,200,255} : dim);
	}

	menu::IplFont &font = menu::IplFont::getInstance();
	for (int i = 0; i < L.n; i++)
	{
		const eng::Control &c = L.c[i];
		if (c.kind != eng::CTRL_TEXT || !c.text)
			continue;
		font.drawInit(c.role == eng::ROLE_TITLE ? white :
		              c.role == eng::ROLE_FREE_TEXT ? (GXColor) {40,40,40,255} : (GXColor) {200,200,200,255});
		font.drawString((int)c.rect.x, (int)c.rect.y, (char*)c.text, c.scale, false);
	}
}

/* ---- The letter strip (y 94-118) and the scroll thumb's drag ---------------------------------------
 * The strip is "#" and A-Z over the list. A letter with no name is dim; the focused row's letter is lit
 * (a cyan line). Pointer: A on a letter jumps to it. Pad: the C stick (the Classic's right stick) steps to
 * the next or previous letter that has names. A jump puts focus on that letter's FIRST row in list order
 * and shows its page. In the "folders first" sort a letter can have two runs (folders, then files): the
 * jump goes to the first of them, the folder. Pointer on the thumb: hold A and move to scroll; A in the
 * track above or below the thumb turns a page. */
static unsigned fbStripMask;		/* the letters with names, set whenever a page is filled */
char* filenameFromAbsPath(char* absPath);

static const char *fbNameAt(void *, int i)
{
	return romFolderList ? dir_entries[i].name : filenameFromAbsPath(dir_entries[i].name);
}

static void fbStripMaskUpdate(void)
{
	if (explorerSource == 4)		/* Recent: the lit letters are of the FULL list, filtered or not */
		fbStripMask = recent_cells(&fbRecent);
	else
		fbStripMask = dir_entries && num_entries > 0 ? eng::stripMask(num_entries, fbNameAt, NULL) : 0;
}

/* Recent's letter filter (the list is rebuilt from fbRecent; defined with Func_TabRecent). */
static void fbRecentSetFilter(int cell);

/* The item under focus (the list's own focus when no row has it, as on a tab); -1 for none. */
static int fbFocusedIndex(void)
{
	const eng::Layout &L = fbView->laid();
	for (int k = 0; k < NUM_FILE_SLOTS; k++)
		if (L.listItem[k] >= 0 && L.c[L.listItem[k]].focusable && fbView->item(k)->getFocus())
			return fbLastFocus = FB_LIST.top + k;
	/* no row has focus: a tab, or a message box (after a load) in front. Keep the last row */
	if (fbLastFocus >= 0 && fbLastFocus < num_entries)
		return fbLastFocus;
	return FB_LIST.focus;
}

static int fbCurrentCell(void)
{
	if (explorerSource == 4 && fbRecentFilter >= 0)		/* the filter's letter is the lit one */
		return fbRecentFilter;
	const int f = fbFocusedIndex();
	return dir_entries && f >= 0 && f < num_entries ? eng::stripLetter(fbNameAt(NULL, f)) : -1;
}

static void fbJumpToCell(int cell)
{
	if (explorerSource == 4)		/* Recent: a letter filters; the same letter again clears */
	{
		fbRecentSetFilter(recent_filter_press(&fbRecent, fbRecentFilter, cell));
		return;
	}
	const int idx = dir_entries ? eng::stripFirst(num_entries, fbNameAt, NULL, cell) : -1;
	if (idx < 0)
		return;
	FB_LIST.top = idx / FB_LIST.slots * FB_LIST.slots;
	fbFocusIdx = idx;
	fileBrowserFrame_FillPage();
	menu::Focus::getInstance().clearPrimaryFocus();
	/* No row is lit now. The letter strip reads the last focused row: it is the jumped one (else the
	 * old letter stayed lit). The list's own focus, which a tab's Down goes to, is the view's
	 * first row again (past "." and ".."), as after a page turn; the frame's default button, set
	 * by FillPage, stays on the jumped row so the first press lights that one. */
	fbLastFocus = idx;
	int first = 0;
	while (dir_entries && FB_LIST.top + first + 1 < num_entries &&
	       (!strcmp(filenameFromAbsPath(dir_entries[FB_LIST.top + first].name), ".") ||
	        !strcmp(filenameFromAbsPath(dir_entries[FB_LIST.top + first].name), "..")))
		first++;
	FB_LIST.focus = FB_LIST.top + first;
	fbView->relayout();
}

static void fbStripStep(int dir)
{
	const int k = eng::stripStep(fbStripMask, fbCurrentCell(), dir);
	if (k >= 0)
		fbJumpToCell(k);
	else if (explorerSource == 4 && dir < 0 && fbRecentFilter >= 0)
		fbRecentSetFilter(-1);		/* Recent: stepping back past the first letter clears the filter */
}

/* The C stick, edge-triggered with a repeat while held (20 frames, then every 6). */
static void fbPollCStick(void)
{
	int dir = 0;
	for (int i = 0; i < 4 && !dir; i++)
	{
		const int x = PAD_SubStickX(i);
		if (x >= 48) dir = 1; else if (x <= -48) dir = -1;
	}
#ifdef HW_RVL
	WPADData *w = menu::Input::getInstance().getWpad();
	for (int i = 0; i < 4 && !dir; i++)
	{
		if (w[i].err != WPAD_ERR_NONE || w[i].exp.type != WPAD_EXP_CLASSIC)
			continue;
		const float x = w[i].exp.classic.rjs.mag * sinf(w[i].exp.classic.rjs.ang * 3.14159265f / 180.0f);
		if (x > 0.45f) dir = 1; else if (x < -0.45f) dir = -1;
	}
	PADStatus *hid = (PADStatus*)(HID_MEM2_PAD_BUFF);
	for (int i = 0; i < 4 && !dir && hidControllerConnected; i++)
	{
		const int x = hid[i].substickX;
		if (x >= 43) dir = 1; else if (x <= -43) dir = -1;
	}
#endif
	static int held, frames;
	if (!dir) { held = 0; frames = 0; return; }
	if (dir != held) { held = dir; frames = 0; fbStripStep(dir); return; }
	frames++;
	if (frames >= 20 && (frames - 20) % 6 == 0)
		fbStripStep(dir);
}

/* The scroll track's ends (below the Prev arrow, above the Next arrow); false when there is no list. */
static bool fbTrack(int *y0, int *y1)
{
	const eng::Layout &L = fbView->laid();
	if (L.list < 0 || L.slot[L.list][0] < 0 || L.slot[L.list][1] < 0)
		return false;
	const eng::Control &p = L.c[L.slot[L.list][0]], &n = L.c[L.slot[L.list][1]];
	*y0 = (int)(p.rect.y + p.rect.h) + 2;
	*y1 = (int)n.rect.y - 2;
	return true;
}

/* The pointer: A on a letter, A on the thumb and move, A in the track beside the thumb. */
static void fbPollPointer(void)
{
#ifdef HW_RVL
	static bool prevA, dragging;
	static int grab;
	WPADData *w = menu::Input::getInstance().getWpad();
	int chan = -1;
	for (int i = 0; i < 4 && chan < 0; i++)
		if (w[i].ir.valid && w[i].err == WPAD_ERR_NONE)
			chan = i;
	if (chan < 0)
	{
		dragging = false;
		prevA = false;
		return;
	}
	const int px = screenMode ? (int)(w[chan].ir.x * 848 / 640 - 104) : (int)w[chan].ir.x;
	const int py = (int)w[chan].ir.y;
	const bool a = (w[chan].btns_h & WPAD_BUTTON_A) != 0;
	const bool edge = a && !prevA;
	prevA = a;
	if (!a)
	{
		dragging = false;
		return;
	}
	int y0, y1;
	if (!fbTrack(&y0, &y1))
		return;
	if (edge)
	{
		const int cell = eng::stripHit(px, py);
		if (cell >= 0)
		{
			if (fbStripMask & (1u << cell))
				fbJumpToCell(cell);
			return;
		}
		if (px >= (int)STD_SCROLL_X && px < (int)(STD_SCROLL_X + STD_SCROLL_W) && py >= y0 && py < y1)
		{
			const eng::Thumb th = eng::scrollThumb(FB_LIST, y0, y1);
			if (!th.live)
				return;
			if (py >= th.y && py < th.y + th.h)
			{
				dragging = true;
				grab = py - th.y;
			}
			else
			{
				eng::listTurn(FB_LIST, py < th.y ? -1 : 1, false);
				fileBrowserFrame_FillPage();
				menu::Focus::getInstance().clearPrimaryFocus();
			}
		}
	}
	else if (dragging)
	{
		const int top = eng::scrollDragPage(FB_LIST, y0, y1, py - grab) * FB_LIST.slots;
		if (top != FB_LIST.top)
		{
			FB_LIST.top = top;
			fileBrowserFrame_FillPage();
			menu::Focus::getInstance().clearPrimaryFocus();
		}
	}
#endif
}

static void drawLetterStrip(menu::Graphics &gfx)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	const int cur = fbCurrentCell();
	const float scale = (float)STD_STRIP_PX / 24.0f;
	const int y = (int)STD_STRIP_Y, h = (int)STD_STRIP_H;
	for (int k = 0; k < eng::STRIP_CELLS; k++)
	{
		int x, w;
		eng::stripCell(k, &x, &w);
		const bool have = (fbStripMask >> k) & 1u;
		fbFill(gfx, x, y, w, h, k == cur ? (GXColor) {92,92,100,255} : (GXColor) {58,58,62,255});
		fbFill(gfx, x, y, w, 1, (GXColor) {255,255,255,40});
		char s[2] = { eng::stripChar(k), 0 };
		font.drawInit(have ? (GXColor) {245,245,245,255} : (GXColor) {140,140,140,255});
		font.drawString(x + w / 2, y + h / 2, s, scale, true);	/* centered = centered on y too (checked in a capture) */
		if (k == cur)
			fbFill(gfx, x + 3, y + 20, w - 6, 2, (GXColor) {61,181,204,255});
	}
}

#ifdef HW_RVL
extern GXTexRegion texCacheRegionS[8];
#endif
/* The cover slot (STD_COVER_*): the cover of the focused game, when its serial is known (the Recent
 * record has it) and the cover pack (<dev>:/wiistation/covers/index.txt, SD first, then USB) has a box
 * picture for it. The file is read only after the focus has rested on the row COVER_REST frames, so
 * scrolling never decodes a picture. Nothing is drawn when there is no cover. */
#define COVER_REST 12
static char covSerial[12];		/* the serial covState is about */
static int covState;			/* 0 resting / not tried, 1 texture loaded, 2 no cover */
static int covRest;				/* frames covSerial has been focused */
#ifdef HW_RVL
static GXTexObj covTex;
#endif
static void fbCoverFree(void)
{
#ifdef HW_RVL
	cover_art_free();
#endif
	covSerial[0] = 0;
	covState = 0;
	covRest = 0;
}
static bool fbCoverLoad(const char *serial)
{
#ifdef HW_RVL
	static const char *const dirs[] = { "sd:/wiistation/covers", "usb:/wiistation/covers" };
	for (unsigned d = 0; d < sizeof dirs / sizeof dirs[0]; d++)
	{
		char idx[48], path[112];
		cover_row r;
		snprintf(idx, sizeof idx, "%s/index.txt", dirs[d]);
		if (cover_index_find(idx, serial, &r) && r.box[0] &&
		    !cover_index_path(path, sizeof path, dirs[d], r.box) &&
		    cover_art_load(path, &covTex, STD_COVER_W, STD_COVER_H))
			return true;
	}
#else
	(void)serial;
#endif
	return false;
}
static void fbCoverDraw(menu::Graphics &gfx, const char *serial)
{
	if (!serial[0])
	{
		if (covSerial[0])
			fbCoverFree();
		return;
	}
	if (strncmp(serial, covSerial, sizeof covSerial - 1))
	{
		fbCoverFree();
		snprintf(covSerial, sizeof covSerial, "%s", serial);
	}
	if (covState == 0 && ++covRest >= COVER_REST)
		covState = fbCoverLoad(covSerial) ? 1 : 2;
#ifdef HW_RVL
	if (covState != 1)
		return;
	fbFill(gfx, STD_COVER_X - 1, STD_COVER_Y - 1, STD_COVER_W + 2, STD_COVER_H + 2, (GXColor) {120,120,120,255});
	GX_InvalidateTexRegion(&texCacheRegionS[0]);			/* the font's region: other texels */
	GX_LoadTexObjPreloaded(&covTex, &texCacheRegionS[0], GX_TEXMAP0);	/* outside Begin/End */
	gfx.setColor((GXColor) {255,255,255,255});
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, STD_COVER_X, STD_COVER_Y, STD_COVER_W, STD_COVER_H, 0, 1, 0, 1);
#endif
}

/* The facts strip under the list: the focused row's full name, then Format, Size, Region (first
 * column) and Last played, Achievements, Saves (second). What a file shows comes from its name and the
 * lazy read (fbPumpInfo reads the focused row first); a value not known yet is "-". Last played is
 * "Never" until the Recent record (slice 6), and Achievements and Saves are "-" until that record
 * stores the game's serial and its set result. */
static void drawFactsStrip(menu::Graphics &gfx)
{
	menu::IplFont &font = menu::IplFont::getInstance();
	const eng::Metrics &m = menu::glue::METRICS;
	eng::FactsBox b;
	eng::factsLayout(b);
	fbFill(gfx, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, (GXColor) {45,45,45,168});
	fbFill(gfx, b.x0, b.y0, b.x1 - b.x0, 1, (GXColor) {255,255,255,40});

	const int idx = fbFocusedIndex();
	if (!dir_entries || idx < 0 || idx >= num_entries)
	{
		fbCoverDraw(gfx, "");
		return;						/* an empty list: the panel only */
	}
	const char *coverSerial = "";		/* of the focused game, when the Recent record has it */
	const fileBrowser_file &e = dir_entries[idx];
	const char *name = romFolderList ? e.name : filenameFromAbsPath(e.name);
	const bool isDir = (e.attr & FILE_BROWSER_ATTR_DIR) != 0;
	const bool up = isDir && !romFolderList && !strcmp(name, "..");
	const bool done = fbInfo && fbInfo[idx].done;

	char buf[128], val[2][3][96];
	const char *keys[6] = { "Format", "Size", "Region", "Last played", "Achievements", "Saves" };
	const char *vals[6] = { "-", "-", "-", "-", "-", "-" };
	/* A folder row that holds a game (fbPumpFolderGame found one) shows that game's facts: Game (its
	 * file name), Size (the format folded in, "CHD, 624 MB"), Region | Last played, Achievements,
	 * Saves, all of the game's own path. The title stays the folder's name. */
	FbGame *g = isDir && !up && !romFolderList ? fbGameFind(idx) : NULL;
	if (g && g->state != 1)
		g = NULL;
	if (isDir && !g)
	{
		keys[0] = "Type";
		vals[0] = up ? "Up one folder" : "Folder";
	}
	else
	{
		const char *fpath = g ? g->path : e.name;
		const char *fname = g ? filenameFromAbsPath(g->path) : name;
		if (g)
		{
			char f[40], s[24];
			keys[0] = "Game";
			snprintf(val[0][0], sizeof val[0][0], "%s", fname);
			ei_fact_format(fpath, g->discs, f, sizeof f);
			ei_fact_size(g->size, s, sizeof s);
			if (strcmp(s, "-"))
				snprintf(val[0][1], sizeof val[0][1], "%s, %s", f, s);
			else
				snprintf(val[0][1], sizeof val[0][1], "%s", f);
			ei_fact_region(fname, val[0][2], sizeof val[0][2]);
			if (!strcmp(val[0][2], "Unknown"))		/* the tag may be on the folder instead */
				ei_fact_region(name, val[0][2], sizeof val[0][2]);
		}
		else
		{
			ei_fact_format(e.name, done ? fbInfo[idx].discs : 0, val[0][0], sizeof val[0][0]);
			ei_fact_size(done ? e.size : 0, val[0][1], sizeof val[0][1]);
			ei_fact_region(name, val[0][2], sizeof val[0][2]);
		}
		/* Last played, Achievements and Saves come from the Recent record of this path */
		if (!fbRecentLoaded)
			fbRecentLoad();			/* once; explorerOpen and a recorded load read it again */
		const int ri = recent_find(&fbRecent, fpath);
		if (ri >= 0)
		{
			const recent_entry &re = fbRecent.e[ri];
			coverSerial = re.serial;
			recent_when(re.time, (long long)time(NULL), val[1][0], sizeof val[1][0]);
			ei_fact_ach(re.set, val[1][1], sizeof val[1][1]);
			ei_fact_saves(fbSavesCount(idx, re.serial), val[1][2], sizeof val[1][2]);
		}
		else
		{
			snprintf(val[1][0], sizeof val[1][0], "Never");
			ei_fact_ach(0, val[1][1], sizeof val[1][1]);
			ei_fact_saves(-1, val[1][2], sizeof val[1][2]);
		}
		vals[0] = val[0][0];  vals[1] = val[0][1];  vals[2] = val[0][2];
		vals[3] = val[1][0];  vals[4] = val[1][1];  vals[5] = val[1][2];
	}

	fbCoverDraw(gfx, coverSerial);		/* before the text: the font's texture is set again by drawInit */
	const float tScale = (float)STD_FACTS_TITLE_PX / 24.0f;
	font.drawInit((GXColor) {255,255,255,255});
	font.drawString(b.titleX, b.titleY, (char*)eng::fitText(m, up ? "Up one folder" : name, tScale, b.titleRoom,
	                                                         buf, sizeof buf), tScale, false);
	const float scale = (float)STD_FACTS_PX / 24.0f;
	for (int k = 0; k < STD_FACTS_N; k++)
	{
		const eng::FactsCell &c = b.cell[k];
		if (isDir && !g && k > 0)
			break;
		font.drawInit((GXColor) {200,200,200,255});
		font.drawString(c.keyR - m.width(keys[k], scale, m.ctx), c.y, (char*)keys[k], scale, false);
		font.drawInit((GXColor) {245,245,245,255});
		font.drawString(c.valX, c.y, (char*)eng::fitText(m, vals[k], scale, c.valRoom, buf, sizeof buf), scale, false);
	}
}

#ifdef EXPLORER_TIMING
/* A timing build (EXPLORER_TIMING defined by hand at the top of this file and of
 * fileBrowser-libfat.c): one line per folder shown and per row read, to sd:/wiistation/explorer_time.log */
static void explorerLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void explorerLog(const char *fmt, ...)
{
	FILE *f = fopen("sd:/wiistation/explorer_time.log", "ab");
	if (!f) return;
	va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
	fclose(f);
}
#define TICKS_US(t) ((unsigned long long)(t) * 1000ULL / 60750ULL)
#endif

char* filenameFromAbsPath(char* absPath);

static int fbReadAt(void *ctx, unsigned long off, void *buf, unsigned n)
{
	FILE *f = (FILE*)ctx;
	if (fseek(f, (long)off, SEEK_SET))
		return -1;
	return (int)fread(buf, 1, n, f);
}

/* Reads what row i needs: the size where the directory read gave none (DVD, network), and the disc
 * count of a PBP or M3U. True when the row's note changed. */
static bool fbReadInfo(int i)
{
	fileBrowser_file &e = dir_entries[i];
	fbInfo[i].done = 1;
	if (e.attr & FILE_BROWSER_ATTR_DIR)
		return false;
#ifdef EXPLORER_TIMING
	const unsigned long long t0 = gettime();
#endif
	if (e.size == 0)		/* no directory read gives sizes; a stat per file for the whole folder cost
	                         * 0.8 s on 450 files (libfat looks each name up from the folder's start) */
	{
		struct stat st;
		if (!stat(e.name, &st))
			e.size = st.st_size > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (unsigned int)st.st_size;
	}
	int discs = 1;
	if (ei_wants_disc_count(e.name))
	{
		discs = 0;
		FILE *f = fopen(e.name, "rb");
		if (f)
		{
			if (!strcmp(ei_format(e.name), "M3U"))
			{
				char text[2048];
				discs = ei_m3u_discs(text, (unsigned)fread(text, 1, sizeof text, f));
			}
			else
				discs = ei_pbp_discs(fbReadAt, f);
			fclose(f);
		}
	}
	fbInfo[i].discs = discs > 255 ? 255 : discs < 0 ? 0 : discs;
#ifdef EXPLORER_TIMING
	explorerLog("row %d size %u discs %d us %llu %s\n", i, e.size, discs, TICKS_US(gettime() - t0),
	            filenameFromAbsPath(e.name));
#endif
	return discs >= 2;
}

/* ei_find_game's folder lister: the same read as showFolder (readDir, no "." and "..", the
 * explorer's sort), so the first game is the one the folder lists first. The list is kept until the
 * next call (the entries' names are used by the search before then) and freed by the call with no
 * folder. */
struct FbFolderLs { fileBrowser_file *list; };
static int fbFolderList(void *ctx, const char *dir, ei_ent *out, int max)
{
	FbFolderLs *c = (FbFolderLs*)ctx;
	if (c->list) { free(c->list); c->list = NULL; }
	if (!dir)
		return 0;
	fileBrowser_file d;
	memset(&d, 0, sizeof d);
	snprintf(d.name, sizeof d.name, "%s", dir);
	normPath(d.name);
	fileBrowser_file *list = NULL;
	int n = isoFile_readDir(&d, &list);
	if (n < 0)
	{
		if (list) free(list);
		return n;
	}
	int kept = 0;
	for (int i = 0; i < n; i++)
	{
		const char *nm = filenameFromAbsPath(list[i].name);
		if (strcmp(nm, ".") && strcmp(nm, ".."))
			list[kept++] = list[i];
	}
	qsort(list, kept, sizeof(fileBrowser_file), dir_comparator);
	c->list = list;
	const int m = kept < max ? kept : max;
	for (int i = 0; i < m; i++)
	{
		out[i].path = list[i].name;
		out[i].isDir = (list[i].attr & FILE_BROWSER_ATTR_DIR) != 0;
		out[i].size = list[i].size;
	}
	return m;
}

/* Looks for the first game inside the focused folder row, once the focus has rested on it for 12
 * frames (so a fast scroll does not list a folder per row). Depth 3, 64 entries at most; a slow
 * source lists fewer folders (DVD 3, network 2, others 6). One search per call; true when it ran,
 * so the pump does nothing else that frame. Not for "..", the root's folder list or Recent. */
static bool fbPumpFolderGame(int focus)
{
	static int restIdx = -1, restFrames;
	if (focus != restIdx)
	{
		restIdx = focus;
		restFrames = 0;
		return false;
	}
	if (restFrames < 12)
	{
		restFrames++;
		return false;
	}
	if (focus < 0 || focus >= num_entries || explorerSource == 4 || romFolderList || fbGameFind(focus))
		return false;
	const fileBrowser_file &e = dir_entries[focus];
	if (!(e.attr & FILE_BROWSER_ATTR_DIR) || !strcmp(filenameFromAbsPath((char*)e.name), ".."))
		return false;

	FbGame *g = &fbGames[fbGamesNext++ % FB_GAME_SLOTS];
	memset(g, 0, sizeof *g);
	g->idx = focus;
	FbFolderLs ls = { NULL };
	unsigned size = 0;
	const int reads = explorerSource == 3 ? 2 : explorerSource == 2 ? 3 : 6;
	const int ok = ei_find_game(fbFolderList, &ls, e.name, 3, 64, reads, g->path, sizeof g->path, &size);
	if (ls.list) free(ls.list);
	if (!ok)
	{
		g->state = 2;
		return true;
	}
	if (size == 0)			/* the directory read gives no sizes */
	{
		struct stat st;
		if (!stat(g->path, &st))
			size = st.st_size > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (unsigned int)st.st_size;
	}
	g->size = size;
	int discs = 1;
	if (ei_wants_disc_count(g->path))
	{
		discs = 0;
		FILE *f = fopen(g->path, "rb");
		if (f)
		{
			if (!strcmp(ei_format(g->path), "M3U"))
			{
				char text[2048];
				discs = ei_m3u_discs(text, (unsigned)fread(text, 1, sizeof text, f));
			}
			else
				discs = ei_pbp_discs(fbReadAt, f);
			fclose(f);
		}
	}
	g->discs = discs > 255 ? 255 : discs < 0 ? 0 : discs;
	g->state = 1;
	return true;
}

/* Once per frame: the first shown row not read yet. Folders need nothing and cost nothing. When a
 * note appears the layout is made again (not FillPage: that resets the focus). */
static void fbPumpInfo(void)
{
	if (!dir_entries || !fbInfo || romFolderList)
		return;
	const int slots = FB_LIST.slots > 0 ? FB_LIST.slots : 1;
	int end = FB_LIST.top + slots;
	if (end > num_entries)
		end = num_entries;
	const int focus = fbFocusedIndex();		/* the facts strip shows it: read it before the rest */
	if (fbPumpFolderGame(focus))
		return;
	for (int n = -1; n < end - FB_LIST.top; n++)
	{
		const int i = n < 0 ? focus : FB_LIST.top + n;
		if (i < FB_LIST.top || i >= end)
			continue;
		if (fbInfo[i].done)
			continue;
		const fileBrowser_file &e = dir_entries[i];
		if ((e.attr & FILE_BROWSER_ATTR_DIR) || fbRecentDim(i) ||		/* no read on a missing device */
		    (e.size != 0 && !ei_wants_disc_count(e.name)))
		{
			fbInfo[i].done = 1;		/* a folder, or a one-disc image whose size is known: no read */
			fbInfo[i].discs = 1;
			continue;
		}
		if (fbReadInfo(i))
		{
			fbView->relayout();
		}
		return;
	}
}

void FileBrowserFrame::drawChildren(menu::Graphics &gfx)
{
	if(isVisible())
	{
		fbPollCStick();		/* the letter strip: C stick steps, pointer clicks, thumb drag */
		fbPollPointer();
		/* L / R turn the page (the view, through fbTurn); the sort key changes the order */
		const menu::glue::PageInput in = fbView->update();
		if (in.sort)
		{
			fileSortMode ^= 1;
			qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator); fbInfoClear();
			FB_LIST.top = 0;
			fileBrowserFrame_FillPage();
			menu::Focus::getInstance().clearPrimaryFocus();
		}

		//Draw buttons
		menu::ComponentList::const_iterator iteration;
		for (iteration = componentList.begin(); iteration != componentList.end(); ++iteration)
		{
			(*iteration)->draw(gfx);
		}
		fbView->drawOverlay(gfx);
		drawLetterStrip(gfx);
		drawFactsStrip(gfx);
		fbPumpInfo();		/* after the list is drawn: the next shown row's disc count */
	}
}

extern MenuContext *pMenuContext;

void Func_ReturnFromFileBrowserFrame()
{
	if (!explorerUp())		/* up one folder; at a source's root, back to the page that opened the explorer */
		pMenuContext->setActiveFrame(fileBrowserMode == FileBrowserFrame::FILEBROWSER_SWAPCD ?
		                             MenuContext::FRAME_CURRENTROM : MenuContext::FRAME_MAIN);
}

/* A tab loads its source with the Load ROM page's own function (which sets explorerSource). */
static void Func_TabSD()      { Func_LoadFromSD(); }
static void Func_TabUSB()     { Func_LoadFromUSB(); }
static void Func_TabDVD()     { Func_LoadFromDVD(); }
static void Func_TabNetwork() { Func_LoadFromSamba(); }

/* Recent: the last games loaded, newest first (recent.ini). The rows are the entries' own paths, so
 * the same machinery shows them (names, tag, letter strip); a row on a device that is not there is
 * dimmed (fileBrowserItem). */
/* dir_entries from fbRecent: all of it, or only the games of the filter's letter (list order kept). */
static void fbRecentBuild(void)
{
	int idx[RECENT_MAX];
	if (dir_entries) { free(dir_entries); dir_entries = NULL; }
	num_entries = 0;
	const int n = recent_filter_list(&fbRecent, fbRecentFilter, idx, RECENT_MAX);
	if (n > 0)
		dir_entries = (fileBrowser_file*)calloc(n, sizeof(fileBrowser_file));
	if (dir_entries)
		for (int i = 0; i < n; i++)
		{
			snprintf(dir_entries[i].name, sizeof dir_entries[i].name, "%s", fbRecent.e[idx[i]].path);
			dir_entries[i].attr = 0;
			num_entries++;
		}
	fbInfoReset();
}

/* A letter pressed on the strip while Recent is up (-1 = no filter): the list shows only that
 * letter's games, focus on row 0, the page from the top; the lit letters stay those of the full list. */
static void fbRecentSetFilter(int cell)
{
	if (cell == fbRecentFilter)
		return;
	fbRecentFilter = cell;
	fbRecentBuild();
	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	fbFocusIdx = 0;
	fileBrowserFrame_FillPage();
	menu::Focus::getInstance().clearPrimaryFocus();
}

static void Func_TabRecent()
{
	explorerSource = 4;
	fbRecentFilter = -1;		/* opening the tab (or the tab again) clears the filter */
	fbRecentLoad();
	fbSrcProbe();
	fbRecentBuild();
	fbDepth = 0;
	fbUpRow = false;
	fbRootIsList = false;
	romFolderList = false;
	snprintf(fbPath, sizeof fbPath, "Recently played");
	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	fileBrowserFrame_FillPage();
	menu::Focus::getInstance().clearPrimaryFocus();
}


char* filenameFromAbsPath(char* absPath)
{
	char* filename = absPath;
	// Here we want to extract from the absolute path
	//   just the filename
	// First we move the pointer all the way to the end
	//   of the the string so we can work our way back
	while( *filename ) ++filename;
	// Now, just move it back to the last '/' or the start
	//   of the string
	while( filename != absPath && (*(filename-1) != '\\' && *(filename-1) != '/'))
		--filename;
	return filename;
}

/* The engine asks for each shown item: its label, and whether it is a folder. */
static void fileBrowserItem(void *, int index, eng::ListItem &out)
{
	out.text = romFolderList ? dir_entries[index].name : filenameFromAbsPath(dir_entries[index].name);
	out.folder = (dir_entries[index].attr & FILE_BROWSER_ATTR_DIR) != 0;
	out.tag = NULL;
	out.note = NULL;
	out.dim = false;
	if (!out.folder && !romFolderList)
	{
		out.tag = ei_format(dir_entries[index].name);
		char *note = fbNote[index % 16];
		const int discs = fbInfo ? fbInfo[index].discs : 0;
		if (explorerSource == 4)
		{
			/* "today", "yesterday" or the date, then the disc count; a dimmed row names its device */
			const recent_entry *re = fbRecentOf(dir_entries[index].name);
			char when[16] = "";
			if (re)
				recent_when(re->time, (long long)time(NULL), when, sizeof when);
			out.dim = fbRecentDim(index);
			if (out.dim)
			{
				char src[8];
				snprintf(src, sizeof src, "%s", re->src);
				for (char *c = src; *c; c++) *c = (char)toupper((unsigned char)*c);
				snprintf(note, sizeof fbNote[0], "%s, %s", src, when);
			}
			else if (discs >= 2)
				snprintf(note, sizeof fbNote[0], "%s, %d discs", when, discs);
			else
				snprintf(note, sizeof fbNote[0], "%s", when);
			out.note = note;
		}
		else if (discs >= 2)
		{
			snprintf(note, sizeof fbNote[0], "%d discs", discs);
			out.note = note;
		}
	}
}

int loadISO(fileBrowser_file*);
int loadISOSwap(fileBrowser_file*);
extern "C" unsigned int cdrIsoMultidiskCount, cdrIsoMultidiskSelect;   /* cdriso.c: the discs of a PBP */

static int dir_comparator(const void* _x, const void* _y){
	const fileBrowser_file* x = (const fileBrowser_file*)_x;
	const fileBrowser_file* y = (const fileBrowser_file*)_y;
	if (explorerSource == 4)		/* Recent keeps newest first, whatever the sort toggle says */
		return recent_find(&fbRecent, x->name) - recent_find(&fbRecent, y->name);
	int xIsDir = x->attr & FILE_BROWSER_ATTR_DIR;
	int yIsDir = y->attr & FILE_BROWSER_ATTR_DIR;
	// Directories go on top, otherwise alphabetical
	if(fileSortMode && xIsDir != yIsDir)
		return yIsDir - xIsDir;
	else
		return strcasecmp(x->name, y->name);
}

/* Repeated slashes after the device ("sd://x") become one. */
static void normPath(char *p)
{
	char *w = p;
	for (const char *r = p; *r; r++)
		if (!(*r == '/' && w > p && w[-1] == '/'))
			*w++ = *r;
	*w = 0;
}

static void rememberFolder(void)
{
	if (explorerSource > 3)
		return;
	snprintf(tabMemory[explorerSource].path, sizeof tabMemory[0].path, "%s", fbPath);
	tabMemory[explorerSource].depth = fbDepth;
}

/* No list at all: a source that could not be read, or one with nothing to show. */
static void emptyList(const char *path)
{
	if (dir_entries) { free(dir_entries); dir_entries = NULL; }
	num_entries = 0;
	fbInfoReset();
	romFolderList = false;
	fbUpRow = false;
	fbDepth = 0;
	snprintf(fbPath, sizeof fbPath, "%s", path);
	FB_LIST.count = 0;
	FB_LIST.top = 0;
	fileBrowserFrame_FillPage();
	menu::Focus::getInstance().clearPrimaryFocus();
}

/* After going up: the item that is the folder just left gets the focus, on its page (as in
 * Windows). Called with the list built and FB_LIST.count/top set, before FillPage. */
static void focusCameFrom(void)
{
	char from[256];
	snprintf(from, sizeof from, "%s", fbCameFrom);
	fbCameFrom[0] = 0;
	fbFocusIdx = -1;
	if (!from[0] || !dir_entries)
		return;
	normPath(from);
	for (int i = 0; i < num_entries; i++)
	{
		char nm[256];
		snprintf(nm, sizeof nm, "%s", dir_entries[i].name);
		normPath(nm);
		if (!strcmp(nm, from))
		{
			fbFocusIdx = i;
			const int slots = FB_LIST.slots > 0 ? FB_LIST.slots : 1;
			FB_LIST.top = (i / slots) * slots;
			return;
		}
	}
}

/* Shows one folder, at a depth. False when it cannot be read: a box says why (not when quiet), the
 * explorer stays where it is on its tabs, and a source's root is shown as an empty list. `path`
 * may point into dir_entries: it is copied before anything is freed. */
static bool showFolder(const char *path, int depth, bool quiet)
{
	fileBrowser_file dir;
	memset(&dir, 0, sizeof dir);
	snprintf(dir.name, sizeof dir.name, "%s", path);
	normPath(dir.name);

	fileBrowser_file *list = NULL;
#ifdef EXPLORER_TIMING
	const unsigned long long tRead = gettime();
#endif
	int n = isoFile_readDir(&dir, &list);
#ifdef EXPLORER_TIMING
	{
		const unsigned long long tk = gettime() - tRead;
		explorerLog("folder %s files %d readDir us %llu\n", dir.name, n, TICKS_US(tk));
	}
#endif
	if (n < 0)
	{
		if (list) free(list);
		if (!quiet)
		{
			if (depth == 0)
				emptyList(dir.name);
			fileBrowserFrame_Error(&dir, n);
		}
		return false;
	}

	/* The explorer has no "." row and makes its own ".." row, whatever the device reports */
	int kept = 0;
	for (int i = 0; i < n; i++)
	{
		const char *nm = filenameFromAbsPath(list[i].name);
		if (strcmp(nm, ".") && strcmp(nm, ".."))
			list[kept++] = list[i];
	}
	n = kept;
	qsort(list, n, sizeof(fileBrowser_file), dir_comparator);

	const int up = depth > 0 ? 1 : 0;
	fileBrowser_file *all = (fileBrowser_file*)malloc((n + up + 1) * sizeof(fileBrowser_file));
	if (!all)
	{
		if (list) free(list);
		return false;
	}
	if (up)
	{
		memset(&all[0], 0, sizeof all[0]);
		snprintf(all[0].name, sizeof all[0].name, "%s/..", dir.name);
		all[0].attr = FILE_BROWSER_ATTR_DIR;
	}
	if (n > 0)
		memcpy(all + up, list, n * sizeof(fileBrowser_file));
	if (list) free(list);

	if (dir_entries) free(dir_entries);
	dir_entries = all;
	num_entries = n + up;
	fbInfoReset();
	romFolderList = false;
	fbUpRow = up != 0;
	fbDepth = depth;
	snprintf(fbPath, sizeof fbPath, "%s", dir.name);
	rememberFolder();

	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	focusCameFrom();
	fileBrowserFrame_FillPage();
	return true;
}

/* The root, when it is several RomDir folders: their whole paths, to choose from. */
static void showRootList(void)
{
	if (dir_entries) { free(dir_entries); dir_entries = NULL; }
	dir_entries = (fileBrowser_file*)malloc(fbRootListN * sizeof(fileBrowser_file));
	if (!dir_entries) { num_entries = 0; fbInfoReset(); return; }
	memcpy(dir_entries, fbRootList, fbRootListN * sizeof(fileBrowser_file));
	num_entries = fbRootListN;
	fbInfoReset();
	romFolderList = true;
	fbUpRow = false;
	fbDepth = 0;
	snprintf(fbPath, sizeof fbPath, "%s", fbRootDevice);
	rememberFolder();
	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	focusCameFrom();
	fileBrowserFrame_FillPage();
}

/* The folder this source was left in, if it was below the root and can still be read. Not on an
 * autoboot, which names its own folder. */
static bool restoreFolder(void)
{
	if (Autoboot || explorerSource > 3)
		return false;
	const TabMemory &t = tabMemory[explorerSource];
	return t.depth > 0 && t.path[0] && showFolder(t.path, t.depth, true);
}

/* ".." (and B): the parent of the folder shown; from a folder of the RomDir list, the list. */
static bool explorerUp(void)
{
	if (explorerSource > 3 || fbDepth <= 0)
		return false;
	snprintf(fbCameFrom, sizeof fbCameFrom, "%s", fbPath);
	if (fbDepth == 1 && fbRootIsList)
	{
		showRootList();
		fbCameFrom[0] = 0;
		menu::Focus::getInstance().clearPrimaryFocus();
		return true;
	}
	char p[256];
	snprintf(p, sizeof p, "%s", fbPath);
	size_t len = strlen(p);
	while (len > 1 && p[len - 1] == '/')
		p[--len] = 0;
	char *slash = strrchr(p, '/');
	if (slash)
		*slash = 0;
	len = strlen(p);
	if (len && p[len - 1] == ':' && len + 1 < sizeof p)		/* "sd:" is "sd:/" */
		strcat(p, "/");
	showFolder(p, fbDepth - 1, false);
	fbCameFrom[0] = 0;
	menu::Focus::getInstance().clearPrimaryFocus();	/* the default focus (the folder left) takes over */
	return true;
}

/* A source's root: the folder the loader names (autoboot, DVD, SMB when not mounted), or the
 * folder this source was left in. */
void fileBrowserFrame_OpenDirectory(fileBrowser_file* dir)
{
	fbRootIsList = false;
	if (restoreFolder())
		return;
	showFolder(dir->name, 0, false);
}

/* Load from SD / Load from USB: the folders of RomDirSD or RomDirUSB ("/wiistation/isos,
 * /roms/psx") that exist on that device ("sd:", "usb:"). One is the root and opens straight away;
 * several are listed, by whole path, to choose from; none opens the device's root (it used to open
 * /wiistation/isos and say it was missing). An item may also name its device ("sd:/roms"). */
void fileBrowserFrame_OpenRomFolders(fileBrowser_file* deviceDefault, const char *device)
{
	fileBrowser_file found[ROM_DIRS_MAX];
	int n = 0;
	struct stat st;
	char item[CONFIG_ITEM_SIZE];
	const char *list = !strcasecmp(device, "usb:") ? romDirUSB :
	                   !strcasecmp(device, "smb:") ? romDirSMB : romDirSD;

	memset(found, 0, sizeof(found));
	while (n < ROM_DIRS_MAX && config_list_next(&list, item, sizeof(item)))
	{
		char *path = found[n].name;
		if (strchr(item, ':'))
			snprintf(path, sizeof(found[n].name), "%s", item);
		else
			snprintf(path, sizeof(found[n].name), "%s%s%s", device, item[0] == '/' ? "" : "/", item);
		if (!stat(path, &st) && S_ISDIR(st.st_mode))
			found[n++].attr = FILE_BROWSER_ATTR_DIR;
	}
	if (n == 0)   /* no game folder there: the device's root (user, 2026-10-20) */
		snprintf(deviceDefault->name, sizeof(deviceDefault->name), "%s/", device);
	if (n <= 1)
	{
		if (n == 1)
			*deviceDefault = found[0];
		fileBrowserFrame_OpenDirectory(deviceDefault);
		return;
	}
	fbRootIsList = true;
	memcpy(fbRootList, found, sizeof(found));
	fbRootListN = n;
	snprintf(fbRootDevice, sizeof fbRootDevice, "%s", device);
	if (!restoreFolder())
		showRootList();
}

void fileBrowserFrame_Error(fileBrowser_file* dir, int error_code)
{
	char feedback_string[256];
	/* The box is 560px wide and does not wrap, so each line breaks by hand.
	 * scripts/menu_text_width.py measures them. */
	if(error_code == SMB_NETINITERR) {
  	strcpy(feedback_string,"Still connecting to the network.\nTry again in a moment.");
	}
	else if(error_code == SMB_SMBCFGERR) {
  	strcpy(feedback_string,"SMB is not configured.\nSet smbsharename and smbipaddr\nin settings.ini");
	}
	else if(error_code == SMB_SMBRETRY) {
  	strcpy(feedback_string,"Still connecting to the share.\nTry again in a moment.");
	}
	else if(error_code == DVD_ERR_NO_ACCESS) {
	strcpy(feedback_string,"WiiStation cannot use the disc drive.\nStart it from the Homebrew Channel.");
	}
	else if(error_code == DVD_ERR_NO_DRIVE) {
	strcpy(feedback_string,"The disc drive did not answer.");
	}
	else if(error_code == DVD_ERR_NO_DISC) {
	strcpy(feedback_string,"There is no disc in the drive.");
	}
	else if(error_code == DVD_ERR_DRIVE_NO_DVD) {
	strcpy(feedback_string,"This Wii's disc drive cannot read\nDVDs: only drives made up to\nmid-2008 can. Load your games\nfrom SD, USB or a network share.");
	}
	else if(error_code == DVD_ERR_UNREADABLE) {
	strcpy(feedback_string,"This disc cannot be read. Use a data\nDVD-R. A Wii cannot read CDs, and\nmany Wiis made after 2008 cannot\nread DVD-R discs.");
	/* the drive's own code, so a report says why (fileBrowser-DVD.c) */
	if (fileBrowser_DVD_drive_error() == 0xFFFFFE)   /* IOS is still busy with the disc */
		strcat(feedback_string, "\nThe drive is stuck on it: take it out.");
	else if (fileBrowser_DVD_drive_error() == 0xFFFFFF)
		strcat(feedback_string, "\nThe drive did not answer in time.");
	else if (fileBrowser_DVD_drive_error())
		snprintf(feedback_string + strlen(feedback_string), sizeof(feedback_string) - strlen(feedback_string),
		         "\nDrive error %06X.", fileBrowser_DVD_drive_error());
	}
	else if(error_code == SMB_SMBERR) {
	/* the reason libsmb2 gave, cut to the box: "connect: ..." */
	snprintf(feedback_string, sizeof(feedback_string), "Cannot connect to the share.\n%.44s\n%.44s",
	         smb2dev_error(), strlen(smb2dev_error()) > 44 ? smb2dev_error() + 44 : "");
	}
	//set first entry to read 'error' and return to main menu
	else if(dir->name[0])   // name is an array; testing it as a pointer was always true
	  sprintf(feedback_string,"Error opening directory \"%s\"",&dir->name[0]);
	else
	  strcpy(feedback_string,"An error occured");
	menu::MessageBox::getInstance().setMessage(feedback_string);
	/* The explorer stays, on its tabs (the caller has emptied or kept the list). An autoboot has
	 * no one to look at the explorer: it goes back to Main as it always did. */
	if (Autoboot)
		pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

void fileBrowserFrame_FillPage()
{
	fbStripMaskUpdate();
	//set default focus past "." and ".." entries
	int default_index = 0;
	while ( dir_entries && num_entries > 0 && (!strcmp(filenameFromAbsPath(dir_entries[default_index+FB_LIST.top].name),".") ||
			 !strcmp(filenameFromAbsPath(dir_entries[default_index+FB_LIST.top].name),"..")) &&
			 num_entries > default_index+1 ) default_index++;
	FB_LIST.focus = FB_LIST.top + default_index;
	if (fbFocusIdx >= 0 && fbFocusIdx < num_entries)	/* up from a folder: its own row */
		FB_LIST.focus = fbFocusIdx;
	fbFocusIdx = -1;

	//the page as the engine lays it out: once per page turn; the focus on the layout's row (or the lit tab)
	fbView->refresh();
}

extern BOOL hasLoadedISO;
extern char CdromId[10];
extern char CdromLabel[33];
extern signed char autoSaveLoaded;
void Func_SetPlayGame();
void Func_PlayGame();
extern "C" {
void newCD(fileBrowser_file *file);
}

// For special game correction
extern unsigned long dwActFixes;

// Display bios name
extern char biosFileName[16];

static void CheckGameR3000AutoFix(void)
{
    Config.pR3000Fix = 0;
    if (Config.Cpu != DYNACORE_DYNAREC_OLD)
    {
        return;
    }

    int autoFixR3000Len = 8;
    char autoFixR3000JR[autoFixR3000Len][10] = {
         "SLES00037" // Alone in the Dark - Jack is Back
        ,"SLPS00141"
        ,"SLUS00239"

        ,"SLUS00590" // Need for Speed - V-Rally
        ,"SLES00250"
        ,"SLPS01149"
        ,"SLPS91099"
        ,"SLPS91430"
    };

    int i;
    for (i = 0; i < autoFixR3000Len; i++)
    {
        if (strcmp(CdromId, autoFixR3000JR[i]) == 0) {
            Config.pR3000Fix = 1;
            break;
        }
    }

    autoFixR3000Len = 2;
    char autoFixR3000LW[autoFixR3000Len][10] = {
         "SLUS01005" // Supercross 2000
        ,"SLES02373"
    };
    for (i = 0; i < autoFixR3000Len; i++)
    {
        if (strcmp(CdromId, autoFixR3000LW[i]) == 0) {
            Config.pR3000Fix = 2;
            break;
        }
    }

    autoFixR3000Len = 2;
    char autoFixR3000SRA[autoFixR3000Len][10] = {
         // Hot Wheels Turbo Racing
         "SLUS00964" // for NTSC-U,
        ,"SLES02198" // for PAL
    };
    for (i = 0; i < autoFixR3000Len; i++)
    {
        if (strcmp(CdromId, autoFixR3000SRA[i]) == 0) {
            Config.pR3000Fix = 3;
            break;
        }
    }

    autoFixR3000Len = 2;
    char autoFixR3000SLL[autoFixR3000Len][10] = {
         // Blast chamber
         "SLUS00219" // for NTSC,
        ,"SLES00476" // for PAL
    };
    for (i = 0; i < autoFixR3000Len; i++)
    {
        if (strcmp(CdromId, autoFixR3000SLL[i]) == 0) {
            Config.pR3000Fix = 4;
            break;
        }
    }
}

/* A Recent row is loaded from its own device: point the reader at it (this mounts a USB drive,
 * a disc or the share the first time) and look again. False, with the box up, when the device is
 * still not there; the explorer stays as it is. */
static bool fbRecentOpenDevice(int i)
{
	const recent_entry *re = fbRecentOf(dir_entries[i].name);
	const int src = re ? fbSrcIndex(re->src) : -1;
	static char msg[64];
	if (src < 0)
	{
		menu::MessageBox::getInstance().setMessage("Device not available");
		return false;
	}
	loadRomUseSource(src);
	fbSrcProbe();
	if (!fbSrcReady[src])
	{
		char name[8];
		snprintf(name, sizeof name, "%s", re->src);
		for (char *c = name; *c; c++) *c = (char)toupper((unsigned char)*c);
		snprintf(msg, sizeof msg, "%s: device not available", name);
		menu::MessageBox::getInstance().setMessage(msg);
		fbView->relayout();			/* the rows of a device that has come up are not dim any more */
		return false;
	}
	fbView->relayout();
	return true;
}

void fileBrowserFrame_LoadFile(int i)
{
	char feedback_string[256] = "Failed to load ISO";
	if (explorerSource == 4 && !(dir_entries[i].attr & FILE_BROWSER_ATTR_DIR) && !fbRecentOpenDevice(i))
		return;
	if(dir_entries[i].attr & FILE_BROWSER_ATTR_DIR){
		// The ".." row goes up; a folder is entered one level below this one
		if (!romFolderList && !strcmp(filenameFromAbsPath(dir_entries[i].name), ".."))
			explorerUp();
		else
			showFolder(dir_entries[i].name, fbDepth + 1, false);
		menu::Focus::getInstance().clearPrimaryFocus();
	} else if (fileBrowserMode == FileBrowserFrame::FILEBROWSER_LOADISO) {
		// We must select this file
		int ret = loadISO( &dir_entries[i] );

		if(!ret){	// If the read succeeded.
			if(Autoboot){
				// Switches for painting textured quads as 2 triangles (small glitches, but better shading!)
                // This function has been automatically started in soft.c and dwActFixes have been determined in gpu code, so need to set it here
                Config.hacks.dwActFixes |= AUTO_FIX_QUADS_AS_2TRIANGLES;

                CheckGameR3000AutoFix(); // call to check pR3000A autoFix (Supercross 2000, etc.)

				// FIXME: The MessageBox is a hacky way to fix input not responding.
				// No time to improve this...
				menu::MessageBox::getInstance().setMessage("Autobooting game...");
				Func_SetPlayGame();
				Func_PlayGame();
				pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
				Autoboot = false;
				return;
			}

			snprintf(feedback_string, sizeof(feedback_string), "Loaded %s", filenameFromAbsPath(dir_entries[i].name));
			fbRecordLoad(dir_entries[i].name);		/* the Recent record; a write failure is silent */

			// Switches for painting textured quads as 2 triangles (small glitches, but better shading!)
			// This function has been automatically started in soft.c and dwActFixes have been determined in gpu code, so need to set it here
			Config.hacks.dwActFixes |= AUTO_FIX_QUADS_AS_2TRIANGLES;
			// auto recJR => psxJR for some game
			CheckGameR3000AutoFix();

			/* "Loaded <file>", a warning when one applies, then the game's rows (RomInfo.h) */
			char RomInfo[512] = "";
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "Game loaded");   /* the File row names it */
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "\n");
			if (check_unsatisfied_libcrypt())
				menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "Warning: LibCrypt protected game with missing SBI\n");
			romInfoText(RomInfo, sizeof(RomInfo));

			switch (autoSaveLoaded)
			{
			case NATIVESAVEDEVICE_NONE:
				break;
			case NATIVESAVEDEVICE_SD:
				menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "\nFound & loaded save from SD card\n");
				break;
			case NATIVESAVEDEVICE_USB:
				menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "\nFound & loaded save from USB device\n");
				break;
			case NATIVESAVEDEVICE_CARDA:
				menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "\nFound & loaded save from memcard in slot A\n");
				break;
			case NATIVESAVEDEVICE_CARDB:
				menu::glue::appendBounded(RomInfo, sizeof(RomInfo), "\nFound & loaded save from memcard in slot B\n");
				break;
			}
			autoSaveLoaded = NATIVESAVEDEVICE_NONE;

			menu::MessageBox::getInstance().setMessage(RomInfo);
		}
		else		// If not.
		{
			menu::MessageBox::getInstance().setMessage(feedback_string);
		}


		pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
		Func_SetPlayGame(); //hasLoadedISO will be set to False if SysInit() fails
	}
	else if (fileBrowserMode == FileBrowserFrame::FILEBROWSER_SWAPCD) {
		//TODO: Properly implement this
		int ret = loadISOSwap( &dir_entries[i] );
		if(!ret) {
		  if (cdrIsoMultidiskCount > 1) {   /* a multi-disc PBP: say which disc is in */
		    static char msg[48];
		    snprintf(msg, sizeof msg, "Swapped CD: disc %u of %u", cdrIsoMultidiskSelect + 1, cdrIsoMultidiskCount);
		    menu::MessageBox::getInstance().setMessage(msg);
		  } else
		  menu::MessageBox::getInstance().setMessage("Swapped CD");
	  } else {
  	  menu::MessageBox::getInstance().setMessage("Error swapping CD");
	  }
	  pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
	}
}

void fileBrowserFrame_AutoBootFile()
{
	int i;
	// fileBrowserFrame_OpenDirectory() (called just before this by
	// Func_LoadFromSD/USB) sets dir_entries = NULL and num_entries <= 0
	// on a failed/empty directory listing -- without this check, i stays
	// 0 and fileBrowserFrame_LoadFile(0) dereferences a NULL dir_entries.
	if(dir_entries == NULL || num_entries <= 0)
		return;
	for(i = 0; i < num_entries; i++)
		if(strcasestr(dir_entries[i].name, AutobootROM) != NULL)
			break;
	// Previously `i < num_entries - 1` never checked the last entry, and
	// on no match at all still called LoadFile(i) with i left at the last
	// index -- silently booting whatever file happened to sort last
	// instead of reporting "not found".
	if(i < num_entries)
		fileBrowserFrame_LoadFile(i);
}

/* Main > Load ISO and Current ISO > Swap CD: the explorer, on the source last used. A tab press
 * does the same for its own source. */
void explorerOpen(int mode)
{
	fileBrowserMode = mode;
	fbRecentLoad();
	/* ExplorerOpensOn (settings.ini): 0 Last Used keeps explorerSource; 1..5 SD, USB, DVD, Network, Recent */
	if (explorerOpensOn >= EXPLOREROPENS_SD && explorerOpensOn <= EXPLOREROPENS_RECENT)
		explorerSource = explorerOpensOn - EXPLOREROPENS_SD;
	switch (explorerSource)
	{
	case 1: Func_LoadFromUSB(); break;
	case 2: Func_LoadFromDVD(); break;
	case 3: Func_LoadFromSamba(); break;
	case 4:
		pMenuContext->setActiveFrame(MenuContext::FRAME_FILEBROWSER, mode);
		Func_TabRecent();
		break;
	default: Func_LoadFromSD(); break;
	}
}
