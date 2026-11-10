/**
 * WiiStation - pages/OptionsPages.h
 *
 * The tables of the Options pages (Advanced Sound / Graphics, the five Advanced tabs, Memory, Achievements, ISO
 * Info): rows by setting key, their help, page properties. Plain data and function names, nothing that
 * needs the Wii: OptionsFrame.cpp (which defines the hooks and binds the keys to the settings) and the host
 * test tests/menu_pages_test.cpp (which stubs the hooks) include this one file, so the test checks the
 * tables the menu runs, not a copy.
 *
 * Index coupling that is now checked: OptPageId's values index PAGES[] (the test compares titles).
**/

#ifndef OPTIONSPAGES_H
#define OPTIONSPAGES_H

#include <stddef.h>
#include <string.h>
#include "../MenuEngine.h"
#include "../MenuTypes.h"
#include "../RomInfo.h"
#include "SettingsPages.h"

namespace eng = menu::engine;

/* The page ids (OptionsFrame::OptionsPages takes these values) and the Settings tab B returns to
 * (SettingsFrame::SettingsSubmenus' values; OptionsFrame.cpp asserts they match). */
enum OptPageId {
	OPT_PAGE_SOUND = 0, OPT_PAGE_ADVGFX, OPT_PAGE_PLUGINS, OPT_PAGE_STORAGE, OPT_PAGE_MEMORY,
	OPT_PAGE_ADV_GPU, OPT_PAGE_ADV_INPUT, OPT_PAGE_ADV_SPU, OPT_PAGE_ACHIEVEMENTS, OPT_PAGE_ISOINFO,
	OPT_PAGE_COUNT
};
enum OptReturn { OPT_RET_GENERAL = 0, OPT_RET_VIDEO = 1, OPT_RET_AUDIO = 3 };

/* The hooks the tables name. Defined in OptionsFrame.cpp; the host test defines empty ones. */
void applySpuReverb(void);
void applyFrameSkip(void);
void Func_AchievementsMode(void);	/* OptionsFrame.cpp: the cycle runs in display order, not numeric */
void applyVideoGamma(void);			/* Gamecube/libgui/GraphicsGX.cpp */
extern "C" void applyCopyFilter(void);	/* Gamecube/libgui/GraphicsGX.cpp */
void Func_UsbPadTestAdvanced(void);	/* UsbPadTestFrame.cpp */
void pluginsEnter(void);
void pluginsLeave(void);
void isoInfoEnter(void);	/* the ISO Info disc band: loads the disc picture, if the pack has one */
void isoInfoLeave(void);
void infoMem1(char *buf, int len);
void infoMem2(char *buf, int len);
void infoTexPool(char *buf, int len);
void infoLockedCache(char *buf, int len);
void infoFontCache(char *buf, int len);
void infoJitBuffer(char *buf, int len);
void infoFixedBuffers(char *buf, int len);
void infoPsxBuffers(char *buf, int len);
void infoSpuPlugin(char *buf, int len);
void infoAchFile(char *buf, int len);

/* Rows are SetRow (pages/SettingsPages.h): the same type, reader and writer as the Settings tabs. A cycle is
 * ROW_CYCLE, a radio row ROW_CHOICE with one SEG_EACH, a readout ROW_READOUT, an action ROW_BUTTONS. The
 * setting is named by its key in the OPTIONS table (GamecubeMain.cpp); apply tells the emulator (NULL = it
 * reads the setting itself). A row with no help of its own shows its setting's. */
#define ROW_CYCLE(key, apply) \
	{ eng::ROW_CYCLE, NULL, { SEG_CYCLE_THEN(key, apply) } }
#define ROW_RADIO(key, apply) \
	{ eng::ROW_CHOICE, NULL, { SEG_EACH(key, apply) } }
/* A readout. It never takes focus (Monty, 2026-11-09), so it has no help line. */
#define ROW_INFO(label, fn) \
	{ eng::ROW_READOUT, label, { }, 0, false, NULL, { NULL, NULL }, fn }
/* A label and one button that runs fn, e.g. opens a page of its own. No setting. */
#define ROW_ACTION(label, text, fn) \
	{ eng::ROW_BUTTONS, label, { SEG_ACT(label, text, fn) } }

/* The same with the row's own help, where the setting's would not do (or there is none: an
 * action). h1, h2: the footer's two lines (h2 may be NULL). */
#define ROW_CYCLE_H(key, apply, h1, h2) \
	{ eng::ROW_CYCLE, NULL, { SEG_CYCLE_THEN(key, apply) }, 0, false, NULL, { h1, h2 } }
#define ROW_RADIO_H(key, apply, h1, h2) \
	{ eng::ROW_CHOICE, NULL, { SEG_EACH(key, apply) }, 0, false, NULL, { h1, h2 } }
#define ROW_ACTION_H(label, text, fn, h1, h2) \
	{ eng::ROW_BUTTONS, label, { SEG_ACT(label, text, fn) }, 0, false, NULL, { h1, h2 } }

static const SetRow SOUND_ROWS[] =
{
	ROW_CYCLE_H("SoundResampler", NULL,
		"Turns the mixer's 44100 Hz into 48000 Hz. Hold repeats samples, Linear blends two,",
		"Cubic blends four (cleanest, most CPU). Applies at once."),
	ROW_CYCLE("SoundReverb", applySpuReverb),
	ROW_CYCLE_H("SoundMixerPrecision", NULL,
		"Legacy cuts off the low bits when the mixer scales a voice's volume.",
		"Hi-Fi rounds to the nearest value instead: slightly less noise."),
	ROW_CYCLE_H("SoundXaResampler", NULL,
		"Converts 37800 Hz XA streams to 44100 Hz. Legacy: the old nearest or Gaussian step.",
		"Hi-Fi: the PS1's own 29-tap filter. 18900 Hz streams always use Legacy."),
};

/* The five Advanced tabs (CPU, GPU, SPU, CD, Input) are standard pages (Docs/MENU_PLAN.md,
 * eng::Page::standard): a setting with a few values is a Choice (all its values at once, the
 * current one lit), CPU Speed's eight steps a Cycle, and each row's help (SetRow::help, or its
 * setting's own) is the footer, shown for the focused row (eng::FooterHelp). Which core and which renderer is running is worth
 * seeing whole. Both tear down and restart the emulator when a game is loaded, so they are
 * applied once on leaving the page (pluginsEnter / pluginsLeave) rather than on every press. */
static const SetRow PLUGIN_ROWS[] =
{
	ROW_RADIO("Core", NULL),
	ROW_RADIO("CpuTiming", NULL),
	ROW_CYCLE("CpuSpeed", NULL),
};

/* The SPU plugin: there is one (P.E.Op.S. DFSound, from PCSX-ReARMed), so it is a readout.
 * The output driver it plays through is the Audio tab's Sound Driver (SoundDriver). */

static const SetRow SPU_ROWS[] =
{
	ROW_INFO("SPU Plugin", infoSpuPlugin),
};

static const SetRow GPU_ROWS[] =
{
	ROW_CYCLE("gpuPlugin", NULL),
	ROW_RADIO("GpuTiming", NULL),
	ROW_CYCLE("GxTexMin", NULL),   /* OpenGX trials, read at game start (Docs/GX_QUALITY.md) */
	ROW_CYCLE("GxColor", NULL),
	ROW_RADIO("GxMask", NULL),  /* applied at the next present (GlesGpu updateDisplayGl) */
	ROW_RADIO("SkipFrames", applyFrameSkip),
};

static const SetRow INPUT_ROWS[] =
{
	ROW_RADIO("SioTiming", NULL),
	ROW_ACTION_H("USB Pad", "Test", Func_UsbPadTestAdvanced, "Shows what a USB controller sends, button by button, and logs it to usbpad.log.", NULL),	/* the USB Pad Test page (UsbPadTestFrame.cpp) */
};

/* Advanced Graphics is one page, two columns (OptPage::columns): Dithering takes a whole line, then the
 * cycles pair up in table order (Video Color | MDEC Chroma, Gamma | Filter Tour, ...). Dithering and MDEC Chroma are read when a disc image is opened, so they take effect
 * at the next game load (SETTINGS.md section 2). Gamma is written to the video encoder now, Deflicker Level to the
 * frame copy now (and again at every game start or resume, go()). Video Mode is read when WiiStation starts. Filter
 * Tour runs while a game does. Soft Mode (SoftGPU/acc.c) is read for every GPU command list: it applies at once. */
static const SetRow ADVGFX_ROWS[] =
{
	ROW_RADIO_H("Dithering", NULL,
		"None: never. Default: when the game asks. Always: every draw.",
		"Used by Soft Mode Fast (next game load) and OpenGX with GX Color 6-bit."),
	ROW_CYCLE_H("FmvColour", NULL,
		"OpenGX, for movies a game decodes in 24-bit: 24-bit is full color.",
		"15-bit shows bands in dark scenes."),
	ROW_CYCLE_H("MdecChroma", NULL,
		"Sharp: each color sample covers a 2x2 block of pixels.",
		"Smooth: blends the color between blocks and costs more. Applies at once."),
	ROW_CYCLE_H("Gamma", applyVideoGamma,
		"Brightens the whole picture, menu too, in steps from 1.0 (no change) to 1.5.",
		"Applies at once."),
	ROW_CYCLE_H("FilterTour", NULL,
		"Starts at the next game start or resume: every 4 seconds it changes the filters",
		"and names them under the FPS counter. Your settings return when the game stops."),
	/* Deflicker (the on/off the Video tab's Filters row used to toggle) next to its Level; both
	 * apply to the frame copy now */
	ROW_CYCLE("DeflickerFilter", applyCopyFilter),
	ROW_CYCLE_H("DeflickerLevel", applyCopyFilter,
		"Normal is the video mode's own filter. Light blurs less, Strong more.",
		"Interlaced modes only; needs Deflicker."),
	ROW_CYCLE_H("SoftGpuMode", NULL,
		"Soft GPUs only, at once. Accurate: PS1 drawing rules, with dither.",
		"Hi-Fi: the same, but no dither and finer colors. Fast: the old quicker code."),
	ROW_CYCLE("VideoMode", NULL),
};

/* Fast Load is read by the CD drive emulation (cdrom.c) as it runs: it applies at once. */
static const SetRow CD_ROWS[] =
{
	ROW_RADIO("CdBuffer", NULL),
	ROW_RADIO("CdPrefetch", NULL),
	ROW_RADIO("CdChdHunks", NULL),
	ROW_RADIO("fastLoad", NULL),
};

/* Readouts only. Every large MEM2 region is reserved at a fixed address (Gamecube/MEM2.h),
 * so there is nothing here a setting could move at runtime; what this page is for is seeing
 * where the memory went, which is what any change to that layout has to be argued from. */
static const SetRow MEMORY_ROWS[] =
{
	ROW_INFO("MEM1 (fast)", infoMem1),
	ROW_INFO("MEM2 heap", infoMem2),
	ROW_INFO("Texture pool", infoTexPool),
	ROW_INFO("Locked cache", infoLockedCache),
	ROW_INFO("Font glyph cache", infoFontCache),
	ROW_INFO("Recompiler buffer", infoJitBuffer),
	ROW_INFO("PS1 RAM + VRAM", infoPsxBuffers),
	ROW_INFO("SPU + BIOS buffers", infoFixedBuffers),
};

/*  The ISO Info page: the loaded game's rows, as RomInfo.h lists them  */

#define ISOINFO_ROW(label, fn) ROW_INFO(label, fn),
static const SetRow ISOINFO_ROWS[] = { ROM_INFO_ROWS(ISOINFO_ROW) };
#undef ISOINFO_ROW
/* With disc art the band shows the first three rows (Game, Disc, Image) and the grid starts at the fourth. */
#define ISOINFO_BAND_ROWS	3

/* The mode is a cycle: its five values do not fit a radio row (eng::MAX_SLOTS). A change
 * applies at the next game load (achievements_load), so there is no apply function. Each row has its own
 * footer help: the settings' own texts are the long form (SETTINGS.md). The accounts are in Players. */
static const SetRow ACH_ROWS[] =
{
	{ eng::ROW_CYCLE, NULL, { SEG_CYCLE_ACT("Achievements", Func_AchievementsMode) }, 0, false, NULL,
	  { "Local: RetroAchievements' set, nothing sent. Official: sent to a Players account.",
	    "Custom: your own set file. Test: same, nothing kept. Reload the game after a change." } },
	ROW_RADIO_H("AchievementUnofficial", NULL, "Official and Local: also the set's Unofficial achievements (not yet approved).", "Load the game again after a change."),
	ROW_RADIO_H("AchievementEncore", NULL, "Official: achievements you already have can be earned again this session.", "Nothing new is sent to the site. Load the game again after a change."),
	ROW_RADIO_H("AchievementChallenge", NULL, "Shows the flashing icon for an achievement that is one step away.", "Off hides it at once. The unlock card and the progress pop-up stay."),
	ROW_INFO("Set file", infoAchFile),
};

struct OptPage
{
	int					id;				// its OptPageId: PAGES[id] (the test checks it)
	const char*			title;			// shown at the top; a tabbed page shows its tabs instead
	const char*			tab;			// the page's tab among the Advanced pages, or NULL
	const SetRow*		rows;
	int					nrows;
	int					returnSubmenu;	// the Settings tab B goes back to; RETURN_CURRENT_ISO: Current ISO
	void				(*onEnter)(void);
	void				(*onLeave)(void);
	eng::Density		standard;		// the menu standard's density; the rows' help is the footer
	int					columns;		// 2: the Cycle rows pair up, two cells per line (eng::Page::columns); 0 = one per line
};

/* Where the Options frame puts the logo (OptionsFrame::logoSpot): a radio row reaches x=634. */
static const eng::LogoSpot OPT_LOGO = eng::LOGO_SPOT_BOTTOM;

#define COUNT(a)	((int)(sizeof(a) / sizeof((a)[0])))
#define RETURN_CURRENT_ISO	(-1)

/* A PAGES[] entry by field group (plain C++11 has no designated initializers): the name, the rows, the help
 * lines under them, where B goes, the enter/leave hooks, and the layout (a grid, or the menu standard). */
#define PG_NAME(id, title, tab)	id, title, tab
#define PG_ROWS(rows)			rows, COUNT(rows)
#define PG_HOOKS				pluginsEnter, pluginsLeave
#define PG_NOHOOKS				NULL, NULL
#define PG_STD(density)			density
#define PG_COLS(n)				n

static const OptPage PAGES[] =
{
	{ PG_NAME(OPT_PAGE_SOUND, "Advanced Sound", NULL),      PG_ROWS(SOUND_ROWS),   OPT_RET_AUDIO,   PG_NOHOOKS, PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_ADVGFX, "Advanced Graphics", NULL), PG_ROWS(ADVGFX_ROWS), OPT_RET_VIDEO,   PG_NOHOOKS, PG_STD(eng::DENSITY_REGULAR), PG_COLS(2) },
	{ PG_NAME(OPT_PAGE_PLUGINS, "Advanced", "CPU"),         PG_ROWS(PLUGIN_ROWS),  OPT_RET_GENERAL, PG_HOOKS,   PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_STORAGE, "Advanced", "CD"),          PG_ROWS(CD_ROWS),      OPT_RET_GENERAL, PG_HOOKS,   PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_MEMORY, "Memory", NULL),             PG_ROWS(MEMORY_ROWS),  OPT_RET_GENERAL, PG_NOHOOKS, PG_STD(eng::DENSITY_TEXT) },
	{ PG_NAME(OPT_PAGE_ADV_GPU, "Advanced", "GPU"),         PG_ROWS(GPU_ROWS),     OPT_RET_GENERAL, PG_HOOKS,   PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_ADV_INPUT, "Advanced", "Input"),     PG_ROWS(INPUT_ROWS),   OPT_RET_GENERAL, PG_HOOKS,   PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_ADV_SPU, "Advanced", "SPU"),         PG_ROWS(SPU_ROWS),     OPT_RET_GENERAL, PG_HOOKS,   PG_STD(eng::DENSITY_TEXT) },
	{ PG_NAME(OPT_PAGE_ACHIEVEMENTS, "Achievements", NULL), PG_ROWS(ACH_ROWS),     OPT_RET_GENERAL, PG_NOHOOKS, PG_STD(eng::DENSITY_REGULAR) },
	{ PG_NAME(OPT_PAGE_ISOINFO, "ISO Info", NULL),          PG_ROWS(ISOINFO_ROWS), RETURN_CURRENT_ISO, isoInfoEnter, isoInfoLeave, PG_STD(eng::DENSITY_TEXT) },   /* F1: readouts on the standard grid */
};

/* The Advanced pages, in the order their tabs run across the top. A tab switch stays inside
 * the group: the page entered first decides where B returns to, and the core/plugin choice
 * (pluginsEnter/pluginsLeave) is taken on entering the group and applied on leaving it. */
static const int ADV_TABS[] = {
	OPT_PAGE_PLUGINS, OPT_PAGE_ADV_GPU, OPT_PAGE_ADV_SPU,
	OPT_PAGE_STORAGE, OPT_PAGE_ADV_INPUT
};
#define NUM_TABS	((int)(sizeof(ADV_TABS) / sizeof(ADV_TABS[0])))
/* The tab group a page belongs to (its pages in tab order), or NULL for a page without tabs. */
static inline const int *tabGroupOf(int page, int *n)
{
	for (int t = 0; t < NUM_TABS; t++) if (ADV_TABS[t] == page) { *n = NUM_TABS; return ADV_TABS; }
	*n = 0;
	return NULL;
}
/* The engine centres the tab row. Its widths are the Settings tab row's (SettingsFrame.h), so
 * moving between the two menus does not shift anything. */

#define NUM_PAGES		COUNT(PAGES)
/* Every page must fit the engine's fixed tables. */
static_assert(COUNT(SOUND_ROWS) <= eng::MAX_ROWS && COUNT(PLUGIN_ROWS) <= eng::MAX_ROWS &&
              COUNT(ADVGFX_ROWS) <= eng::MAX_ROWS && COUNT(CD_ROWS) <= eng::MAX_ROWS &&
              COUNT(GPU_ROWS) <= eng::MAX_ROWS && COUNT(SPU_ROWS) <= eng::MAX_ROWS &&
              COUNT(INPUT_ROWS) <= eng::MAX_ROWS && COUNT(MEMORY_ROWS) <= eng::MAX_ROWS &&
              COUNT(ACH_ROWS) <= eng::MAX_ROWS && COUNT(ISOINFO_ROWS) <= eng::MAX_ROWS,
              "an Options page has at most eng::MAX_ROWS rows");
static_assert(NUM_TABS <= eng::MAX_TABS, "the Advanced tabs fit the engine's tab table");

static_assert(COUNT(PAGES) == OPT_PAGE_COUNT, "PAGES: one entry per OptPageId (each entry's id is checked against its index by tests/menu_pages_test.cpp)");

/* ---- The engine page of an Options page: the Wii's OptionsFrame and the host test build it here ---- */

/* Describes PAGES[page] as an engine page (the rows through buildRow, as the Settings tabs). */
/* skip: leading rows left out of the page (the ISO Info rows the disc band shows instead); rowY0: the first row's top
 * (0 = the standard's). */
static inline void buildOptionsPage(int page, int curTab, const SetBind &bind, SetPage &o, int skip = 0, float rowY0 = 0)
{
	const OptPage &p = PAGES[page];
	int nTabs = 0;
	const int *group = tabGroupOf(page, &nTabs);

	memset(&o, 0, sizeof o);
	for (int t = 0; t < nTabs; t++)
		o.tabs[t].text = PAGES[group[t]].tab;	/* a standard page measures the text: no width */
	for (int i = 0; i < p.nrows - skip; i++)
		buildRow(p.rows[skip + i], i, bind, o);
	o.page.title    = p.title;
	o.page.tabs     = group ? o.tabs : NULL;
	o.page.nTabs    = nTabs;
	o.page.curTab   = curTab < 0 ? 0 : curTab;
	o.page.rows     = o.rows;
	o.page.nRows    = p.nrows - skip;
	o.page.rowY0    = rowY0;
	o.page.logo     = OPT_LOGO;
	o.page.focus    = eng::FOCUS_ROWS;
	o.page.standard = p.standard;
	o.page.columns  = p.columns;
	o.page.footer   = o.footer;
}

#endif
