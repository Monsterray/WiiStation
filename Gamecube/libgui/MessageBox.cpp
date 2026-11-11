/**
 * Wii64 - MessageBox.cpp
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

#include "MessageBox.h"
#include "GraphicsGX.h"
#include "GuiResources.h"
#include "resources.h"
#include "Button.h"
#include "CursorManager.h"
#include "FocusManager.h"
#include "InputManager.h"
#include "IPLFont.h"
#include <stdio.h>
#include "../hb_probe.h"

extern GXTexRegion texCacheRegionS[8];              /* GraphicsGX.cpp: the font's texture region */
extern "C" int perf_chain_index(void);             /* GamecubeMain.cpp: >0 during a chain */
extern "C" void SysPrintf(const char *fmt, ...);

/* A chained run has nobody to press OK: a message box there waited for ever (2026-10-09, the
 * bench Wii: "This disc cannot be read" held the chain 7.9 hours, and every drawn frame
 * counted as progress for the hang watchdog). So during a chain a message goes to the
 * console log (tty.log), to stdout (the HBC agent's kept log) and to the heartbeat trail,
 * and the chain goes on. Returns true when the box is to be skipped. */
static bool unattended_message(const char *string)
{
	static char last[256];

	if (!perf_chain_index() || strstr(string, "Autobooting"))
		return false;
	snprintf(last, sizeof last, "%s", string);
	for (char *c = last; *c; c++)
		if (*c == '\n')
			*c = ' ';
	SysPrintf("message (chain %d, not shown): %s\n", perf_chain_index(), last);
	printf("message (chain %d, not shown): %s\n", perf_chain_index(), last);
	hb_phase("message", last);
	return true;
}

namespace menu {

void Func_MessageBoxOK();
void Func_MessageBoxCancel();

#define NUM_FRAME_BUTTONS 3
#define FRAME_BUTTONS messageBoxButtons
#define FRAME_STRINGS messageBoxStrings

static char FRAME_STRINGS[2][7] =
	{ "OK",
	  "Cancel"};

#define MESSAGEBOX_TEXT_WIDTH 512
char messageBoxText[MESSAGEBOX_TEXT_WIDTH];

/* Wrap messageBoxText to the box: each line is cut by its measured width in the menu font (the
 * box is 560 wide). A line breaks at the last space that fits, else after the last '/', else
 * inside the word (a long path, never in the middle of a UTF-8 character). */
static void wrapInto(const char *src, char *out, int cap, int maxW, float scale = 1.0f)
{
	char tmp[MESSAGEBOX_TEXT_WIDTH];
	int o = 0;
	const char *p = src;

	while (1)
	{
		const char *e = strchr(p, '\n');
		int len = e ? (int)(e - p) : (int)strlen(p);
		while (len > 0)
		{
			int n = 0, sp = -1, sl = -1;
			memcpy(tmp, p, len); tmp[len] = '\0';
			if (IplFont::getInstance().getStringWidth(tmp, scale) <= maxW)
				n = len;
			else
			{
				for (int i = 1; i <= len; i++)
				{
					tmp[i] = '\0';
					if ((p[i] & 0xC0) == 0x80) { tmp[i] = p[i]; continue; }
					if (IplFont::getInstance().getStringWidth(tmp, scale) > maxW) break;
					n = i;
					tmp[i] = p[i];
				}
				if (n < 1) n = 1;
				for (int i = 0; i < n; i++)
				{
					if (p[i] == ' ') sp = i;
					if (p[i] == '/') sl = i + 1;
				}
				if (p[n] == ' ') sp = n;
				if (sp > 0) n = sp;
				else if (sl > 0) n = sl;
			}
			if (o + n + 2 >= cap) { n = 0; len = 0; break; }
			memcpy(out + o, p, n); o += n;
			p += n; len -= n;
			if (len > 0)
			{
				out[o++] = '\n';
				if (*p == ' ') { p++; len--; }
			}
		}
		if (!e || o + 2 >= cap) break;
		out[o++] = '\n';
		p = e + 1;
	}
	out[o] = '\0';
}

static void wrapMessage()
{
	char out[MESSAGEBOX_TEXT_WIDTH];
	wrapInto(messageBoxText, out, sizeof out, 500);
	memcpy(messageBoxText, out, strlen(out) + 1);
}

/* The Game Loaded box (setGameLoaded): a title, "Key: value" rows with the key bold and a long value wrapped under
 * the value column, plain note lines, and the disc picture on the right when there is one. Built once when the box
 * opens; drawMessageBox only draws it. */
#define INFO_MAX_LINES 10
#define INFO_X 70			/* left edge of the keys */
#define INFO_Y 124			/* top of the first line */
#define INFO_LINE 22
#define INFO_SCALE 0.8f
#define INFO_PIC_X 436
#define INFO_PIC_Y 138
#define INFO_PIC_D 136
#define INFO_TITLE_SCALE 1.35f
static GXTexObj *infoDisc = NULL;
static bool infoMode = false;
static int infoCount, infoValueX;
struct InfoLine { char text[128]; char key[24]; unsigned char kind; };
static InfoLine infoLines[INFO_MAX_LINES];
enum { INFO_ROW_FIRST, INFO_ROW_MORE, INFO_NOTE_FIRST, INFO_NOTE };

/* Bold: the menu font has one weight, so bold is the string drawn a pixel to the right (TextBox does the same) */
static void drawBold(int x, int y, char *s, float scale, bool centered)
{
	IplFont::getInstance().drawString(x, y, s, scale, centered);
	IplFont::getInstance().drawString(x + 1, y, s, scale, centered);
}

static void infoAdd(unsigned char kind, const char *key, const char *text)
{
	if (infoCount >= INFO_MAX_LINES)
		return;
	snprintf(infoLines[infoCount].text, sizeof infoLines[0].text, "%s", text);
	snprintf(infoLines[infoCount].key, sizeof infoLines[0].key, "%s", key);
	infoLines[infoCount++].kind = kind;
}

/* Splits a wrapped text (lines joined by '\n') into lines of the box */
static void infoAddWrapped(unsigned char first, unsigned char more, const char *key, const char *wrapped)
{
	bool isFirst = true;
	const char *p = wrapped;
	while (1)
	{
		const char *e = strchr(p, '\n');
		char line[128];
		int n = e ? (int)(e - p) : (int)strlen(p);
		if (n > (int)sizeof line - 1) n = sizeof line - 1;
		memcpy(line, p, n); line[n] = '\0';
		infoAdd(isFirst ? first : more, isFirst ? key : "", line);
		isFirst = false;
		if (!e) break;
		p = e + 1;
	}
}

static void infoBuild(const char *text, bool pic)
{
	const int right = pic ? INFO_PIC_X - 14 : 600 - 30;
	char keys[INFO_MAX_LINES][24], vals[INFO_MAX_LINES][128];
	int nrows = 0, keyW = 0;
	const char *p = text;
	infoCount = 0;
	/* rows first (key TAB value), measured so the value column is one x */
	while (*p && nrows < INFO_MAX_LINES)
	{
		const char *e = strchr(p, '\n');
		int n = e ? (int)(e - p) : (int)strlen(p);
		const char *tab = (const char *)memchr(p, '\t', n);
		if (tab)
		{
			snprintf(keys[nrows], sizeof keys[0], "%.*s:", (int)(tab - p) < 20 ? (int)(tab - p) : 20, p);
			snprintf(vals[nrows], sizeof vals[0], "%.*s", n - (int)(tab - p) - 1 < 127 ? n - (int)(tab - p) - 1 : 127, tab + 1);
			int w = IplFont::getInstance().getStringWidth(keys[nrows], INFO_SCALE) + 1;
			if (w > keyW) keyW = w;
			nrows++;
		}
		if (!e) break;
		p = e + 1;
	}
	infoValueX = INFO_X + keyW + 10;
	for (int i = 0; i < nrows; i++)
	{
		char wrapped[256];
		wrapInto(vals[i], wrapped, sizeof wrapped, right - infoValueX, INFO_SCALE);
		infoAddWrapped(INFO_ROW_FIRST, INFO_ROW_MORE, keys[i], wrapped);
	}
	/* then the notes: the lines without a TAB */
	bool first = true;
	for (p = text; *p; )
	{
		const char *e = strchr(p, '\n');
		int n = e ? (int)(e - p) : (int)strlen(p);
		if (n > 0 && !memchr(p, '\t', n))
		{
			char one[128], wrapped[256];
			snprintf(one, sizeof one, "%.*s", n < 127 ? n : 127, p);
			wrapInto(one, wrapped, sizeof wrapped, right - INFO_X, INFO_SCALE);
			infoAddWrapped(first ? INFO_NOTE_FIRST : INFO_NOTE, INFO_NOTE, "", wrapped);
			first = false;
		}
		if (!e) break;
		p = e + 1;
	}
}

struct ButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	float			x;
	float			y;
	float			width;
	float			height;
	int				focusUp;
	int				focusDown;
	int				focusLeft;
	int				focusRight;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =
{ //	button	buttonStyle	buttonString		x		y		width	height	Up	Dwn	Lft	Rt	clickFunc				returnFunc
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[0],	220.0,	380.0,	200.0,	56.0,	-1,	-1,	-1,	-1,	Func_MessageBoxCancel,	Func_MessageBoxCancel }, // OK
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[0],	145.0,	380.0,	150.0,	56.0,	-1,	-1,	 2,	 2,	Func_MessageBoxOK,		Func_MessageBoxCancel }, // OK (OK/Cancel)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[1],	345.0,	380.0,	150.0,	56.0,	-1,	-1,	 1,	 1,	Func_MessageBoxCancel,	Func_MessageBoxCancel }, // Cancel (OK/Cancel)
};

MessageBox::MessageBox()
		: buttonImage(0),
		  buttonFocusImage(0),
		  messageBoxActive(false),
		  currentCursorFrame(0),
		  currentFocusFrame(0),
		  returnFocus(0),
		  returnValue(0)
{
	buttonImage = Resources::getInstance().getImage(Resources::IMAGE_DEFAULT_BUTTON);
	buttonFocusImage = Resources::getInstance().getImage(Resources::IMAGE_DEFAULT_BUTTONFOCUS);

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		FRAME_BUTTONS[i].button = new menu::Button(FRAME_BUTTONS[i].buttonStyle, &FRAME_BUTTONS[i].buttonString, 
										FRAME_BUTTONS[i].x, FRAME_BUTTONS[i].y, 
										FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].height);

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		if (FRAME_BUTTONS[i].focusUp != -1) FRAME_BUTTONS[i].button->setNextFocus(Focus::DIRECTION_UP, FRAME_BUTTONS[FRAME_BUTTONS[i].focusUp].button);
		if (FRAME_BUTTONS[i].focusDown != -1) FRAME_BUTTONS[i].button->setNextFocus(Focus::DIRECTION_DOWN, FRAME_BUTTONS[FRAME_BUTTONS[i].focusDown].button);
		if (FRAME_BUTTONS[i].focusLeft != -1) FRAME_BUTTONS[i].button->setNextFocus(Focus::DIRECTION_LEFT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusLeft].button);
		if (FRAME_BUTTONS[i].focusRight != -1) FRAME_BUTTONS[i].button->setNextFocus(Focus::DIRECTION_RIGHT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusRight].button);
		FRAME_BUTTONS[i].button->setActive(true);
		if (FRAME_BUTTONS[i].clickedFunc) FRAME_BUTTONS[i].button->setClicked(FRAME_BUTTONS[i].clickedFunc);
		if (FRAME_BUTTONS[i].returnFunc) FRAME_BUTTONS[i].button->setReturn(FRAME_BUTTONS[i].returnFunc);
		add(FRAME_BUTTONS[i].button);
		Cursor::getInstance().addComponent(this, FRAME_BUTTONS[i].button, FRAME_BUTTONS[i].x, 
												FRAME_BUTTONS[i].x+FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].y, 
												FRAME_BUTTONS[i].y+FRAME_BUTTONS[i].height);
	}

	setDefaultFocus(FRAME_BUTTONS[0].button);
	setBackFunc(Func_MessageBoxCancel);
	setEnabled(true);

	showFrame();
	boxColor = (GXColor) {100, 100, 100, 210};
	textColor = (GXColor) {255, 255, 255, 255};

}

MessageBox::~MessageBox()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		Cursor::getInstance().removeComponent(this, FRAME_BUTTONS[i].button);
		delete FRAME_BUTTONS[i].button;
	}
}

void MessageBox::setMessage(const char* string)
{
	infoMode = false;
	showMessage(string);
}

/* The box after a game loads. text: "Key<TAB>value" lines are rows, any other line a note (a warning, the save that
 * was loaded); disc: the disc picture's texture, or NULL for none (the text then has the full width). */
void MessageBox::setGameLoaded(const char* text, GXTexObj* disc)
{
	infoBuild(text, disc != NULL);
	infoDisc = disc;
	infoMode = true;
	showMessage(text);
	infoMode = false;
	infoDisc = NULL;
}

void MessageBox::showMessage(const char* string)
{
	if (unattended_message(string))
		return;
	if (messageFade > 0.0f) messageFade = 0.0f;
	messageBoxActive = true;

  if(strstr(string, "Autobooting"))
  {
	  messageFade = 1.0f;

	  FRAME_BUTTONS[0].button->setVisible(false);
	  FRAME_BUTTONS[0].button->setActive(false);
	  FRAME_BUTTONS[1].button->setVisible(false);
	  FRAME_BUTTONS[1].button->setActive(false);
	  FRAME_BUTTONS[2].button->setVisible(false);
	  FRAME_BUTTONS[2].button->setActive(false);
	  setDefaultFocus(this);
  }
  else
  {
	  FRAME_BUTTONS[0].button->setVisible(true);
	  FRAME_BUTTONS[0].button->setActive(true);
	  FRAME_BUTTONS[1].button->setVisible(false);
	  FRAME_BUTTONS[1].button->setActive(false);
	  FRAME_BUTTONS[2].button->setVisible(false);
	  FRAME_BUTTONS[2].button->setActive(false);
	  setDefaultFocus(FRAME_BUTTONS[0].button);
  }

	currentCursorFrame = Cursor::getInstance().getCurrentFrame();
	Cursor::getInstance().setCurrentFrame(this);
	currentFocusFrame = Focus::getInstance().getCurrentFrame();
	returnFocus = Focus::getInstance().actionTarget();   /* the button pressed: focus goes back to it */
	Focus::getInstance().setCurrentFrame(this);
	//memset(messageBoxText, 0, MESSAGEBOX_TEXT_WIDTH);
	strncpy(messageBoxText, string, MESSAGEBOX_TEXT_WIDTH);
	messageBoxText[MESSAGEBOX_TEXT_WIDTH - 1] = '\0';
	wrapMessage();

	while (messageBoxActive)
		menu::Gui::getInstance().draw();
//	Input::getInstance().clearInputData();
}

int MessageBox::askMessage(const char* string)
{
	if (unattended_message(string))
		return 0;   /* "No", the box's own default */
	if (messageFade > 0.0f) messageFade = 0.0f;
	messageBoxActive = true;
	FRAME_BUTTONS[0].button->setVisible(false);
	FRAME_BUTTONS[0].button->setActive(false);
	FRAME_BUTTONS[1].button->setVisible(true);
	FRAME_BUTTONS[1].button->setActive(true);
	FRAME_BUTTONS[2].button->setVisible(true);
	FRAME_BUTTONS[2].button->setActive(true);
	setDefaultFocus(FRAME_BUTTONS[2].button);

	currentCursorFrame = Cursor::getInstance().getCurrentFrame();
	Cursor::getInstance().setCurrentFrame(this);
	currentFocusFrame = Focus::getInstance().getCurrentFrame();
	returnFocus = Focus::getInstance().actionTarget();   /* the button pressed: focus goes back to it */
	Focus::getInstance().setCurrentFrame(this);
	//memset(messageBoxText, 0, MESSAGEBOX_TEXT_WIDTH);
	strncpy(messageBoxText, string, MESSAGEBOX_TEXT_WIDTH);
	messageBoxText[MESSAGEBOX_TEXT_WIDTH - 1] = '\0';
	wrapMessage();

	setReturnValue(0);
	while (messageBoxActive)
		menu::Gui::getInstance().draw();
//	Input::getInstance().clearInputData();
	return getReturnValue();
}

void MessageBox::fadeMessage(const char* string)
{
	messageBoxActive = true;
	FRAME_BUTTONS[0].button->setVisible(false);
	FRAME_BUTTONS[0].button->setActive(false);
	FRAME_BUTTONS[1].button->setVisible(false);
	FRAME_BUTTONS[1].button->setActive(false);
	FRAME_BUTTONS[2].button->setVisible(false);
	FRAME_BUTTONS[2].button->setActive(false);
	setDefaultFocus(this);

//	currentCursorFrame = Cursor::getInstance().getCurrentFrame();
//	Cursor::getInstance().setCurrentFrame(this);
//	currentFocusFrame = Focus::getInstance().getCurrentFrame();
//	Focus::getInstance().setCurrentFrame(this);
	//memset(messageBoxText, 0, MESSAGEBOX_TEXT_WIDTH);
	strncpy(messageBoxText, string, MESSAGEBOX_TEXT_WIDTH);
	messageBoxText[MESSAGEBOX_TEXT_WIDTH - 1] = '\0';
	wrapMessage();

	messageFade = 1.0f;

//	while (messageBoxActive)
//		menu::Gui::getInstance().draw();
//	Input::getInstance().clearInputData();
}

void MessageBox::setReturnValue(int returnVal)
{
	returnValue = returnVal;
}

int MessageBox::getReturnValue()
{
	return returnValue;
}

void MessageBox::deactivate()
{
	messageBoxActive = false;
	if (currentCursorFrame) Cursor::getInstance().setCurrentFrame(currentCursorFrame);
	currentCursorFrame = NULL;
	Focus::getInstance().clearPrimaryFocus();
	if (currentFocusFrame) Focus::getInstance().setCurrentFrame(currentFocusFrame);
	currentFocusFrame = NULL;
	/* Closing the box (OK, Cancel or B) leaves focus on the button that opened it, not on the
	 * screen's first button: B on the main menu after Credits or Quit did move it. */
	Focus::getInstance().setReturnFocus(returnFocus);
	returnFocus = NULL;
}

bool MessageBox::getActive()
{
	return messageBoxActive;
}

void MessageBox::drawMessageBox(Graphics& gfx)
{
	GXColor tmpBoxColor = boxColor;
	GXColor tmpTextColor = textColor;
	float x = 40; float y = 30; float width = 560; float height = 420;
	if (messageFade > 0.0f)
	{
		tmpBoxColor.a = tmpBoxColor.a*messageFade;
		tmpTextColor.a = tmpTextColor.a*messageFade;
		y = 140;
		height = 200;
		messageFade -= 0.01f;
		if (!(messageFade > 0.0f)) messageBoxActive = false;
	}

	gfx.setColor(tmpBoxColor);

	gfx.enableBlending(true);
	gfx.setTEV(GX_MODULATE);

	gfx.setDepth(-10.0f);
	gfx.newModelView();
	gfx.loadModelView();
	gfx.loadOrthographic();

	buttonImage->activateImage(GX_TEXMAP0);
	gfx.drawImage(0, x, y, width/2, height/2, 0.0, width/16.0, 0.0, height/16.0);
	gfx.drawImage(0, x+width/2, y, width/2, height/2, width/16.0, 0.0, 0.0, height/16.0);
	gfx.drawImage(0, x, y+height/2, width/2, height/2, 0.0, width/16.0, height/16.0, 0.0);
	gfx.drawImage(0, x+width/2, y+height/2, width/2, height/2, width/16.0, 0.0, height/16.0, 0.0);

	if (infoMode)
	{
		if (infoDisc)
		{
			GX_InvalidateTexRegion(&texCacheRegionS[0]);				/* the font's region: other texels */
			GX_LoadTexObjPreloaded(infoDisc, &texCacheRegionS[0], GX_TEXMAP0);
			gfx.setColor((GXColor) {255,255,255,255});
			gfx.setTEV(GX_MODULATE);
			gfx.enableBlending(true);
			gfx.drawImage(0, INFO_PIC_X, INFO_PIC_Y, INFO_PIC_D, INFO_PIC_D, 0, 1, 0, 1);
		}
		char title[] = "Game Loaded";
		IplFont::getInstance().drawInit(tmpTextColor);
		drawBold((int)(x + width/2), 46, title, INFO_TITLE_SCALE, true);
		int ly = INFO_Y;
		for (int i = 0; i < infoCount; i++)
		{
			InfoLine &l = infoLines[i];
			if (l.kind == INFO_ROW_FIRST && i > 0) ly += 6;
			if (l.kind == INFO_NOTE_FIRST) ly += 18;
			if (l.kind == INFO_NOTE_FIRST || l.kind == INFO_NOTE)
			{
				GXColor note = !strncmp(l.text, "Warning", 7) ? (GXColor) {255,210,110,255} : (GXColor) {170,230,170,255};
				note.a = tmpTextColor.a;
				IplFont::getInstance().setColor(note);
				IplFont::getInstance().drawString(INFO_X, ly, l.text, INFO_SCALE, false);
				IplFont::getInstance().setColor(tmpTextColor);
			}
			else
			{
				if (l.kind == INFO_ROW_FIRST)
					drawBold(INFO_X, ly, l.key, INFO_SCALE, false);
				IplFont::getInstance().drawString(infoValueX, ly, l.text, INFO_SCALE, false);
			}
			ly += INFO_LINE;
		}
		drawChildren(gfx);
		return;
	}

	//detect number of lines
#ifdef HW_RVL
	#define MAX_LINES 16
#else
	#define MAX_LINES 9
#endif
	int ind = 0;
	int numLines = 0;
	int lineStart[MAX_LINES];
	int lineEnd[MAX_LINES];
	lineStart[numLines] = ind;
	while (1)
	{
		if ((messageBoxText[ind]=='\n') || (messageBoxText[ind]=='\0')) 
		{
			lineEnd[numLines++] = ind;
			if (numLines==MAX_LINES) break;
			if (messageBoxText[ind]=='\0') break;
			lineStart[numLines] = ind+1;
		}
		ind++;
	}
	
	int lineSpacing = 20 + 2*MIN((MAX_LINES - numLines),6);
	char tempStr[256];
	IplFont::getInstance().drawInit(tmpTextColor);
	for (int i = 0; i < numLines; i++)
	{
		int heightOffset = -lineSpacing*numLines/2+lineSpacing*i;
		int numChar = lineEnd[i]-lineStart[i]; 
		numChar = numChar <= 255 ? numChar : 255;
		strncpy(tempStr,&messageBoxText[lineStart[i]],numChar);
		tempStr[numChar] = '\0';
		IplFont::getInstance().drawString((int) (x+width/2), (int) (y+height/2)-20+heightOffset, tempStr, 1.0, true);
	}
//	IplFont::getInstance().drawString((int) (x+width/2), (int) (y+height/2)-20+heightOffset, messageBoxText, 1.0, true);

	drawChildren(gfx);

}

void Func_MessageBoxOK()
{
	MessageBox::getInstance().setReturnValue(1);
	MessageBox::getInstance().deactivate();
}

void Func_MessageBoxCancel()
{
	MessageBox::getInstance().deactivate();
}

} //namespace menu 
