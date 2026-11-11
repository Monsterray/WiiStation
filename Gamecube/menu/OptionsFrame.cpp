/**
 * WiiStation - OptionsFrame.cpp
 *
 * The sub-pages reached from the Settings tabs. One table-driven frame serves all of them:
 *
 *   Audio tab   -> "Advanced"  : sound options under test
 *   General tab -> "Advanced"  : the tabs CPU, GPU, SPU, CD and Input (standard pages, below)
 *  Video tab    -> "Advanced Graphics" : dithering, the video decoder, the TV picture
 *               -> "Memory"    : what the machine's memory is doing right now
 *  General tab  -> "Achievements": the achievement mode, the set file and the account
 *
 * HOW TO ADD A ROW
 *   A setting row (ROW_CYCLE or ROW_RADIO) needs: its OPTIONS[] line in Gamecube/GamecubeMain.cpp
 *   (the key, the variable, its lowest and highest value, the label, the choice names and the
 *   help line; that line is what saves and loads it, and what the menu shows); a row in
 *   SETTINGS.md; and one entry below with the key and an apply function (NULL when the emulator
 *   reads the variable directly). The label, names and range are read from the table by key
 *   when the page is shown: nothing is copied here. scripts/settings_check.py checks each key.
 *     ROW_CYCLE is one button that steps through the values: compact, right for a long list
 *     (a standard page shows it with an arrow at each end).
 *     ROW_RADIO shows every value at once as its own button with the current one lit, the
 *     way the Settings tabs themselves do it: right when the choice is worth seeing whole.
 *     It fits eng::MAX_SLOTS values; a row with fewer leaves the trailing slots empty.
 *     On a standard page (OptPage::standard) a ROW_RADIO is a Choice: touching segments of equal
 *     width, each as wide as the longest name needs, so four values and three both fit.
 *   A readout row (ROW_INFO) needs only a label and a function that writes the right-hand
 *   text. Readouts are read when their page opens.
 *
 * HOW TO ADD A PAGE
 *   One OptRow[] array, one PAGES[] entry (title, rows, optional help lines, which Settings
 *   tab B returns to, and optional enter/leave hooks), one value in OptionsFrame::OptionsPages,
 *   and a button somewhere in SettingsFrame that calls setActiveFrame(FRAME_OPTIONS, that value).
 *   A standard page (the Advanced tabs) gives OptPage::standard (a density) instead of help
 *   lines: each row's OptRow::help (or its setting's own help) is the one or two lines shown
 *   under the rows while that row has focus (a tab focused: none), as on the Settings tabs.
 *   Help is a term and its explanation, in two columns under the rows: the term is drawn
 *   bold and right-aligned so the colons line up, and a line with no term continues the one
 *   above it. A help block that runs into the rows or the logo is reported by the menu
 *   engine's layout check in debug builds and by tests/menu_engine/run.sh, which
 *   measure the lines against the screen (Gamecube/menu/MenuLayout.h).
 *
 * WHERE THINGS GO
 *   The menu engine (Gamecube/menu/MenuEngine.h) works out every position and every focus
 *   link. This frame describes the page on show as an engine Page, built from the tables
 *   below, and places its buttons, labels and help at the rectangles the engine gives back.
 *   That happens when a page or a tab is shown, never per frame. The click handlers, the
 *   settings bindings and the readouts are the same as they always were.
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
#include <ogc/system.h>
#include <ogc/lwp_heap.h>
#include "MenuGlue.h"
#include "MenuPageView.h"

#include "MenuContext.h"
#include "SettingsFrame.h"
#include "OptionsFrame.h"
#include "RomInfo.h"
#include "MenuEngine.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"   /* Config: the ISO Info page */
#include "../MEM2.h"
#include "../cover_art.h"
#include "../fileBrowser/cover_index.h"
#include "MenuLayout.h"
#include "../../achievements.h"

extern "C" {
#include "../../mem2_manager.h"
#include "../lc.h"
}

extern MenuContext *pMenuContext;
extern "C" void SysPrintf(const char *fmt, ...);

namespace eng = menu::engine;

/* Applied when the Plugins page is left, not on every press: changing either plugin with a
 * game loaded tears the emulator down and starts it again, which would fire on each step of
 * a cycling button. Defined in SettingsFrame.cpp, where the reset machinery already lives. */
extern void ApplyPluginSelection(char wantCore, char wantGpu);

namespace menu { extern heap_cntrl* GXtexCache; extern unsigned int fontHeapBytes; }   /* the font glyph heap, IPLFont.cpp */

void Func_ReturnFromOptionsFrame();

/*  Rows  */

#include "pages/OptionsPages.h"

/* The header's ids are what the frames' enums say. */
static_assert(OptionsFrame::PAGE_SOUND == OPT_PAGE_SOUND && OptionsFrame::PAGE_ADVGFX == OPT_PAGE_ADVGFX &&
              OptionsFrame::PAGE_PLUGINS == OPT_PAGE_PLUGINS && OptionsFrame::PAGE_STORAGE == OPT_PAGE_STORAGE &&
              OptionsFrame::PAGE_MEMORY == OPT_PAGE_MEMORY && OptionsFrame::PAGE_ADV_GPU == OPT_PAGE_ADV_GPU &&
              OptionsFrame::PAGE_ADV_INPUT == OPT_PAGE_ADV_INPUT && OptionsFrame::PAGE_ADV_SPU == OPT_PAGE_ADV_SPU &&
              OptionsFrame::PAGE_ACHIEVEMENTS == OPT_PAGE_ACHIEVEMENTS && OptionsFrame::PAGE_ISOINFO == OPT_PAGE_ISOINFO,
              "OptionsFrame::OptionsPages and OptPageId (pages/OptionsPages.h) must agree");
static_assert(SettingsFrame::SUBMENU_GENERAL == OPT_RET_GENERAL && SettingsFrame::SUBMENU_VIDEO == OPT_RET_VIDEO &&
              SettingsFrame::SUBMENU_AUDIO == OPT_RET_AUDIO, "SettingsFrame::SettingsSubmenus and OptReturn must agree");

/* setSpuReverb(int) takes the raw setting value; the table's apply slot is void(void), so
 * this wrapper reads the setting itself, the same way GamecubeMain.cpp does at startup. */
extern "C" void setSpuReverb(int soundReverb);
/* The Memory readouts: Lightrec's code use (deps/lightrec), the OpenGX texture pool (deps/opengx),
 * and the PS1 RAM / VRAM sizes (gpulib). */
extern "C" {
unsigned int lightrec_get_mem_usage(int type);	/* MEM_FOR_CODE is 0 */
void ogx_tex_mem(unsigned *n, unsigned *data_kb, unsigned *semi_kb);
extern const unsigned globalVramSize;
}
void applySpuReverb(void) { setSpuReverb(soundReverb); }
void SetFrameLimit();			/* SettingsFrame.cpp: what the Frame Skip buttons called after setting frameSkip */
void applyFrameSkip(void) { SetFrameLimit(); }

/*  Readouts (Memory page)  */

/* MEM1 is the 24 MB of fast 1T-SRAM on the graphics die: emulated PSX RAM, the GPU's
 * buffers and everything the CPU touches per frame live here. What is left is what any
 * future allocation has to fit into. */
#define MEM1_KB (24u * 1024)
void infoMem1(char *buf, int len)
{
	unsigned long free_kb = (unsigned long)(SYS_GetArena1Size() >> 10);
	snprintf(buf, len, "%lu used, %lu free KB", MEM1_KB - free_kb, free_kb);
}

/* The locked cache: half of the CPU's 32 KB data cache, locked as 16 KB of scratch memory
 * for the regions the LockedCache setting turns on (Gamecube/lc.c, Docs/LOCKED_CACHE.md).
 * It is set up when a game starts, so this shows the last game's regions. */
void infoLockedCache(char *buf, int len)
{
	char names[48];
	unsigned used = lc_usage(names, sizeof names);
	snprintf(buf, len, "%u / %u KB (%s)", used >> 10, LC_TOTAL_BYTES >> 10, names);
}

/* MEM2 is the 64 MB of slower off-die GDDR3. This is the general heap the emulator carves
 * textures, the disc-image buffers and the sound buffers out of (mem2_manager.c). */
void infoMem2(char *buf, int len)
{
	unsigned long used = gx_mem2_used() >> 10, total = gx_mem2_total() >> 10;
	snprintf(buf, len, "%lu used, %lu free KB", used, total - used);
}

/* The OpenGX textures in MEM2: how many, and their 16-bit data (and the semi-transparent copies). */
void infoTexPool(char *buf, int len)
{
	unsigned n = 0, data = 0, semi = 0;
	ogx_tex_mem(&n, &data, &semi);
	snprintf(buf, len, "%u textures, %u + %u KB", n, data, semi);
}

/* The glyph cache: one 1152-byte RGB5A3 tile per character, in a block of the general MEM2
 * heap sized to the loaded font (IPLFont.cpp): a Latin menu takes a few hundred KB, a full
 * CJK font up to MEM2.h CN_FONT_SIZE. */
void infoFontCache(char *buf, int len)
{
	unsigned long used = 0, total = (unsigned long)menu::fontHeapBytes >> 10;
	if (menu::GXtexCache) {
		heap_iblock info;
		__lwp_heap_getinfo(menu::GXtexCache, &info);
		used = info.used_size >> 10;
	}
	snprintf(buf, len, "%lu / %lu KB", used, total);
}

/* The recompiler's output buffer, reserved at a fixed MEM2 address for whichever core is
 * selected. Neither is sized by a setting today. */
void infoJitBuffer(char *buf, int len)
{
	if (dynacore == DYNACORE_DYNAREC_OLD)
		snprintf(buf, len, "%lu KB (PPC)", (unsigned long)RECMEM2_SIZE >> 10);
	else
		snprintf(buf, len, "%u of %lu KB used", lightrec_get_mem_usage(0) >> 10, (unsigned long)LIGHTREC_CODE_SIZE >> 10);
}

/* Sound and disc buffers, both fixed regions. */
void infoFixedBuffers(char *buf, int len)
{
	snprintf(buf, len, "%lu + %lu KB",
		(unsigned long)SPU_BUF_SIZE >> 10, (unsigned long)PSXR_BUF_SIZE >> 10);
}

/* The emulated PlayStation's main RAM (2 MB) and VRAM: the sizes of the buffers, not what a game uses. */
void infoPsxBuffers(char *buf, int len)
{
	snprintf(buf, len, "%u + %lu KB", 2048u, (unsigned long)globalVramSize >> 10);
}

/*  Readouts (Achievements page)  */

/* The AchievementFile setting; the tail of it when it is longer than the readout. */
void infoAchFile(char *buf, int len)
{
	size_t n = strlen(achievementFile);
	if (!n)
		snprintf(buf, len, "None");
	else if (n >= (size_t)len)
		snprintf(buf, len, "...%s", achievementFile + n - (len - 4));
	else
		snprintf(buf, len, "%s", achievementFile);
}

/*  Pages: the tables are in pages/OptionsPages.h  */

void infoSpuPlugin(char *buf, int len) { snprintf(buf, len, "P.E.Op.S. DFSound"); }

static char pluginsCoreOnEntry, pluginsGpuOnEntry;
void pluginsEnter(void) { pluginsCoreOnEntry = dynacore; pluginsGpuOnEntry = gpuPlugin; }
void pluginsLeave(void)
{
	if (dynacore != pluginsCoreOnEntry || gpuPlugin != pluginsGpuOnEntry) {
		char core = dynacore, gpu = gpuPlugin;
		/* ApplyPluginSelection re-reads both from the globals; hand it the wanted pair and
		 * let it decide whether a running game has to be restarted. */
		dynacore = pluginsCoreOnEntry;
		gpuPlugin = pluginsGpuOnEntry;
		ApplyPluginSelection(core, gpu);
	}
}


/*  The ISO Info disc band (design I4): the game's disc picture from the cover pack, when it has one  */

void fbCoverForget(void);	/* FileBrowserFrame.cpp: the explorer's cover shares cover_art's texture */
extern GXTexRegion texCacheRegionS[8];

static int				currentPage;

/* Loaded when the page opens (one decode, 600 x 600 -> STD_DISC_D square RGB5A3, 55 KB in MEM2 through cover_art, which holds
 * one texture at a time) and freed when the page is left. discBand: the picture is there, so the page shows the band and its
 * grid starts at the fourth row; no picture (no pack, no serial, no disc file): the page as it was. */
static bool				discBand;
static GXTexObj			discTex;
static char				bandName[96], bandDisc[48], bandImage[48];

/* The loaded game's disc picture into *tex (also used by the Game Loaded box); the caller frees it with cover_art_free */
bool isoDiscPictureLoad(GXTexObj *tex)
{
	static const char *const dirs[] = { "sd:/wiistation/covers", "usb:/wiistation/covers" };
	const char *serial = romInfoSerial();
	if (!serial[0])
		return false;
	for (unsigned d = 0; d < sizeof dirs / sizeof dirs[0]; d++)
	{
		char idx[48], path[112];
		cover_row r;
		snprintf(idx, sizeof idx, "%s/index.txt", dirs[d]);
		if (cover_index_find(idx, serial, &r) && r.disc[0] && !cover_index_path(path, sizeof path, dirs[d], r.disc) &&
		    cover_art_load(path, tex, STD_DISC_D, STD_DISC_D))
			return true;
	}
	return false;
}
static bool discLoad(void) { return isoDiscPictureLoad(&discTex); }

void isoInfoEnter(void)
{
	fbCoverForget();
	discBand = discLoad();
	if (discBand)
	{
		romInfoGame(bandName, sizeof bandName);
		romInfoDisc(bandDisc, sizeof bandDisc);
		romInfoImage(bandImage, sizeof bandImage);
	}
}
void isoInfoLeave(void)
{
	if (discBand)
		cover_art_free();
	discBand = false;
}

/* Rows the grid leaves out because the band shows them */
static int rowSkip(void) { return discBand && currentPage == OPT_PAGE_ISOINFO ? ISOINFO_BAND_ROWS : 0; }

static void discDraw(menu::Graphics &gfx)
{
	if (!discBand || currentPage != OPT_PAGE_ISOINFO)
		return;
	GX_InvalidateTexRegion(&texCacheRegionS[0]);				/* the font's region: other texels */
	GX_LoadTexObjPreloaded(&discTex, &texCacheRegionS[0], GX_TEXMAP0);	/* outside Begin/End */
	gfx.setColor((GXColor) {255,255,255,255});
	gfx.setTEV(GX_MODULATE);
	gfx.enableBlending(true);
	gfx.drawImage(0, STD_DISC_X, STD_DISC_Y, STD_DISC_D, STD_DISC_D, 0, 1, 0, 1);
	const int room = (int)STD_CTRL_X1 - STD_DISC_TEXT_X;
	menu::glue::text(STD_DISC_TEXT_X, STD_DISC_NAME_Y, bandName, 1.0f, (GXColor) {30,30,30,255}, room);
	menu::glue::text(STD_DISC_TEXT_X, STD_DISC_LINE1_Y, bandDisc, 16.0f / 24, (GXColor) {60,60,60,255}, room);
	menu::glue::text(STD_DISC_TEXT_X, STD_DISC_LINE2_Y, bandImage, 16.0f / 24, (GXColor) {60,60,60,255}, room);
}


static int				returnSubmenu;		// the Settings tab B goes back to
static bool				tabSwitch;			// activateSubmenu called for a tab, inside the group
static OptionsFrame*	optionsFrame;
static menu::glue::PageView*	view;	/* the widgets of every page: tabs, rows, help, footer */

/* The page on show, as the engine sees it. Its layout (about 7 KB) is the view's. */
static SetPage		BUILT;

/* Every click of the page view: a tab (row -1, slot = the tab), or a button of a row. */
static void optAct(void *, int row, int slot)
{
	if (row < 0)
	{
		optionsFrame->showTab(slot);
		return;
	}
	const OptPage &p = PAGES[currentPage];
	const int skip = rowSkip();
	if (row < p.nrows - skip)
	{
		const SetRow &r = p.rows[skip + row];
		segPress(r.seg[r.seg[0].value == SEGV_EACH ? 0 : slot], slot);
		syncRows(*view, p.rows + skip, p.nrows - skip);
	}
}

/* The Achievements mode steps in display order Off, Local, Official, Custom, Test; the numbers are the settings files'
 * (0 Off, 1 Custom, 2 Test, 3 Official, 4 Local), so the next is not the next number. */
void Func_AchievementsMode(void)
{
	static const char NEXT[5] = { 4, 2, 0, 1, 3 };
	const SettingOption *s = settingByKey("Achievements");
	*s->value = (*s->value >= 0 && *s->value <= 4) ? NEXT[(int)*s->value] : 0;
}

/* A script's "menuclick name n" on the page on show (SettingsFrame_ScriptClick tries the Settings tabs first). */
bool OptionsFrame_ScriptClick(const char *name, int n)
{
	const OptPage &p = PAGES[currentPage];
	const int skip = rowSkip();
	for (int r = skip; r < p.nrows; r++)
		if (scriptPress(p.rows[r].seg[0], name, n))
		{
			syncRows(*view, p.rows + skip, p.nrows - skip);
			return true;
		}
	return false;
}

/*  The page, for the engine  */

static const char *readoutOf(int row)
{
	return view->value(row);
}

static const SetBind BIND = { segText, segLit, segSetting, readoutOf, false, true };

OptionsFrame::OptionsFrame()
{
	optionsFrame = this;
	view = new menu::glue::PageView(this, optAct, NULL,
	                                Func_ReturnFromOptionsFrame, PAGES[PAGE_SOUND].title);
	setDefaultFocus(view->rowButton(0));
	setBackFunc(Func_ReturnFromOptionsFrame);
	setEnabled(true);
	activateSubmenu(PAGE_SOUND);
}

int OptionsFrame::logoSpot() const { return OPT_LOGO; }

OptionsFrame::~OptionsFrame()
{
	delete view;
	view = NULL;
}

void OptionsFrame::activateSubmenu(int submenu)
{
	if (submenu < 0 || submenu >= NUM_PAGES) submenu = PAGE_SOUND;
	currentPage = submenu;

	int nTabs = 0, curTab = -1;
	const int *group = tabGroupOf(currentPage, &nTabs);
	for (int t = 0; t < nTabs; t++)
	{
		view->setTabText(t, PAGES[group[t]].tab);
		if (group[t] == currentPage) curTab = t;
	}

	if (!tabSwitch)
	{
		returnSubmenu = PAGES[currentPage].returnSubmenu;
		if (PAGES[currentPage].onEnter) PAGES[currentPage].onEnter();
	}
	const int skip = rowSkip();

	/* Hidden first; the layout then shows what this page has. The footer help starts blank. */
	view->hideAll();
	/* The values the rows show. A readout's text is read here, before the engine lays it out. */
	syncRows(*view, PAGES[currentPage].rows + skip, PAGES[currentPage].nrows - skip);

	/* The engine lays the page out and links its focus: once, here, not per frame. */
	buildOptionsPage(currentPage, curTab, BIND, BUILT, skip, skip ? (float)STD_DISC_ROWS_Y0 : 0.0f);
	if (!view->show(BUILT.page))
		SysPrintf("OptionsFrame: page %d does not fit the menu engine\n", currentPage);
#ifdef PERF_PROF
	else
	{
		static unsigned logged;		/* bit N: page N has been checked and logged */
		if (!(logged & (1u << currentPage)))
		{
			logged |= 1u << currentPage;
			menu::glue::logProblems("OptionsFrame", PAGES[currentPage].title, BUILT.page, view->layout());
		}
	}
#endif

	/* Focus starts on the page's first control. After a tab press it stays on the tab row, so
	 * Left/Right keep turning pages. */
	menu::Button *start = NULL;
	if (tabSwitch && curTab >= 0)
		start = view->tab(curTab);
	else
		start = view->startButton();
	setDefaultFocus(start);
}

/* Back from the USB Pad Test page to Advanced > Input, in the same visit to the Advanced pages:
 * the CPU and GPU taken on entering them are still the ones compared on leaving. */
void OptionsFrame_BackToInput()
{
	tabSwitch = true;
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_ADV_INPUT);
	tabSwitch = false;
}

void OptionsFrame::showTab(int tab)
{
	int n = 0;
	const int *group = tabGroupOf(currentPage, &n);
	if (!group || tab < 0 || tab >= n || group[tab] == currentPage) return;
	tabSwitch = true;
	activateSubmenu(group[tab]);
	tabSwitch = false;
}

/* Readouts are read once, when their page opens (activateSubmenu), not per frame. */
void OptionsFrame::drawChildren(menu::Graphics& gfx)
{
	if (!isVisible()) return;

	view->update(BUILT.page);
	discDraw(gfx);			/* before the texts: drawInit sets the font's texture again */

	menu::ComponentList::const_iterator iteration;
	for (iteration = componentList.begin(); iteration != componentList.end(); ++iteration)
		(*iteration)->draw(gfx);
}

void Func_ReturnFromOptionsFrame()
{
	void (*onLeave)(void) = PAGES[currentPage].onLeave;

	/* Go back to the tab first: a plugin change with a game loaded restarts the emulator
	 * and ends in Func_SetPlayGame(), which puts the game on screen. Switching frames
	 * afterwards would undo that, so the hook runs last. */
	if (returnSubmenu == RETURN_CURRENT_ISO)
		pMenuContext->setActiveFrame(MenuContext::FRAME_CURRENTROM);
	else
		pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, returnSubmenu);
	if (onLeave) onLeave();
}
