/**
 * WiiSX - Gui.cpp
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

#include <stdlib.h>   /* exit() */
#include <stdio.h>
#include <stdarg.h>
#include <ogc/lwp_watchdog.h>
#include "../version.h"
#include "Gui.h"
#include "../ws_crash.h"
#include "IPLFont.h"
#include "InputManager.h"
#include "CursorManager.h"
#include "FocusManager.h"
#include "MessageBox.h"
#include "LoadingBar.h"
#include "GuiResources.h"
#include "../menu/MenuLayout.h"   /* where the logo sits; shared with the frames */
#include "../perf_prof.h"

extern "C" {
#include "../gc_input/controller.h"
#ifdef WII
#include <di/di.h>
extern "C" void fileBrowser_DVD_shutdown(void);   /* fileBrowser-DVD.c */
#include <ogc/machine/processor.h>
#endif
}
#include "../../HidController/KernelHID.h"

extern char ws_shutdown;

/* The Quit log, sd:/wiistation/quit.log: one line per step of the way out, each written and
 * closed at once, so the last line survives a freeze that needs the power button (a Wii froze
 * on Quit on 2026-10-09 and kept no crash report). Started by quit_log_begin(); every other
 * call is a no-op until then. Ends at the agent's unmount of the card in exit(). */
static int quit_log_on;
static u64 quit_log_t0;

extern "C" void quit_crumb(const char *fmt, ...)
{
	if (!quit_log_on)
		return;
	FILE *f = fopen("sd:/wiistation/quit.log", "a");
	if (!f)
		return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(f, "%6u ms  ", (unsigned)ticks_to_millisecs(diff_ticks(quit_log_t0, gettime())));
	vfprintf(f, fmt, ap);
	fputc('\n', f);
	va_end(ap);
	fclose(f);
}

static void quit_crumb_atexit(void) { quit_crumb("atexit: the first handler (then VM_Deinit, the agent's)"); }

extern "C" void quit_log_begin(const char *how)
{
	FILE *f = fopen("sd:/wiistation/quit.log", "w");
	if (f) {
		fprintf(f, "WiiStation %s: %s\n", WIISTATION_VERSION, how);
		fclose(f);
	}
	quit_log_t0 = gettime();
	quit_log_on = 1;
	atexit(quit_crumb_atexit);
}
#ifdef WII
bool isWiiU = (*(vu16*)0xCD8005A0 == 0xCAFE) && CONF_GetAspectRatio();
#endif

namespace menu {

Gui::Gui()
	: fade(9)
{
	menuLogo = new Logo();
	/* The main screen's position; MenuContext::setActiveFrame moves it per frame, and
	 * this is what an autoboot (which never shows a frame) leaves it at. */
	menuLogo->setLocation(LOGO_MAIN_X, LOGO_MAIN_Y, LOGO_Z);
	menuLogo->setVisible(true);
}

Gui::~Gui()
{
	delete menuLogo;
	delete gfx;
}

void Gui::setVmode(GXRModeObj *vmode)
{
	gfx = new Graphics(vmode);
	IplFont::getInstance().setVmode(vmode);
}

void Gui::addFrame(Frame *frame)
{
	frameList.push_back(frame);
}

void Gui::removeFrame(Frame *frame)
{
	frameList.erase(std::remove(frameList.begin(),frameList.end(),frame),frameList.end());
}

void Gui::draw()
{
//	printf("Gui draw\n");
	unsigned long long menu_t0 = perf_now_us();
	PERF_INC(menu_frames);
	ws_alive();   /* progress, for the agent's hang watchdog */
	Input::getInstance().refreshInput();
	Cursor::getInstance().updateCursor();
	Focus::getInstance().updateFocus();
	if(padAutoAssign)
	{
		// auto_assign_controllers() re-probes every controller type (each of
		// which does its own WPAD_ScanPads()/WPAD_Probe() calls) across both
		// virtual controller ports every time it runs. Rescanning hardware
		// that hasn't changed on every single menu frame is wasted work, so
		// only run it every 15 frames (~250ms) instead -- still fast enough
		// that a newly connected controller gets picked up almost instantly
		// on a menu screen.
		static int autoAssignCounter = 0;
		if(--autoAssignCounter <= 0)
		{
			auto_assign_controllers();
			autoAssignCounter = 15;
		}
	}
	//Update time??

#ifdef WII
	// Adjust Wii U aspect ratio via DMCU
	static char oldScreenMode = -1;
	if (isWiiU && (oldScreenMode != screenMode)) {
		write32(0xd8006a0, screenMode ? 0x30000004 : 0x30000002);
		mask32(0xd8006a8, 0, 2);
		oldScreenMode = screenMode;
	}
#endif

	//Get graphics framework and pass to Frame draw fns?
	gfx->drawInit();
	drawBackground();
	FrameList::const_iterator iteration;
	for (iteration = frameList.begin(); iteration != frameList.end(); ++iteration)
	{
		(*iteration)->drawChildren(*gfx);
	}
//	menuLogo->drawComponent(*gfx);
	menuLogo->draw(*gfx);
	if (MessageBox::getInstance().getActive()) MessageBox::getInstance().drawMessageBox(*gfx);
	if (LoadingBar::getInstance().getActive()) LoadingBar::getInstance().drawLoadingBar(*gfx);
	Cursor::getInstance().drawCursor(*gfx);

	if(ws_shutdown)
	{
		Cursor::getInstance().setFreezeAction(true);
		Focus::getInstance().setFreezeAction(true);
		gfx->enableBlending(true);
		gfx->setTEV(GX_PASSCLR);
		gfx->setDepth(-10.0f);
		gfx->newModelView();
		gfx->loadModelView();
		gfx->loadOrthographic();

		gfx->setColor((GXColor){0, 0, 0, fade});
		if(screenMode)	gfx->fillRect(-104, 0, 848, 480);
		else			gfx->fillRect(0, 0, 640, 480);

		{
			static int faded;
			if (!faded++) quit_crumb("fade: first frame");
		}
		if(fade == 255)
		{
			quit_crumb("fade: done");
			VIDEO_SetBlack(true);
			VIDEO_Flush();
		 	VIDEO_WaitVSync();

			quit_crumb("screen black");
		 	#ifdef HW_RVL
			HIDClose(1);
			quit_crumb("HIDClose(1) done");
			#endif // HW_RVL
#ifdef WII
			// If this is a Wii U, restore the original aspect ratio
			if(isWiiU) {
				write32(0xd8006a0, 0x30000004);
				mask32(0xd8006a8, 0, 2);
			}
#endif
			if(ws_shutdown==1)	//Power off System
				SYS_ResetSystem(SYS_POWEROFF, 0, 0);
			else			//Return to Loader
			{
#ifdef WII
				fileBrowser_DVD_shutdown();   /* DI_Close, unless a disc spin-up may still hold IOS */
				quit_crumb("DVD shutdown done");
#endif
				void (*rld)() = (void (*)()) 0x80001800;
#ifdef HW_DOL
				#define PSOSDLOADID 0x7c6000a6
				// try to reload
				if(*(volatile unsigned int*)0x80001800 == PSOSDLOADID) {
					rld();
				}
				else {
					*(volatile unsigned int*)0xCC003024 = 0;  //reboot
			  }
#else
				/* exit(), not a jump to the loader's stub: libogc2's exit runs the atexit
				 * handlers and the reset functions first -- the HBC agent's kept log
				 * (`hbc.py lastlog` said "no kept log" after every exit) and its write-back
				 * of the SD card -- then goes to the same stub ("STUBHAXX" at 0x80001804),
				 * or to the Wii System Menu when there is none, as this did by hand. */
				(void)rld;
				quit_crumb("exit(0)");
				exit(0);
#endif
			}
		}

		char increment = 3;
		fade = fade +increment > 255 ? 255 : fade + increment;
	}

	gfx->swapBuffers();
	PERF_ADD(menu_us, perf_now_us() - menu_t0);
}

void Gui::drawBackground()
{
	//Draw Menu Backdrop
	Resources::getInstance().getImage(Resources::IMAGE_MENU_BACKGROUND)->activateImage(GX_TEXMAP0);
//	gfx->setTEV(GX_REPLACE);
	GXColor muxCol = (GXColor){0,0,0,255};
	GX_SetTevColor(GX_TEVREG0,muxCol);
	GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_C0,GX_CC_ZERO,GX_CC_TEXC,GX_CC_TEXC);
	GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
	gfx->enableBlending(false);
	if(screenMode)	gfx->drawImage(0, -104, 0, 848, 480, 0, 1, 0, 1);
	else			gfx->drawImage(0, 0, 0, 640, 480, (848.0-640.0)/2/848.0, 1.0 - (848.0-640.0)/2/848.0, 0, 1);
	gfx->setTEV(GX_PASSCLR);
}

} //namespace menu
