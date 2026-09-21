/**
 * WiiStation - OptionsFrame.cpp
 *
 * The sub-pages reached from the Settings tabs. One table-driven frame serves all of them:
 *
 *   Audio tab   -> "Advanced"  : sound options under test
 *   General tab -> "Plugins"   : which CPU core and which GPU renderer
 *               -> "Storage"   : how disc images are read
 *               -> "Memory"    : what the machine's memory is doing right now
 *
 * HOW TO ADD A ROW
 *   A setting row (OPT_CYCLE) needs: a `char` variable and an enum of its values in
 *   Gamecube/wiiSXconfig.h; its definition, default and one OPTIONS[] line in
 *   Gamecube/GamecubeMain.cpp (that line is what saves and loads it); a row in SETTINGS.md;
 *   and one entry below with the label, the variable, its lowest and highest value, the
 *   value names in that order, and an apply function (NULL when the emulator reads the
 *   variable directly).
 *   A readout row (OPT_INFO) needs only a label and a function that writes the right-hand
 *   text. Readouts refresh every frame while their page is open.
 *
 * HOW TO ADD A PAGE
 *   One OptRow[] array, one PAGES[] entry (title, rows, which Settings tab B returns to,
 *   and optional enter/leave hooks), one value in OptionsFrame::OptionsPages, and a button
 *   somewhere in SettingsFrame that calls setActiveFrame(FRAME_OPTIONS, that value).
 *
 * Layout, focus order, click handlers and the label refresh are all derived from the
 * tables; nothing else has to be touched.
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

#include "MenuContext.h"
#include "SettingsFrame.h"
#include "OptionsFrame.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../wiiSXconfig.h"
#include "../MEM2.h"

extern "C" {
#include "../../mem2_manager.h"
}

extern MenuContext *pMenuContext;

/* Applied when the Plugins page is left, not on every press: changing either plugin with a
 * game loaded tears the emulator down and starts it again, which would fire on each step of
 * a cycling button. Defined in SettingsFrame.cpp, where the reset machinery already lives. */
extern void ApplyPluginSelection(char wantCore, char wantGpu);

namespace menu { extern heap_cntrl* GXtexCache; }   /* the font glyph heap, IPLFont.cpp */

void Func_ReturnFromOptionsFrame();

/*  Rows  */

#define OPT_CYCLE 0
#define OPT_INFO  1

struct OptRow
{
	int					kind;
	const char*			label;		// text on the left
	char*				var;		// OPT_CYCLE: the setting it edits
	char				min, max;	// OPT_CYCLE: A cycles min..max and wraps
	const char* const*	names;		// OPT_CYCLE: names[value - min], shown on the button
	void				(*apply)(void);	// OPT_CYCLE: tell the emulator; NULL = it reads var itself
	void				(*info)(char *buf, int len);	// OPT_INFO: writes the right-hand text
};

#define ROW_CYCLE(label, var, lo, hi, names, apply) \
	{ OPT_CYCLE, label, (char*)&(var), (char)(lo), (char)(hi), names, apply, NULL }
#define ROW_INFO(label, fn) \
	{ OPT_INFO, label, NULL, 0, 0, NULL, NULL, fn }

static const char* const RESAMPLER_NAMES[]   = { "Hold", "Linear", "Cubic" };
static const char* const ON_OFF_NAMES[]      = { "Off", "On" };
static const char* const LEGACY_HIFI_NAMES[] = { "Legacy", "Hi-Fi" };
static const char* const CD_BUFFER_NAMES[]   = { "16 KB", "64 KB", "256 KB" };
static const char* const CD_HUNKS_NAMES[]    = { "2", "4", "8" };
/* dynacore: 0 Lightrec, 1 Interpreter, 2 old PPC dynarec (wiiSXconfig.h) */
static const char* const CPU_CORE_NAMES[]    = { "Lightrec", "Interpreter", "PPC Dynarec" };
/* gpuPlugin: 0 Old Soft, 1 New Soft, 2 OpenGX */
static const char* const GPU_PLUGIN_NAMES[]  = { "Old Soft", "New Soft", "OpenGX" };

/* setSpuReverb(int) takes the raw setting value; the table's apply slot is void(void), so
 * this wrapper reads the setting itself, the same way GamecubeMain.cpp does at startup. */
extern "C" void setSpuReverb(int soundReverb);
static void applySpuReverb(void) { setSpuReverb(soundReverb); }

/*  Readouts (Memory page)  */

/* MEM1 is the 24 MB of fast 1T-SRAM on the graphics die: emulated PSX RAM, the GPU's
 * buffers and everything the CPU touches per frame live here. What is left is what any
 * future allocation has to fit into. */
static void infoMem1(char *buf, int len)
{
	snprintf(buf, len, "%lu KB free", (unsigned long)(SYS_GetArena1Size() >> 10));
}

/* MEM2 is the 64 MB of slower off-die GDDR3. This is the general heap the emulator carves
 * textures, the disc-image buffers and the sound buffers out of (mem2_manager.c). */
static void infoMem2(char *buf, int len)
{
	unsigned long used = gx_mem2_used() >> 10, total = gx_mem2_total() >> 10;
	snprintf(buf, len, "%lu / %lu KB", used, total);
}

/* The glyph cache: one 1152-byte RGB5A3 tile per character, allocated from a fixed 9 MB
 * MEM2 region (MEM2.h CN_FONT_SIZE) so that a full CJK font would fit. A Latin menu font
 * uses a fraction of it; this row is here to make that visible. */
static void infoFontCache(char *buf, int len)
{
	unsigned long used = 0, total = (unsigned long)CN_FONT_SIZE >> 10;
	if (menu::GXtexCache) {
		heap_iblock info;
		__lwp_heap_getinfo(menu::GXtexCache, &info);
		used = info.used_size >> 10;
	}
	snprintf(buf, len, "%lu / %lu KB", used, total);
}

/* The recompiler's output buffer, reserved at a fixed MEM2 address for whichever core is
 * selected. Neither is sized by a setting today. */
static void infoJitBuffer(char *buf, int len)
{
	if (dynacore == DYNACORE_DYNAREC_OLD)
		snprintf(buf, len, "%lu KB (PPC)", (unsigned long)RECMEM2_SIZE >> 10);
	else
		snprintf(buf, len, "%lu KB (Lightrec)", (unsigned long)LIGHTREC_BUF_SIZE >> 10);
}

/* Sound and disc buffers, both fixed regions. */
static void infoFixedBuffers(char *buf, int len)
{
	snprintf(buf, len, "SPU %lu KB, BIOS %lu KB",
		(unsigned long)SPU_BUF_SIZE >> 10, (unsigned long)PSXR_BUF_SIZE >> 10);
}

/*  Pages  */

static const OptRow SOUND_ROWS[] =
{
	ROW_CYCLE("Output Resampler", soundResampler,      SOUND_RESAMPLE_HOLD,          SOUND_RESAMPLE_CUBIC,        RESAMPLER_NAMES,   NULL),
	ROW_CYCLE("Reverb",           soundReverb,         SOUND_REVERB_OFF,             SOUND_REVERB_ON,             ON_OFF_NAMES,      applySpuReverb),
	ROW_CYCLE("Mixer Precision",  soundMixerPrecision, SOUND_MIXER_PRECISION_LEGACY, SOUND_MIXER_PRECISION_HIFI,  LEGACY_HIFI_NAMES, NULL),
	ROW_CYCLE("XA Resampler",     soundXaResampler,    SOUND_XA_RESAMPLER_LEGACY,    SOUND_XA_RESAMPLER_HIFI,     LEGACY_HIFI_NAMES, NULL),
};

/* Both of these tear down and restart the emulator when a game is loaded, so they are
 * applied once on leaving the page (pluginsEnter/pluginsLeave) rather than per press. */
static const OptRow PLUGIN_ROWS[] =
{
	ROW_CYCLE("CPU Core",   dynacore,  DYNACORE_DYNAREC, DYNACORE_DYNAREC_OLD, CPU_CORE_NAMES,   NULL),
	ROW_CYCLE("GPU Plugin", gpuPlugin, OLD_SOFT,         OPEN_GX,              GPU_PLUGIN_NAMES, NULL),
};

static char pluginsCoreOnEntry, pluginsGpuOnEntry;
static void pluginsEnter(void) { pluginsCoreOnEntry = dynacore; pluginsGpuOnEntry = gpuPlugin; }
static void pluginsLeave(void)
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

/* All three are read when a disc image is opened, so they take effect at the next game
 * load rather than immediately (SETTINGS.md section 2). */
static const OptRow STORAGE_ROWS[] =
{
	ROW_CYCLE("CD Read Buffer", cdBuffer,   CD_BUFFER_16K,   CD_BUFFER_256K, CD_BUFFER_NAMES, NULL),
	ROW_CYCLE("CD Read-Ahead",  cdPrefetch, CD_PREFETCH_OFF, CD_PREFETCH_ON, ON_OFF_NAMES,    NULL),
	ROW_CYCLE("CHD Hunk Cache", cdChdHunks, CD_CHD_HUNKS_2,  CD_CHD_HUNKS_8, CD_HUNKS_NAMES,  NULL),
};

/* Readouts only. Every large MEM2 region is reserved at a fixed address (Gamecube/MEM2.h),
 * so there is nothing here a setting could move at runtime; what this page is for is seeing
 * where the memory went, which is what any change to that layout has to be argued from. */
static const OptRow MEMORY_ROWS[] =
{
	ROW_INFO("MEM1 (fast, 24 MB)",  infoMem1),
	ROW_INFO("MEM2 heap",           infoMem2),
	ROW_INFO("Font glyph cache",    infoFontCache),
	ROW_INFO("Recompiler buffer",   infoJitBuffer),
	ROW_INFO("Fixed buffers",       infoFixedBuffers),
};

struct OptPage
{
	const char*		title;
	const OptRow*	rows;
	int				nrows;
	int				returnSubmenu;		// the Settings tab B goes back to
	void			(*onEnter)(void);
	void			(*onLeave)(void);
};

static const OptPage PAGES[] =
{
	{ "Advanced Sound", SOUND_ROWS,   (int)(sizeof(SOUND_ROWS)   / sizeof(OptRow)), SettingsFrame::SUBMENU_AUDIO,   NULL,         NULL         },
	{ "Plugins",        PLUGIN_ROWS,  (int)(sizeof(PLUGIN_ROWS)  / sizeof(OptRow)), SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Storage",        STORAGE_ROWS, (int)(sizeof(STORAGE_ROWS) / sizeof(OptRow)), SettingsFrame::SUBMENU_GENERAL, NULL,         NULL         },
	{ "Memory",         MEMORY_ROWS,  (int)(sizeof(MEMORY_ROWS)  / sizeof(OptRow)), SettingsFrame::SUBMENU_GENERAL, NULL,         NULL         },
};

#define NUM_PAGES	((int)(sizeof(PAGES) / sizeof(PAGES[0])))
#define OPT_MAX_ROWS	8

/*  Layout: title, then a row every ROW_DY from ROW_Y0. No Back button: B returns to the
 *  tab the page came from (setBackFunc). 40-high buttons, labels 20 below the button top. */
#define TITLE_X		320.0
#define TITLE_Y		44.0
#define ROW_Y0		76.0
#define ROW_DY		46.0
#define LABEL_X		175.0
#define BUTTON_X	340.0
#define BUTTON_W	250.0
#define BUTTON_H	40.0

#define VALUE_LEN	32

struct OptWidget
{
	menu::Button*	button;
	menu::TextBox*	textBox;
	menu::TextBox*	valueBox;		// OPT_INFO rows show text, not a button
	char*			labelString;	// the TextBox keeps a pointer to this
	char*			valueString;	// the Button/TextBox keeps a pointer to this
	char			value[VALUE_LEN];
};

static OptWidget		ROWS[OPT_MAX_ROWS];
/* Pages of settings are left with B, like every other sub-page, so they carry no Back
 * button. A page of readouts has nothing else to focus, though, and focus has to land on
 * something real: this button is shown on those pages only. */
static menu::Button*	backButton;
static char*			backString;
static char				backText[8] = "Back";
static menu::TextBox*	titleBox;
static char*			titleString;
static char				titleText[32];
static int				currentPage;

/*  One click handler per possible row; the Button API takes a plain function pointer with
 *  no argument, so the row index has to be baked in. Add a line here and to ROW_FUNCS if
 *  OPT_MAX_ROWS grows. */
static void optCycle(int row);
static void Func_OptRow0() { optCycle(0); }
static void Func_OptRow1() { optCycle(1); }
static void Func_OptRow2() { optCycle(2); }
static void Func_OptRow3() { optCycle(3); }
static void Func_OptRow4() { optCycle(4); }
static void Func_OptRow5() { optCycle(5); }
static void Func_OptRow6() { optCycle(6); }
static void Func_OptRow7() { optCycle(7); }
static void (*const ROW_FUNCS[OPT_MAX_ROWS])() =
	{ Func_OptRow0, Func_OptRow1, Func_OptRow2, Func_OptRow3,
	  Func_OptRow4, Func_OptRow5, Func_OptRow6, Func_OptRow7 };

/* Every page must fit on the screen, and the page table must not outgrow the widgets. */
typedef char opt_sound_fits[(int)(sizeof(SOUND_ROWS)   / sizeof(OptRow)) <= OPT_MAX_ROWS ? 1 : -1];
typedef char opt_plugin_fits[(int)(sizeof(PLUGIN_ROWS)  / sizeof(OptRow)) <= OPT_MAX_ROWS ? 1 : -1];
typedef char opt_storage_fits[(int)(sizeof(STORAGE_ROWS) / sizeof(OptRow)) <= OPT_MAX_ROWS ? 1 : -1];
typedef char opt_memory_fits[(int)(sizeof(MEMORY_ROWS)  / sizeof(OptRow)) <= OPT_MAX_ROWS ? 1 : -1];

static const OptRow* pageRow(int row)
{
	return &PAGES[currentPage].rows[row];
}

/* Re-read one row from the thing it reflects: the setting's value name, or the readout. */
static void refreshRow(int row)
{
	const OptRow *o = pageRow(row);
	if (o->kind == OPT_INFO) {
		o->info(ROWS[row].value, VALUE_LEN);
	} else {
		int v = *o->var;
		if (v < o->min || v > o->max) v = o->min;	// a value the file parser let through
		snprintf(ROWS[row].value, VALUE_LEN, "%s", o->names[v - o->min]);
	}
	ROWS[row].valueString = ROWS[row].value;
}

static void optCycle(int row)
{
	const OptRow *o;
	if (row >= PAGES[currentPage].nrows) return;
	o = pageRow(row);
	if (o->kind != OPT_CYCLE) return;
	{
		int v = *o->var + 1;
		if (v > o->max || v < o->min) v = o->min;
		*o->var = (char)v;
	}
	refreshRow(row);
	if (o->apply) o->apply();
}

OptionsFrame::OptionsFrame()
		: activePage(PAGE_SOUND)
{
	titleString = titleText;
	snprintf(titleText, sizeof(titleText), "%s", PAGES[PAGE_SOUND].title);
	titleBox = new menu::TextBox(&titleString, TITLE_X, TITLE_Y, 1.0, true);
	add(titleBox);

	for (int i = 0; i < OPT_MAX_ROWS; i++)
	{
		float y = ROW_Y0 + i * ROW_DY;
		ROWS[i].labelString = (char*)"";
		ROWS[i].value[0] = '\0';
		ROWS[i].valueString = ROWS[i].value;
		ROWS[i].textBox  = new menu::TextBox(&ROWS[i].labelString, LABEL_X, y + 20.0, 1.0, true);
		ROWS[i].valueBox = new menu::TextBox(&ROWS[i].valueString, BUTTON_X + BUTTON_W / 2, y + 20.0, 1.0, true);
		ROWS[i].button   = new menu::Button(BTN_A_NRM, &ROWS[i].valueString, BUTTON_X, y, BUTTON_W, BUTTON_H);
		ROWS[i].button->setActive(true);
		ROWS[i].button->setClicked(ROW_FUNCS[i]);
		ROWS[i].button->setReturn(Func_ReturnFromOptionsFrame);
		add(ROWS[i].textBox);
		add(ROWS[i].valueBox);
		add(ROWS[i].button);
		menu::Cursor::getInstance().addComponent(this, ROWS[i].button, BUTTON_X, BUTTON_X + BUTTON_W, y, y + BUTTON_H);
	}

	backString = backText;
	backButton = new menu::Button(BTN_A_NRM, &backString, 270.0, 420.0, 100.0, 40.0);
	backButton->setActive(true);
	backButton->setClicked(Func_ReturnFromOptionsFrame);
	backButton->setReturn(Func_ReturnFromOptionsFrame);
	add(backButton);
	menu::Cursor::getInstance().addComponent(this, backButton, 270.0, 370.0, 420.0, 460.0);

	setDefaultFocus(ROWS[0].button);
	setBackFunc(Func_ReturnFromOptionsFrame);
	setEnabled(true);
	activateSubmenu(PAGE_SOUND);
}

OptionsFrame::~OptionsFrame()
{
	delete titleBox;
	for (int i = 0; i < OPT_MAX_ROWS; i++)
	{
		delete ROWS[i].textBox;
		delete ROWS[i].valueBox;
		menu::Cursor::getInstance().removeComponent(this, ROWS[i].button);
		delete ROWS[i].button;
	}
	menu::Cursor::getInstance().removeComponent(this, backButton);
	delete backButton;
}

void OptionsFrame::activateSubmenu(int submenu)
{
	int n;

	if (submenu < 0 || submenu >= NUM_PAGES) submenu = PAGE_SOUND;
	activePage = submenu;
	currentPage = submenu;
	n = PAGES[currentPage].nrows;

	snprintf(titleText, sizeof(titleText), "%s", PAGES[currentPage].title);
	if (PAGES[currentPage].onEnter) PAGES[currentPage].onEnter();

	for (int i = 0; i < OPT_MAX_ROWS; i++)
	{
		bool used = (i < n);
		bool cycles = used && pageRow(i)->kind == OPT_CYCLE;
		if (used) {
			ROWS[i].labelString = (char*)pageRow(i)->label;
			refreshRow(i);
		}
		ROWS[i].textBox->setVisible(used);
		/* A setting is a button you press; a readout is plain text in the same column. */
		ROWS[i].button->setVisible(cycles);
		ROWS[i].button->setActive(cycles);
		ROWS[i].valueBox->setVisible(used && !cycles);
	}

	/* Focus runs down the rows that can be pressed, and wraps. A page of readouts has
	 * none, and B is the only way out, which is what the back function is for. */
	menu::Button *first = NULL, *prev = NULL;
	for (int i = 0; i < n; i++)
	{
		if (pageRow(i)->kind != OPT_CYCLE) continue;
		if (!first) first = ROWS[i].button;
		if (prev) {
			prev->setNextFocus(menu::Focus::DIRECTION_DOWN, ROWS[i].button);
			ROWS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, prev);
		}
		prev = ROWS[i].button;
	}
	if (first && prev) {
		prev->setNextFocus(menu::Focus::DIRECTION_DOWN, first);
		first->setNextFocus(menu::Focus::DIRECTION_UP, prev);
	}

	/* Only a readout page needs the Back button; elsewhere B is the way out. */
	backButton->setVisible(first == NULL);
	backButton->setActive(first == NULL);
	setDefaultFocus(first ? first : backButton);
}

/* Readouts are live: the emulator is not running while this page is open, so re-reading a
 * few heap counters per frame costs nothing that anyone can see. */
void OptionsFrame::drawChildren(menu::Graphics& gfx)
{
	if (!isVisible()) return;

	for (int i = 0; i < PAGES[currentPage].nrows; i++)
		if (pageRow(i)->kind == OPT_INFO)
			refreshRow(i);

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
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, PAGES[currentPage].returnSubmenu);
	if (onLeave) onLeave();
}
