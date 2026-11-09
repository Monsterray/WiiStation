/**
 * WiiSX - MenuContext.cpp
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

#include <unistd.h>   /* usleep: the wait for the share */
#include "MenuEngine.h"	/* before the libgui headers: Button.h's include guard is BUTTON_H, a name the engine uses */
#include "MenuContext.h"
#include "MenuLayout.h"
#include "../perf_prof.h"
#include "../hb_probe.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/InputManager.h"
#include "../gc_input/controller.h"

extern bool AutobootBios;      /* GamecubeMain.cpp: autoboot.txt said "BIOS" */
void Func_ExecuteBios();       /* SettingsFrame.cpp */
void Func_LoadFromSD();        /* ExplorerSources.cpp */
void Func_LoadFromDVD();       /* ExplorerSources.cpp */
void ExitToLoaderNow();       /* MainFrame.cpp: Quit, answered Yes */
void Func_LoadFromSamba();     /* ExplorerSources.cpp */
#include "../fileBrowser/smb2dev.h"
extern "C" void glResetCacheRegion(void);
extern int backFromMenu;
extern char originalMode;
#include "../hbc_home.h"
#include "../lab_net.h"
void lab_menu_exit(const char *report);   /* GamecubeMain.cpp: menu.log, results, back to HBC */
extern "C" int perf_chain_index(void);   /* GamecubeMain.cpp: 0 = no chain with games */
#include "../../achievements.h"
#include "../ach_toast.h"
#include "AchievementsFrame.h"
extern BOOL hasLoadedISO;      /* GamecubeMain.cpp */
extern volatile char ws_shutdown;          /* GamecubeMain.cpp: 2 = back to the loader, through Gui.cpp's fade */
extern "C" void SysClose();

MenuContext *pMenuContext;

MenuContext::MenuContext(GXRModeObj *vmode)
		: currentActiveFrame(0),
		  mainFrame(0),
		  fileBrowserFrame(0),
		  currentRomFrame(0),
		  settingsFrame(0),
		  configureInputFrame(0),
		  configureButtonsFrame(0),
		  optionsFrame(0),
		  customizeCoopFrame(0),
		  usbPadTestFrame(0),
		  achievementsFrame(0),
		  playersFrame(0),
		  accountFrame(0),
		  activeIndex(0),
		  activeSubmenu(0)
{
	pMenuContext = this;
	for (int i = 0; i <= FRAME_ACCOUNT; i++)
	{
		focusMemory[i].button = NULL;
		focusMemory[i].submenu = 0;
	}

	menu::Gui::getInstance().setVmode(vmode);

	mainFrame = new MainFrame();
	fileBrowserFrame = new FileBrowserFrame();
	currentRomFrame = new CurrentRomFrame();
	settingsFrame = new SettingsFrame();
	configureInputFrame = new ConfigureInputFrame();
	configureButtonsFrame = new ConfigureButtonsFrame();
	optionsFrame = new OptionsFrame();
	customizeCoopFrame = new CustomizeCoopFrame();
	usbPadTestFrame = new UsbPadTestFrame();
	achievementsFrame = new AchievementsFrame();
	playersFrame = new PlayersFrame();
	accountFrame = new AccountFrame();

	menu::Gui::getInstance().addFrame(mainFrame);
	menu::Gui::getInstance().addFrame(fileBrowserFrame);
	menu::Gui::getInstance().addFrame(currentRomFrame);
	menu::Gui::getInstance().addFrame(settingsFrame);
	menu::Gui::getInstance().addFrame(configureInputFrame);
	menu::Gui::getInstance().addFrame(configureButtonsFrame);
	menu::Gui::getInstance().addFrame(optionsFrame);
	menu::Gui::getInstance().addFrame(customizeCoopFrame);
	menu::Gui::getInstance().addFrame(usbPadTestFrame);
	menu::Gui::getInstance().addFrame(achievementsFrame);
	menu::Gui::getInstance().addFrame(playersFrame);
	menu::Gui::getInstance().addFrame(accountFrame);

	menu::Focus::getInstance().setFocusActive(true);

	// Don't show the menu if we autoboot a game.
	if (!strlen(&AutobootROM[0]))
		setActiveFrame(FRAME_MAIN);
}

MenuContext::~MenuContext()
{
	delete accountFrame;
	delete playersFrame;
	delete achievementsFrame;
	delete usbPadTestFrame;
	delete customizeCoopFrame;
	delete optionsFrame;
	delete configureButtonsFrame;
	delete configureInputFrame;
	delete settingsFrame;
	delete currentRomFrame;
	delete fileBrowserFrame;
	delete mainFrame;
	pMenuContext = NULL;
}

/* sd:/wiistation/autoinput.txt's "menupage <n>": see PadWiiSX.c, where it is parsed. */
extern "C" {
	void autoinput_load(void);
	void autoinput_reset(const char *path);
	extern unsigned autoinput_menupage;
	extern unsigned autoinput_menutour[40], autoinput_menutours;
	extern unsigned autoinput_menuopen_page;
	extern unsigned autoinput_menukeys_frame;
	extern char autoinput_menukeys[64];
	extern char autoinput_menuclick[32];
	extern unsigned autoinput_menuclicks, autoinput_menuclick_frame;
	void SettingsFrame_ScriptClick(const char *name, int n);
}

bool MainFrame_gameStarted();   /* MainFrame.cpp */
void Func_PlayGame();            /* MainFrame.cpp */
namespace menu { extern u16 menu_script_keys; }   /* InputManager.cpp */

static int smbPageWait;   /* menupage 71: frames left to wait for the share */

bool MenuContext::isRunning()
{
	bool isRunning = true;

	/* The game stopped while an achievement pop-up was on screen: open the book on its detail */
	{
		unsigned id = ach_toast_take_jump();
		if (id && achievements_count())
			Func_AchievementsOpenOn(id);
	}

	achievements_idle();   /* Official mode: network completions while the menu is open */

	/* The Homebrew Channel's agent (hbc_home.c): HOME opens its overlay; an exit asked for
	 * from the PC leaves as Exit to Loader does, the game closed first. */
	if (hbc_home_menu_frame()) {
		if (hasLoadedISO)
			SysClose();
		ws_shutdown = 2;
	}
//	printf("MenuContext isRunning\n");

	static unsigned labPages, labLastPage, labFrame, labEnd;   /* lab menu walk: see the end of this function's script block */

	/* A few frames in, so that the menu has drawn itself once and the video mode has
	 * settled, open the page the script asked for -- then never again. */
	{
		static int framesUntilMenuPage = 30, framesUntilOpenPage = 10;
		unsigned page = 0;
		static unsigned tourI, tourWait;
		if (tourI && tourI < autoinput_menutours && --tourWait == 0) {
			page = autoinput_menutour[tourI++];
			tourWait = 60;
		}
		if (framesUntilMenuPage > 0 && --framesUntilMenuPage == 0) {
			perf_stack_log("menu frame 30");
			/* read the script anew: a read before the SD card was mounted (the controller
			 * assignment at power-on asks for scripted ports) found nothing, and kept that */
			autoinput_reset("sd:/wiistation/autoinput.txt");
			autoinput_load();
			page = autoinput_menupage;
			if (autoinput_menutours) { page = autoinput_menutour[0]; tourI = 1; tourWait = 60; }
			/* Lab mode: the walk's last scheduled action plus ~1 s ends the run (labEnd, in menu
			 * frames); a script with nothing to do ends it at once. Without this a menu-only lab
			 * run sat in the menu until someone at the Wii left it (LRWii, 2026-11-04). */
			unsigned e = 0, n = strlen(autoinput_menukeys);
			if (autoinput_menupage || autoinput_menuopen_page) e = 90;
			if (autoinput_menutours && 30 + 60 * autoinput_menutours + 60 > e) e = 30 + 60 * autoinput_menutours + 60;
			if (n && autoinput_menukeys_frame + 12 * n + 60 > e) e = autoinput_menukeys_frame + 12 * n + 60;
			if (autoinput_menuclicks) {
				unsigned c = (autoinput_menuclick_frame ? autoinput_menuclick_frame : 40) + 60;
				if (c > e) e = c;
			}
			labEnd = (e ? e : 90) + 30;
		}
		/* "menuopen <vblank> <page>": the game stopped there for it; the page ten frames on */
		if (autoinput_menuopen_page && --framesUntilOpenPage <= 0) {
			page = autoinput_menuopen_page;
			autoinput_menuopen_page = 0;
			framesUntilOpenPage = 10;
		}
		if (page) {
			labPages++;
			labLastPage = page;
			autoinput_menupage = page;
			if (autoinput_menupage >= 1 && autoinput_menupage <= 5)
				setActiveFrame(FRAME_SETTINGS,
					SettingsFrame::SUBMENU_GENERAL + (int)autoinput_menupage - 1);
			else if (autoinput_menupage >= 6 && autoinput_menupage <= 14)   /* 11 GPU, 12 Input, 13 SPU (Advanced tabs), 14 Achievements */
				setActiveFrame(FRAME_OPTIONS,
					OptionsFrame::PAGE_SOUND + (int)autoinput_menupage - 6);
			else if (autoinput_menupage == 15)   /* Current ISO > Achievements: the list */
				setActiveFrame(FRAME_ACHIEVEMENTS, 0);
			else if (autoinput_menupage == 18)   /* Players */
				setActiveFrame(FRAME_PLAYERS, 0);
			else if (autoinput_menupage == 16)   /* Current ISO */
				setActiveFrame(FRAME_CURRENTROM);
			else if (autoinput_menupage == 17)   /* Current ISO > Show ISO Info */
				setActiveFrame(FRAME_OPTIONS, OptionsFrame::PAGE_ISOINFO);
			else if (autoinput_menupage == 20)   /* Configure Input */
				setActiveFrame(FRAME_CONFIGUREINPUT, ConfigureInputFrame::SUBMENU_REINIT);
			else if (autoinput_menupage >= 21 && autoinput_menupage <= 36)   /* Customize Co-Op */
				setActiveFrame(FRAME_CUSTOMIZECOOP, (int)autoinput_menupage - 21);
			else if (autoinput_menupage >= 40 && autoinput_menupage <= 65)
				setActiveFrame(FRAME_CONFIGUREBUTTONS, (int)autoinput_menupage - 40);   /* Configure Buttons, by virtual controller */
			else if (autoinput_menupage == 70)
				Func_LoadFromSD();   /* Load ROM > Load from SD: the RomDir folders */
			else if (autoinput_menupage == 72)
				Func_LoadFromDVD();   /* Load ROM > Load from DVD: the disc mounted as dvd: */
			else if (autoinput_menupage == 71)
				smbPageWait = 60 * 30;
			else if (autoinput_menupage == 90)
				ExitToLoaderNow();   /* Main > Quit, answered Yes */   /* Load from SMB, once the share is mounted (below) */
		}
	}

	/* menupage 71: the share mounts a few seconds after the menu comes up (network, then
	 * the SMB session), so wait for it -- at most 30 s -- then open Load from SMB. */
	if (smbPageWait > 0 && (smb2dev_mounted() || --smbPageWait == 0)) {
		smbPageWait = 0;
		Func_LoadFromSamba();
	}

	/* Ten frames after that (or at the script's frame), press what the script asked for: late
	 * enough that the page has drawn itself once, so the frame dump holds both the before and
	 * the after. */
	{
		static unsigned menuFrame;
		if (++menuFrame == (autoinput_menuclick_frame ? autoinput_menuclick_frame : 40) && autoinput_menuclicks)
			SettingsFrame_ScriptClick(autoinput_menuclick, (int)autoinput_menuclicks);
	}

	/* The achievements book is open with a game started: the menu combination (the one that
	 * brought the menu up) goes straight back into the game. It counts as a new press only
	 * after it was seen released here, so the press that opened the menu does not send the
	 * game straight back. */
	{
		static bool armed;
		if (activeIndex != FRAME_ACHIEVEMENTS || !MainFrame_gameStarted())
			armed = false;
		else if (!(pad_menu_combo_held() || (menu::menu_script_keys & MENU_SCRIPT_COMBO)))
			armed = true;
		else if (armed)
		{
			armed = false;
			Func_PlayGame();
		}
	}

	/* Lab mode (scripts/wii_run.py --menu): the walk is over when its script is (labEnd), or after
	 * 15 minutes whatever happens. Report it, send the results, back to HBC. Only a game-less run:
	 * a lab chain that opens the menu in a game (menuopen, the achievements book) must go on. */
	if (lab_active() && !perf_chain_index() && !hasLoadedISO) {
		static unsigned long long labT0;
		if (!labT0)
			labT0 = perf_now_us();
		bool late = perf_now_us() - labT0 > 15ULL * 60 * 1000000;
		if (++labFrame >= 30 && ((labEnd && labFrame >= labEnd) || late)) {
			char rep[160];
			snprintf(rep, sizeof rep, "menu walk: ok=%d pages=%u last_page=%u keys=%u frames=%u end=%s",
				late ? 0 : 1, labPages, labLastPage, (unsigned)strlen(autoinput_menukeys), labFrame,
				late ? "time limit" : "script end");
			lab_menu_exit(rep);
		}
	}

	draw();

/*	PADStatus* gcPad = menu::Input::getInstance().getPad();
	if(gcPad[0].button & PAD_BUTTON_START)
		isRunning = false;*/
	
	return isRunning;
}

void MenuContext::setActiveFrame(int frameIndex)
{
	enterFrame(frameIndex, 0);
}

void MenuContext::enterFrame(int frameIndex, int submenu)
{
	menu::Focus &focus = menu::Focus::getInstance();
	bool back = focus.actionIsBack();
	menu::Component *returnTo = NULL;

	/* A screen left by a press that is not B remembers the button pressed, so that a Back to
	 * it can return there. A screen left with no press (a script) remembers nothing. */
	if (activeIndex && !back)
	{
		focusMemory[activeIndex].button = focus.actionTarget();
		focusMemory[activeIndex].submenu = activeSubmenu;
	}

	/* A Back takes what the screen remembers, but only for the tab it was left from. */
	if (back && frameIndex >= FRAME_MAIN && frameIndex <= FRAME_ACCOUNT)
	{
		if (focusMemory[frameIndex].submenu == submenu)
			returnTo = focusMemory[frameIndex].button;
		focusMemory[frameIndex].button = NULL;
	}

	if(currentActiveFrame)
		currentActiveFrame->hideFrame();

	switch(frameIndex) {
	case FRAME_MAIN:
		currentActiveFrame = mainFrame;
		break;
	case FRAME_FILEBROWSER:
		currentActiveFrame = fileBrowserFrame;
		break;
	case FRAME_CURRENTROM:
		currentActiveFrame = currentRomFrame;
		break;
	case FRAME_SETTINGS:
		currentActiveFrame = settingsFrame;
		break;
	case FRAME_CONFIGUREINPUT:
		currentActiveFrame = configureInputFrame;
		break;
	case FRAME_CONFIGUREBUTTONS:
		currentActiveFrame = configureButtonsFrame;
		break;
	case FRAME_OPTIONS:
		currentActiveFrame = optionsFrame;
		break;
	case FRAME_CUSTOMIZECOOP:
		currentActiveFrame = customizeCoopFrame;
		break;
	case FRAME_USBPADTEST:
		currentActiveFrame = usbPadTestFrame;
		break;
	case FRAME_ACHIEVEMENTS:
		currentActiveFrame = achievementsFrame;
		break;
	case FRAME_PLAYERS:
		currentActiveFrame = playersFrame;
		break;
	case FRAME_ACCOUNT:
		currentActiveFrame = accountFrame;
		break;
	}

	/* The logo goes where the frame's page table says (Frame::logoSpot: top right, bottom right, or
	 * none). Only this switch shows or hides it: a frame's own activateSubmenu() must not (Configure
	 * Input's constructor lays itself out with one, and hid the logo from start-up on).
	 * tests/menu_engine/run.sh checks every shipped page against the logo's box. */
	{
		const int spot = currentActiveFrame ? currentActiveFrame->logoSpot() : menu::engine::LOGO_SPOT_TOP;
		const bool bottom = spot == menu::engine::LOGO_SPOT_BOTTOM;
		menu::Gui::getInstance().menuLogo->setVisible(spot != menu::engine::LOGO_SPOT_NONE);
		menu::Gui::getInstance().menuLogo->setLocation(
			bottom ? LOGO_PAGE_X : LOGO_MAIN_X,
			bottom ? LOGO_PAGE_Y : LOGO_MAIN_Y, LOGO_Z);
	}

	if(currentActiveFrame)
	{
		currentActiveFrame->showFrame();
		menu::Focus::getInstance().setCurrentFrame(currentActiveFrame);
		menu::Cursor::getInstance().setCurrentFrame(currentActiveFrame);
	}

	focus.setReturnFocus(returnTo);
	activeIndex = frameIndex;
	activeSubmenu = submenu;
}

void MenuContext::setActiveFrame(int frameIndex, int submenu)
{
	enterFrame(frameIndex, submenu);
	if(currentActiveFrame) currentActiveFrame->activateSubmenu(submenu);
}

menu::Frame* MenuContext::getFrame(int frameIndex)
{
	menu::Frame* pFrame = NULL;
	switch(frameIndex) {
	case FRAME_MAIN:
		pFrame = mainFrame;
		break;
	case FRAME_FILEBROWSER:
		pFrame = fileBrowserFrame;
		break;
	case FRAME_CURRENTROM:
		pFrame = currentRomFrame;
		break;
	case FRAME_SETTINGS:
		pFrame = settingsFrame;
		break;
	case FRAME_CONFIGUREINPUT:
		pFrame = configureInputFrame;
		break;
	case FRAME_CONFIGUREBUTTONS:
		pFrame = configureButtonsFrame;
		break;
	case FRAME_OPTIONS:
		pFrame = optionsFrame;
		break;
	case FRAME_CUSTOMIZECOOP:
		pFrame = customizeCoopFrame;
		break;
	case FRAME_USBPADTEST:
		pFrame = usbPadTestFrame;
		break;
	case FRAME_ACHIEVEMENTS:
		pFrame = achievementsFrame;
		break;
	case FRAME_PLAYERS:
		pFrame = playersFrame;
		break;
	case FRAME_ACCOUNT:
		pFrame = accountFrame;
		break;
	}

	return pFrame;
}

void MenuContext::draw()
{
	menu::Gui::getInstance().draw();
}

void MenuContext::Autoboot()
{
	if(AutobootBios)
	{
		/* Same preamble as Func_PlayGame(): one GUI frame so the video output
		 * is set up, a cleared EFB, the TV-mode switch request and a fresh
		 * texture cache. Without it the BIOS runs but the screen stays black. */
		AutobootBios = false;
		menu::Gui::getInstance().draw();
		menu::Gui::getInstance().gfx->clearEFB((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);   /* far: OpenGX reads depth as bit 15 = 0 there (STP_BIT15.md) */
		if (originalMode)
			backFromMenu = 1;
		glResetCacheRegion();
		Func_ExecuteBios();
		return;
	}
	hb_phase("autoboot: open the folder", AutobootPath);
	if(strcasestr(AutobootPath,"sd:/") != NULL)
		Func_LoadFromSD();
	else if(strcasestr(AutobootPath,"smb:/") != NULL)
	{
		/* a game on the share: wait for the network thread to mount it, at most 30 s */
		for (int i = 0; i < 300 && !smb2dev_mounted(); i++)
			usleep(100 * 1000);
		Func_LoadFromSamba();
	}
	else if(strcasestr(AutobootPath,"dvd:/") != NULL)
		Func_LoadFromDVD();
	else
		Func_LoadFromUSB();

	hb_phase("autoboot: load the game", AutobootROM);
	fileBrowserFrame_AutoBootFile();
	hb_phase("autoboot: done", AutobootROM);
}
