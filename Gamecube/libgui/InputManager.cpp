/**
 * Wii64 - InputManager.cpp
 * Copyright (C) 2009 sepp256
 *
 * Wii64 homepage: http://www.emulatemii.com
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
#include <ogc/machine/processor.h>

#include "InputManager.h"
#include "FocusManager.h"
#include "CursorManager.h"
#include "../gc_input/controller.h"
#include "../DEBUG.h"

#include "../../HidController/KernelHID.h"
bool isWiiVC = false;

void ShutdownWii();
void initHid();



namespace menu {

Input::Input()
{
#ifdef HW_RVL
	initHid();
#endif // HW_RVL

	PAD_Init();
#ifdef HW_RVL
	CONF_Init();
	WUPC_Init();
	//WiiDRC_Init();
	//isWiiVC = WiiDRC_Inited();
	WPAD_Init();
	WPAD_SetIdleTimeout(120);
	WPAD_SetVRes(WPAD_CHAN_ALL, 640, 480);
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetPowerButtonCallback((WPADShutdownCallback)ShutdownWii);
	SYS_SetPowerCallback(ShutdownWii);

	/* HID */
	HIDUpdateControllerIni();

#endif
//	VIDEO_SetPostRetraceCallback (PAD_ScanPads);
}

Input::~Input()
{
#ifdef HW_RVL
	WUPC_Shutdown();
	WPAD_Shutdown();
#endif
}

void Input::initHid()
{
    int i;
    WiiDRC_Init();
    isWiiVC = WiiDRC_Inited();

	//memset((void*)0x93003010, 0, 0x190); //clears alot of pad stuff
	//DCFlushRange((void*)0x93003010, 0x190);
	//struct BTPadCont *BTPad = (struct BTPadCont*)0x932F0000;
	//for(i = 0; i < WPAD_MAX_WIIMOTES; ++i)
	//	BTPad[i].used = C_NOT_SET;
}

extern "C" unsigned autoinput_menukeys_frame;   /* PadWiiSX.c: "menukeys <frame> <keys>" */
extern "C" char autoinput_menukeys[64];

/* The scripted menu presses: what pad 0 holds on this menu frame (0 = nothing). The focus
 * manager ORs it into pad 0's buttons (it reads PAD_ButtonsHeld, not gcPad[]). */
u16 menu_script_keys;

static u16 scripted_menu_keys(void)
{
	static unsigned frame;
	unsigned f = ++frame, k;
	if (!autoinput_menukeys[0] || f < autoinput_menukeys_frame)
		return 0;
	f -= autoinput_menukeys_frame;
	k = f / 12;
	if (k >= strlen(autoinput_menukeys) || f % 12 >= 6)
		return 0;
	switch (autoinput_menukeys[k]) {
	case 'U': return PAD_BUTTON_UP;    case 'D': return PAD_BUTTON_DOWN;
	case 'L': return PAD_BUTTON_LEFT;  case 'R': return PAD_BUTTON_RIGHT;
	case 'A': return PAD_BUTTON_A;     case 'B': return PAD_BUTTON_B;
	case 'S': return PAD_BUTTON_START; case 'Z': return PAD_TRIGGER_Z;
	}
	return 0;   /* '.', a space: a pause */
}

void Input::refreshInput()
{
	if(padNeedScan){ gc_connected = PAD_ScanPads(); padNeedScan = 0; }
	PAD_Read(gcPad);
	PAD_Clamp(gcPad);
	menu_script_keys = scripted_menu_keys();
#ifdef HW_RVL
	wpad_scan_if_needed();
//	WPAD_ScanPads();
	wiiPad = WPAD_Data(0);
	wupcData = WUPC_Data(0);
	wiidrcData = WiiDRC_Data();

	if (hidPadNeedScan)
	{
		hidPadNeedScan = 0;
		if (loadingControllerIni)
        {
            HIDUpdateControllerIni();
        }
	}
#endif
}

#ifdef HW_RVL
WPADData* Input::getWpad()
{
	return wiiPad;
}

WUPCData* Input::getWupc()
{
	return wupcData;
}

const WiiDRCData* Input::getWiiDRC()
{
	return wiidrcData;
}
#endif

PADStatus* Input::getPad()
{
	return &gcPad[0];
}

void Input::clearInputData()
{
	Focus::getInstance().clearInputData();
	Cursor::getInstance().clearInputData();
}

} //namespace menu
