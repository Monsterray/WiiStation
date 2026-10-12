/**
 * WiiStation - OptionsFrame.cpp
 *
 * The sub-pages reached from the Settings tabs. One table-driven frame serves all of them:
 *
 *   Audio tab   -> "Advanced"  : sound options under test
 *   General tab -> "Plugins"   : which CPU core and which GPU renderer
 *               -> "CD"        : how disc images are read
 *  Video tab    -> "Advanced Graphics" : dithering and the video decoder
 *               -> "Memory"    : what the machine's memory is doing right now
 *
 * HOW TO ADD A ROW
 *   A setting row (ROW_CYCLE or ROW_RADIO) needs: its OPTIONS[] line in Gamecube/GamecubeMain.cpp
 *   (the key, the variable, its lowest and highest value, the label, the choice names and the
 *   help line; that line is what saves and loads it, and what the menu shows); a row in
 *   SETTINGS.md; and one entry below with the key and an apply function (NULL when the emulator
 *   reads the variable directly). The label, names and range are read from the table by key
 *   when the page is shown: nothing is copied here. scripts/settings_check.py checks each key.
 *     ROW_CYCLE is one button that steps through the values: compact, right for a long list
 *     or for a setting whose values are obvious (On/Off).
 *     ROW_RADIO shows every value at once as its own button with the current one lit, the
 *     way the Settings tabs themselves do it: right when the choice is worth seeing whole.
 *     It fits OPT_MAX_CHOICES values; a row with fewer leaves the trailing slots empty.
 *   A readout row (ROW_INFO) needs only a label and a function that writes the right-hand
 *   text. Readouts are read when their page opens.
 *
 * HOW TO ADD A PAGE
 *   One OptRow[] array, one PAGES[] entry (title, rows, optional help lines, which Settings
 *   tab B returns to, and optional enter/leave hooks), one value in OptionsFrame::OptionsPages,
 *   and a button somewhere in SettingsFrame that calls setActiveFrame(FRAME_OPTIONS, that value).
 *   Help is a term and its explanation, in two columns under the rows: the term is drawn
 *   bold and right-aligned so the colons line up, and a line with no term continues the one
 *   above it. A help block that runs into the rows or the logo is reported by the menu
 *   engine's layout check in debug builds; scripts/menu_text_width.py measures the lines
 *   themselves against the screen (Gamecube/menu/MenuLayout.h).
 *
 * WHERE THINGS GO
 *   The menu engine (Gamecube/menu/MenuEngine.h) works out every position and every focus
 *   link. This frame describes the page on show as an engine Page, built from the tables
 *   below, and places its buttons, labels and help at the rectangles the engine gives back.
 *   That happens when a page or a tab is shown, never per frame. The click handlers, the
 *   settings bindings and the readouts are the same as they always were.
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
#include "MenuGlue.h"

#include "MenuContext.h"
#include "SettingsFrame.h"
#include "OptionsFrame.h"
#include "MenuEngine.h"
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../wiiSXconfig.h"
#include "../MEM2.h"

extern "C" {
#include "../../mem2_manager.h"
#include "../lc.h"
}

extern MenuContext *pMenuContext;
extern "C" void SysPrintf(const char *fmt, ...);

namespace eng = menu::engine;

/* Applied when the Plugins page is left, not on every press: changing either plugin with a
 * game loaded tears the emulator down and starts it again, which would fire on each step of
 * a cycling button. Defined in SettingsFrame.cpp, where the reset machinery already lives. */
extern void ApplyPluginSelection(char wantCore, char wantGpu);

namespace menu { extern heap_cntrl* GXtexCache; extern unsigned int fontHeapBytes; }   /* the font glyph heap, IPLFont.cpp */

void Func_ReturnFromOptionsFrame();

/*  Rows  */

#define OPT_CYCLE 0
#define OPT_INFO  1
#define OPT_RADIO 2

struct OptRow
{
	int					kind;
	const char*			key;		// CYCLE/RADIO: the setting, by its key in the OPTIONS table (GamecubeMain.cpp)
	void				(*apply)(void);	// CYCLE/RADIO: tell the emulator; NULL = it reads the setting itself
	void				(*info)(char *buf, int len);	// INFO: writes the right-hand text
	const char*			label;		// INFO: the readout's label (a setting's label is in the table)
	bool				fit;		// RADIO: size each button to its own word, not to RADIO_W
};

#define ROW_CYCLE(key, apply) \
	{ OPT_CYCLE, key, apply, NULL, NULL, false }
#define ROW_RADIO(key, apply) \
	{ OPT_RADIO, key, apply, NULL, NULL, false }
/* The same, but each button only as wide as its own word. Right when the words differ a
 * lot in length; a row whose choices are of a size reads better on the fixed grid. */
#define ROW_RADIO_FIT(key, apply) \
	{ OPT_RADIO, key, apply, NULL, NULL, true }
#define ROW_INFO(label, fn) \
	{ OPT_INFO, NULL, NULL, fn, label, false }

/* setSpuReverb(int) takes the raw setting value; the table's apply slot is void(void), so
 * this wrapper reads the setting itself, the same way GamecubeMain.cpp does at startup. */
extern "C" void setSpuReverb(int soundReverb);
/* The Memory readouts: Lightrec's code use (deps/lightrec), the OpenGX texture pool (deps/opengx),
 * and the PS1 RAM / VRAM sizes (gpulib). */
extern "C" {
unsigned int lightrec_get_mem_usage(int type);	/* MEM_FOR_CODE is 0 */
void ogx_tex_mem(unsigned *n, unsigned *data_kb, unsigned *semi_kb);
extern const unsigned globalVramSize;
}
static void applySpuReverb(void) { setSpuReverb(soundReverb); }
void applyVideoGamma(void);		/* Gamecube/libgui/GraphicsGX.cpp: one VI write */

/*  Readouts (Memory page)  */

/* MEM1 is the 24 MB of fast 1T-SRAM on the graphics die: emulated PSX RAM, the GPU's
 * buffers and everything the CPU touches per frame live here. What is left is what any
 * future allocation has to fit into. */
#define MEM1_KB (24u * 1024)
static void infoMem1(char *buf, int len)
{
	unsigned long free_kb = (unsigned long)(SYS_GetArena1Size() >> 10);
	snprintf(buf, len, "%lu used, %lu free KB", MEM1_KB - free_kb, free_kb);
}

/* The locked cache: half of the CPU's 32 KB data cache, locked as 16 KB of scratch memory
 * for the regions the LockedCache setting turns on (Gamecube/lc.c, Docs/LOCKED_CACHE.md).
 * It is set up when a game starts, so this shows the last game's regions. */
static void infoLockedCache(char *buf, int len)
{
	char names[48];
	unsigned used = lc_usage(names, sizeof names);
	snprintf(buf, len, "%u / %u KB (%s)", used >> 10, LC_TOTAL_BYTES >> 10, names);
}

/* MEM2 is the 64 MB of slower off-die GDDR3. This is the general heap the emulator carves
 * textures, the disc-image buffers and the sound buffers out of (mem2_manager.c). */
static void infoMem2(char *buf, int len)
{
	unsigned long used = gx_mem2_used() >> 10, total = gx_mem2_total() >> 10;
	snprintf(buf, len, "%lu used, %lu free KB", used, total - used);
}

/* The OpenGX textures in MEM2: how many, and their 16-bit data (and the semi-transparent copies). */
static void infoTexPool(char *buf, int len)
{
	unsigned n = 0, data = 0, semi = 0;
	ogx_tex_mem(&n, &data, &semi);
	snprintf(buf, len, "%u textures, %u + %u KB", n, data, semi);
}

/* The glyph cache: one 1152-byte RGB5A3 tile per character, in a block of the general MEM2
 * heap sized to the loaded font (IPLFont.cpp): a Latin menu takes a few hundred KB, a full
 * CJK font up to MEM2.h CN_FONT_SIZE. */
static void infoFontCache(char *buf, int len)
{
	unsigned long used = 0, total = (unsigned long)menu::fontHeapBytes >> 10;
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
		snprintf(buf, len, "%u of %lu KB used", lightrec_get_mem_usage(0) >> 10, (unsigned long)LIGHTREC_CODE_SIZE >> 10);
}

/* Sound and disc buffers, both fixed regions. */
static void infoFixedBuffers(char *buf, int len)
{
	snprintf(buf, len, "%lu + %lu KB",
		(unsigned long)SPU_BUF_SIZE >> 10, (unsigned long)PSXR_BUF_SIZE >> 10);
}

/* The emulated PlayStation's main RAM (2 MB) and VRAM: the sizes of the buffers, not what a game uses. */
static void infoPsxBuffers(char *buf, int len)
{
	snprintf(buf, len, "%u + %lu KB", 2048u, (unsigned long)globalVramSize >> 10);
}

/*  Pages  */

static const OptRow SOUND_ROWS[] =
{
	ROW_CYCLE("SoundResampler", NULL),
	ROW_CYCLE("SoundReverb", applySpuReverb),
	ROW_CYCLE("SoundMixerPrecision", NULL),
	ROW_CYCLE("SoundXaResampler", NULL),
};

/* A line of help under the rows. `term` is the setting being explained, drawn bold in its
 * own column; `text` continues in the second column, so a NULL term indents under the
 * definition above it. The engine's HelpLine is exactly this. */
typedef eng::HelpLine OptHelp;

/* Radio rows, not cycling ones: which core and which renderer is running is worth seeing
 * whole. Both tear down and restart the emulator when a game is loaded, so they are applied
 * once on leaving the page (pluginsEnter / pluginsLeave) rather than on every press. */
static const OptRow PLUGIN_ROWS[] =
{
	/* Fitted: "Interpreter" is wider than a fixed 135 px button. */
	ROW_RADIO_FIT("Core", NULL),
	ROW_CYCLE("CpuTiming", NULL),
	ROW_CYCLE("CpuSpeed", NULL),
};

/* The SPU plugin: there is one (P.E.Op.S. DFSound, from PCSX-ReARMed), so it is a readout;
 * what can be swapped is where its output is resampled and mixed for the Wii. */
static void infoSpuPlugin(char *buf, int len) { snprintf(buf, len, "P.E.Op.S. DFSound"); }

static const OptRow SPU_ROWS[] =
{
	ROW_INFO("SPU Plugin", infoSpuPlugin),
	ROW_RADIO("SoundHwAccel", NULL),
};

static const OptHelp SPU_HELP[] =
{
	{ "Output:", "CPU resamples and mixes on the Wii's CPU." },
	{ NULL,      "DSP hands the stream to the Wii's sound chip." },
	{ NULL,      "Sound quality is in Audio, Advanced." },
};

static const OptRow GPU_ROWS[] =
{
	/* Fitted: "Soft Timed" is wider than a fixed 135 px button. The row starts further left
	 * than the others to fit (MenuEngine's choiceStart). */
	ROW_RADIO_FIT("gpuPlugin", NULL),
	ROW_CYCLE("GpuTiming", NULL),
};

static const OptHelp GPU_HELP[] =
{
	{ "GPU Plugin:", "OpenGX uses the Wii's GPU; Soft avoids its quirks." },
	{ "GPU Timing:", "Accurate: the GPU is busy as long as on a PS1." },
};

static const OptRow INPUT_ROWS[] =
{
	ROW_CYCLE("SioTiming", NULL),
};

static const OptHelp INPUT_HELP[] =
{
	{ "Port Timing:", "Accurate: each byte on the controller ports takes" },
	{ NULL,           "as long as on a PS1. Fast is the default." },
};

static const OptHelp PLUGIN_HELP[] =
{
	{ "CPU Core:",   "Lightrec runs most games best; Interpreter is" },
	{ NULL,          "exact but slow. A new core restarts the game." },
	{ "CPU Timing:", "Accurate: 3D-maths and multiply waits as on a PS1." },
	{ "CPU Speed:",  "Over 100% eases slowdown in some games, at more" },
	{ NULL,          "Wii CPU time. Save it for one game: This Game." },
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

/* The page is two columns of label + button (OptPage::columns): Dithering, a choice row, takes a
 * whole line, the four cycles pair up below it. Dithering and MDEC Chroma are read when a disc
 * image is opened, so they take effect at the next game load rather than immediately
 * (SETTINGS.md section 2). "Default" is the Dithering setting's middle choice. Gamma is written
 * to the video encoder now; Deflicker Level is read when the video mode is set up. */
static const OptRow ADVGFX_ROWS[] =
{
	ROW_RADIO_FIT("Dithering", NULL),
	ROW_CYCLE("MdecChroma", NULL),
	ROW_CYCLE("FmvColour", NULL),
	ROW_CYCLE("Gamma", applyVideoGamma),
	ROW_CYCLE("DeflickerLevel", NULL),
};

static const OptHelp ADVGFX_HELP[] =
{
	{ "Dithering:",    "Hides the steps between colours in a 15-bit picture." },
	{ NULL,            "Default uses it only where the game asks for it." },
	{ "MDEC Chroma:",  "Sharp is the console: one colour sample per 2x2 pixels." },
	{ NULL,            "Smooth mixes the samples: it looks better, costs more." },
	{ NULL,            "It applies to the next video that starts." },
	{ "Video Colour:", "24-bit is full colour. 15-bit bands in dark scenes." },
	{ "Gamma:",        "Brightens the TV picture. 1.0 changes nothing." },
	{ "Deflicker Level:", "Light blurs less than Normal. Needs Deflicker on." },
};

static const OptRow CD_ROWS[] =
{
	ROW_CYCLE("CdBuffer", NULL),
	ROW_CYCLE("CdPrefetch", NULL),
	ROW_CYCLE("CdChdHunks", NULL),
};

static const OptHelp CD_HELP[] =
{
	{ "CD Read Buffer:", "16 KB is best: the card is read in 32 KB pages anyway," },
	{ NULL,              "and a larger buffer only reads more than the game uses." },
	{ "CD Read-Ahead:",  "A thread reads the next sectors early, so the game" },
	{ NULL,              "waits less for the card. Raw bin/cue and .iso only." },
	{ "CHD Hunk Cache:", "Decoded CHD hunks kept in memory, about 20 KB each." },
	{ NULL,              "More never decodes more often: 8 is best." },
	{ NULL,              "All three apply at the next game load." },
};

/* Readouts only. Every large MEM2 region is reserved at a fixed address (Gamecube/MEM2.h),
 * so there is nothing here a setting could move at runtime; what this page is for is seeing
 * where the memory went, which is what any change to that layout has to be argued from. */
static const OptRow MEMORY_ROWS[] =
{
	ROW_INFO("MEM1 (fast)",         infoMem1),
	ROW_INFO("MEM2 heap",           infoMem2),
	ROW_INFO("Texture pool",        infoTexPool),
	ROW_INFO("Locked cache",        infoLockedCache),
	ROW_INFO("Font glyph cache",    infoFontCache),
	ROW_INFO("Recompiler buffer",   infoJitBuffer),
	ROW_INFO("PS1 RAM + VRAM",      infoPsxBuffers),
	ROW_INFO("SPU + BIOS buffers",  infoFixedBuffers),
};

struct OptPage
{
	const char*			title;			// shown at the top; a tabbed page shows its tabs instead
	const char*			tab;			// the page's tab among the Advanced pages, or NULL
	const OptRow*		rows;
	int					nrows;
	const OptHelp*		help;			// explanation under the rows; NULL for none
	int					nhelp;
	int					returnSubmenu;	// the Settings tab B goes back to
	void				(*onEnter)(void);
	void				(*onLeave)(void);
	float				rowDy;			// the row pitch, or 0 for the default (60)
	int					columns;		// 2: cycle rows in two label + button columns (eng::Page::columns)
};

#define COUNT(a)	((int)(sizeof(a) / sizeof((a)[0])))

static const OptPage PAGES[] =
{
	{ "Advanced Sound", NULL, SOUND_ROWS, COUNT(SOUND_ROWS), NULL,        0,                  SettingsFrame::SUBMENU_AUDIO,   NULL,         NULL         },
	{ "Advanced Graphics", NULL, ADVGFX_ROWS, COUNT(ADVGFX_ROWS), ADVGFX_HELP, COUNT(ADVGFX_HELP), SettingsFrame::SUBMENU_VIDEO, NULL,  NULL,         0.0f, 2 },
	{ "Advanced", "CPU",  PLUGIN_ROWS,  COUNT(PLUGIN_ROWS),  PLUGIN_HELP, COUNT(PLUGIN_HELP), SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Advanced", "CD",   CD_ROWS,      COUNT(CD_ROWS),      CD_HELP,     COUNT(CD_HELP),     SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Memory",   NULL,   MEMORY_ROWS,  COUNT(MEMORY_ROWS),  NULL,        0,                  SettingsFrame::SUBMENU_GENERAL, NULL,         NULL,         46.0f },
	{ "Advanced", "GPU",  GPU_ROWS,     COUNT(GPU_ROWS),     GPU_HELP,    COUNT(GPU_HELP),    SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Advanced", "Input", INPUT_ROWS,  COUNT(INPUT_ROWS),   INPUT_HELP,  COUNT(INPUT_HELP),  SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
	{ "Advanced", "SPU",  SPU_ROWS,     COUNT(SPU_ROWS),     SPU_HELP,    COUNT(SPU_HELP),    SettingsFrame::SUBMENU_GENERAL, pluginsEnter, pluginsLeave },
};

/* The Advanced pages, in the order their tabs run across the top. A tab switch stays inside
 * the group: the page entered first decides where B returns to, and the core/plugin choice
 * (pluginsEnter/pluginsLeave) is taken on entering the group and applied on leaving it. */
static const int ADV_TABS[] = {
	OptionsFrame::PAGE_PLUGINS, OptionsFrame::PAGE_ADV_GPU, OptionsFrame::PAGE_ADV_SPU,
	OptionsFrame::PAGE_STORAGE, OptionsFrame::PAGE_ADV_INPUT
};
#define NUM_TABS	((int)(sizeof(ADV_TABS) / sizeof(ADV_TABS[0])))
/* The engine centres the tab row. Its widths are the Settings tab row's (SettingsFrame.h), so
 * moving between the two menus does not shift anything. */

#define NUM_PAGES		COUNT(PAGES)
#define OPT_MAX_ROWS	8
#define OPT_MAX_CHOICES	3
#define OPT_MAX_HELP	8

#define VALUE_LEN	32

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
/* The setting each row edits, by its key, looked up once when its page is shown (bindPage).
 * A readout has none. */
static const SettingOption*	ROW_SET[OPT_MAX_ROWS];
static menu::TextBox*	helpTermBox[OPT_MAX_HELP];
static menu::TextBox*	helpBox[OPT_MAX_HELP];
static char*			helpTermString[OPT_MAX_HELP];
static char*			helpString[OPT_MAX_HELP];
static char				helpEmpty[1] = "";
static menu::TextBox*	titleBox;
static char*			titleString;
static int				currentPage;
static menu::Button*	TABS[NUM_TABS];
static char*			tabString[NUM_TABS];
static int				returnSubmenu;		// the Settings tab B goes back to
static bool				tabSwitch;			// activateSubmenu called for a tab, inside the group
static OptionsFrame*	optionsFrame;

/* The page on show, as the engine sees it, and the engine's layout of it. The layout is
 * about 7 KB, so it is static, not on the stack. */
static eng::Row			ENG_ROWS[OPT_MAX_ROWS];
static eng::Tab			ENG_TABS[NUM_TABS];
static eng::Page		ENG_PAGE;
static eng::Layout		ENG_LAYOUT;

/* The fixed sizes this frame relies on, against the engine's. */
typedef char opt_rows_match_engine[OPT_MAX_ROWS == eng::MAX_ROWS ? 1 : -1];
typedef char opt_help_match_engine[OPT_MAX_HELP == eng::MAX_HELP ? 1 : -1];
typedef char opt_choices_fit_engine[OPT_MAX_CHOICES <= eng::MAX_SLOTS ? 1 : -1];
typedef char opt_tabs_fit_engine[NUM_TABS <= eng::MAX_TABS ? 1 : -1];

static void Func_OptTab0() { optionsFrame->showTab(0); }
static void Func_OptTab1() { optionsFrame->showTab(1); }
static void Func_OptTab2() { optionsFrame->showTab(2); }
static void Func_OptTab3() { optionsFrame->showTab(3); }
static void Func_OptTab4() { optionsFrame->showTab(4); }
static void (*const TAB_FUNCS[5])() = { Func_OptTab0, Func_OptTab1, Func_OptTab2, Func_OptTab3, Func_OptTab4 };

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

/* Every page must fit on the screen (the engine checks the geometry), the page tables must
 * not outgrow the widgets. */
#define OPT_PAGE_FITS(rows)		(COUNT(rows) <= OPT_MAX_ROWS)
#define OPT_HELP_FITS(help)		(COUNT(help) <= OPT_MAX_HELP)

typedef char opt_sound_fits  [OPT_PAGE_FITS(SOUND_ROWS)   ? 1 : -1];
typedef char opt_plugin_fits [OPT_PAGE_FITS(PLUGIN_ROWS)  ? 1 : -1];
typedef char opt_advgfx_fits[OPT_PAGE_FITS(ADVGFX_ROWS) ? 1 : -1];
typedef char opt_cd_fits[OPT_PAGE_FITS(CD_ROWS) ? 1 : -1];
typedef char opt_gpu_fits[OPT_PAGE_FITS(GPU_ROWS) ? 1 : -1];
typedef char opt_spu_fits[OPT_PAGE_FITS(SPU_ROWS) ? 1 : -1];
typedef char opt_spu_help_fits[OPT_HELP_FITS(SPU_HELP) ? 1 : -1];
typedef char opt_input_fits[OPT_PAGE_FITS(INPUT_ROWS) ? 1 : -1];
typedef char opt_gpu_help_fits[OPT_HELP_FITS(GPU_HELP) ? 1 : -1];
typedef char opt_input_help_fits[OPT_HELP_FITS(INPUT_HELP) ? 1 : -1];
typedef char opt_memory_fits [OPT_PAGE_FITS(MEMORY_ROWS)  ? 1 : -1];
typedef char opt_plugin_help_fits [OPT_HELP_FITS(PLUGIN_HELP)  ? 1 : -1];
typedef char opt_advgfx_help_fits[OPT_HELP_FITS(ADVGFX_HELP) ? 1 : -1];
typedef char opt_cd_help_fits[OPT_HELP_FITS(CD_HELP) ? 1 : -1];

static const OptRow* pageRow(int row)
{
	return &PAGES[currentPage].rows[row];
}

static void bindPage(int page)
{
	for (int i = 0; i < PAGES[page].nrows; i++)
		ROW_SET[i] = PAGES[page].rows[i].kind == OPT_INFO ? NULL : settingByKey(PAGES[page].rows[i].key);
}

/* How many buttons a radio row shows. A row that declares more values than there are slots
 * would silently lose the rest, so it is clamped here and the clamp is the documented limit. */
static int rowChoices(int row)
{
	const SettingOption *s = ROW_SET[row];
	int n = (int)s->max - (int)s->min + 1;
	if (n < 1) n = 1;
	if (n > OPT_MAX_CHOICES) n = OPT_MAX_CHOICES;
	return n;
}

/* Re-read one row from the thing it reflects: the setting's value name, or the readout. */
static void refreshRow(int row)
{
	const OptRow *o = pageRow(row);
	const SettingOption *s = ROW_SET[row];
	if (o->kind == OPT_INFO) {
		o->info(ROWS[row].value, VALUE_LEN);
	} else if (o->kind == OPT_RADIO) {
		int n = rowChoices(row), v = *s->value;
		if (v < s->min || v > s->max) v = s->min;
		for (int c = 0; c < n; c++)
			ROWS[row].choice[c]->setSelected(c == v - s->min);
		return;
	} else {
		int v = *s->value;
		if (v < s->min || v > s->max) v = s->min;	// a value the settings file parser let through
		snprintf(ROWS[row].value, VALUE_LEN, "%s", s->choices[v - s->min]);
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
		const SettingOption *s = ROW_SET[row];
		int v = *s->value + 1;
		if (v > s->max || v < s->min) v = s->min;
		*s->value = (char)v;
	}
	refreshRow(row);
	if (o->apply) o->apply();
}

static void optSet(int row, int choice)
{
	const OptRow *o;
	if (row >= PAGES[currentPage].nrows) return;
	o = pageRow(row);
	if (o->kind != OPT_RADIO || choice >= rowChoices(row)) return;
	*ROW_SET[row]->value = (char)(ROW_SET[row]->min + choice);
	refreshRow(row);
	if (o->apply) o->apply();
}

/*  The page, for the engine  */

/* Describes the page on show as an engine Page. The rows keep their values in ROWS (readouts
 * are read into ROWS[i].value before this runs), so the engine lays out what is drawn. */
static void buildEnginePage(int page, int curTab)
{
	const OptPage &p = PAGES[page];
	const bool tabbed = p.tab != NULL;

	for (int t = 0; t < NUM_TABS; t++)
	{
		ENG_TABS[t].text  = PAGES[ADV_TABS[t]].tab;
		ENG_TABS[t].width = settingsTabWidths[t];
	}
	for (int i = 0; i < p.nrows; i++)
	{
		const OptRow *o = &p.rows[i];
		const SettingOption *s = ROW_SET[i];
		eng::Row &r = ENG_ROWS[i];
		memset(&r, 0, sizeof r);
		r.label = o->kind == OPT_INFO ? o->label : s->label;
		if (o->kind == OPT_INFO)
		{
			r.kind = eng::ROW_READOUT;
			r.text = ROWS[i].value;
		}
		else if (o->kind == OPT_RADIO)
		{
			r.kind  = eng::ROW_CHOICE;
			r.names = s->choices;
			r.lo    = s->min;
			r.count = rowChoices(i);
			r.value = *s->value;
			r.fit   = o->fit;
		}
		else
		{
			r.kind  = eng::ROW_CYCLE;
			r.names = s->choices;
			r.lo    = s->min;
			r.count = s->max - s->min + 1;
			r.value = *s->value;
		}
	}

	ENG_PAGE.title  = p.title;
	ENG_PAGE.tabs   = tabbed ? ENG_TABS : NULL;
	ENG_PAGE.nTabs  = tabbed ? NUM_TABS : 0;
	ENG_PAGE.curTab = curTab < 0 ? 0 : curTab;
	ENG_PAGE.rows   = ENG_ROWS;
	ENG_PAGE.nRows  = p.nrows;
	ENG_PAGE.help   = p.help;
	ENG_PAGE.nHelp  = p.nhelp;
	ENG_PAGE.logo   = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_OPTIONS is in LOGO_BOTTOM_FRAMES */
	ENG_PAGE.focus  = eng::FOCUS_ROWS;
	ENG_PAGE.rowDy  = p.rowDy;
	ENG_PAGE.columns = p.columns;
}

/* The widget a control of the engine's layout is drawn with. */
static menu::Button* buttonFor(const eng::Control &c)
{
	switch (c.role)
	{
	case eng::ROLE_TAB:    return TABS[c.slot];
	case eng::ROLE_BUTTON: return ROWS[c.row].button;
	case eng::ROLE_CHOICE: return ROWS[c.row].choice[c.slot];
	default:               return NULL;
	}
}

/* Every widget off and unlit: the engine's layout then switches on what the page shows. */
static void hideWidgets()
{
	titleBox->setVisible(false);
	for (int t = 0; t < NUM_TABS; t++)
	{
		TABS[t]->setVisible(false);
		TABS[t]->setActive(false);
		TABS[t]->setSelected(false);
	}
	for (int i = 0; i < OPT_MAX_ROWS; i++)
	{
		ROWS[i].textBox->setVisible(false);
		ROWS[i].rlabelBox->setVisible(false);
		ROWS[i].valueBox->setVisible(false);
		ROWS[i].button->setVisible(false);
		ROWS[i].button->setActive(false);
		for (int c = 0; c < OPT_MAX_CHOICES; c++)
		{
			ROWS[i].choice[c]->setVisible(false);
			ROWS[i].choice[c]->setActive(false);
			ROWS[i].choice[c]->setSelected(false);
		}
	}
	for (int i = 0; i < OPT_MAX_HELP; i++)
	{
		helpTermString[i] = helpEmpty;
		helpString[i] = helpEmpty;
		helpTermBox[i]->setVisible(false);
		helpBox[i]->setVisible(false);
	}
}

/* A button at the engine's rectangle. Its lit state is set by the caller, as it always was.
 * The pointer's hit box follows it, so a click lands where the button is drawn. */
static void placeButton(menu::Button *b, const eng::Control &c)
{
	b->setBounds(c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	b->setVisible(true);
	b->setActive(true);
	menu::Cursor::getInstance().moveComponent(optionsFrame, b,
		c.rect.x, c.rect.x + c.rect.w, c.rect.y, c.rect.y + c.rect.h);
}

/* Each shown control at its place. Text takes the engine's anchor: a TextBox draws itself
 * from there (centred, or right-aligned for the help terms), as it always has. */
static void placeLayout()
{
	for (int i = 0; i < ENG_LAYOUT.n; i++)
	{
		const eng::Control &c = ENG_LAYOUT.c[i];
		switch (c.role)
		{
		case eng::ROLE_TAB:
			placeButton(TABS[c.slot], c);
			TABS[c.slot]->setSelected(c.selected);
			break;
		case eng::ROLE_TITLE:
			titleString = (char*)c.text;
			titleBox->setPosition(c.ax, c.ay);
			titleBox->setVisible(true);
			break;
		case eng::ROLE_LABEL:
		case eng::ROLE_RLABEL:
		{
			menu::TextBox *box = c.role == eng::ROLE_LABEL ? ROWS[c.row].textBox : ROWS[c.row].rlabelBox;
			ROWS[c.row].labelString = (char*)c.text;
			box->setPosition(c.ax, c.ay);
			box->setVisible(true);
			break;
		}
		case eng::ROLE_VALUE:
			ROWS[c.row].valueBox->setPosition(c.ax, c.ay);
			ROWS[c.row].valueBox->setVisible(true);
			break;
		case eng::ROLE_BUTTON:
			placeButton(ROWS[c.row].button, c);
			break;
		case eng::ROLE_CHOICE:
			ROWS[c.row].choiceString[c.slot] = (char*)c.text;
			placeButton(ROWS[c.row].choice[c.slot], c);
			break;
		case eng::ROLE_HELP_TERM:
			helpTermString[c.slot] = (char*)c.text;
			helpTermBox[c.slot]->setPosition(c.ax, c.ay);
			helpTermBox[c.slot]->setVisible(true);
			break;
		case eng::ROLE_HELP_TEXT:
			helpString[c.slot] = (char*)c.text;
			helpBox[c.slot]->setPosition(c.ax, c.ay);
			helpBox[c.slot]->setVisible(true);
			break;
		}
	}
}

/* The engine's focus links, on the buttons. A link of -1 is no link. */
static void linkButtons()
{
	for (int i = 0; i < ENG_LAYOUT.n; i++)
	{
		const eng::Control &c = ENG_LAYOUT.c[i];
		if (!c.focusable)
			continue;
		menu::glue::linkFocus(buttonFor(c), ENG_LAYOUT, c, buttonFor);
	}
}

#ifdef PERF_PROF
/* The engine's checks, in debug builds only. Each page's problems are logged once, the first
 * time it is laid out. A problem is a finding about the layout, not a crash: a label wider
 * than its button, a readout over its label, a control in the logo's corner. */
static void logLayoutProblems(int page)
{
	static unsigned logged;		/* bit N: page N has been checked and logged */
	static eng::Problem probs[eng::MAX_PROBLEMS];
	int np;

	if (logged & (1u << page))
		return;
	logged |= 1u << page;

	np = eng::checkLayout(ENG_PAGE, menu::glue::METRICS, ENG_LAYOUT, probs, eng::MAX_PROBLEMS);
	if (np > 0)
		SysPrintf("OptionsFrame: page '%s' has %d layout problem(s)\n", PAGES[page].title, np);
	for (int k = 0; k < np && k < eng::MAX_PROBLEMS; k++)
	{
		const eng::Problem &p = probs[k];
		if (p.b >= 0)
			SysPrintf("OptionsFrame:   %s: '%s' and '%s'\n", eng::problemName(p.kind),
				ENG_LAYOUT.c[p.a].text, ENG_LAYOUT.c[p.b].text);
		else
			SysPrintf("OptionsFrame:   %s: '%s'\n", eng::problemName(p.kind), ENG_LAYOUT.c[p.a].text);
	}
}
#endif

OptionsFrame::OptionsFrame()
{
	titleString = (char*)PAGES[PAGE_SOUND].title;
	titleBox = new menu::TextBox(&titleString, 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
	add(titleBox);
	optionsFrame = this;

	for (int t = 0; t < NUM_TABS; t++)
	{
		tabString[t] = (char*)PAGES[ADV_TABS[t]].tab;
		/* BTN_A_SEL, as the Settings tabs: the current one stays lit */
		TABS[t] = new menu::Button(BTN_A_SEL, &tabString[t], 0.0f, 0.0f, 0.0f, 0.0f);
		TABS[t]->setActive(true);
		TABS[t]->setClicked(TAB_FUNCS[t]);
		TABS[t]->setReturn(Func_ReturnFromOptionsFrame);
		add(TABS[t]);
		/* The pointer's box is set by activateSubmenu(), from the engine's layout. */
		menu::Cursor::getInstance().addComponent(this, TABS[t], 0.0f, 0.0f, 0.0f, 0.0f);
	}

	for (int i = 0; i < OPT_MAX_ROWS; i++)
	{
		ROWS[i].labelString = (char*)"";
		ROWS[i].value[0] = '\0';
		ROWS[i].valueString = ROWS[i].value;
		/* The label lives in two places depending on the row kind, so it gets two boxes and
		 * whichever one the current page needs is the one made visible. */
		ROWS[i].textBox   = new menu::TextBox(&ROWS[i].labelString, 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
		ROWS[i].rlabelBox = new menu::TextBox(&ROWS[i].labelString, 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
		ROWS[i].valueBox  = new menu::TextBox(&ROWS[i].valueString, 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
		ROWS[i].button    = new menu::Button(BTN_A_NRM, &ROWS[i].valueString, 0.0f, 0.0f, 0.0f, 0.0f);
		ROWS[i].button->setActive(true);
		ROWS[i].button->setClicked(ROW_FUNCS[i]);
		ROWS[i].button->setReturn(Func_ReturnFromOptionsFrame);
		add(ROWS[i].textBox);
		add(ROWS[i].rlabelBox);
		add(ROWS[i].valueBox);
		add(ROWS[i].button);
		menu::Cursor::getInstance().addComponent(this, ROWS[i].button, 0.0f, 0.0f, 0.0f, 0.0f);

		for (int c = 0; c < OPT_MAX_CHOICES; c++)
		{
			ROWS[i].choiceString[c] = (char*)"";
			/* BTN_A_SEL is the style the Settings tabs use for a one-of-several choice: the
			 * selected one stays lit whether or not it has focus. */
			ROWS[i].choice[c] = new menu::Button(BTN_A_SEL, &ROWS[i].choiceString[c], 0.0f, 0.0f, 0.0f, 0.0f);
			ROWS[i].choice[c]->setActive(true);
			ROWS[i].choice[c]->setClicked(SET_FUNCS[i][c]);
			ROWS[i].choice[c]->setReturn(Func_ReturnFromOptionsFrame);
			add(ROWS[i].choice[c]);
			menu::Cursor::getInstance().addComponent(this, ROWS[i].choice[c], 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	for (int i = 0; i < OPT_MAX_HELP; i++)
	{
		helpTermString[i] = helpEmpty;
		helpString[i] = helpEmpty;
		helpTermBox[i] = new menu::TextBox(&helpTermString[i], 0.0f, 0.0f, eng::grid::HELP_SCALE, false);
		helpTermBox[i]->setRightAligned(true);
		helpTermBox[i]->setBold(true);
		helpBox[i] = new menu::TextBox(&helpString[i], 0.0f, 0.0f, eng::grid::HELP_SCALE, false);
		add(helpTermBox[i]);
		add(helpBox[i]);
	}

	setDefaultFocus(ROWS[0].button);
	setBackFunc(Func_ReturnFromOptionsFrame);
	setEnabled(true);
	activateSubmenu(PAGE_SOUND);
}

OptionsFrame::~OptionsFrame()
{
	delete titleBox;
	for (int t = 0; t < NUM_TABS; t++)
	{
		menu::Cursor::getInstance().removeComponent(this, TABS[t]);
		delete TABS[t];
	}
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
}

void OptionsFrame::activateSubmenu(int submenu)
{
	if (submenu < 0 || submenu >= NUM_PAGES) submenu = PAGE_SOUND;
	currentPage = submenu;
	bindPage(currentPage);

	int curTab = -1;
	for (int t = 0; t < NUM_TABS; t++)
		if (ADV_TABS[t] == currentPage) curTab = t;

	if (!tabSwitch)
	{
		returnSubmenu = PAGES[currentPage].returnSubmenu;
		if (PAGES[currentPage].onEnter) PAGES[currentPage].onEnter();
	}

	/* Hidden first; the layout then shows what this page has. */
	hideWidgets();
	/* The values the rows show. A readout's text is read here, before the engine lays it out. */
	for (int i = 0; i < PAGES[currentPage].nrows; i++)
		refreshRow(i);

	/* The engine lays the page out and links its focus: once, here, not per frame. */
	buildEnginePage(currentPage, curTab);
	if (eng::layoutPage(ENG_PAGE, menu::glue::METRICS, ENG_LAYOUT) < 0)
	{
		/* The tables are fixed and checked at compile time, so this is a programming error.
		 * Nothing is shown rather than a half-placed page, and the error says which page. */
		ENG_LAYOUT.n = 0;
		ENG_LAYOUT.defaultFocus = -1;
		SysPrintf("OptionsFrame: page %d does not fit the menu engine\n", currentPage);
	}
	else
	{
		eng::linkFocus(ENG_PAGE, ENG_LAYOUT);
		placeLayout();
		linkButtons();
#ifdef PERF_PROF
		logLayoutProblems(currentPage);
#endif
	}

	/* Focus starts on the page's first control. After a tab press it stays on the tab row, so
	 * Left/Right keep turning pages. */
	menu::Button *start = NULL;
	if (tabSwitch && curTab >= 0)
		start = TABS[curTab];
	else if (ENG_LAYOUT.defaultFocus >= 0)
		start = buttonFor(ENG_LAYOUT.c[ENG_LAYOUT.defaultFocus]);
	setDefaultFocus(start);
}

void OptionsFrame::showTab(int tab)
{
	if (tab < 0 || tab >= NUM_TABS || ADV_TABS[tab] == currentPage) return;
	tabSwitch = true;
	activateSubmenu(ADV_TABS[tab]);
	tabSwitch = false;
}

/* Readouts are read once, when their page opens (activateSubmenu), not per frame. */
void OptionsFrame::drawChildren(menu::Graphics& gfx)
{
	if (!isVisible()) return;

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
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS, returnSubmenu);
	if (onLeave) onLeave();
}
