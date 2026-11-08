/**
 * WiiStation - pages/OptionsPages.h
 *
 * The tables of the Options pages (Advanced Sound / Graphics, the five Advanced tabs, Memory, Achievements, ISO
 * Info): rows by setting key, footers, help, page properties. Plain data and function names, nothing that
 * needs the Wii: OptionsFrame.cpp (which defines the hooks and binds the keys to the settings) and the host
 * test tests/menu_pages_test.cpp (which stubs the hooks) include this one file, so the test checks the
 * tables the menu runs, not a copy.
 *
 * Index coupling that is now checked: OptPageId's values index PAGES[] (the test compares titles), and
 * every footer array has one entry per row (the test).
**/

#ifndef OPTIONSPAGES_H
#define OPTIONSPAGES_H

#include <stddef.h>
#include "../MenuEngine.h"
#include "../RomInfo.h"

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
void applyVideoGamma(void);			/* Gamecube/libgui/GraphicsGX.cpp */
extern "C" void applyCopyFilter(void);	/* Gamecube/libgui/GraphicsGX.cpp */
void Func_UsbPadTestAdvanced(void);	/* UsbPadTestFrame.cpp */
void pluginsEnter(void);
void pluginsLeave(void);
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

#define OPT_CYCLE 0
#define OPT_INFO  1
#define OPT_RADIO 2
#define OPT_ACTION 3

struct OptRow
{
	int					kind;
	const char*			key;		// CYCLE/RADIO: the setting, by its key in the OPTIONS table (GamecubeMain.cpp); ACTION: the button's text
	void				(*apply)(void);	// CYCLE/RADIO: tell the emulator; NULL = it reads the setting itself; ACTION: what the button does
	void				(*info)(char *buf, int len);	// INFO: writes the right-hand text
	const char*			label;		// INFO/ACTION: the row's label (a setting's label is in the table)
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
/* A label and one button that runs fn, e.g. opens a page of its own. No setting. */
#define ROW_ACTION(label, text, fn) \
	{ OPT_ACTION, text, fn, NULL, label, false }

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

/* The five Advanced tabs (CPU, GPU, SPU, CD, Input) are standard pages (Docs/MENU_PLAN.md,
 * eng::Page::standard): a setting with a few values is a Choice (all its values at once, the
 * current one lit), CPU Speed's eight steps a Cycle, and each row's help is the footer, shown
 * for the focused row (eng::FooterHelp). Which core and which renderer is running is worth
 * seeing whole. Both tear down and restart the emulator when a game is loaded, so they are
 * applied once on leaving the page (pluginsEnter / pluginsLeave) rather than on every press. */
static const OptRow PLUGIN_ROWS[] =
{
	ROW_RADIO("Core", NULL),
	ROW_RADIO("CpuTiming", NULL),
	ROW_CYCLE("CpuSpeed", NULL),
};

/* Footer help: row i's one or two lines (13 px, under the rows; the engine checks that each fits). */
static const eng::FooterHelp PLUGIN_FOOTER[] =
{
	{ { "Lightrec runs most games best; Interpreter is exact but slow.", "A new core restarts the game." } },
	{ { "Accurate: 3D math and multiply waits as on a PS1.", NULL } },
	{ { "Over 100% eases slowdown in some games, at more Wii CPU time.", "Save it for one game: This Game." } },
};

/* The SPU plugin: there is one (P.E.Op.S. DFSound, from PCSX-ReARMed), so it is a readout.
 * The output driver it plays through is the Audio tab's Sound Driver (SoundDriver). */

static const OptRow SPU_ROWS[] =
{
	ROW_INFO("SPU Plugin", infoSpuPlugin),
};

/* Nothing on the page can take focus, so the footer shows row 0's help. */
static const eng::FooterHelp SPU_FOOTER[] =
{
	{ { "Emulates the PS1 sound chip on the Wii's CPU.", "The Sound Driver is on the Audio tab; sound quality in Audio, Advanced." } },
};

static const OptRow GPU_ROWS[] =
{
	ROW_CYCLE("gpuPlugin", NULL),
	ROW_RADIO("GpuTiming", NULL),
	ROW_CYCLE("GxTexMin", NULL),   /* OpenGX trials, read at game start (Docs/GX_QUALITY.md) */
	ROW_CYCLE("GxColor", NULL),
	ROW_RADIO("GxMask", NULL),     /* applied at the next present (GlesGpu updateDisplayGl) */
	ROW_RADIO("SkipFrames", applyFrameSkip),
};

static const eng::FooterHelp GPU_FOOTER[] =
{
	{ { "OpenGX draws with the Wii's GPU. Soft Fast and Soft Timed draw with the CPU:", "slower, but exact. A change applies at the next game load." } },
	{ { "Accurate: the GPU is busy as long as on a PS1.", NULL } },
	{ { "OpenGX: smooths ground drawn smaller than a pixel. A trial.", NULL } },
	{ { "OpenGX: 6-bit dither, or anti-aliased edges in 240p mode. A trial.", NULL } },
	{ { "Accurate: the PS1 mask bit per pixel. Fast guesses it.", NULL } },
	{ { "On: skips frames when the Wii is too slow.", NULL } },
};

static const OptRow INPUT_ROWS[] =
{
	ROW_RADIO("SioTiming", NULL),
	ROW_ACTION("USB Pad", "Test", Func_UsbPadTestAdvanced),	/* the USB Pad Test page (UsbPadTestFrame.cpp) */
};

static const eng::FooterHelp INPUT_FOOTER[] =
{
	{ { "Accurate: each byte on the controller ports takes as long as on a PS1.", "Fast is the default." } },
	{ { "Shows what a USB controller sends, button by button, and logs it to usbpad.log.", NULL } },
};

/* The page is two columns of label + button (OptPage::columns): Dithering, a choice row, takes a
 * whole line, the cycles pair up below it. No help lines: the room went to the rows, and
 * SETTINGS.md section 3 explains each. Dithering and MDEC Chroma are read when a disc image is
 * opened, so they take effect at the next game load (SETTINGS.md section 2). Gamma is written to
 * the video encoder now, Deflicker Level to the frame copy now (and again at every game start or
 * resume, go()). Video Mode is read when WiiStation starts. Filter Tour runs while a game does.
 * Soft Mode (SoftGPU/acc.c) is read for every GPU command list: it applies at once. It is here,
 * not on the Advanced GPU tab, which holds five rows only at a 54 px pitch with four help lines. */
static const OptRow ADVGFX_ROWS[] =
{
	ROW_RADIO_FIT("Dithering", NULL),
	ROW_CYCLE("FmvColour", NULL),
	ROW_CYCLE("MdecChroma", NULL),
	ROW_CYCLE("Gamma", applyVideoGamma),
	ROW_CYCLE("FilterTour", NULL),
	/* Deflicker (the on/off the Video tab's Filters row used to toggle) next to its Level; both
	 * apply to the frame copy now */
	ROW_CYCLE("DeflickerFilter", applyCopyFilter),
	ROW_CYCLE("DeflickerLevel", applyCopyFilter),
	/* this way round: Video Mode's "PAL 60" in the left column pushes the grid into the margin */
	ROW_CYCLE("SoftGpuMode", NULL),
	ROW_CYCLE("VideoMode", NULL),
};

/* Fast Load is read by the CD drive emulation (cdrom.c) as it runs: it applies at once. */
static const OptRow CD_ROWS[] =
{
	ROW_RADIO("CdBuffer", NULL),
	ROW_RADIO("CdPrefetch", NULL),
	ROW_RADIO("CdChdHunks", NULL),
	ROW_RADIO("fastLoad", NULL),
};

static const eng::FooterHelp CD_FOOTER[] =
{
	{ { "16 KB is best: the card is read in 32 KB pages, so a larger", "buffer only reads more than the game uses. Next game load." } },
	{ { "A thread reads the next sectors early, so the game waits less", "for the card. Raw bin/cue and .iso only. Next game load." } },
	{ { "Decoded CHD hunks, about 20 KB each. 8 is best.", "Applies at the next game load." } },
	{ { "Shorter CD waits. Some games break with it.", NULL } },
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

/*  The ISO Info page: the loaded game's rows, as RomInfo.h lists them  */

#define ISOINFO_ROW(label, fn) ROW_INFO(label, fn),
static const OptRow ISOINFO_ROWS[] = { ROM_INFO_ROWS(ISOINFO_ROW) };
#undef ISOINFO_ROW

static const OptHelp ISOINFO_HELP[] =
{
	{ "Achievements:", "Only for a PS1 disc image. A .exe has" },
	{ NULL,            "no disc to identify, so it gets none." },
};

/* The mode is a cycle: its five values do not fit a radio row (OPT_MAX_CHOICES). A change
 * applies at the next game load (achievements_load), so there is no apply function. The 56 px
 * buttons of the five rows keep a gap at a 58 px pitch starting at y 78 (PAGES); the four help
 * lines still end above the bottom edge. */
static const OptRow ACH_ROWS[] =
{
	ROW_CYCLE("Achievements", NULL),
	ROW_RADIO("AchievementUnofficial", NULL),
	ROW_RADIO("AchievementEncore", NULL),
	ROW_RADIO("AchievementChallenge", NULL),
	ROW_INFO("Set file", infoAchFile),
};

static const OptHelp ACH_HELP[] =
{
	/* Four lines fit under the four rows; the rest is in SETTINGS.md. The accounts are in
	 * Players (a Mii's Edit page). */
	{ "Local:", "RetroAchievements' set, nothing sent." },
	{ "Official:", "The same, sent to an account (Players)." },
	{ "Custom, Test:", "Your own set file. Test keeps nothing." },
	{ NULL,        "Load the game again after a change." },
};

struct OptPage
{
	const char*			title;			// shown at the top; a tabbed page shows its tabs instead
	const char*			tab;			// the page's tab among the Advanced pages, or NULL
	const OptRow*		rows;
	int					nrows;
	const OptHelp*		help;			// explanation under the rows; NULL for none
	int					nhelp;
	int					returnSubmenu;	// the Settings tab B goes back to; RETURN_CURRENT_ISO: Current ISO
	void				(*onEnter)(void);
	void				(*onLeave)(void);
	float				rowDy;			// the row pitch, or 0 for the default (60)
	int					columns;		// 2: cycle rows in two label + button columns (eng::Page::columns)
	float				rowY0;			// the first row's top, or 0 for the default (100)
	int					standard;		// an eng::Density: the page is laid out by the menu standard; 0 = the grid
	const eng::FooterHelp*	footer;		// standard pages: row i's help, shown for the focused row
};

#define COUNT(a)	((int)(sizeof(a) / sizeof((a)[0])))
#define RETURN_CURRENT_ISO	(-1)

static const OptPage PAGES[] =
{
	{ "Advanced Sound", NULL, SOUND_ROWS, COUNT(SOUND_ROWS), NULL,        0,                  OPT_RET_AUDIO,   NULL,         NULL         },
	{ "Advanced Graphics", NULL, ADVGFX_ROWS, COUNT(ADVGFX_ROWS), NULL,  0,                  OPT_RET_VIDEO, NULL,  NULL,         56.0f, 2, 90.0f },
	{ "Advanced", "CPU",  PLUGIN_ROWS,  COUNT(PLUGIN_ROWS),  NULL,        0,                  OPT_RET_GENERAL, pluginsEnter, pluginsLeave, 0.0f, 0, 0.0f, eng::DENSITY_REGULAR, PLUGIN_FOOTER },
	{ "Advanced", "CD",   CD_ROWS,      COUNT(CD_ROWS),      NULL,        0,                  OPT_RET_GENERAL, pluginsEnter, pluginsLeave, 0.0f, 0, 0.0f, eng::DENSITY_REGULAR, CD_FOOTER },
	{ "Memory",   NULL,   MEMORY_ROWS,  COUNT(MEMORY_ROWS),  NULL,        0,                  OPT_RET_GENERAL, NULL,         NULL,         46.0f },
	{ "Advanced", "GPU",  GPU_ROWS,     COUNT(GPU_ROWS),     NULL,        0,                  OPT_RET_GENERAL, pluginsEnter, pluginsLeave, 0.0f, 0, 0.0f, eng::DENSITY_REGULAR, GPU_FOOTER },
	{ "Advanced", "Input", INPUT_ROWS,  COUNT(INPUT_ROWS),   NULL,        0,                  OPT_RET_GENERAL, pluginsEnter, pluginsLeave, 0.0f, 0, 0.0f, eng::DENSITY_REGULAR, INPUT_FOOTER },
	{ "Advanced", "SPU",  SPU_ROWS,     COUNT(SPU_ROWS),     NULL,        0,                  OPT_RET_GENERAL, pluginsEnter, pluginsLeave, 0.0f, 0, 0.0f, eng::DENSITY_TEXT,    SPU_FOOTER },
	{ "Achievements", NULL, ACH_ROWS,   COUNT(ACH_ROWS),     ACH_HELP,    COUNT(ACH_HELP),    OPT_RET_GENERAL, NULL,         NULL,         58.0f, 0, 78.0f },
	{ "ISO Info", NULL,   ISOINFO_ROWS, COUNT(ISOINFO_ROWS), NULL,        0,                  RETURN_CURRENT_ISO,             NULL,         NULL,         0.0f, 0, 0.0f, eng::DENSITY_TEXT,    NULL },   /* F1: readouts on the standard grid; ISOINFO_HELP is the warnings' text later */
};

/* The Advanced pages, in the order their tabs run across the top. A tab switch stays inside
 * the group: the page entered first decides where B returns to, and the core/plugin choice
 * (pluginsEnter/pluginsLeave) is taken on entering the group and applied on leaving it. */
static const int ADV_TABS[] = {
	OPT_PAGE_PLUGINS, OPT_PAGE_ADV_GPU, OPT_PAGE_ADV_SPU,
	OPT_PAGE_STORAGE, OPT_PAGE_ADV_INPUT
};
#define NUM_TABS	((int)(sizeof(ADV_TABS) / sizeof(ADV_TABS[0])))
/* The engine centres the tab row. Its widths are the Settings tab row's (SettingsFrame.h), so
 * moving between the two menus does not shift anything. */

#define NUM_PAGES		COUNT(PAGES)
#define OPT_MAX_ROWS	9
#define OPT_MAX_CHOICES	4
#define OPT_MAX_HELP	8


#define VALUE_LEN	32

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
typedef char opt_input_fits[OPT_PAGE_FITS(INPUT_ROWS) ? 1 : -1];
typedef char opt_memory_fits [OPT_PAGE_FITS(MEMORY_ROWS)  ? 1 : -1];
typedef char opt_ach_fits[OPT_PAGE_FITS(ACH_ROWS) ? 1 : -1];
typedef char opt_ach_help_fits[OPT_HELP_FITS(ACH_HELP) ? 1 : -1];
typedef char opt_isoinfo_fits[OPT_PAGE_FITS(ISOINFO_ROWS) ? 1 : -1];
typedef char opt_isoinfo_help_fits[OPT_HELP_FITS(ISOINFO_HELP) ? 1 : -1];


/* A footer has one entry per row; a mismatch used to show the wrong help (or read past the array). */
static_assert(COUNT(PLUGIN_FOOTER) == COUNT(PLUGIN_ROWS), "PLUGIN_FOOTER: one entry per row");
static_assert(COUNT(SPU_FOOTER) == COUNT(SPU_ROWS), "SPU_FOOTER: one entry per row");
static_assert(COUNT(GPU_FOOTER) == COUNT(GPU_ROWS), "GPU_FOOTER: one entry per row");
static_assert(COUNT(INPUT_FOOTER) == COUNT(INPUT_ROWS), "INPUT_FOOTER: one entry per row");
static_assert(COUNT(CD_FOOTER) == COUNT(CD_ROWS), "CD_FOOTER: one entry per row");
static_assert(COUNT(PAGES) == OPT_PAGE_COUNT, "PAGES: one entry per OptPageId");

#endif
