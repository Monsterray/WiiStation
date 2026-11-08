/**
 * WiiSX - SettingsFrame.cpp
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

/*
 * The five Settings tabs. Each tab is a table of rows and buttons (pages/SettingsPages.h) bound to the
 * settings by key; every setting label and choice comes from the settings table (GamecubeMain.cpp).
 * This file builds the engine page from a tab, shows it on the page view (MenuPageView.h), runs a click
 * (a generic write of the setting, or the button's own function) and holds the functions the buttons name.
 */

#include <sys/stat.h>
#include "MenuGlue.h"
#include "MenuPageView.h"

#include "MenuContext.h"
#include "MenuTypes.h"
#include "../perf_prof.h"
#include "SettingsFrame.h"
#include "OptionsFrame.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "pages/SettingsPages.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/IPLFont.h"
#include <string.h>
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "../libgui/gui2/gettext.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"
#include "../vm/vm.h"

#ifdef SHOW_DEBUG
#include "../DEBUG.h"
#endif // SHOW_DEBUG
extern "C" {
#include "../gc_input/controller.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-CARD.h"
#include "../../gpu.h"
#include "../../deps/opengx/GL/gl.h"
}

extern void Func_SetPlayGame();

void Func_ReturnFromSettingsFrame();
void SetFrameLimit();

extern "C" void VIDEO_SetTrapFilter(bool enable);

extern BOOL hasLoadedISO;
extern volatile int stop;
extern char menuActive;
extern MenuContext *pMenuContext;

extern "C" {
void SysReset();
int SysInit();
void SysClose();
void SysStartCPU();
void CheckCdrom();
void CheckPsxType();
void LoadCdrom();
void pauseAudio(void);  void pauseInput(void);
void resumeAudio(void); void resumeInput(void);
void IplFont_loadFontFile(FILE* fontFile);
FILE* IplFont_getFontFile(char* sdUsb);
void psxResetRcntRate();
void pl_chg_psxtype(int is_pal_);
void gpuChangePsxType();
void setSpuInterpolation(int spuInterpolation);
void setSpuTempo(int soundTempo);
void SysPrintf(const char *fmt, ...);
}

/* The tab ids are the frame's SUBMENU_* (--page 1..5, setActiveFrame). */
static_assert(SettingsFrame::SUBMENU_GENERAL == SET_TAB_GENERAL && SettingsFrame::SUBMENU_VIDEO == SET_TAB_VIDEO &&
              SettingsFrame::SUBMENU_INPUT == SET_TAB_INPUT && SettingsFrame::SUBMENU_AUDIO == SET_TAB_AUDIO &&
              SettingsFrame::SUBMENU_SAVES == SET_TAB_SAVES, "SettingsFrame::SettingsSubmenus and SetTabId must agree");

/* The tab row's widths, by tab id. Shared with OptionsFrame, whose Advanced tabs sit in the same row. */
const float settingsTabWidths[SET_TAB_COUNT] = { 110.0f, 100.0f, 100.0f, 100.0f, 100.0f };

/*  What a button shows and does, from its table entry  */

/* The setting a button edits, or NULL for an action. */
static const SettingOption *settingOf(const SetSeg &g)
{
	return g.key ? settingByKey(g.key) : NULL;
}

/* A choice's name, a flag's label or a cycle's current name; an action's text. */
static const char *segText(const SetSeg &g)
{
	if (g.textFn)
		return g.textFn();
	if (g.text)
		return g.text;
	const SettingOption *s = settingOf(g);
	if (!s)
		return g.name;
	if (g.value == SEGV_FLAG)
		return s->label;
	int v = g.value == SEGV_NEXT ? (int)*s->value : g.value;
	if (v < s->min || v > s->max)
		v = s->min;	/* a value the settings file let through */
	return s->choices[v - s->min];
}

/* Lit: the setting holds this choice, or a flag is on. A cycle and an action are never lit. */
static bool segLit(const SetSeg &g)
{
	if (g.lit)
		return g.lit();
	const SettingOption *s = settingOf(g);
	if (!s || g.value == SEGV_NEXT)
		return false;
	if (g.value == SEGV_FLAG)
		return *s->value == s->max;
	return *s->value == g.value;
}

/* A click: the button's own function, or a write of its setting and then its apply function. */
static void press(const SetSeg &g)
{
	if (g.act)
	{
		g.act();
		return;
	}
	const SettingOption *s = settingOf(g);
	if (!s)
		return;
	int v;
	if (g.value == SEGV_FLAG)
		v = *s->value == s->max ? s->min : s->max;
	else if (g.value == SEGV_NEXT)
		v = *s->value + 1 > s->max || *s->value + 1 < s->min ? s->min : *s->value + 1;
	else
		v = g.value;
	*s->value = (char)v;
	if (g.apply)
		g.apply();
}

/*  The tab on show  */

static menu::glue::PageView	*view;
static const SetTab			*shown;			/* the tab on show */
static eng::Row				SET_ROWS[eng::MAX_ROWS];
static eng::ActionButton	SET_ACTIONS[eng::MAX_ROWS][4];
static const char			*SET_TEXT[eng::MAX_ROWS][4];
static eng::Tab				SET_TAB_BTNS[SET_TAB_COUNT];
static eng::Page			SET_PAGE;

static int tabPosOf(int tab)
{
	for (int p = 0; p < SET_TAB_COUNT; p++)
		if (SET_TAB_AT_POS[p] == tab)
			return p;
	return 0;
}

/* Describes a tab as an engine page: the text of every button and which are lit, as the variables hold
 * them now. */
static void bindTab(const SetTab &tab)
{
	for (int r = 0; r < tab.nRows; r++)
	{
		const SetRow &s = tab.rows[r];
		eng::Row &row = SET_ROWS[r];
		memset(&row, 0, sizeof row);
		memset(SET_ACTIONS[r], 0, sizeof SET_ACTIONS[r]);
		const int n = setRowSegs(s);
		const int nChoices = s.kind == eng::ROW_CHOICE ? n - s.plain : n;
		const SettingOption *first = settingOf(s.seg[0]);
		bool picked = false;

		row.kind    = s.kind;
		row.label   = s.label ? s.label : (first ? first->label : NULL);
		row.names   = SET_TEXT[r];
		row.count   = n;
		row.actions = SET_ACTIONS[r];
		row.plain   = s.plain;
		row.prefix  = s.prefix;
		for (int c = 0; c < n; c++)
		{
			const SetSeg &g = s.seg[c];
			SET_TEXT[r][c] = SET_ACTIONS[r][c].text = segText(g);
			/* a cycle has the width of the longest name (the standard's small cycles take their own) */
			SET_ACTIONS[r][c].width = g.value == SEGV_NEXT && !s.prefix ? 200.0f : 0.0f;
			const bool lit = segLit(g);
			if (s.kind == eng::ROW_CHOICE && c < nChoices)
			{
				if (lit && !picked)
				{
					row.value = c;
					picked = true;
				}
			}
			else
				SET_ACTIONS[r][c].lit = lit && !s.noLit;
			/* drawn always, inactive with no game loaded */
			if (g.value == SEGV_GAME && !hasLoadedISO)
				row.inactive |= 1u << c;
		}
	}
	for (int t = 0; t < SET_TAB_COUNT; t++)
	{
		SET_TAB_BTNS[t].text  = SET_TAB_NAMES[SET_TAB_AT_POS[t]];
		SET_TAB_BTNS[t].width = settingsTabWidths[SET_TAB_AT_POS[t]];
	}
}

/* Lays a tab out on the engine and puts its buttons and labels in place. Done when the tab is shown,
 * never per frame. */
static void showTab(int tabId)
{
	const SetTab &tab = SET_TABS_DEF[tabId];
	shown = &tab;

	view->hideAll();
	bindTab(tab);
	for (int r = 0; r < tab.nRows; r++)
		for (int c = 0; c < setRowSegs(tab.rows[r]); c++)
		{
			const SetSeg &g = tab.rows[r].seg[c];
			view->mark(r, c, (g.value == SEGV_FLAG ? menu::glue::PageView::TOGGLE : 0) |
			                 (g.value == SEGV_NEXT ? menu::glue::PageView::CYCLE : 0) |
			                 (tab.rows[r].noLit ? menu::glue::PageView::FLAT : 0));
		}

	SET_PAGE.title      = NULL;
	SET_PAGE.tabs       = SET_TAB_BTNS;
	SET_PAGE.nTabs      = SET_TAB_COUNT;
	SET_PAGE.curTab     = tabPosOf(tabId);
	SET_PAGE.rows       = SET_ROWS;
	SET_PAGE.nRows      = tab.nRows;
	SET_PAGE.logo       = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_SETTINGS is in LOGO_BOTTOM_FRAMES */
	SET_PAGE.focus      = eng::FOCUS_ROWS;
	SET_PAGE.labelCx    = 150.0f;	/* the Settings labels: centered at x 150 */
	SET_PAGE.labelDy    = 28.0f;	/* on the middle of a 56-high row */
	SET_PAGE.standard   = eng::DENSITY_REGULAR;
	SET_PAGE.footer     = tab.footer;

	if (!view->show(SET_PAGE, true))
	{
		/* The tables are fixed, so this is a programming error. The tab stays hidden. */
		SysPrintf("SettingsFrame: tab %d does not fit the menu engine\n", tabId);
		return;
	}
	/* a row of actions: no segment is the current value */
	for (int r = 0; r < tab.nRows; r++)
		if (tab.rows[r].noLit)
			for (int c = 0; c < setRowSegs(tab.rows[r]); c++)
				view->setLit(r, c, false);
#ifdef PERF_PROF
	{
		static unsigned logged;		/* bit N: tab N has been checked and logged */
		if (!(logged & (1u << tabId)))
		{
			logged |= 1u << tabId;
			menu::glue::logProblems("SettingsFrame", SET_TAB_NAMES[tabId], SET_PAGE, view->layout());
		}
	}
#endif
}

/* After a click: the texts and lit buttons follow the variables. The layout stays: a button's width is
 * the layout's, a name that changes is drawn in it. */
static void syncTab(void)
{
	if (!view || !shown)
		return;
	for (int r = 0; r < shown->nRows; r++)
	{
		const SetRow &s = shown->rows[r];
		for (int c = 0; c < setRowSegs(s); c++)
		{
			view->setText(r, c, segText(s.seg[c]));
			view->setLit(r, c, segLit(s.seg[c]) && !s.noLit);
		}
	}
}

/* Every click of the page view: a tab (row -1, slot = the tab's place in the row), or a button of a row. */
static void settingsAct(void *, int row, int slot)
{
	if (row < 0)
	{
		pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, SET_TAB_AT_POS[slot]);
		return;
	}
	press(shown->rows[row].seg[slot]);
	syncTab();
}

/*  The frame  */

SettingsFrame::SettingsFrame()
		: activeSubmenu(SUBMENU_VIDEO)
{
	view = new menu::glue::PageView(this, eng::MAX_ROWS, eng::MAX_SLOTS, 2, SET_TAB_COUNT, 0, settingsAct, NULL,
	                                Func_ReturnFromSettingsFrame, "");
	for (int t = 0; t < SET_TAB_COUNT; t++)
		view->setTabText(t, SET_TAB_NAMES[SET_TAB_AT_POS[t]]);

	setDefaultFocus(view->tab(0));
	setBackFunc(Func_ReturnFromSettingsFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_VIDEO);
}

SettingsFrame::~SettingsFrame()
{
	delete view;
	view = NULL;
	shown = NULL;
}

void SettingsFrame::activateSubmenu(int submenu)
{
	if (submenu < 0 || submenu >= SET_TAB_COUNT)
		submenu = SUBMENU_VIDEO;
	activeSubmenu = submenu;
	showTab(submenu);

	/* Focus starts on the tab's own button; the help starts blank (hideAll). */
	setDefaultFocus(view->tab(tabPosOf(submenu)));
}

void SettingsFrame::drawChildren(menu::Graphics &gfx)
{
	if(isVisible())
	{
		/* The tab buttons (L / R, and the others menu::glue::tabShoulder reads) turn the tabs, as a
		 * press of the tab itself does. */
		{
			int pos = tabPosOf(activeSubmenu);
			int tab = menu::glue::tabShoulderStep(pos, SET_TAB_COUNT);
			if (tab != pos)
				pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, SET_TAB_AT_POS[tab]);
		}

		view->updateFooter(SET_PAGE);

		//Draw buttons
		menu::ComponentList::const_iterator iteration;
		for (iteration = componentList.begin(); iteration != componentList.end(); ++iteration)
		{
			(*iteration)->draw(gfx);
		}
	}
}

/*  Buttons that do more than write a setting  */

/* A loaded game starts again with the new BIOS, core or renderer. */
static void restartGame(void)
{
	SysInit();
	CheckCdrom();
	SysReset();
	LoadCdrom();
	Func_SetPlayGame();
	menu::MessageBox::getInstance().setMessage("Game Reset");
}

static void setBiosDevice(int device)
{
	int needInit = 0;
	if (hasLoadedISO && biosDevice != device)
	{
		SysClose();
		needInit = 1;
	}
	biosDevice = device;
	if (hasLoadedISO && needInit)
		restartGame();
}

int checkBiosExists(int testDevice)
{
	fileBrowser_file testFile;
	memset(&testFile, 0, sizeof(fileBrowser_file));

	biosFile_dir = (testDevice == BIOSDEVICE_SD) ? &biosDir_libfat_Default : &biosDir_libfat_USB;
	sprintf(&testFile.name[0], "%s/SCPH1001.BIN", &biosFile_dir->name[0]);
	biosFile_readFile  = fileBrowser_libfat_readFile;
	biosFile_open      = fileBrowser_libfat_open;
	biosFile_init      = fileBrowser_libfat_init;
	biosFile_deinit    = fileBrowser_libfat_deinit;
	biosFile_init(&testFile);  //initialize the bios device (it might not be the same as ISO device)
	return biosFile_open(&testFile);
}

/* A BIOS on SD or USB: without the file, HLE stays. */
static void tryBios(int device, const char *missing)
{
	if (checkBiosExists(device) == FILE_BROWSER_ERROR_NO_FILE)
	{
		menu::MessageBox::getInstance().setMessage(missing);
		device = BIOSDEVICE_HLE;
	}
	setBiosDevice(device);
}

void Func_BiosSelectHLE() { setBiosDevice(BIOSDEVICE_HLE); }
void Func_BiosSelectSD()  { tryBios(BIOSDEVICE_SD, "BIOS not found on SD"); }
void Func_BiosSelectUSB() { tryBios(BIOSDEVICE_USB, "BIOS not found on USB"); }

void Func_BiosSelectDVD()
{
  	menu::MessageBox::getInstance().setMessage("DVD BIOS not implemented");
}

bool executedBios = false;
bool executingBios = false;
void biosFileInit();

void Func_ExecuteBios()
{
	if (!hasLoadedISO)
	{
		frameLimit[0] = FRAMELIMIT_AUTO;
	    frameLimit[1] = FRAMELIMIT_AUTO;
		biosFileInit();
	}
	LoadCdBios = BOOTTHRUBIOS_YES;
	if(Config.HLE == BIOS_HLE) {
		menu::MessageBox::getInstance().setMessage("You must select a BIOS, not HLE");
		return;
	}
	if(hasLoadedISO || executedBios) {
		//TODO: Implement yes/no that current game will be reset
		SysClose();
	}

	//TODO: load/save memcards here
	if(SysInit() < 0) {
		menu::MessageBox::getInstance().setMessage("Failed to initialize system.\nTry loading an ISO.");
		return;
	}
	executedBios = true;
	executingBios = true;
	perf_reset();          /* same as go(): fresh counters and a fresh perf.log for this run */
	CheckCdrom();
	SysReset();
	pauseRemovalThread();
	resumeAudio();
	resumeInput();
	menuActive = 0;
	SysStartCPU();
	menuActive = 1;
	pauseInput();
	pauseAudio();
	continueRemovalThread();
}

void Func_SelectLanguage()
{
    FILE* fontFile = NULL;
    do
	{
		lang++;
		if (lang > TURKISH)
		{
			lang = ENGLISH;
		}
		else
		{
			fontFile = IplFont_getFontFile("sd");
			if (fontFile == NULL)
			{
				fontFile = IplFont_getFontFile("usb");
			}
		}
	}
	while (lang != ENGLISH && fontFile == NULL);

    IplFont_loadFontFile(fontFile);
    ChangeLanguage();
}

extern void writeConfig(FILE* f);

void Func_SaveSettingsSD()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	configFile_file = &saveDir_libfat_Default;
	struct stat s;
	mkdir("sd:/wiistation", 0777);   /* a new card has no folder yet: make it */
	if (stat("sd:/wiistation/", &s)) {
		menu::MessageBox::getInstance().setMessage("Error opening directory sd:/wiistation");
		return;
	}
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "sd:/wiistation/settings.ini", "wb" );  //attempt to open file
		if(f) {
			writeConfig(f);                                   //write out the config
			fclose(f);
			menu::MessageBox::getInstance().setMessage("Saved settings to SD");
			return;
		}
	}
	menu::MessageBox::getInstance().setMessage("Error saving settings to SD");
}

void Func_SaveSettingsUSB()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	configFile_file = &saveDir_libfat_USB;
	struct stat s;
	mkdir("usb:/wiistation", 0777);   /* a new card has no folder yet: make it */
	if (stat("usb:/wiistation/", &s)) {
		menu::MessageBox::getInstance().setMessage("Error opening directory usb:/wiistation");
		return;
	}
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "usb:/wiistation/settings.ini", "wb" ); //attempt to open file
		if(f) {
			writeConfig(f);                                   //write out the config
			fclose(f);
			menu::MessageBox::getInstance().setMessage("Saved settings to USB");
			return;
		}
	}
	menu::MessageBox::getInstance().setMessage("Error saving settings to USB");
}

/* Save the settings for the game that is loaded, to <device>:/wiistation/settings/<CdromId>.ini:
 * only the settings that differ from settings.ini (writeGameConfig, GamecubeMain.cpp). The game
 * reads settings.ini, then this file on top, each time it starts. USB is used when it has the
 * settings folder, otherwise SD; makeParentDirs creates the folder. */
extern int writeGameConfig(FILE* f);

void Func_SaveSettingsSeparately()
{
	struct stat s;
	char settingPathBuf[64], msg[64];
	extern char CdromId[10];
	if (!CdromId[0]) {   /* a PS-X EXE has no ID: "settings/.ini" would be no game's file */
		menu::MessageBox::getInstance().setMessage("This game has no ID: save to SD or USB");
		return;
	}
	bool usb = !stat("usb:/wiistation/settings/", &s);
	const char* dev = usb ? "USB" : "SD";
	snprintf(settingPathBuf, sizeof settingPathBuf, "%s:/wiistation/settings/%s.ini", usb ? "usb" : "sd", CdromId);
	if (fileBrowser_libfat_init(usb ? &saveDir_libfat_USB : &saveDir_libfat_Default)) {   //only if device initialized ok
		makeParentDirs(settingPathBuf);
		FILE* f = fopen(settingPathBuf, "wb");
		if (f) {
			int n = writeGameConfig(f);
			fclose(f);
			snprintf(msg, sizeof msg, "This game: %d setting(s) saved to %s", n, dev);
			menu::MessageBox::getInstance().setMessage(msg);
			return;
		}
	}
	snprintf(msg, sizeof msg, "Error saving settings to %s", dev);
	menu::MessageBox::getInstance().setMessage(msg);
}

void SetFrameLimit()
{
    if (gpuPtr == &oldSoftGpu)
    {
        gpuPtr->setframelimit(0);
    }
}

/* The Frame Rate cycle: Auto, PS1, TV, Unlimited, then Auto again. The stored values are the
 * FRAMERATE_ numbers, so the order is written out here. */
void Func_FrameRateCycle()
{
	static const char order[] = { FRAMERATE_AUTO, FRAMERATE_PS1, FRAMERATE_TV, FRAMERATE_UNLIMITED };
	int i = 0;
	while (order[i] != frameRateMode)
		i++;
	frameRateMode = order[(i + 1) % 4];
	syncFrameLimit();
	SetFrameLimit();
}

/* The hooks that tell the emulator after the generic write of a setting. */
void applyOriginalMode(void) { displayModeChanged = 1; }

void applyTrapFilter(void)
{
	VIDEO_SetTrapFilter(trapFilter == TRAPFILTER_ENABLE);
}

void applyBilinear(void)
{
	if (gpuPlugin == OPEN_GX)
	{
		extern unsigned int gTexMovieName;
		glChgTextureFilter(gTexMovieName);
	}
}

void applyInterpolation(void) { setSpuInterpolation(spuInterpolation); }

void Func_ForceNTSC()
{
	long oldPsxType = Config.PsxType;
	if(forceNTSC == FORCENTSC_ENABLE)
	{
		forceNTSC = FORCENTSC_DISABLE;
		if (hasLoadedISO)
		{
			CheckPsxType();
		}
	}
	else
	{
		forceNTSC = FORCENTSC_ENABLE;
		Config.PsxType = PSX_TYPE_NTSC;
	}

	if (hasLoadedISO && oldPsxType != Config.PsxType)
	{
		psxResetRcntRate();
		pl_chg_psxtype(Config.PsxType);
		gpuChangePsxType();

		SetFrameLimit();
	}
}

void Func_ConfigureInput()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREINPUT,ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_ConfigureButtons()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREBUTTONS,ConfigureButtonsFrame::SUBMENU_PSX_PADNONE);
}

void Func_AdvancedSound()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_SOUND);
}

void Func_PluginsPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_PLUGINS);
}

void Func_AdvGfxPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_ADVGFX);
}

void Func_MemoryPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_MEMORY);
}

void Func_AchievementsPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_ACHIEVEMENTS);
}

/* Called once when the Plugins page is left, with the pair the user settled on. Switching
 * either plugin with a game running means tearing the emulator down and building it again,
 * which is why it does not happen on every press of a cycling button. */
void ApplyPluginSelection(char wantCore, char wantGpu)
{
	int needInit = 0;

	if (hasLoadedISO && (dynacore != wantCore || gpuPlugin != wantGpu)) {
		SysClose();
		needInit = 1;
	}
	dynacore  = wantCore;
	gpuPlugin = wantGpu;

	if      (gpuPlugin == OLD_SOFT) gpuPtr = &oldSoftGpu;
	else if (gpuPlugin == NEW_SOFT) gpuPtr = &newSoftGpu;
	else                            gpuPtr = &glesGpu;

	if (hasLoadedISO && needInit) {
		if (dynacore == DYNACORE_DYNAREC)
			VM_Init(1024*1024, 256*1024);
		restartGame();
	}
}

/* What one memory card is, in one button: 0 Off, 1 Shared, 2 Game. Two settings hold it
 * -- whether the card is there at all, and which file it lives in -- because both are
 * written to the settings file under the names they have always had. */
static int cardType(int card)
{
	if (memCard[card] == MEMCARD_DISABLE) return 0;
	return memCardFile[card] == MEMCARDFILE_SHARED ? 1 : 2;
}

/* A memory card's button: "Off" when the card is not there, else its file type's choice. */
static const char *cardText(int card)
{
	int t = cardType(card);
	if (t == 0)
		return settingByKey(card ? "Memcard1" : "Memcard0")->choices[0];
	return settingByKey(card ? "Memcard1File" : "Memcard0File")->choices[t - 1];
}

const char *textCard0(void) { return cardText(0); }
const char *textCard1(void) { return cardText(1); }

/* Off -> Shared -> Game -> Off. The file choice takes effect at the next game load,
 * because the card in memory was read from the old file. */
static void cardTypeCycle(int card)
{
	int next = (cardType(card) + 1) % 3;
	memCard[card] = next ? MEMCARD_ENABLE : MEMCARD_DISABLE;
	if (next) memCardFile[card] = (next == 1) ? MEMCARDFILE_SHARED : MEMCARDFILE_PER_GAME;
}

void Func_CardType1() { cardTypeCycle(0); }
void Func_CardType2() { cardTypeCycle(1); }

/* Save Button Configs: the device choice. Each row writes every button file to its device, the
 * same as Configure Buttons' Save, and sets the device that Save uses from now on. */
void saveAllButtonConfigs(const char* dev, bool fade);   /* ConfigureButtonsFrame.cpp */

void Func_SaveButtonsSD()
{
	saveAllButtonConfigs("sd", false);
}

void Func_SaveButtonsUSB()
{
	saveAllButtonConfigs("usb", false);
}

/* The Audio tab's Enable row: one toggle per sound source, lit when that source plays.
 * The settings file keeps the historical "Audio" / "DisableXa" / "DisableCdda" keys and
 * Config.Xa/Config.Cdda keep their inverted sense, so each toggle writes both. */
void Func_ToggleXa()
{
	int on = (Config.Xa == XA_DISABLE);          // was muted -> turn it on
	Config.Xa = xaDisabled = on ? XA_ENABLE : XA_DISABLE;
}

void Func_ToggleCdda()
{
	int on = (Config.Cdda == CDDA_DISABLE);
	Config.Cdda = cddaDisabled = on ? CDDA_ENABLE : CDDA_DISABLE;
}

bool litXa(void)   { return Config.Xa != XA_DISABLE; }
bool litCdda(void) { return Config.Cdda != CDDA_DISABLE; }

/* Sync: how the sound output keeps pace with the mixer. A three-way group:
 * Off (neither), Tempo (the legacy mixer-clock pull-back, SoundTempo) or Rate (the output-
 * stage rate control, SoundRateControl). The settings file keeps two keys; this is the one
 * place that makes them exclusive. ratectl.c reads soundRateControl itself. */
static void soundSyncSelect(int which)
{
	soundTempo       = (which == 1) ? SOUND_TEMPO_ON : SOUND_TEMPO_OFF;
	soundRateControl = (which == 2) ? SOUND_RATE_CONTROL_ON : SOUND_RATE_CONTROL_OFF;
	setSpuTempo(soundTempo);
}
void Func_SoundSyncOff()   { soundSyncSelect(0); }
void Func_SoundSyncTempo() { soundSyncSelect(1); }
void Func_SoundSyncRate()  { soundSyncSelect(2); }

bool litSyncRate(void)  { return soundRateControl != 0; }
bool litSyncTempo(void) { return !soundRateControl && soundTempo; }
bool litSyncOff(void)   { return !soundRateControl && !soundTempo; }

void Func_ReturnFromSettingsFrame()
{
	menu::Gui::getInstance().menuLogo->setLocation(580.0, 70.0, -50.0);
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

/* Load Settings: one settings file read over the settings now, as at boot or a game load
 * (loadSettingsNow, GamecubeMain.cpp). A new CPU core or renderer is applied as the Advanced
 * pages apply it (a loaded game restarts), a new language loads its font, and the General tab
 * shows the values read. dev NULL: this game's own file. */
extern bool loadSettingsNow(const char *dev);

static void loadSettingsFrom(const char *dev, const char *loaded, const char *missing)
{
	char core = dynacore, gpu = gpuPlugin, language = lang;
	char msg[64];

	if (!loadSettingsNow(dev))
	{
		menu::MessageBox::getInstance().setMessage(missing);
		return;
	}
	if (lang != language)
	{
		FILE *fontFile = IplFont_getFontFile((char*)"sd");
		if (!fontFile)
			fontFile = IplFont_getFontFile((char*)"usb");
		IplFont_loadFontFile(fontFile);
		ChangeLanguage();
	}
	{
		char wantCore = dynacore, wantGpu = gpuPlugin;
		bool reset = hasLoadedISO && (wantCore != core || wantGpu != gpu);
		dynacore = core;
		gpuPlugin = gpu;
		ApplyPluginSelection(wantCore, wantGpu);
		snprintf(msg, sizeof msg, "%s%s", loaded, reset ? "\nGame Reset" : "");
	}
	showTab(SET_TAB_GENERAL);
	menu::MessageBox::getInstance().setMessage(msg);
}

void Func_LoadSettingsSD()   { loadSettingsFrom("sd", "Loaded settings from SD", "No settings file on SD"); }
void Func_LoadSettingsUSB()  { loadSettingsFrom("usb", "Loaded settings from USB", "No settings file on USB"); }
void Func_LoadSettingsGame() { loadSettingsFrom(NULL, "Loaded this game's settings", "No settings file for this game"); }

/* Press one Settings button from a script (autoinput.txt "menuclick <name> <n>", see PadWiiSX.c).
 * Nothing else in an unattended run can reach a menu button: the menu reads the pads
 * directly, not through the controller drivers that autoinput feeds. A choice (a button with a
 * setting and a value) is pressed by its value: the one segment whose value is n. Any other
 * button is pressed n times. */
static bool scriptPress(const SetSeg &g, const char *name, int n)
{
	if (strcmp(g.name, name))
		return false;
	if (g.key && g.value >= 0)
	{
		if (g.value != n)
			return false;
		press(g);
		return true;
	}
	for (int i = 0; i < n; i++)
		press(g);
	return true;
}

extern "C" void SettingsFrame_ScriptClick(const char *name, int n)
{
	bool done = false;
	for (int t = 0; t < SET_TAB_COUNT && !done; t++)
		for (int r = 0; r < SET_TABS_DEF[t].nRows && !done; r++)
			for (int c = 0; c < setRowSegs(SET_TABS_DEF[t].rows[r]) && !done; c++)
				done = scriptPress(SET_TABS_DEF[t].rows[r].seg[c], name, n);
	for (int i = 0; i < SET_COUNT(SET_SCRIPT_ONLY) && !done; i++)
		done = scriptPress(SET_SCRIPT_ONLY[i], name, n);
	if (done)
		syncTab();
	else
		SysPrintf("menuclick: no button named '%s'\n", name);
}
