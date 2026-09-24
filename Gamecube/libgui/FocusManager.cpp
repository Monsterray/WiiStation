/**
 * Wii64 - FocusManager.cpp
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

#include <math.h>
#include "FocusManager.h"
#include "InputManager.h"
#include "Frame.h"
#include "IPLFont.h"

#include "../../HidController/KernelHID.h"

namespace menu {

/* The menu with the stick as well as the D-pad (2026-09-23). A stick pushed past STICK_ON of
 * its full travel reads as that D-pad direction until it falls back under STICK_OFF: the gap
 * stops a stick resting near the edge from flickering, and a thumb resting on the stick, or a
 * worn stick's drift, stays well under STICK_ON. The larger axis wins, so a diagonal gives one
 * direction; a held stick is one press, as a held D-pad is. Each device's result is ORed into
 * its own D-pad bits, so everything below treats the stick exactly as the D-pad. */
#define STICK_ON  0.60f
#define STICK_OFF 0.35f
enum { STICK_UP = 1, STICK_DOWN = 2, STICK_LEFT = 4, STICK_RIGHT = 8 };

/* x right +, y up +, each -1..1 of full travel. held: this stick's direction last time. */
static int stickDirection(float x, float y, int *held)
{
	float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
	float m = ax > ay ? ax : ay;
	int dir = 0;

	if (m >= STICK_ON)
		dir = ax > ay ? (x > 0 ? STICK_RIGHT : STICK_LEFT) : (y > 0 ? STICK_UP : STICK_DOWN);
	else if (m >= STICK_OFF)
		dir = *held;
	*held = dir;
	return dir;
}

static u32 stickBits(int dir, u32 up, u32 down, u32 left, u32 right)
{
	return (dir & STICK_UP ? up : 0) | (dir & STICK_DOWN ? down : 0) |
	       (dir & STICK_LEFT ? left : 0) | (dir & STICK_RIGHT ? right : 0);
}

#define GC_STICK_FULL 96.0f

static u16 stickGC(int i)
{
	static int held[4];
	int d = stickDirection(PAD_StickX(i) / GC_STICK_FULL, PAD_StickY(i) / GC_STICK_FULL, &held[i]);
	return (u16)stickBits(d, PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT);
}

#ifdef HW_RVL
/* A Wii joystick reports angle (degrees, 0 = up, clockwise) and magnitude (0..1) */
static int stickJoy(const joystick_t *j, int *held)
{
	float a = j->ang * 3.14159265f / 180.0f;
	return stickDirection(j->mag * sinf(a), j->mag * cosf(a), held);
}

/* The Classic Controller's left stick in Classic D-pad bits; the Nunchuk's in the Wiimote's */
static u32 stickWii(const WPADData *w, int i)
{
	static int held[4];
	if (w->exp.type == WPAD_EXP_CLASSIC)
		return stickBits(stickJoy(&w->exp.classic.ljs, &held[i]), WPAD_CLASSIC_BUTTON_UP,
			WPAD_CLASSIC_BUTTON_DOWN, WPAD_CLASSIC_BUTTON_LEFT, WPAD_CLASSIC_BUTTON_RIGHT);
	if (w->exp.type == WPAD_EXP_NUNCHUK)
		return stickBits(stickJoy(&w->exp.nunchuk.js, &held[i]), WPAD_BUTTON_UP,
			WPAD_BUTTON_DOWN, WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT);
	held[i] = 0;
	return 0;
}

static u32 stickWiiUPro(int i)
{
	static int held[4];
	int d = stickDirection(WUPC_lStickX(i) / 1024.0f, WUPC_lStickY(i) / 1024.0f, &held[i]);
	return stickBits(d, WPAD_CLASSIC_BUTTON_UP, WPAD_CLASSIC_BUTTON_DOWN,
		WPAD_CLASSIC_BUTTON_LEFT, WPAD_CLASSIC_BUTTON_RIGHT);
}

static u16 stickWiiUGamepad(void)
{
	static int held;
	int d = stickDirection(WiiDRC_lStickX() / 75.0f, WiiDRC_lStickY() / 75.0f, &held);
	return (u16)stickBits(d, WIIDRC_BUTTON_UP, WIIDRC_BUTTON_DOWN, WIIDRC_BUTTON_LEFT, WIIDRC_BUTTON_RIGHT);
}

static u16 stickGCHid(const PADStatus *p, int i)
{
	static int held[4];
	int d = stickDirection(p->stickX / GC_STICK_FULL, p->stickY / GC_STICK_FULL, &held[i]);
	return (u16)stickBits(d, PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT);
}
#endif

Focus::Focus()
		: focusActive(false),
		  pressed(false),
		  frameSwitch(true),
		  clearInput(true),
		  freezeAction(false),
		  buttonsPressed(0),
		  focusList(0),
		  primaryFocusOwner(0),
		  secondaryFocusOwner(0)
{
	for (int i=0; i<4; i++) {
		previousButtonsWii[i] = 0;
		previousButtonsGC[i] = 0;
		previousButtonsGCHid[i] = 0;
	}
}

Focus::~Focus()
{
}

void Focus::updateFocus()
{
	int focusDirection = 0;
	int buttonsDown = 0;
#ifdef HW_RVL
	WPADData* wiiPad = Input::getInstance().getWpad();
	PADStatus* hidGcPad = (PADStatus*)(HID_MEM2_PAD_BUFF); //PadBuff
	if (hidControllerConnected)
	{
		HidFormatData();
	}
#endif
//	PADStatus* gcPad = Input::getInstance().getPad();

	if (!focusActive) return;

	if (frameSwitch)
	{
		if(primaryFocusOwner) primaryFocusOwner->setFocus(false);
		if (currentFrame) primaryFocusOwner = currentFrame->getDefaultFocus();
		else primaryFocusOwner = NULL;
		frameSwitch = false;
	}

	if(clearInput)
	{
		for (int i=0; i<4; i++)
		{
			previousButtonsGC[i] = PAD_ButtonsHeld(i) | stickGC(i);
#ifdef HW_RVL
			previousButtonsGCHid[i] = hidGcPad[i].button | stickGCHid(&hidGcPad[i], i);
			previousButtonsWii[i] = wiiPad[i].btns_h | stickWii(&wiiPad[i], i);
			previousButtonsWiiUPro[i] = WUPC_ButtonsHeld(i) | stickWiiUPro(i);
			if(i == 0)
				previousButtonsWiiUGamepad[i] = WiiDRC_ButtonsHeld() | stickWiiUGamepad();
			else
				previousButtonsWiiUGamepad[i] = 0;
#endif
		}
		clearInput = false;
	}

	for (int i=0; i<4; i++)
	{
#ifdef HW_RVL
		u32 currentButtonsWiiUPro = WUPC_ButtonsHeld(i) | stickWiiUPro(i);
		u16 currentButtonsWiiUGamepad;
		u32 currentButtonsWii = wiiPad[i].btns_h | stickWii(&wiiPad[i], i);
		u16 currentButtonsGCHid = hidGcPad[i].button | stickGCHid(&hidGcPad[i], i);
		if(i == 0)
			currentButtonsWiiUGamepad = WiiDRC_ButtonsHeld() | stickWiiUGamepad();
		else
			currentButtonsWiiUGamepad = 0;
#endif
		u16 currentButtonsGC = PAD_ButtonsHeld(i) | stickGC(i);
		if (currentButtonsGC ^ previousButtonsGC[i])
		{
			u16 currentButtonsDownGC = (currentButtonsGC ^ previousButtonsGC[i]) & currentButtonsGC;
			switch (currentButtonsDownGC & 0xf) {
			case PAD_BUTTON_LEFT:
				focusDirection = DIRECTION_LEFT;
				break;
			case PAD_BUTTON_RIGHT:
				focusDirection = DIRECTION_RIGHT;
				break;
			case PAD_BUTTON_DOWN:
				focusDirection = DIRECTION_DOWN;
				break;
			case PAD_BUTTON_UP:
				focusDirection = DIRECTION_UP;
				break;
			default:
				focusDirection = DIRECTION_NONE;
			}
			if (currentButtonsDownGC & PAD_BUTTON_A) buttonsDown |= ACTION_SELECT;
			if (currentButtonsDownGC & PAD_BUTTON_B) buttonsDown |= ACTION_BACK;
			if (freezeAction)
			{
				focusDirection = DIRECTION_NONE;
				buttonsDown = 0;
			}
			if (primaryFocusOwner) primaryFocusOwner = primaryFocusOwner->updateFocus(focusDirection,buttonsDown);
			else primaryFocusOwner = currentFrame->updateFocus(focusDirection,buttonsDown);
			previousButtonsGC[i] = currentButtonsGC;
			break;
		}
#ifdef HW_RVL
		else if (currentButtonsWii ^ previousButtonsWii[i])
		{
			u32 currentButtonsDownWii = (currentButtonsWii ^ previousButtonsWii[i]) & currentButtonsWii;
			if (wiiPad[i].exp.type == WPAD_EXP_CLASSIC)
			{
				switch (currentButtonsDownWii & 0xc0030000) {
				case WPAD_CLASSIC_BUTTON_LEFT:
					focusDirection = DIRECTION_LEFT;
					break;
				case WPAD_CLASSIC_BUTTON_RIGHT:
					focusDirection = DIRECTION_RIGHT;
					break;
				case WPAD_CLASSIC_BUTTON_DOWN:
					focusDirection = DIRECTION_DOWN;
					break;
				case WPAD_CLASSIC_BUTTON_UP:
					focusDirection = DIRECTION_UP;
					break;
				default:
					focusDirection = DIRECTION_NONE;
				}
			}
			else
			{
				switch (currentButtonsDownWii & 0xf00) {
				case WPAD_BUTTON_LEFT:
					focusDirection = DIRECTION_LEFT;
					break;
				case WPAD_BUTTON_RIGHT:
					focusDirection = DIRECTION_RIGHT;
					break;
				case WPAD_BUTTON_DOWN:
					focusDirection = DIRECTION_DOWN;
					break;
				case WPAD_BUTTON_UP:
					focusDirection = DIRECTION_UP;
					break;
				default:
					focusDirection = DIRECTION_NONE;
				}
			}
			if (currentButtonsDownWii & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) buttonsDown |= ACTION_SELECT;
			if (currentButtonsDownWii & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) buttonsDown |= ACTION_BACK;
			if (freezeAction)
			{
				focusDirection = DIRECTION_NONE;
				buttonsDown = 0;
			}
			if (primaryFocusOwner) primaryFocusOwner = primaryFocusOwner->updateFocus(focusDirection,buttonsDown);
			else primaryFocusOwner = currentFrame->updateFocus(focusDirection,buttonsDown);
			previousButtonsWii[i] = currentButtonsWii;
			break;
		}
		else if (currentButtonsWiiUPro ^ previousButtonsWiiUPro[i])
		{
			switch (currentButtonsWiiUPro & 0xc0030000) {
			case WPAD_CLASSIC_BUTTON_LEFT:
				focusDirection = DIRECTION_LEFT;
				break;
			case WPAD_CLASSIC_BUTTON_RIGHT:
				focusDirection = DIRECTION_RIGHT;
				break;
			case WPAD_CLASSIC_BUTTON_DOWN:
				focusDirection = DIRECTION_DOWN;
				break;
			case WPAD_CLASSIC_BUTTON_UP:
				focusDirection = DIRECTION_UP;
				break;
			default:
				focusDirection = DIRECTION_NONE;
			}
			if (currentButtonsWiiUPro & WPAD_CLASSIC_BUTTON_A) buttonsDown |= ACTION_SELECT;
			if (currentButtonsWiiUPro & WPAD_CLASSIC_BUTTON_B) buttonsDown |= ACTION_BACK;
			if (freezeAction)
			{
				focusDirection = DIRECTION_NONE;
				buttonsDown = 0;
			}
			if (primaryFocusOwner) primaryFocusOwner = primaryFocusOwner->updateFocus(focusDirection, buttonsDown);
			else primaryFocusOwner = currentFrame->updateFocus(focusDirection, buttonsDown);
			previousButtonsWiiUPro[i] = currentButtonsWiiUPro;
			break;
		}
		else if (currentButtonsWiiUGamepad ^ previousButtonsWiiUGamepad[i])
		{
			switch (currentButtonsWiiUGamepad & 0x0F00) {
			case WIIDRC_BUTTON_LEFT:
				focusDirection = DIRECTION_LEFT;
				break;
			case WIIDRC_BUTTON_RIGHT:
				focusDirection = DIRECTION_RIGHT;
				break;
			case WIIDRC_BUTTON_DOWN:
				focusDirection = DIRECTION_DOWN;
				break;
			case WIIDRC_BUTTON_UP:
				focusDirection = DIRECTION_UP;
				break;
			default:
				focusDirection = DIRECTION_NONE;
			}
			if (currentButtonsWiiUGamepad & WIIDRC_BUTTON_A) buttonsDown |= ACTION_SELECT;
			if (currentButtonsWiiUGamepad & WIIDRC_BUTTON_B) buttonsDown |= ACTION_BACK;
			if (freezeAction)
			{
				focusDirection = DIRECTION_NONE;
				buttonsDown = 0;
			}
			if (primaryFocusOwner) primaryFocusOwner = primaryFocusOwner->updateFocus(focusDirection, buttonsDown);
			else primaryFocusOwner = currentFrame->updateFocus(focusDirection, buttonsDown);
			previousButtonsWiiUGamepad[i] = currentButtonsWiiUGamepad;
			break;
		}
		else if (currentButtonsGCHid ^ previousButtonsGCHid[i])
		{
			switch (currentButtonsGCHid & 0x0F) {
			case PAD_BUTTON_LEFT:
				focusDirection = DIRECTION_LEFT;
				break;
			case PAD_BUTTON_RIGHT:
				focusDirection = DIRECTION_RIGHT;
				break;
			case PAD_BUTTON_DOWN:
				focusDirection = DIRECTION_DOWN;
				break;
			case PAD_BUTTON_UP:
				focusDirection = DIRECTION_UP;
				break;
			default:
				focusDirection = DIRECTION_NONE;
			}
			if (currentButtonsGCHid & PAD_BUTTON_A) buttonsDown |= ACTION_SELECT;
			if (currentButtonsGCHid & PAD_BUTTON_B) buttonsDown |= ACTION_BACK;
			if (freezeAction)
			{
				focusDirection = DIRECTION_NONE;
				buttonsDown = 0;
			}
			if (primaryFocusOwner) primaryFocusOwner = primaryFocusOwner->updateFocus(focusDirection, buttonsDown);
			else primaryFocusOwner = currentFrame->updateFocus(focusDirection, buttonsDown);
			previousButtonsGCHid[i] = currentButtonsGCHid;
			break;
		}
#endif
	}
}

void Focus::addComponent(Component* component)
{
	focusList.push_back(component);
}

void Focus::removeComponent(Component* component)
{
	ComponentList::iterator iter = std::find(focusList.begin(), focusList.end(),component);
	if(iter != focusList.end())
	{
		focusList.erase(iter);
	}
}

Frame* Focus::getCurrentFrame()
{
	return currentFrame;
}

void Focus::setCurrentFrame(Frame* frame)
{
	currentFrame = frame;
	frameSwitch = true;
	Input::getInstance().clearInputData();
}

void Focus::setFocusActive(bool focusActiveBool)
{
	focusActive = focusActiveBool;
	if (primaryFocusOwner) primaryFocusOwner->setFocus(focusActive);
}

void Focus::clearInputData()
{
	clearInput = true;
}

void Focus::clearPrimaryFocus()
{
	if(primaryFocusOwner) primaryFocusOwner->setFocus(false);
	primaryFocusOwner = NULL;
	frameSwitch = true;
}

void Focus::setFreezeAction(bool freeze)
{
	freezeAction = freeze;
}

} //namespace menu
