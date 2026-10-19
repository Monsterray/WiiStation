/**
 * WiiStation - UsbPadTestFrame.h
 * Copyright (C) 2026 WiiStation
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

#ifndef USBPADTESTFRAME_H
#define USBPADTESTFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

/* Settings > Input > Test USB Pad: asks for each PlayStation button in turn, reads what the USB
 * controller sends, and writes it all to <device>:/wiistation/usbpad.log
 * (scripts/usbpad_read.py, Docs/CONTROLLER_TESTING.md). */
class UsbPadTestFrame : public menu::Frame
{
public:
	UsbPadTestFrame();
	~UsbPadTestFrame();
	void activateSubmenu(int submenu);
	void updateFrame(float deltaTime);
	void drawChildren(menu::Graphics& gfx);
};

void Func_UsbPadTest();   /* the Settings button: opens the page */

#endif
