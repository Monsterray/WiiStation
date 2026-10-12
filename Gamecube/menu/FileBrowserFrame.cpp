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
#include "../config_parse.h"   /* config_list_next */
#include "MenuGlue.h"
#define CONFIG_ITEM_SIZE 256
#include "MenuContext.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "FileBrowserFrame.h"
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
#include "../../database.h"
extern long ISOgetTN(unsigned char *buffer);
extern char debugInfo[256];
}


namespace eng = menu::engine;

void Func_PrevPage();
void Func_NextPage();
void Func_ReturnFromFileBrowserFrame();
void Func_Select1();
void Func_Select2();
void Func_Select3();
void Func_Select4();
void Func_Select5();
void Func_Select6();
void Func_Select7();
void Func_Select8();
void Func_Select9();
void Func_Select10();

static const int NUM_FRAME_BUTTONS = 12;	/* 0 Prev, 1 Next, 2..11 the file slots */
static const int NUM_FILE_SLOTS = 10;

static char fileBrowserStrings[3][5] =
	{ "Prev",
	  "Next",
	  ""};

/* The directory listing, owned here: dir_entries holds num_entries entries. */
static fileBrowser_file* dir_entries;
static bool				romFolderList;   /* dir_entries are the RomDir folders: show whole paths */
static int				num_entries;

/* The buttons: 0 Prev, 1 Next, 2..11 the file slots. Their boxes, focus links, text and label
 * colour come from the engine's list layout (FB_LAYOUT), made again whenever the page changes. */
struct FileBrowserButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
	int				ctl;			/* this button's control in FB_LAYOUT */
};

static FileBrowserButtonInfo fileBrowserButtons[NUM_FRAME_BUTTONS] =
{ //	button	buttonStyle		buttonString		clickFunc		returnFunc
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[0],	Func_PrevPage,	Func_ReturnFromFileBrowserFrame }, // Prev
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[1],	Func_NextPage,	Func_ReturnFromFileBrowserFrame }, // Next
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select1,	Func_ReturnFromFileBrowserFrame }, // File Button 1
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select2,	Func_ReturnFromFileBrowserFrame }, // File Button 2
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select3,	Func_ReturnFromFileBrowserFrame }, // File Button 3
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select4,	Func_ReturnFromFileBrowserFrame }, // File Button 4
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select5,	Func_ReturnFromFileBrowserFrame }, // File Button 5
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select6,	Func_ReturnFromFileBrowserFrame }, // File Button 6
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select7,	Func_ReturnFromFileBrowserFrame }, // File Button 7
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select8,	Func_ReturnFromFileBrowserFrame }, // File Button 8
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select9,	Func_ReturnFromFileBrowserFrame }, // File Button 9
	{	NULL,	BTN_A_NRM,	fileBrowserStrings[2],	Func_Select10,	Func_ReturnFromFileBrowserFrame }, // File Button 10
};

/*  The list: the engine's ROW_LIST, paged (Gamecube/menu/MenuEngine.h)  */

static void fileBrowserItem(void *ctx, int index, eng::ListItem &out);

/* The file list: 10 slots, the page turned by Prev and Next (clamped) or by L and R (wrapped).
 * The boxes are those the browser has always drawn: 400 x 35 from (120, 40), 40 apart; Prev and
 * Next beside the list. */
static eng::ListState FB_LIST = { 0, NUM_FILE_SLOTS, 0, -1 };
static eng::ListSpec  FB_SPEC = {
	&FB_LIST, eng::LIST_PAGED, fileBrowserItem, NULL,
	120.0f, 40.0f, 400.0f, 35.0f, 40.0f,
	{ "Prev",  35.0f, 220.0f, 70.0f, 40.0f },
	{ "Next", 535.0f, 220.0f, 70.0f, 40.0f }
};
static eng::Row    FB_ROW;
static eng::Page   FB_PAGE;
static eng::Layout FB_LAYOUT;

/* The engine's text widths: IplFont, as the other frames use it (CurrentRomFrame.cpp). */
static void setupFileBrowserPage(void)
{
	memset(&FB_ROW, 0, sizeof FB_ROW);
	FB_ROW.kind = eng::ROW_LIST;
	FB_ROW.list = &FB_SPEC;
	memset(&FB_PAGE, 0, sizeof FB_PAGE);
	FB_PAGE.rows = &FB_ROW;
	FB_PAGE.nRows = 1;
	FB_PAGE.logo = eng::LOGO_SPOT_TOP;		/* the browser has always had the logo at the top */
	FB_PAGE.focus = eng::FOCUS_ROWS;
}

/* Lays the page out for the list as it is now: once per page turn, never per frame. */
static void layoutFileBrowser(void)
{
	eng::layoutPage(FB_PAGE, menu::glue::METRICS, FB_LAYOUT);
	eng::linkFocus(FB_PAGE, FB_LAYOUT);
}

/* The control of fileBrowserButtons[i]: Prev, Next, then the window's items, in slot order. */
static int fileBrowserControl(int i)
{
	if (i == 0) return FB_LAYOUT.slot[FB_LAYOUT.list][0];
	if (i == 1) return FB_LAYOUT.slot[FB_LAYOUT.list][1];
	return FB_LAYOUT.listItem[i - 2];
}

/* The button drawn for a control of FB_LAYOUT, or NULL. */
static menu::Button *fileBrowserButtonAt(int ctl)
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		if (fileBrowserButtons[i].ctl == ctl)
			return fileBrowserButtons[i].button;
	return NULL;
}

/* The button drawn for a layout control. */
static menu::Button *fileBrowserButtonOf(const eng::Control &c)
{
	return fileBrowserButtonAt(int(&c - FB_LAYOUT.c));
}

/* Copies the layout onto the buttons: text, label colour, active, and every focus link. */
static void applyFileBrowserLayout(void)
{

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = FB_LAYOUT.c[fileBrowserButtons[i].ctl];
		fileBrowserButtons[i].buttonString = (char*)c.text;	/* the button reads its text through this */
		if (c.folder)
			fileBrowserButtons[i].button->setLabelColor((GXColor) {254,213,95,255});
		else
			fileBrowserButtons[i].button->setLabelColor((GXColor) {255,255,255,255});
		fileBrowserButtons[i].button->setActive(c.focusable);
	}
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		menu::glue::linkFocus(fileBrowserButtons[i].button, FB_LAYOUT,
		                      FB_LAYOUT.c[fileBrowserButtons[i].ctl], fileBrowserButtonOf);
}

FileBrowserFrame::FileBrowserFrame()
{
	setupFileBrowserPage();
	layoutFileBrowser();		/* an empty list: every button is drawn inactive, at its box */

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		fileBrowserButtons[i].ctl = fileBrowserControl(i);
		const eng::Control &c = FB_LAYOUT.c[fileBrowserButtons[i].ctl];
		fileBrowserButtons[i].button = new menu::Button(fileBrowserButtons[i].buttonStyle, &fileBrowserButtons[i].buttonString,
										c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	}
	applyFileBrowserLayout();

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		const eng::Control &c = FB_LAYOUT.c[fileBrowserButtons[i].ctl];
		if (fileBrowserButtons[i].clickedFunc) fileBrowserButtons[i].button->setClicked(fileBrowserButtons[i].clickedFunc);
		if (fileBrowserButtons[i].returnFunc) fileBrowserButtons[i].button->setReturn(fileBrowserButtons[i].returnFunc);
		add(fileBrowserButtons[i].button);
		menu::Cursor::getInstance().addComponent(this, fileBrowserButtons[i].button, c.rect.x,
												c.rect.x+c.rect.w, c.rect.y,
												c.rect.y+c.rect.h);
	}

	for (int i = 2; i < NUM_FRAME_BUTTONS; i++)
	{
		fileBrowserButtons[i].button->setLabelMode(menu::Button::LABEL_SCROLLONFOCUS);
		fileBrowserButtons[i].button->setLabelScissor(6);
	}

	setDefaultFocus(fileBrowserButtons[2].button);
	setBackFunc(Func_ReturnFromFileBrowserFrame);
	setEnabled(true);

}

FileBrowserFrame::~FileBrowserFrame()
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, fileBrowserButtons[i].button);
		delete fileBrowserButtons[i].button;
	}

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

void FileBrowserFrame::drawChildren(menu::Graphics &gfx)
{
	if(isVisible())
	{
#ifdef HW_RVL
		WPADData* wiiPad = menu::Input::getInstance().getWpad();
		PADStatus* hidGcPad = (PADStatus*)(HID_MEM2_PAD_BUFF); //PadBuff
#endif
		for (int i=0; i<4; i++)
		{
			u16 currentButtonsGC = PAD_ButtonsHeld(i);
			if (currentButtonsGC ^ previousButtonsGC[i])
			{
				u16 currentButtonsDownGC = (currentButtonsGC ^ previousButtonsGC[i]) & currentButtonsGC;
				previousButtonsGC[i] = currentButtonsGC;
				if (currentButtonsDownGC & PAD_TRIGGER_Z)
				{
					// Change sort method
					fileSortMode ^= 1;
					// Resort the list
					qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator);
					FB_LIST.top = 0;
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
				else if (currentButtonsDownGC & PAD_TRIGGER_R)
				{
					//move to next set & return
					eng::listTurn(FB_LIST, 1, true);
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
				else if (currentButtonsDownGC & PAD_TRIGGER_L)
				{
					//move to the previous set & return
					eng::listTurn(FB_LIST, -1, true);
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
			}
#ifdef HW_RVL
			else if (wiiPad[i].btns_h ^ previousButtonsWii[i])
			{
				u32 currentButtonsDownWii = (wiiPad[i].btns_h ^ previousButtonsWii[i]) & wiiPad[i].btns_h;
				previousButtonsWii[i] = wiiPad[i].btns_h;
				if (wiiPad[i].exp.type == WPAD_EXP_CLASSIC)
				{
					if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_ZR)
					{
						// Change sort method
						fileSortMode ^= 1;
						// Resort the list
						qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator);
						FB_LIST.top = 0;
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
					else if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_R)
					{
						//move to next set & return
						eng::listTurn(FB_LIST, 1, true);
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
					else if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_L)
					{
						//move to the previous set & return
						eng::listTurn(FB_LIST, -1, true);
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
				}
				else
				{
					if (currentButtonsDownWii & WPAD_BUTTON_1)
					{
						// Change sort method
						fileSortMode ^= 1;
						// Resort the list
						qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator);
						FB_LIST.top = 0;
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
					else if (currentButtonsDownWii & WPAD_BUTTON_PLUS)
					{
						//move to next set & return
						eng::listTurn(FB_LIST, 1, true);
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
					else if (currentButtonsDownWii & WPAD_BUTTON_MINUS)
					{
						//move to the previous set & return
						eng::listTurn(FB_LIST, -1, true);
						fileBrowserFrame_FillPage();
						menu::Focus::getInstance().clearPrimaryFocus();
						break;
					}
				}
			}
			else if (hidControllerConnected && (hidGcPad[i].button ^ previousButtonsGCHid[i]))
			{
				previousButtonsGCHid[i] = hidGcPad[i].button;
				if (hidGcPad[i].button & PAD_TRIGGER_R1)
				{
					// Change sort method
					fileSortMode ^= 1;
					// Resort the list
					qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator);
					FB_LIST.top = 0;
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
				else if (hidGcPad[i].button & PAD_TRIGGER_R)
				{
					//move to next set & return
					eng::listTurn(FB_LIST, 1, true);
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
				else if (hidGcPad[i].button & PAD_TRIGGER_L)
				{
					//move to the previous set & return
					eng::listTurn(FB_LIST, -1, true);
					fileBrowserFrame_FillPage();
					menu::Focus::getInstance().clearPrimaryFocus();
					break;
				}
			}
#endif //HW_RVL
		}

		//Draw buttons
		menu::ComponentList::const_iterator iteration;
		for (iteration = componentList.begin(); iteration != componentList.end(); ++iteration)
		{
			(*iteration)->draw(gfx);
		}
	}
}

void Func_PrevPage()
{
	eng::listTurn(FB_LIST, -1, false);
	fileBrowserFrame_FillPage();
}

void Func_NextPage()
{
	eng::listTurn(FB_LIST, 1, false);
	fileBrowserFrame_FillPage();
}

extern MenuContext *pMenuContext;

void Func_ReturnFromFileBrowserFrame()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

void Func_Select1() { fileBrowserFrame_LoadFile(FB_LIST.top + 0); }

void Func_Select2() { fileBrowserFrame_LoadFile(FB_LIST.top + 1); }

void Func_Select3() { fileBrowserFrame_LoadFile(FB_LIST.top + 2); }

void Func_Select4() { fileBrowserFrame_LoadFile(FB_LIST.top + 3); }

void Func_Select5() { fileBrowserFrame_LoadFile(FB_LIST.top + 4); }

void Func_Select6() { fileBrowserFrame_LoadFile(FB_LIST.top + 5); }

void Func_Select7() { fileBrowserFrame_LoadFile(FB_LIST.top + 6); }

void Func_Select8() { fileBrowserFrame_LoadFile(FB_LIST.top + 7); }

void Func_Select9() { fileBrowserFrame_LoadFile(FB_LIST.top + 8); }

void Func_Select10() { fileBrowserFrame_LoadFile(FB_LIST.top + 9); }


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
}

int loadISO(fileBrowser_file*);
int loadISOSwap(fileBrowser_file*);

static int dir_comparator(const void* _x, const void* _y){
	const fileBrowser_file* x = (const fileBrowser_file*)_x;
	const fileBrowser_file* y = (const fileBrowser_file*)_y;
	int xIsDir = x->attr & FILE_BROWSER_ATTR_DIR;
	int yIsDir = y->attr & FILE_BROWSER_ATTR_DIR;
	// Directories go on top, otherwise alphabetical
	if(fileSortMode && xIsDir != yIsDir)
		return yIsDir - xIsDir;
	else
		return strcasecmp(x->name, y->name);
}

void fileBrowserFrame_OpenDirectory(fileBrowser_file* dir)
{
	if(dir_entries){ free(dir_entries); dir_entries = NULL; }

	romFolderList = false;
	// Read the directories and return on error
	num_entries = isoFile_readDir(dir, &dir_entries);
	if(num_entries <= 0)
	{
		if(dir_entries) { free(dir_entries); dir_entries = NULL; }
		fileBrowserFrame_Error(dir, num_entries);
		return;
	}

	// Sort the listing
	qsort(dir_entries, num_entries, sizeof(fileBrowser_file), dir_comparator);

	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	fileBrowserFrame_FillPage();
}

static const int ROM_DIRS_MAX = 16;
/* Load from SD / Load from USB: the folders of RomDirSD or RomDirUSB ("/wiistation/isos,
 * /roms/psx") that exist on that device ("sd:", "usb:"). One opens straight away; several
 * are listed, by whole path, to choose from; none opens the device's /wiistation/isos as
 * before, which says what is missing. An item may also name its device ("sd:/roms"). */
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
	if (n == 0)   /* the share's own top, or the card's /wiistation/isos */
		snprintf(deviceDefault->name, sizeof(deviceDefault->name), "%s%s", device,
		         !strcasecmp(device, "smb:") ? "/" : "/wiistation/isos");
	if (n <= 1)
	{
		if (n == 1)
			*deviceDefault = found[0];
		fileBrowserFrame_OpenDirectory(deviceDefault);
		return;
	}
	if(dir_entries){ free(dir_entries); dir_entries = NULL; }
	dir_entries = (fileBrowser_file*)malloc(n * sizeof(fileBrowser_file));
	if (!dir_entries) { num_entries = 0; return; }
	memcpy(dir_entries, found, n * sizeof(fileBrowser_file));
	num_entries = n;
	romFolderList = true;
	FB_LIST.count = num_entries;
	FB_LIST.top = 0;
	fileBrowserFrame_FillPage();
}

void fileBrowserFrame_Error(fileBrowser_file* dir, int error_code)
{
	char feedback_string[256];
	//disable all buttons
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		fileBrowserButtons[i].button->setActive(false);
	for (int i = 0; i<NUM_FILE_SLOTS; i++)
		fileBrowserButtons[i+2].buttonString = fileBrowserStrings[2];
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
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

void fileBrowserFrame_FillPage()
{
	//set default focus past "." and ".." entries
	int default_index = 0;
	while ( (!strcmp(filenameFromAbsPath(dir_entries[default_index+FB_LIST.top].name),".") ||
			 !strcmp(filenameFromAbsPath(dir_entries[default_index+FB_LIST.top].name),"..")) &&
			 num_entries > default_index+1 ) default_index++;
	FB_LIST.focus = FB_LIST.top + default_index;

	//the page as the engine lays it out: once per page turn
	layoutFileBrowser();
	applyFileBrowserLayout();
	if (menu::Button *def = fileBrowserButtonAt(FB_LAYOUT.defaultFocus))
		pMenuContext->getFrame(MenuContext::FRAME_FILEBROWSER)->setDefaultFocus(def);
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

void fileBrowserFrame_LoadFile(int i)
{
	char feedback_string[256] = "Failed to load ISO";
	if(dir_entries[i].attr & FILE_BROWSER_ATTR_DIR){
		// Here we are 'recursing' into a subdirectory
		// We have to do a little dance here to avoid a dangling pointer
		fileBrowser_file* dir = (fileBrowser_file*)malloc( sizeof(fileBrowser_file) );
		memcpy(dir, dir_entries+i, sizeof(fileBrowser_file));
		fileBrowserFrame_OpenDirectory(dir);
		free(dir);
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

			char RomInfo[512] = "";
			char buffer [50];
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), feedback_string);
			sprintf(buffer,"\nCD-ROM Label: %s\n",CdromLabel);
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
			sprintf(buffer,"CD-ROM ID: %s\n", CdromId);
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            if (check_unsatisfied_libcrypt())
            {
                sprintf(buffer, "LibCrypt protected game with missing SBI\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            if (Config.hacks.gpu_slow_list_walking)
            {
                sprintf(buffer, "GpuSlowListWalking auto fixed\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            if (Config.cycle_multiplier_override)
            {
                sprintf(buffer, "CycleMultiplierOverride fixed\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            if (Config.hacks.gpu_busy_hack)
            {
                sprintf(buffer, "GPU 'Fake Busy States' hacked\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            if (Config.hacks.dwActFixes)
            {
                sprintf(buffer, "Special game auto fixed %08lx\n", Config.hacks.dwActFixes);
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            if (Config.hacks.lightrec_hacks)
            {
                sprintf(buffer, "Applied Lightrec hacks\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }
            // Switches for painting textured quads as 2 triangles (small glitches, but better shading!)
            // This function has been automatically started in soft.c and dwActFixes have been determined in gpu code, so need to set it here
            Config.hacks.dwActFixes |= AUTO_FIX_QUADS_AS_2TRIANGLES;

            // auto recJR => psxJR for some game
            CheckGameR3000AutoFix();
            if (Config.pR3000Fix)
            {
                sprintf(buffer, "pR3000 auto fixed\n");
                menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
            }

			/* Under 1 MB in KB (rounded up), so a small image does not read "0 MB" */
			if (isoFile.size < 1024 * 1024)
				sprintf(buffer, "ISO Size: %u KB\n", (isoFile.size + 1023) / 1024);
			else
				sprintf(buffer, "ISO Size: %u MB\n", isoFile.size / 1024 / 1024);
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
			sprintf(buffer,"Country: %s\n",forceNTSC == FORCENTSC_ENABLE ? "FORCE NTSC" : ((!Config.PsxType) ? "NTSC":"PAL"));
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
			sprintf(buffer,"BIOS: %s\n",(Config.HLE==BIOS_USER_DEFINED) ? "PSX":"HLE");
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
			sprintf(buffer, "BIOS Name: %s\n", biosFileName);
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);
			unsigned char tracks[2];
            ISOgetTN(&tracks[0]);
            sprintf(buffer,"Number of tracks %u\n", tracks[1]);
			menu::glue::appendBounded(RomInfo, sizeof(RomInfo), buffer);

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
