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
 *   A setting row (ROW_CYCLE or ROW_RADIO) needs: a `char` variable and an enum of its
 *   values in Gamecube/wiiSXconfig.h; its definition, default and one OPTIONS[] line in
 *   Gamecube/GamecubeMain.cpp (that line is what saves and loads it); a row in SETTINGS.md;
 *   and one entry below with the label, the variable, its lowest and highest value, the
 *   value names in that order, and an apply function (NULL when the emulator reads the
 *   variable directly).
 *     ROW_CYCLE is one button that steps through the values: compact, right for a long list
 *     or for a setting whose values are obvious (On/Off).
 *     ROW_RADIO shows every value at once as its own button with the current one lit, the
 *     way the Settings tabs themselves do it: right when the choice is worth seeing whole.
 *     It fits OPT_MAX_CHOICES values; a row with fewer leaves the trailing slots empty.
 *   A readout row (ROW_INFO) needs only a label and a function that writes the right-hand
 *   text. Readouts refresh every frame while their page is open.
 *
 * HOW TO ADD A PAGE
 *   One OptRow[] array, one PAGES[] entry (title, rows, optional help lines, which Settings
 *   tab B returns to, and optional enter/leave hooks), one value in OptionsFrame::OptionsPages,
 *   and a button somewhere in SettingsFrame that calls setActiveFrame(FRAME_OPTIONS, that value).
 *   Help is a term and its explanation, in two columns under the rows: the term is drawn
 *   bold and right-aligned so the colons line up, and a line with no term continues the one
 *   above it. A page that has help needs its rows to end above HELP_Y0, which a compile-time
 *   check below enforces; scripts/menu_text_width.py measures the lines themselves against
 *   the screen and against the spinning logo's corner (Gamecube/menu/MenuLayout.h).
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
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "MenuLayout.h"
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
#define OPT_RADIO 2

struct OptRow
{
	int					kind;
	const char*			label;		// text on the left
	char*				var;		// CYCLE/RADIO: the setting it edits
	char				min, max;	// CYCLE: A cycles min..max and wraps. RADIO: one button each
	const char* const*	names;		// CYCLE/RADIO: names[value - min], shown on the button
	void				(*apply)(void);	// CYCLE/RADIO: tell the emulator; NULL = it reads var itself
	void				(*info)(char *buf, int len);	// INFO: writes the right-hand text
};

#define ROW_CYCLE(label, var, lo, hi, names, apply) \
	{ OPT_CYCLE, label, (char*)&(var), (char)(lo), (char)(hi), names, apply, NULL }
#define ROW_RADIO(label, var, lo, hi, names, apply) \
	{ OPT_RADIO, label, (char*)&(var), (char)(lo), (char)(hi), names, apply, NULL }
#define ROW_INFO(label, fn) \
	{ OPT_INFO, label, NULL, 0, 0, NULL, NULL, fn }

static const char* const RESAMPLER_NAMES[]   = { "Hold", "Linear", "Cubic" };
static const char* const ON_OFF_NAMES[]      = { "Off", "On" };
static const char* const LEGACY_HIFI_NAMES[] = { "Legacy", "Hi-Fi" };
static const char* const CD_BUFFER_NAMES[]   = { "16 KB", "64 KB", "256 KB" };
static const char* const CD_HUNKS_NAMES[]    = { "2", "4", "8" };
/* dynacore: 0 Lightrec, 1 Interpreter, 2 old PPC dynarec (wiiSXconfig.h) */
static const char* const CPU_CORE_NAMES[]    = { "Lightrec", "Interpreter", "Dynarec" };
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

/* A line of help under the rows. `term` is the setting being explained, drawn bold in its
 * own column; `text` continues in the second column, so a NULL term indents under the
 * definition above it. Both columns are measured against the screen by
 * scripts/menu_text_width.py -- a line that is too long simply runs off the right. */
struct OptHelp { const char* term; const char* text; };

/* Radio rows, not cycling ones: which core and which renderer is running is worth seeing
 * whole, the way the General tab used to show it. Both tear down and restart the emulator
 * when a game is loaded, so they are applied once on leaving the page (pluginsEnter /
 * pluginsLeave) rather than on every press. */
static const OptRow PLUGIN_ROWS[] =
{
	ROW_RADIO("CPU Core",   dynacore,  DYNACORE_DYNAREC, DYNACORE_DYNAREC_OLD, CPU_CORE_NAMES,   NULL),
	ROW_RADIO("GPU Plugin", gpuPlugin, OLD_SOFT,         OPEN_GX,              GPU_PLUGIN_NAMES, NULL),
};

static const OptHelp PLUGIN_HELP[] =
{
	{ "CPU Core:",   "Lightrec is the recompiler most games run best on." },
	{ NULL,          "Interpreter is exact but slow; Dynarec is the older one." },
	{ "GPU Plugin:", "OpenGX draws with the Wii's graphics hardware. The two" },
	{ NULL,          "software renderers are slower, but avoid its quirks." },
	{ NULL,          "" },
	{ NULL,          "Changing either restarts a loaded game." },
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

static const OptHelp STORAGE_HELP[] =
{
	{ "CD Read Buffer:", "How much each disc-image file reads at a time. Larger" },
	{ NULL,              "means fewer, longer card reads while a game streams." },
	{ "CD Read-Ahead:",  "A thread keeps the next 31 sectors ready early. Raw" },
	{ NULL,              "bin/cue and .iso only; it helps hardware, not Dolphin." },
	{ "CHD Hunk Cache:", "Decoded CHD hunks kept in memory, about 20 KB each." },
	{ NULL,              "More keeps two areas of the disc warm." },
	{ NULL,              "" },
	{ NULL,              "All three apply at the next game load." },
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
	const char*			title;
	const OptRow*		rows;
	int					nrows;
	const OptHelp*		help;			// explanation under the rows; NULL for none
	int					nhelp;
	int					returnSubmenu;	// the Settings tab B goes back to
	void				(*onEnter)(void);
	void				(*onLeave)(void);
};

#define COUNT(a)	((int)(sizeof(a) / sizeof((a)[0])))

static const OptPage PAGES[] =
{
	{ "Advanced Sound", SOUND_ROWS,   COUNT(SOUND_ROWS),   NULL,         0,                    SettingsFrame::SUBMENU_AUDIO,   NULL,         NULL         },
	{ "Plugins",        PLUGIN_ROWS,  COUNT(PLUGIN_ROWS),  PLUGIN_HELP,  COUNT(PLUGIN_HELP),   SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Storage",        STORAGE_ROWS, COUNT(STORAGE_ROWS), STORAGE_HELP, COUNT(STORAGE_HELP),  SettingsFrame::SUBMENU_GENERAL, NULL,         NULL         },
	{ "Memory",         MEMORY_ROWS,  COUNT(MEMORY_ROWS),  NULL,         0,                    SettingsFrame::SUBMENU_GENERAL, NULL,         NULL         },
};

#define NUM_PAGES		COUNT(PAGES)
#define OPT_MAX_ROWS	6
#define OPT_MAX_CHOICES	3
#define OPT_MAX_HELP	8

/*  Layout: title, then a row every ROW_DY from ROW_Y0, then any help text. Rows are 40 high
 *  in a 54-high slot, so there is a clear gap between them. No Back button on a page of
 *  settings: B returns to the tab the page came from (setBackFunc).
 *
 *  A ROW_CYCLE or ROW_INFO row is a label on the left and one button (or one piece of text)
 *  in the right-hand column. A ROW_RADIO row needs the whole width, so its label sits
 *  further left and its buttons occupy OPT_MAX_CHOICES evenly spaced slots. */
#define TITLE_X		320.0
#define TITLE_Y		44.0
#define ROW_Y0		86.0
#define ROW_DY		56.0
#define LABEL_X		175.0		// ROW_CYCLE / ROW_INFO label, centred
#define BUTTON_X	340.0
#define BUTTON_W	250.0
#define BUTTON_H	40.0
#define RLABEL_X	110.0		// ROW_RADIO label, centred
#define RADIO_X0	205.0		// first choice button
#define RADIO_W		135.0
#define RADIO_DX	147.0		// 135 wide with a 12 gap; three of them end at 634

/* Help is two columns: the term right-aligned so its colons line up, then the text.
 * The lower lines are beside the spinning logo's corner and have to stop short of it,
 * which MENU_ROW_RIGHT() works out from the line's own y (see MenuLayout.h). */
/* Both columns are centred on the screen at page time (activateSubmenu measures the widest
 * term and the widest explanation), so these are only where the widgets start out. */
#define HELP_TERM_R	152.0		// the term ENDS here
#define HELP_TEXT_X	162.0		// the text starts here
#define HELP_COL_GAP	10.0	// between the term column and the text column
/* The help block starts where the page's next row would have been, so the space above it
 * is the same as the space between two rows. HELP_Y0 is the earliest that can be, which
 * is what the widgets are first placed at before a page moves them. */
#define HELP_Y(nrows)	(ROW_Y0 + (nrows) * ROW_DY)
#define HELP_Y0			HELP_Y(1)
#define HELP_LINE_DY	19.0	// between the lines of one explanation: tight, they read as one
#define HELP_CELL_GAP	 9.0	// extra before a line that starts a new term
#define HELP_SCALE		0.62
#define HELP_LINE_H		(24.0 * HELP_SCALE)

#define VALUE_LEN	32

/* What one help string will measure on screen. getStringWidth takes a char*, and the
 * tables are const, so the cast is here rather than at every call. */
static int helpWidth(const char *s)
{
	if (!s || !*s) return 0;
	return menu::IplFont::getInstance().getStringWidth((char*)s, HELP_SCALE);
}

struct OptWidget
{
	menu::Button*	button;							// ROW_CYCLE
	menu::Button*	choice[OPT_MAX_CHOICES];		// ROW_RADIO
	menu::TextBox*	textBox;						// the label, in the ROW_CYCLE/ROW_INFO column
	menu::TextBox*	rlabelBox;						// the same label, further left, for ROW_RADIO
	menu::TextBox*	valueBox;						// ROW_INFO shows text, not a button
	char*			labelString;					// the TextBox keeps a pointer to this
	char*			valueString;					// the Button/TextBox keeps a pointer to this
	char*			choiceString[OPT_MAX_CHOICES];
	char			value[VALUE_LEN];
};

static OptWidget		ROWS[OPT_MAX_ROWS];
static menu::TextBox*	helpTermBox[OPT_MAX_HELP];
static menu::TextBox*	helpBox[OPT_MAX_HELP];
static char*			helpTermString[OPT_MAX_HELP];
static char*			helpString[OPT_MAX_HELP];
static char				helpEmpty[1] = "";
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

/*  One click handler per possible row, and per choice within a radio row: the Button API
 *  takes a plain function pointer with no argument, so the position has to be baked in.
 *  Grow both tables if OPT_MAX_ROWS or OPT_MAX_CHOICES grows. */
static void optCycle(int row);
static void optSet(int row, int choice);

#define OPT_CYCLE_FN(r)		static void Func_OptRow##r() { optCycle(r); }
#define OPT_SET_FN(r, c)	static void Func_OptSet##r##_##c() { optSet(r, c); }
#define OPT_SET_FNS(r)		OPT_SET_FN(r, 0) OPT_SET_FN(r, 1) OPT_SET_FN(r, 2)
#define OPT_SET_ROW(r)		{ Func_OptSet##r##_0, Func_OptSet##r##_1, Func_OptSet##r##_2 }

OPT_CYCLE_FN(0) OPT_CYCLE_FN(1) OPT_CYCLE_FN(2)
OPT_CYCLE_FN(3) OPT_CYCLE_FN(4) OPT_CYCLE_FN(5)
OPT_SET_FNS(0)  OPT_SET_FNS(1)  OPT_SET_FNS(2)
OPT_SET_FNS(3)  OPT_SET_FNS(4)  OPT_SET_FNS(5)

static void (*const ROW_FUNCS[OPT_MAX_ROWS])() =
	{ Func_OptRow0, Func_OptRow1, Func_OptRow2, Func_OptRow3, Func_OptRow4, Func_OptRow5 };
static void (*const SET_FUNCS[OPT_MAX_ROWS][OPT_MAX_CHOICES])() =
	{ OPT_SET_ROW(0), OPT_SET_ROW(1), OPT_SET_ROW(2),
	  OPT_SET_ROW(3), OPT_SET_ROW(4), OPT_SET_ROW(5) };

/* Every page must fit on the screen, the page tables must not outgrow the widgets, and a
 * page with help text must leave room for it. */
#define OPT_PAGE_FITS(rows)		(COUNT(rows) <= OPT_MAX_ROWS)
/* Worst case for a page: every help line starts a new term, so every one takes the gap. */
#define OPT_HELP_FITS(rows, help)	(COUNT(help) <= OPT_MAX_HELP && \
	HELP_Y(COUNT(rows)) + (COUNT(help) - 1) * (HELP_LINE_DY + HELP_CELL_GAP) \
		+ HELP_LINE_H <= MENU_BOTTOM)

typedef char opt_sound_fits  [OPT_PAGE_FITS(SOUND_ROWS)   ? 1 : -1];
typedef char opt_plugin_fits [OPT_PAGE_FITS(PLUGIN_ROWS)  ? 1 : -1];
typedef char opt_storage_fits[OPT_PAGE_FITS(STORAGE_ROWS) ? 1 : -1];
typedef char opt_memory_fits [OPT_PAGE_FITS(MEMORY_ROWS)  ? 1 : -1];
typedef char opt_plugin_help_fits [OPT_HELP_FITS(PLUGIN_ROWS,  PLUGIN_HELP)  ? 1 : -1];
typedef char opt_storage_help_fits[OPT_HELP_FITS(STORAGE_ROWS, STORAGE_HELP) ? 1 : -1];
/* A radio row must not run off the right, and the term column must end before the text. */
typedef char opt_help_columns[HELP_TEXT_X > HELP_TERM_R ? 1 : -1];
/* The rows above the help must not run into it, and the rows themselves must stay on screen. */
typedef char opt_rows_on_screen[ROW_Y0 + (OPT_MAX_ROWS - 1) * ROW_DY + BUTTON_H <= MENU_BOTTOM ? 1 : -1];
typedef char opt_radio_on_screen[RADIO_X0 + (OPT_MAX_CHOICES - 1) * RADIO_DX + RADIO_W <= 640.0 ? 1 : -1];

static const OptRow* pageRow(int row)
{
	return &PAGES[currentPage].rows[row];
}

/* How many buttons a radio row shows. A row that declares more values than there are slots
 * would silently lose the rest, so it is clamped here and the clamp is the documented limit. */
static int rowChoices(const OptRow *o)
{
	int n = (int)o->max - (int)o->min + 1;
	if (n < 1) n = 1;
	if (n > OPT_MAX_CHOICES) n = OPT_MAX_CHOICES;
	return n;
}

/* Re-read one row from the thing it reflects: the setting's value name, or the readout. */
static void refreshRow(int row)
{
	const OptRow *o = pageRow(row);
	if (o->kind == OPT_INFO) {
		o->info(ROWS[row].value, VALUE_LEN);
	} else if (o->kind == OPT_RADIO) {
		int n = rowChoices(o), v = *o->var;
		if (v < o->min || v > o->max) v = o->min;
		for (int c = 0; c < n; c++)
			ROWS[row].choice[c]->setSelected(c == v - o->min);
		return;
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

static void optSet(int row, int choice)
{
	const OptRow *o;
	if (row >= PAGES[currentPage].nrows) return;
	o = pageRow(row);
	if (o->kind != OPT_RADIO || choice >= rowChoices(o)) return;
	*o->var = (char)(o->min + choice);
	refreshRow(row);
	if (o->apply) o->apply();
}

/* Which button vertical focus should land on when moving into `row`, coming from column
 * `col`. Keeping the column means Up/Down does not jump back to the left of a radio row. */
static menu::Button* focusTarget(int row, int col)
{
	const OptRow *o = pageRow(row);
	if (o->kind != OPT_RADIO) return ROWS[row].button;
	{
		int n = rowChoices(o);
		if (col >= n) col = n - 1;
		return ROWS[row].choice[col];
	}
}

static bool rowIsFocusable(int row)
{
	int kind = pageRow(row)->kind;
	return kind == OPT_CYCLE || kind == OPT_RADIO;
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
		/* The label lives in two places depending on the row kind, so it gets two boxes and
		 * whichever one the current page needs is the one made visible. */
		ROWS[i].textBox   = new menu::TextBox(&ROWS[i].labelString, LABEL_X,  y + 20.0, 1.0, true);
		ROWS[i].rlabelBox = new menu::TextBox(&ROWS[i].labelString, RLABEL_X, y + 20.0, 1.0, true);
		ROWS[i].valueBox  = new menu::TextBox(&ROWS[i].valueString, BUTTON_X + BUTTON_W / 2, y + 20.0, 1.0, true);
		ROWS[i].button   = new menu::Button(BTN_A_NRM, &ROWS[i].valueString, BUTTON_X, y, BUTTON_W, BUTTON_H);
		ROWS[i].button->setActive(true);
		ROWS[i].button->setClicked(ROW_FUNCS[i]);
		ROWS[i].button->setReturn(Func_ReturnFromOptionsFrame);
		add(ROWS[i].textBox);
		add(ROWS[i].rlabelBox);
		add(ROWS[i].valueBox);
		add(ROWS[i].button);
		menu::Cursor::getInstance().addComponent(this, ROWS[i].button, BUTTON_X, BUTTON_X + BUTTON_W, y, y + BUTTON_H);

		for (int c = 0; c < OPT_MAX_CHOICES; c++)
		{
			float cx = RADIO_X0 + c * RADIO_DX;
			ROWS[i].choiceString[c] = (char*)"";
			/* BTN_A_SEL is the style the Settings tabs use for a one-of-several choice: the
			 * selected one stays lit whether or not it has focus. */
			ROWS[i].choice[c] = new menu::Button(BTN_A_SEL, &ROWS[i].choiceString[c], cx, y, RADIO_W, BUTTON_H);
			ROWS[i].choice[c]->setActive(true);
			ROWS[i].choice[c]->setClicked(SET_FUNCS[i][c]);
			ROWS[i].choice[c]->setReturn(Func_ReturnFromOptionsFrame);
			add(ROWS[i].choice[c]);
			menu::Cursor::getInstance().addComponent(this, ROWS[i].choice[c], cx, cx + RADIO_W, y, y + BUTTON_H);
		}
	}

	for (int i = 0; i < OPT_MAX_HELP; i++)
	{
		float y = HELP_Y0 + i * (HELP_LINE_DY + HELP_CELL_GAP);   /* placed per page below */
		helpTermString[i] = helpEmpty;
		helpString[i] = helpEmpty;
		helpTermBox[i] = new menu::TextBox(&helpTermString[i], HELP_TERM_R, y, HELP_SCALE, false);
		helpTermBox[i]->setRightAligned(true);
		helpTermBox[i]->setBold(true);
		helpBox[i] = new menu::TextBox(&helpString[i], HELP_TEXT_X, y, HELP_SCALE, false);
		add(helpTermBox[i]);
		add(helpBox[i]);
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
		delete ROWS[i].rlabelBox;
		delete ROWS[i].valueBox;
		menu::Cursor::getInstance().removeComponent(this, ROWS[i].button);
		delete ROWS[i].button;
		for (int c = 0; c < OPT_MAX_CHOICES; c++)
		{
			menu::Cursor::getInstance().removeComponent(this, ROWS[i].choice[c]);
			delete ROWS[i].choice[c];
		}
	}
	for (int i = 0; i < OPT_MAX_HELP; i++)
	{
		delete helpTermBox[i];
		delete helpBox[i];
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
		bool used   = (i < n);
		int  kind   = used ? pageRow(i)->kind : OPT_INFO;
		bool cycles = used && kind == OPT_CYCLE;
		bool radio  = used && kind == OPT_RADIO;
		int  nc     = radio ? rowChoices(pageRow(i)) : 0;

		if (used) {
			ROWS[i].labelString = (char*)pageRow(i)->label;
			for (int c = 0; c < nc; c++)
				ROWS[i].choiceString[c] = (char*)pageRow(i)->names[c];
		}
		/* A radio row needs the full width, so its label sits further left. Both boxes read
		 * the same string; exactly one of them is shown. */
		ROWS[i].textBox->setVisible(used && !radio);
		ROWS[i].rlabelBox->setVisible(radio);
		/* A setting is a button you press; a readout is plain text in the same column. */
		ROWS[i].button->setVisible(cycles);
		ROWS[i].button->setActive(cycles);
		ROWS[i].valueBox->setVisible(used && kind == OPT_INFO);
		for (int c = 0; c < OPT_MAX_CHOICES; c++)
		{
			bool on = radio && c < nc;
			ROWS[i].choice[c]->setVisible(on);
			ROWS[i].choice[c]->setActive(on);
			if (!on) ROWS[i].choice[c]->setSelected(false);
		}
		if (used) refreshRow(i);
	}

	/* Focus runs down the rows that can be pressed, and wraps; left and right step through
	 * a radio row's choices. A page of readouts has nothing focusable, and B is the only
	 * way out, which is what the back function is for. */
	int firstRow = -1, prevRow = -1;
	for (int i = 0; i < n; i++)
	{
		if (!rowIsFocusable(i)) continue;
		if (firstRow < 0) firstRow = i;
		if (pageRow(i)->kind == OPT_RADIO)
		{
			int nc = rowChoices(pageRow(i));
			for (int c = 0; c < nc; c++)
			{
				ROWS[i].choice[c]->setNextFocus(menu::Focus::DIRECTION_LEFT,  ROWS[i].choice[(c + nc - 1) % nc]);
				ROWS[i].choice[c]->setNextFocus(menu::Focus::DIRECTION_RIGHT, ROWS[i].choice[(c + 1) % nc]);
			}
		}
		if (prevRow >= 0)
		{
			int ncPrev = pageRow(prevRow)->kind == OPT_RADIO ? rowChoices(pageRow(prevRow)) : 1;
			int ncThis = pageRow(i)->kind      == OPT_RADIO ? rowChoices(pageRow(i))      : 1;
			for (int c = 0; c < ncPrev; c++)
				focusTarget(prevRow, c)->setNextFocus(menu::Focus::DIRECTION_DOWN, focusTarget(i, c));
			for (int c = 0; c < ncThis; c++)
				focusTarget(i, c)->setNextFocus(menu::Focus::DIRECTION_UP, focusTarget(prevRow, c));
		}
		prevRow = i;
	}
	/* Also when there is only one focusable row: it then links to itself, which is what stops
	 * Up/Down following a link left over from the page shown before. */
	if (firstRow >= 0)
	{
		int ncLast  = pageRow(prevRow)->kind  == OPT_RADIO ? rowChoices(pageRow(prevRow))  : 1;
		int ncFirst = pageRow(firstRow)->kind == OPT_RADIO ? rowChoices(pageRow(firstRow)) : 1;
		for (int c = 0; c < ncLast; c++)
			focusTarget(prevRow, c)->setNextFocus(menu::Focus::DIRECTION_DOWN, focusTarget(firstRow, c));
		for (int c = 0; c < ncFirst; c++)
			focusTarget(firstRow, c)->setNextFocus(menu::Focus::DIRECTION_UP, focusTarget(prevRow, c));
	}

	/* Help text under the rows, if the page has any.
	 *
	 * The two columns are centred on the screen together, so the block is measured first:
	 * the widest term and the widest explanation give its width. A page whose lower lines
	 * reach the logo's corner then moves left far enough to clear it, and no further. */
	float termR = HELP_TERM_R, textX = HELP_TEXT_X;
	{
		const OptHelp *help = PAGES[currentPage].help;
		int n = PAGES[currentPage].nhelp, wTerm = 0, wText = 0, shift = 0;
		float y = HELP_Y(PAGES[currentPage].nrows);
		for (int i = 0; i < n; i++) {
			if (help[i].term) {
				int w = helpWidth(help[i].term);
				if (w > wTerm) wTerm = w;
			}
			{
				int w = helpWidth(help[i].text);
				if (w > wText) wText = w;
			}
		}
		termR = (MENU_W - (wTerm + HELP_COL_GAP + wText)) / 2 + wTerm;
		textX = termR + HELP_COL_GAP;
		/* Nothing may enter the logo's corner, and the term must stay on screen. */
		for (int i = 0; i < n; i++) {
			int over;
			if (i > 0 && help[i].term) y += HELP_CELL_GAP;
			over = (int)(textX + helpWidth(help[i].text) - MENU_ROW_RIGHT(y + HELP_LINE_H));
			if (over > shift) shift = over;
			y += HELP_LINE_DY;
		}
		if (termR - wTerm - shift < MENU_EDGE) shift = (int)(termR - wTerm - MENU_EDGE);
		termR -= shift;
		textX -= shift;
	}

	/* The lines of one explanation sit close together so they read as one paragraph; the
	 * space goes before the line that starts a new term. */
	float helpY = HELP_Y(PAGES[currentPage].nrows);
	for (int i = 0; i < OPT_MAX_HELP; i++)
	{
		bool on = i < PAGES[currentPage].nhelp;
		const char *term = on ? PAGES[currentPage].help[i].term : NULL;
		if (i > 0 && term) helpY += HELP_CELL_GAP;
		helpTermString[i] = term ? (char*)term : helpEmpty;
		helpString[i] = on ? (char*)PAGES[currentPage].help[i].text : helpEmpty;
		helpTermBox[i]->setPosition(termR, helpY);
		helpBox[i]->setPosition(textX, helpY);
		helpY += HELP_LINE_DY;
		helpTermBox[i]->setVisible(term != NULL);
		helpBox[i]->setVisible(on);
	}

	/* Only a readout page needs the Back button; elsewhere B is the way out. */
	backButton->setVisible(firstRow < 0);
	backButton->setActive(firstRow < 0);
	setDefaultFocus(firstRow >= 0 ? focusTarget(firstRow, 0) : backButton);
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
