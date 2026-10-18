/**
 * WiiStation - UsbPadTestFrame.cpp
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

/* The USB Pad Test page (Settings > Input > Test USB Pad), in the spirit of PadTest DX.
 *
 * It lights one PlayStation button at a time on the pad picture, waits for the USB controller's
 * report to change, and logs what changed: the raw bytes, which bytes and bits moved, and what
 * WiiStation's mapping (the controller ini + the HID pad's button config) made of it. Then both
 * sticks in each direction, then "press nothing". A on the Wii Remote / GameCube pad skips a
 * button the controller does not have; so does waiting 10 s. B stops. The log is
 * <device>:/wiistation/usbpad.log; scripts/usbpad_read.py turns it into a table.
 *
 * The reports come from HidController/KernelHID.c (HID_MEM2_PACKET_ADDR, hidProbe). A pad with
 * no controller ini is only read while this page is open (hidRawMode), and then only after it
 * is plugged in again; it is never given to a game. Nothing here changes how games read pads. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/pad.h>
#include "MenuContext.h"
#include "SettingsFrame.h"
#include "UsbPadTestFrame.h"
#include "../libgui/GuiResources.h"
#include "../libgui/Image.h"
#include "../libgui/IPLFont.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../version.h"
extern "C" {
#include "../gc_input/controller.h"
}
#include "../../HidController/KernelHID.h"

extern MenuContext *pMenuContext;

#define PAD_X 204   /* the pad picture, 232x152, where Configure Buttons draws it */
#define PAD_Y 188
#define MAXLEN 64   /* report bytes looked at and logged */

/* What to press, and where it is on the picture (x, y from its top left). stick: 0 a button,
 * 1 the left stick, 2 the right; dx, dy: the direction to push it. */
struct Step { const char *name; int x, y; int stick, dx, dy; };
static const Step STEPS[] = {
	{ "Up",        48, 40, 0, 0, 0 }, { "Down",     48, 70, 0, 0, 0 },
	{ "Left",      33, 55, 0, 0, 0 }, { "Right",    63, 55, 0, 0, 0 },
	{ "Triangle", 183, 35, 0, 0, 0 }, { "Circle",  205, 56, 0, 0, 0 },
	{ "Cross",    183, 77, 0, 0, 0 }, { "Square",  162, 56, 0, 0, 0 },
	{ "L1",        47,  8, 0, 0, 0 }, { "R1",      183,  8, 0, 0, 0 },
	{ "L2",        47,  8, 0, 0, 0 }, { "R2",      183,  8, 0, 0, 0 },
	{ "Select",    98, 56, 0, 0, 0 }, { "Start",   133, 56, 0, 0, 0 },
	{ "L3",        81, 92, 0, 0, 0 }, { "R3",      153, 92, 0, 0, 0 },
	{ "LStickUp",    81, 92, 1, 0, -1 }, { "LStickDown",  81, 92, 1, 0, 1 },
	{ "LStickLeft",  81, 92, 1, -1, 0 }, { "LStickRight", 81, 92, 1, 1, 0 },
	{ "RStickUp",   153, 92, 2, 0, -1 }, { "RStickDown", 153, 92, 2, 0, 1 },
	{ "RStickLeft", 153, 92, 2, -1, 0 }, { "RStickRight",153, 92, 2, 1, 0 },
	{ "Nothing",   116, 60, 0, 0, 0 },
};
static const int NSTEPS = sizeof(STEPS) / sizeof(STEPS[0]);
static const int NOTHING = NSTEPS - 1;

enum { ST_FIND, ST_REST, ST_WAIT, ST_HOLD, ST_RELEASE, ST_DONE };
static int state, step, frames;
static int haveCtrl;          /* reports are coming in */
static int len;               /* report length */
static u8 cur[MAXLEN], held[MAXLEN], restMin[MAXLEN], restMax[MAXLEN];
static FILE *logFile;
static char logPath[48];
static char line1[96], line2[96], line3[160], line4[96], lastResult[96];

static const u8 *packet() { return (const u8 *)HID_MEM2_PACKET_ADDR; }

static void readReport(void)
{
	len = hidProbe.lastLen > 0 ? hidProbe.lastLen : (int)hidProbe.maxPacket;
	if (len <= 0 || len > MAXLEN) len = MAXLEN;
	memcpy(cur, packet(), len);
}

/* Byte i is away from rest: outside what it did at rest, by more than its noise for a noisy one
 * (a stick at rest, a motion sensor), by anything for a still one (buttons). */
static bool moved(const u8 *r, int i)
{
	int lo = restMin[i], hi = restMax[i];
	int m = hi > lo ? (hi - lo > 24 ? hi - lo : 24) : 0;
	return r[i] < lo - m || r[i] > hi + m;
}
static bool anyMoved(const u8 *r)
{
	for (int i = 0; i < len; i++)
		if (moved(r, i)) return true;
	return false;
}

static void hex(char *out, int size, const u8 *b, int n)
{
	int k = 0;
	out[0] = 0;
	for (int i = 0; i < n && k + 3 < size; i++)
		k += snprintf(out + k, size - k, "%02X", b[i]);
}

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
	if (!logFile) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(logFile, fmt, ap);
	va_end(ap);
	fflush(logFile);
}

/* What WiiStation makes of the pad now: the GameCube-style buttons the ini decodes, the
 * PlayStation buttons the HID pad's button config turns them into, the sticks. Only for a pad
 * with an ini (a raw pad is decoded by nothing). */
static void logMapping(void)
{
	if (!hidControllerConnected)
	{
		logf_(" gc=- ps1=-");
		return;
	}
	HidFormatData();
	PADStatus *p = (PADStatus *)HID_MEM2_PAD_BUFF;
	u16 b = p[0].button;
	char gc[160] = "", ps1[160] = "";
	for (int i = 1; i <= 14 && i < controller_HidGC.num_buttons; i++)
	{
		button_t *t = &controller_HidGC.buttons[i];
		if ((b & t->mask) == t->mask)
			snprintf(gc + strlen(gc), sizeof(gc) - strlen(gc), "%s%s", gc[0] ? "," : "", t->name);
	}
	controller_config_t *c = &controller_HidGC.config[0];
	const struct { button_tp t; const char *name; } map[] = {
		{ c->DU, "Up" }, { c->DD, "Down" }, { c->DL, "Left" }, { c->DR, "Right" },
		{ c->TRI, "Triangle" }, { c->CIR, "Circle" }, { c->CRO, "Cross" }, { c->SQU, "Square" },
		{ c->L1, "L1" }, { c->R1, "R1" }, { c->L2, "L2" }, { c->R2, "R2" },
		{ c->SELECT, "Select" }, { c->START, "Start" }, { c->L3, "L3" }, { c->R3, "R3" } };
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (map[i].t && (b & map[i].t->mask) == map[i].t->mask)
			snprintf(ps1 + strlen(ps1), sizeof(ps1) - strlen(ps1), "%s%s", ps1[0] ? "," : "", map[i].name);
	logf_(" gc=%s ps1=%s buttons=%04X sticks=%d,%d,%d,%d triggers=%u,%u",
		gc[0] ? gc : "-", ps1[0] ? ps1 : "-", b, p[0].stickX, p[0].stickY,
		p[0].substickX, p[0].substickY, p[0].triggerL, p[0].triggerR);
}

/* The bytes of r away from rest, as off:rest>now (hex), into out */
static void changes(char *out, int size, const u8 *r)
{
	int k = 0;
	out[0] = 0;
	for (int i = 0; i < len && k + 12 < size; i++)
		if (moved(r, i))
			k += snprintf(out + k, size - k, "%s%d:%02X>%02X", k ? "," : "", i,
				(restMin[i] + restMax[i]) / 2, r[i]);
}

static void openLog(void)
{
	static const char *paths[] = { "sd:/wiistation/usbpad.log", "usb:/wiistation/usbpad.log" };
	if (logFile) fclose(logFile);
	logFile = NULL;
	for (int i = 0; i < 2 && !logFile; i++)
		if ((logFile = fopen(paths[i], "w")) != NULL)
			snprintf(logPath, sizeof(logPath), "%s", paths[i]);
	if (!logFile) snprintf(logPath, sizeof(logPath), "(no card to write to)");
}

static void closeLog(const char *how)
{
	logf_("end: %s\n", how);
	if (logFile) fclose(logFile);
	logFile = NULL;
}

/* The header: the HID driver, the devices, the picked one and its descriptors. */
static void logHeader(void)
{
	char buf[1100];
	logf_("# WiiStation USB Pad Test -- read it with scripts/usbpad_read.py\n");
	logf_("version: %s\n", WIISTATION_VERSION);
	logf_("hid: %s\n", IsHidRuning() ? "running" : "not-running");
	logf_("devices: %u", hidProbe.nDevices);
	for (unsigned i = 0; i < hidProbe.nDevices && i < 8; i++)
		logf_(" %04X:%04X", hidProbe.vids[i], hidProbe.pids[i]);
	logf_("\n");
	if (!hidProbe.picked)
	{
		logf_("device: none\nmode: none\n");
		return;
	}
	logf_("device: vid=%04X pid=%04X product=\"%s\" class=%02X subclass=%02X protocol=%02X "
		"interface=%u ep_in=%02X ep_out=%02X maxpacket=%u\n", hidProbe.vid, hidProbe.pid,
		hidProbe.product, hidProbe.ifClass, hidProbe.ifSubClass, hidProbe.ifProtocol,
		hidProbe.ifNumber, hidProbe.epIn, hidProbe.epOut, hidProbe.maxPacket);
	for (int d = 0; d < 2; d++)
	{
		char ini[64];
		snprintf(ini, sizeof(ini), "%s:/wiistation/controllers/%04X_%04X.ini", d ? "usb" : "sd",
			hidProbe.vid, hidProbe.pid);
		logf_("ini: %s %s\n", ini, access(ini, F_OK) == 0 ? "found" : "missing");
	}
	logf_("mode: %s\n", !hidProbe.reading ? "not-read" : hidProbe.raw ? "raw" : "mapped");
	hex(buf, sizeof(buf), hidProbe.params, sizeof(hidProbe.params));
	logf_("params: %s\n", buf);
	if (hidProbe.request)
		logf_("reportdesc: not-read\n");
	else if (hidProbe.descLen <= 0)
		logf_("reportdesc: error=%d\n", hidProbe.descLen);
	else
	{
		hex(buf, sizeof(buf), hidProbe.desc, hidProbe.descLen);
		logf_("reportdesc: len=%d %s\n", hidProbe.descLen, buf);
	}
}

static void logRest(void)
{
	char buf[MAXLEN * 2 + 1];
	logf_("reportlen: %d\n", len);
	hex(buf, sizeof(buf), restMin, len);
	logf_("rest: %s\n", buf);
	logf_("noise:");
	bool any = false;
	for (int i = 0; i < len; i++)
		if (restMax[i] != restMin[i])
		{
			logf_(" %d=%02X-%02X", i, restMin[i], restMax[i]);
			any = true;
		}
	logf_("%s\n", any ? "" : " none");
}

static void setPrompt(void)
{
	if (step >= NSTEPS) return;
	if (step == NOTHING)
		snprintf(line1, sizeof(line1), "Press nothing for 3 seconds");
	else if (STEPS[step].stick)   /* "LStickDown": the left stick, down */
		snprintf(line1, sizeof(line1), "Push the %s stick %s", STEPS[step].stick == 1 ? "left" : "right",
			STEPS[step].dy < 0 ? "up" : STEPS[step].dy > 0 ? "down" : STEPS[step].dx < 0 ? "left" : "right");
	else
		snprintf(line1, sizeof(line1), "Press %s on the USB controller", STEPS[step].name);
}

/* One step's line in the log, and on the screen */
static void logStep(const char *result, const u8 *r)
{
	char raw[MAXLEN * 2 + 1], ch[400];
	logf_("step %d %s: %s", step + 1, STEPS[step].name, result);
	if (r)
	{
		hex(raw, sizeof(raw), r, len);
		changes(ch, sizeof(ch), r);
		logf_(" raw=%s changed=%s", raw, ch[0] ? ch : "-");
		logMapping();
		snprintf(lastResult, sizeof(lastResult), "%s: %s %.60s", STEPS[step].name, result, ch);
	}
	else
		snprintf(lastResult, sizeof(lastResult), "%s: %s", STEPS[step].name, result);
	logf_("\n");
}

static void nextStep(void)
{
	step++;
	frames = 0;
	if (step >= NSTEPS)
	{
		closeLog("done");
		state = ST_DONE;
		snprintf(line1, sizeof(line1), "Done: A to test again, B to go back");
		return;
	}
	state = ST_WAIT;
	setPrompt();
}

/* (Re)start: find the pad, learn its rest, then the steps. */
static void startTest(void)
{
	hidRawMode = 1;
	hidProbe.request = 1;   /* the HID thread reads the report descriptor and the name */
	state = ST_FIND;
	step = frames = 0;
	haveCtrl = 0;
	lastResult[0] = 0;
	snprintf(line1, sizeof(line1), "Looking for a USB controller...");
}

void UsbPadTestFrame::updateFrame(float deltaTime)
{
	frames++;
	if (hidProbe.reading)
		readReport();

	/* the device line */
	if (!IsHidRuning())
		snprintf(line2, sizeof(line2), "USB controllers are not available (no IOS58 USB HID)");
	else if (hidProbe.nDevices == 0)
		snprintf(line2, sizeof(line2), "No USB controller: plug one in");
	else if (!hidProbe.picked)
		snprintf(line2, sizeof(line2), "No USB controller (keyboard or mouse only)");
	else if (!hidProbe.reading)
		snprintf(line2, sizeof(line2), "%04X:%04X has no controller ini: unplug it and plug it in again",
			hidProbe.vid, hidProbe.pid);
	else
		snprintf(line2, sizeof(line2), "%04X:%04X %.24s, %s", hidProbe.vid, hidProbe.pid,
			hidProbe.product[0] ? hidProbe.product : "", hidProbe.raw ? "raw (no ini)" : "ini found");
	if (hidProbe.reading)
	{
		char h[MAXLEN * 3 + 1];
		int k = 0, n = len < 20 ? len : 20;
		for (int i = 0; i < n; i++)
			k += snprintf(h + k, sizeof(h) - k, "%02X ", cur[i]);
		snprintf(line3, sizeof(line3), "Report (%d bytes): %s%s", len, h, len > n ? "..." : "");
	}
	else
		line3[0] = 0;

	/* a pad that turns up (or comes back raw after a replug) mid-test starts it again */
	if (!haveCtrl && hidProbe.reading && state != ST_FIND && state != ST_DONE)
		startTest();

	switch (state)
	{
	case ST_FIND:
		if (hidProbe.reading && (!hidProbe.request || frames > 120))
		{
			haveCtrl = 1;
			state = ST_REST;
			frames = 0;
			memcpy(restMin, cur, len);
			memcpy(restMax, cur, len);
			snprintf(line1, sizeof(line1), "Hands off the controller...");
		}
		else if (frames > 180)   /* 3 s: no controller; walk the steps anyway, so the log says so */
		{
			hidProbe.request = 0;
			openLog();
			logHeader();
			logf_("reportlen: 0\n");
			step = -1;
			nextStep();
		}
		break;
	case ST_REST:   /* 1 s of rest: each byte's range */
		for (int i = 0; i < len; i++)
		{
			if (cur[i] < restMin[i]) restMin[i] = cur[i];
			if (cur[i] > restMax[i]) restMax[i] = cur[i];
		}
		if (frames >= 60)
		{
			openLog();
			logHeader();
			logRest();
			step = -1;
			nextStep();
		}
		break;
	case ST_WAIT:
		if (!haveCtrl)
		{
			if (frames >= 120) { logStep("no-controller", NULL); nextStep(); }
			break;
		}
		if (step == NOTHING)
		{
			if (anyMoved(cur)) { logStep("moved", cur); nextStep(); }
			else if (frames >= 180) { logStep("rest-ok", NULL); nextStep(); }
			break;
		}
		if (anyMoved(cur))
		{
			memcpy(held, cur, len);
			state = ST_HOLD;
			frames = 0;
		}
		else if (frames >= 600)
		{
			logStep("timeout", NULL);
			nextStep();
		}
		break;
	case ST_HOLD:   /* a third of a second into the press: the bytes it holds */
		if (anyMoved(cur))
			memcpy(held, cur, len);
		if (frames >= 20 || !anyMoved(cur))
		{
			logStep("pressed", held);
			state = ST_RELEASE;
			frames = 0;
			snprintf(line1, sizeof(line1), "Let go");
		}
		break;
	case ST_RELEASE:
		if (!anyMoved(cur) || frames >= 300)
		{
			if (frames >= 300) logf_("step %d %s: release-timeout\n", step + 1, STEPS[step].name);
			nextStep();
		}
		break;
	}
}

/* A press on the menu's controllers (Wii Remote, GameCube pad, ...). A configured USB pad drives
 * the menu too: its presses are what is being tested, so they never skip or stop. */
static bool usbPadBusy(void)
{
	return haveCtrl && hidProbe.reading && state != ST_DONE && (readReport(), anyMoved(cur));
}

static void Func_UsbPadTestSkip()
{
	if (usbPadBusy()) return;
	if (state == ST_DONE) { startTest(); return; }
	if (state == ST_WAIT || state == ST_HOLD)
	{
		logStep("skipped", NULL);
		nextStep();
	}
}

static void Func_UsbPadTestBack()
{
	if (usbPadBusy()) return;
	if (logFile) closeLog("stopped");
	hidRawMode = 0;
	state = ST_DONE;
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, SettingsFrame::SUBMENU_INPUT);
}

void Func_UsbPadTest()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_USBPADTEST, 0);
}

UsbPadTestFrame::UsbPadTestFrame()
{
	setBackFunc(Func_UsbPadTestBack);
	setSelectFunc(Func_UsbPadTestSkip);
	setEnabled(true);
	snprintf(line4, sizeof(line4), "Wii Remote / GameCube pad:  A skip,  B stop");
}

UsbPadTestFrame::~UsbPadTestFrame()
{
	if (logFile) fclose(logFile);
}

void UsbPadTestFrame::activateSubmenu(int submenu)
{
	startTest();
}

void UsbPadTestFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;
	updateFrame(0);   /* nothing calls a frame's updateTime: the page ticks once per drawn frame */
	GXColor white = {255, 255, 255, 255}, lit = {255, 200, 0, 255}, grey = {160, 160, 160, 255};

	gfx.setColor(white);
	menu::Image *pad = menu::Resources::getInstance().getImage(menu::Resources::IMAGE_PSX_CONTROLLER);
	pad->activateImage(GX_TEXMAP0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	gfx.enableBlending(true);
	gfx.drawImage(0, PAD_X, PAD_Y, 232, 152, 0, 1, 0, 1);
	gfx.setTEV(GX_PASSCLR);

	/* the button asked for: a ring; a stick also gets the direction */
	if (state >= ST_WAIT && state <= ST_RELEASE && step < NOTHING)
	{
		const Step &s = STEPS[step];
		int x = PAD_X + s.x, y = PAD_Y + s.y;
		gfx.setColor(lit);
		gfx.setLineWidth(3);
		gfx.drawCircle(x, y, s.stick || step >= 14 ? 22 : 11, 24);
		if (s.stick)
			gfx.drawLine(x, y, x + 34 * s.dx, y + 34 * s.dy);
		gfx.setLineWidth(1);
	}

	menu::IplFont &font = menu::IplFont::getInstance();
	char title[] = "USB Pad Test";
	font.drawInit(white);
	font.drawString(320, 40, title, 1.0f, true);
	font.drawInit(state == ST_DONE ? white : lit);
	font.drawString(320, 90, line1, 1.0f, true);
	if (step >= 8 && step <= 11 && state >= ST_WAIT && state <= ST_RELEASE)
		font.drawString(PAD_X + STEPS[step].x, PAD_Y - 22, (char *)STEPS[step].name, 0.8f, true);
	font.drawInit(white);
	font.drawString(320, 130, line2, 0.7f, true);
	if (state > ST_REST)
	{
		char prog[96];
		snprintf(prog, sizeof(prog), "Step %d of %d   %s", step < NSTEPS ? step + 1 : NSTEPS, NSTEPS, lastResult);
		font.drawString(320, 360, prog, 0.6f, true);
	}
	font.drawString(320, 385, line3, 0.55f, true);
	font.drawInit(grey);
	if (state == ST_DONE || state > ST_REST)
	{
		char where[80];
		snprintf(where, sizeof(where), "Log: %s", logPath);
		font.drawString(320, 410, where, 0.6f, true);
	}
	font.drawString(320, 435, line4, 0.6f, true);
}
