/* menu_engine_test.cpp - the menu framework's layout and focus, on the host.
 *
 *     tests/menu_engine/run.sh            (builds with clang++ or g++, runs against fonts/En.dat)
 *
 * Every OptionsFrame page is described here (its row and help tables are copied from
 * Gamecube/menu/OptionsFrame.cpp) and laid out by MenuEngine. The expected rectangles,
 * text positions, focus links and help block are re-derived from OptionsFrame's own code --
 * its constructor, activateSubmenu() and the help loops -- and compared with the engine's.
 * The text widths are the real font's: fonts/En.dat, read the way IplFont reads it
 * (1156-byte records, big-endian code, width in byte 3), so a label that is too wide for its
 * button is found here, not on a television.
 *
 * Also checks that checkLayout() catches what it should (synthetic bad pages), and reports
 * how FOCUS_GEOMETRY would link the same pages.
 *
 * Exit status: the number of failed checks, capped at 255.
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Gamecube/menu/MenuEngine.h"

using namespace menu::engine;

static int failures = 0;

static void fail(const char *where, const char *what)
{
	printf("  FAIL  %-12s %s\n", where, what);
	failures++;
}

/*  The font: fonts/En.dat, the way IplFont loads it  */

static unsigned char glyphWidth[256];

static bool loadFont(const char *path)
{
	const size_t REC = 1152 + 4;	/* [u16 code, big-endian][u8][u8 advance][1152-byte tile] */
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long len;
	bool have[256];
	size_t i;

	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
	if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(buf);
		return false;
	}
	fclose(f);

	memset(have, 0, sizeof have);
	for (i = 0; i + REC <= (size_t)len; i += REC) {
		unsigned code = ((unsigned)buf[i] << 8) | buf[i + 1];
		if (code < 256) {
			glyphWidth[code] = buf[i + 3];
			have[code] = true;
		}
	}
	free(buf);
	/* IplFont falls back to the blank character for anything it does not have. */
	for (i = 0; i < 256; i++)
		if (!have[i])
			glyphWidth[i] = have[' '] ? glyphWidth[' '] : 0;
	return have[' '];
}

/* IplFont::getStringWidth: sum of (advance + 1), plus 5, times the scale, in whole pixels. */
static int fontWidth(const char *s, float scale, void *)
{
	int sum = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		sum += glyphWidth[*p] + 1;
	return (int)((sum + 5) * scale);
}

/* IplFont::getStringHeight: CH_FONT_HEIGHT (24) times the scale. */
static int fontHeight(const char *, float scale, void *)
{
	return (int)(24 * scale);
}

static const Metrics kFont = { fontWidth, fontHeight, NULL };

/*  OptionsFrame's tables, copied from Gamecube/menu/OptionsFrame.cpp  */

static const char *const RESAMPLER_NAMES[]    = { "Hold", "Linear", "Cubic" };
static const char *const ON_OFF_NAMES[]       = { "Off", "On" };
static const char *const LEGACY_HIFI_NAMES[]  = { "Legacy", "Hi-Fi" };
static const char *const CD_BUFFER_NAMES[]    = { "16 KB", "64 KB", "256 KB" };
static const char *const CD_HUNKS_NAMES[]     = { "2", "4", "8" };
static const char *const CPU_CORE_NAMES[]     = { "Lightrec", "Interpreter", "Dynarec" };
static const char *const GPU_PLUGIN_NAMES[]   = { "Soft Fast", "Soft Timed", "OpenGX" };
static const char *const FAST_ACCURATE_NAMES[] = { "Fast", "Accurate" };
static const char *const CPU_SPEED_NAMES[]    = { "Auto", "50%", "75%", "100%", "125%", "150%", "200%", "300%" };
static const char *const SPU_OUTPUT_NAMES[]   = { "CPU", "DSP" };
static const char *const FRAME_RATE_NAMES[]   = { "Auto", "TV", "PS1" };
static const char *const DITHER_NAMES[]       = { "None", "Default", "Always" };
static const char *const CHROMA_NAMES[]       = { "Sharp", "Smooth" };
static const char *const FMV_COLOUR_NAMES[]   = { "15-bit", "24-bit" };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions. Every setting's
 * lowest value is 0 in wiiSXconfig.h, so count is the number of names. */
#define CYC(label, names, value)   { ROW_CYCLE,   label, names, 0, (int)(sizeof(names) / sizeof((names)[0])), value, NULL, false, NULL }
#define RADIO(label, names, value) { ROW_CHOICE,  label, names, 0, (int)(sizeof(names) / sizeof((names)[0])), value, NULL, false, NULL }
#define RADIO_FIT(label, names, value) { ROW_CHOICE, label, names, 0, (int)(sizeof(names) / sizeof((names)[0])), value, NULL, true, NULL }
#define INFO(label, text)          { ROW_READOUT, label, NULL, 0, 0, 0, text, false, NULL }

static Row SOUND_ROWS[] = {
	CYC("Output Resampler", RESAMPLER_NAMES,   1),
	CYC("Reverb",           ON_OFF_NAMES,      1),
	CYC("Mixer Precision",  LEGACY_HIFI_NAMES, 0),
	CYC("XA Resampler",     LEGACY_HIFI_NAMES, 1),
};

static Row PLUGIN_ROWS[] = {
	RADIO_FIT("CPU Core", CPU_CORE_NAMES,   0),
	CYC("CPU Timing",   FAST_ACCURATE_NAMES, 0),
	CYC("CPU Speed",    CPU_SPEED_NAMES,    3),
};

static Row CD_ROWS[] = {
	CYC("CD Read Buffer", CD_BUFFER_NAMES, 0),
	CYC("CD Read-Ahead",  ON_OFF_NAMES,    1),
	CYC("CHD Hunk Cache", CD_HUNKS_NAMES,  2),
};

/* Readouts: the text is whatever the emulator reports, so the longest it can be is used. */
static Row MEMORY_ROWS[] = {
	INFO("MEM1 (fast)",        "24576 / 24576 KB"),
	INFO("MEM2 heap",          "65536 / 65536 KB"),
	INFO("Locked cache",       "16 / 16 KB (spu-gauss, tex-tile)"),	/* lc_usage(): the region names */
	INFO("Font glyph cache",   "1024 / 1024 KB"),
	INFO("Recompiler buffer",  "16384 KB (Lightrec, MEM1)"),
	INFO("SPU + BIOS buffers", "64 + 512 KB"),
};

static Row GPU_ROWS[] = {
	RADIO_FIT("GPU Plugin", GPU_PLUGIN_NAMES, 2),
	CYC("GPU Timing",     FAST_ACCURATE_NAMES, 0),
	RADIO("Frame Rate",   FRAME_RATE_NAMES,  0),
};

static Row INPUT_ROWS[] = {
	CYC("Port Timing", FAST_ACCURATE_NAMES, 1),
};

static Row SPU_ROWS[] = {
	INFO("SPU Plugin", "P.E.Op.S. DFSound"),
	RADIO("Output",    SPU_OUTPUT_NAMES, 1),
};

static Row ADVGFX_ROWS[] = {
	RADIO_FIT("Dithering",     DITHER_NAMES, 1),
	CYC("MDEC Chroma",         CHROMA_NAMES, 0),
	CYC("Video Colour",        FMV_COLOUR_NAMES, 1),
};

static const HelpLine SPU_HELP[] = {
	{ "Output:", "CPU resamples and mixes on the Wii's CPU." },
	{ NULL,      "DSP hands the stream to the Wii's sound chip." },
	{ NULL,      "Sound quality is in Audio, Advanced." },
};

static const HelpLine GPU_HELP[] = {
	{ "GPU Plugin:", "OpenGX uses the Wii's GPU; Soft avoids its quirks." },
	{ "GPU Timing:", "Accurate: the GPU is busy as long as on a PS1." },
	{ "Frame Rate:", "TV: 59.94 Hz, one frame per TV refresh: smoothest." },
	{ NULL,          "PS1: 59.82 Hz, a PlayStation's own speed." },
};

static const HelpLine INPUT_HELP[] = {
	{ "Port Timing:", "Accurate: each byte on the controller ports takes" },
	{ NULL,           "as long as on a PS1. Fast is the default." },
};

static const HelpLine PLUGIN_HELP[] = {
	{ "CPU Core:",   "Lightrec runs most games best; Interpreter is" },
	{ NULL,          "exact but slow. A new core restarts the game." },
	{ "CPU Timing:", "Accurate: 3D-maths and multiply waits as on a PS1." },
	{ "CPU Speed:",  "Over 100% eases slowdown in some games, at more" },
	{ NULL,          "Wii CPU time. Save it for one game: This Game." },
};

static const HelpLine ADVGFX_HELP[] = {
	{ "Dithering:",    "Hides the steps between colours in a 15-bit picture." },
	{ NULL,            "Default uses it only where the game asks for it." },
	{ "MDEC Chroma:",  "Sharp is the console: one colour sample per 2x2 pixels." },
	{ NULL,            "Smooth mixes the samples: it looks better, costs more." },
	{ NULL,            "It applies to the next video that starts." },
	{ "Video Colour:", "24-bit is full colour, as on the console. 15-bit is" },
	{ NULL,            "the old way: it shows bands in dark scenes." },
};

static const HelpLine CD_HELP[] = {
	{ "CD Read Buffer:", "16 KB is best: the card is read in 32 KB pages anyway," },
	{ NULL,              "and a larger buffer only reads more than the game uses." },
	{ "CD Read-Ahead:",  "A thread reads the next sectors early, so the game" },
	{ NULL,              "waits less for the card. Raw bin/cue and .iso only." },
	{ "CHD Hunk Cache:", "Decoded CHD hunks kept in memory, about 20 KB each." },
	{ NULL,              "More never decodes more often: 8 is best." },
	{ NULL,              "All three apply at the next game load." },
};

/* The Advanced tab row: ADV_TABS order, and the same widths as TAB_WS. */
static const Tab OPT_TABS[] = {
	{ "CPU", 110.0f }, { "GPU", 100.0f }, { "SPU", 100.0f }, { "CD", 100.0f }, { "Input", 100.0f },
};
/* The tab row's left edges as OptionsFrame writes them (TAB_X). */
static const float TAB_X_LITERAL[5] = { 25.0f, 155.0f, 275.0f, 395.0f, 515.0f };

/* One OptionsFrame page: PAGES[] in OptionsFrame.cpp. curTab indexes OPT_TABS, unless the page
 * has its own tabs (tabs != NULL), as the Settings General tab does. labelCx and labelDy are
 * the Settings look (0: the engine's own, the OptionsFrame look). */
struct TestPage {
	const char      *name;
	const char      *title;
	bool             tabbed;
	int              curTab;
	Row             *rows;
	int              nRows;
	const HelpLine  *help;
	int              nHelp;
	const Tab       *tabs;
	int              nTabs;
	float            labelCx;
	float            labelDy;
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static TestPage PAGES[] = {
	{ "Adv Sound",   "Advanced Sound",    false, 0, SOUND_ROWS,   COUNT(SOUND_ROWS),   NULL,        0 },
	{ "Adv Graphics","Advanced Graphics", false, 0, ADVGFX_ROWS, COUNT(ADVGFX_ROWS), ADVGFX_HELP, COUNT(ADVGFX_HELP) },
	{ "CPU",         NULL,                true,  0, PLUGIN_ROWS, COUNT(PLUGIN_ROWS), PLUGIN_HELP, COUNT(PLUGIN_HELP) },
	{ "CD",          NULL,                true,  3, CD_ROWS,     COUNT(CD_ROWS),     CD_HELP,     COUNT(CD_HELP) },
	{ "Memory",      "Memory",            false, 0, MEMORY_ROWS, COUNT(MEMORY_ROWS), NULL,        0 },
	{ "GPU",         NULL,                true,  1, GPU_ROWS,    COUNT(GPU_ROWS),    GPU_HELP,    COUNT(GPU_HELP) },
	{ "Input",       NULL,                true,  4, INPUT_ROWS,  COUNT(INPUT_ROWS),  INPUT_HELP,  COUNT(INPUT_HELP) },
	{ "SPU",         NULL,                true,  2, SPU_ROWS,    COUNT(SPU_ROWS),    SPU_HELP,    COUNT(SPU_HELP),
	  NULL, 0, 0.0f, 0.0f },
};
#define NPAGES COUNT(PAGES)

/*  The Settings General tab (menupage 1), from SettingsFrame.cpp's FRAME_BUTTONS and
 *  FRAME_TEXTBOXES, and its activateSubmenu() case for SUBMENU_GENERAL.
 *
 *  The rows are the six lines of the tab, top to bottom. Each button's box is the number the
 *  table gives it: 73/75 at y 100, the BIOS four at 160, Yes/No and Execute BIOS at 220, Select
 *  Language at 280, Fast Load at 340, the three Save buttons at 400. Labels sit at the centre
 *  of their row (y + 28) and their centre is x 150 (Settings' TextBox, centred). */
static const char *const GEN_BIOS_NAMES[]  = { "HLE", "SD", "USB", "DVD" };
static const char *const GEN_BOOT_NAMES[]  = { "Yes", "No", "Execute BIOS" };
static const char *const GEN_FAST_NAMES[]  = { "Yes", "No" };
static const float        GEN_BIOS_W[]     = { 70.0f, 55.0f, 70.0f, 70.0f };
static const float        GEN_BOOT_W[]     = { 75.0f, 75.0f, 171.0f };
static const float        GEN_FAST_W[]     = { 75.0f, 75.0f };
static const ActionButton GEN_ADV_ACTIONS[]  = { { "Advanced", 130.0f, false }, { "Memory", 130.0f, false } };
static const ActionButton GEN_LANG_ACTIONS[] = { { "En", 90.0f, false } };
static const ActionButton GEN_SAVE_ACTIONS[] = { { "SD", 55.0f, false }, { "USB", 70.0f, false }, { "This Game", 140.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row GENERAL_ROWS[] = {
	{ ROW_BUTTONS, NULL,                 NULL,           0, 2, 0, NULL, false, GEN_ADV_ACTIONS,  0.0f,   30.0f, NULL,       0 },
	{ ROW_CHOICE,  "Select BIOS",        GEN_BIOS_NAMES, 0, 4, 0, NULL, false, NULL,             295.0f,  0.0f, GEN_BIOS_W, 0 },
	{ ROW_CHOICE,  "Boot Through BIOS",  GEN_BOOT_NAMES, 0, 3, 0, NULL, false, NULL,             295.0f,  0.0f, GEN_BOOT_W, 1 },
	{ ROW_BUTTONS, "Select Language",    NULL,           0, 1, 0, NULL, false, GEN_LANG_ACTIONS, 295.0f,  0.0f, NULL,       0 },
	{ ROW_CHOICE,  "Fast Load",          GEN_FAST_NAMES, 0, 2, 0, NULL, false, NULL,             295.0f,  0.0f, GEN_FAST_W, 0 },
	{ ROW_BUTTONS, "Save Settings",      NULL,           0, 3, 0, NULL, false, GEN_SAVE_ACTIONS, 253.0f,  0.0f, NULL,       0 },
};

static const Tab GEN_TABS[] = {
	{ "General", 110.0f }, { "Video", 100.0f }, { "Input", 100.0f }, { "Audio", 100.0f }, { "Saves", 100.0f },
};

/*  The Settings Video tab (menupage 2), from SettingsFrame.cpp's FRAME_BUTTONS, FRAME_TEXTBOXES and
 *  layoutVideo(). The lit buttons are one set state: Show FPS On, FPS Limit Auto with Force NTSC on,
 *  Frame Skip Off, Interlaced off with 240p on, Trap off with Deflicker on. The button texts are the
 *  state's (Screen Mode 4:3, Bilinear Default). The rows, labels and widths are the table's. */
static const char *const VID_ON_OFF_NAMES[] = { "On", "Off" };
static const float        VID_ON_OFF_W[]    = { 75.0f, 75.0f };
static const ActionButton VID_LIMIT_ACTIONS[]  = { { "Auto", 75.0f, true }, { "Off", 75.0f, false }, { "Force NTSC", 160.0f, true } };
static const ActionButton VID_SCREEN_ACTIONS[] = { { "4:3", 130.0f, false }, { "Interlaced", 125.0f, false }, { "240p", 66.0f, true } };
static const ActionButton VID_FILTER_ACTIONS[] = { { "Default", 95.0f, false }, { "Trap", 75.0f, false }, { "Deflicker", 110.0f, true } };
static const ActionButton VID_ADV_ACTIONS[]    = { { "Advanced", 150.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row VIDEO_ROWS[] = {
	{ ROW_CHOICE,  "Show FPS",    VID_ON_OFF_NAMES, 0, 2, 0, NULL, false, NULL,               295.0f, 0.0f, VID_ON_OFF_W, 0 },
	{ ROW_BUTTONS, "Limit FPS",   NULL,             0, 3, 0, NULL, false, VID_LIMIT_ACTIONS,  295.0f, 0.0f, NULL,         0 },
	{ ROW_CHOICE,  "Frame Skip",  VID_ON_OFF_NAMES, 0, 2, 1, NULL, false, NULL,               295.0f, 0.0f, VID_ON_OFF_W, 0 },
	{ ROW_BUTTONS, "Screen Mode", NULL,             0, 3, 0, NULL, false, VID_SCREEN_ACTIONS, 295.0f, 0.0f, NULL,         0 },
	{ ROW_BUTTONS, "Filters",     NULL,             0, 3, 0, NULL, false, VID_FILTER_ACTIONS, 238.0f, 0.0f, NULL,         0 },
	{ ROW_BUTTONS, NULL,          NULL,             0, 1, 0, NULL, false, VID_ADV_ACTIONS,    245.0f, 0.0f, NULL,         0 },
};

/*  The Settings Input tab (menupage 3), from SettingsFrame.cpp's FRAME_BUTTONS, FRAME_TEXTBOXES and
 *  layoutInput(). The state is the default one: Standard pad, rumble on, Light Gun off, Auto Load
 *  Default. The rows and widths are the table's; the labels are textboxes 10, 29, 11, 12 and 13. */
static const char *const INP_PSX_NAMES[]    = { "Standard", "Analog", "Stick D-pad" };
static const float        INP_PSX_W[]       = { 105.0f, 100.0f, 140.0f };
static const char *const INP_RUMBLE_NAMES[] = { "Yes", "No" };
static const float        INP_RUMBLE_W[]    = { 75.0f, 75.0f };
static const ActionButton INP_CONFIG_ACTIONS[]   = { { "Configure Input", 220.0f, false }, { "Configure Buttons", 235.0f, false } };
static const ActionButton INP_LIGHTGUN_ACTIONS[] = { { "Off", 135.0f, false } };
static const ActionButton INP_SAVEBTN_ACTIONS[]  = { { "SD", 55.0f, false }, { "USB", 70.0f, false } };
static const ActionButton INP_AUTOLOAD_ACTIONS[] = { { "Default", 135.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row SET_INPUT_ROWS[] = {
	{ ROW_BUTTONS, NULL,                  NULL,            0, 2, 0, NULL, false, INP_CONFIG_ACTIONS,  85.0f,  15.0f, NULL,       0 },
	{ ROW_CHOICE,  "PSX Controller Type", INP_PSX_NAMES,   0, 3, 0, NULL, false, NULL,                271.0f,  0.0f, INP_PSX_W,  0 },
	{ ROW_BUTTONS, "Light Gun",           NULL,            0, 1, 0, NULL, false, INP_LIGHTGUN_ACTIONS, 295.0f,  0.0f, NULL,       0 },
	{ ROW_CHOICE,  "Enable Rumble",       INP_RUMBLE_NAMES, 0, 2, 0, NULL, false, NULL,                295.0f,  0.0f, INP_RUMBLE_W, 0 },
	{ ROW_BUTTONS, "Save Button Configs", NULL,            0, 2, 0, NULL, false, INP_SAVEBTN_ACTIONS,  295.0f,  0.0f, NULL,       0 },
	{ ROW_BUTTONS, "Auto Load Slot",      NULL,            0, 1, 0, NULL, false, INP_AUTOLOAD_ACTIONS, 295.0f,  0.0f, NULL,       0 },
};

/* The General tab is the first Settings tab, so its tab row is lit at 0; Video's is lit at 1. */
static TestPage SETTINGS_PAGES[] = {
	{ "General",     NULL,                true,  0, GENERAL_ROWS, COUNT(GENERAL_ROWS), NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
	{ "Video",       NULL,                true,  1, VIDEO_ROWS,   COUNT(VIDEO_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
	{ "Input",       NULL,                true,  2, SET_INPUT_ROWS,   COUNT(SET_INPUT_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
};
#define NSETTINGS COUNT(SETTINGS_PAGES)

/* The tab row of a page: its own, or the Advanced row. */
static const Tab *tabsOf(const TestPage &t)  { return t.tabs ? t.tabs : OPT_TABS; }
static int        nTabsOf(const TestPage &t) { return t.tabs ? t.nTabs : (int)COUNT(OPT_TABS); }

static Page toPage(const TestPage &t, FocusMode mode)
{
	Page p;
	p.title = t.title;
	p.tabs = t.tabbed ? tabsOf(t) : NULL;
	p.nTabs = t.tabbed ? nTabsOf(t) : 0;
	p.curTab = t.curTab;
	p.rows = t.rows;
	p.nRows = t.nRows;
	p.help = t.help;
	p.nHelp = t.nHelp;
	p.logo = LOGO_SPOT_BOTTOM;	/* LOGO_BOTTOM_FRAMES has FRAME_OPTIONS and FRAME_SETTINGS */
	p.focus = mode;
	p.labelCx = t.labelCx;
	p.labelDy = t.labelDy;
	return p;
}

/*  The expected layout, re-derived from OptionsFrame  */

struct Key {
	int role, row, slot;
};
static const Key NOKEY = { -1, 0, 0 };

static bool sameKey(Key a, Key b)
{
	return a.role == b.role && a.row == b.row && a.slot == b.slot;
}

static Key keyOf(const Control &c)
{
	Key k = { c.role, c.row, c.slot };
	return k;
}

struct Exp {
	Key       key;
	bool      text;
	float     x, y, w, h;		/* a button's box; a text's drawn box, in whole pixels */
	float     ax, ay;			/* a text's anchor */
	TextAlign align;
	const char *str;
	bool      focusable, selected;
	Key       link[DIR_COUNT];
};

/* OptionsFrame draws text through TextBox, which cuts the anchor to whole pixels first. */
static void drawnBox(const char *s, float scale, TextAlign align, float ax, float ay,
                     float *x, float *y, float *w, bool bold)
{
	int tw = fontWidth(s, scale, NULL), th = fontHeight(s, scale, NULL);
	int ix = (int)ax, iy = (int)ay;
	int left = ix;
	if (align == ALIGN_RIGHT_TOP) {
		left = ix - tw;
		*x = (float)left;
		*y = (float)iy;
	} else if (align == ALIGN_CENTRE) {
		*x = (float)(left - tw / 2);
		*y = (float)(iy - th / 2);
	} else {
		*x = (float)left;
		*y = (float)iy;
	}
	*w = (float)(tw + (bold ? 1 : 0));
}

static int addExpText(Exp *e, int n, CtrlRole role, int row, int slot, float ax, float ay,
                      TextAlign align, float scale, const char *s)
{
	Exp &x = e[n];
	memset(&x, 0, sizeof x);
	x.key.role = role;
	x.key.row = row;
	x.key.slot = slot;
	x.text = true;
	x.ax = ax;
	x.ay = ay;
	x.align = align;
	x.str = s;
	drawnBox(s, scale, align, ax, ay, &x.x, &x.y, &x.w, role == ROLE_HELP_TERM);
	x.h = (float)fontHeight(s, scale, NULL);
	for (int d = 0; d < DIR_COUNT; d++)
		x.link[d] = NOKEY;
	return n + 1;
}

static int addExpButton(Exp *e, int n, CtrlRole role, int row, int slot,
                        float x, float y, float w, float h, const char *s, bool selected)
{
	Exp &b = e[n];
	memset(&b, 0, sizeof b);
	b.key.role = role;
	b.key.row = row;
	b.key.slot = slot;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.str = s;
	b.focusable = true;
	b.selected = selected;
	for (int d = 0; d < DIR_COUNT; d++)
		b.link[d] = NOKEY;
	return n + 1;
}

/* A choice's button width when its row fits its words: Button::fitToLabel, 2 * ceil(...). */
static float fitWidth(const char *s)
{
	float t = (float)fontWidth(s, 1.0f, NULL);
	return 2.0f * ceilf((t + 16.0f + 16.0f) / 2.0f);
}

static int rowChoices(const Row &r)
{
	return r.count;
}

static Key tabKey(int t)
{
	Key k = { ROLE_TAB, -1, t };
	return k;
}

/* OptionsFrame's focusTarget(row, col): a choice row clamps the column to its last button. A
 * row of action buttons (Settings' General tab) is the same, with the buttons' role. */
static Key focusTarget(const TestPage &t, int row, int col)
{
	const Row &r = t.rows[row];
	Key k;
	if (r.kind == ROW_CHOICE || r.kind == ROW_BUTTONS) {
		k.role = r.kind == ROW_CHOICE ? ROLE_CHOICE : ROLE_BUTTON;
		k.row = row;
		k.slot = col >= rowChoices(r) ? rowChoices(r) - 1 : col;
	} else {
		k.role = ROLE_BUTTON;
		k.row = row;
		k.slot = 0;
	}
	return k;
}

static bool rowIsFocusable(const Row &r)
{
	return r.kind == ROW_CYCLE || r.kind == ROW_CHOICE || r.kind == ROW_BUTTONS;
}

static int choicesOf(const TestPage &t, int row)
{
	const Row &r = t.rows[row];
	return (r.kind == ROW_CHOICE || r.kind == ROW_BUTTONS) ? rowChoices(r) : 1;
}

static void setLink(Exp *e, int n, Key from, int dir, Key to)
{
	for (int i = 0; i < n; i++)
		if (sameKey(e[i].key, from)) {
			e[i].link[dir] = to;
			return;
		}
	fail("expect", "a link from a control the test did not describe");
}

/* Fills in the focus links OptionsFrame::activateSubmenu sets, loop for loop. */
static void expectLinks(const TestPage &t, Exp *e, int n)
{
	int firstRow = -1, prevRow = -1;

	for (int i = 0; i < t.nRows; i++) {
		const int nc = rowChoices(t.rows[i]);
		if ((t.rows[i].kind != ROW_CHOICE && t.rows[i].kind != ROW_BUTTONS) || nc < 2)
			continue;		/* a row of one button has no Left or Right */
		for (int c = 0; c < nc; c++) {
			setLink(e, n, focusTarget(t, i, c), DIR_LEFT,  focusTarget(t, i, (c + nc - 1) % nc));
			setLink(e, n, focusTarget(t, i, c), DIR_RIGHT, focusTarget(t, i, (c + 1) % nc));
		}
	}
	if (t.tabbed)
		for (int tb = 0; tb < nTabsOf(t); tb++) {
			int nt = nTabsOf(t);
			setLink(e, n, tabKey(tb), DIR_LEFT,  tabKey((tb + nt - 1) % nt));
			setLink(e, n, tabKey(tb), DIR_RIGHT, tabKey((tb + 1) % nt));
		}

	for (int i = 0; i < t.nRows; i++) {
		if (!rowIsFocusable(t.rows[i]))
			continue;
		if (firstRow < 0)
			firstRow = i;
		if (prevRow >= 0) {
			int ncPrev = choicesOf(t, prevRow), ncThis = choicesOf(t, i);
			for (int c = 0; c < ncPrev; c++)
				setLink(e, n, focusTarget(t, prevRow, c), DIR_DOWN, focusTarget(t, i, c));
			for (int c = 0; c < ncThis; c++)
				setLink(e, n, focusTarget(t, i, c), DIR_UP, focusTarget(t, prevRow, c));
		}
		prevRow = i;
	}
	if (firstRow >= 0) {
		int ncLast = choicesOf(t, prevRow), ncFirst = choicesOf(t, firstRow);
		Key tb = tabKey(t.curTab);
		for (int c = 0; c < ncLast; c++)
			setLink(e, n, focusTarget(t, prevRow, c), DIR_DOWN, t.tabbed ? tb : focusTarget(t, firstRow, c));
		for (int c = 0; c < ncFirst; c++)
			setLink(e, n, focusTarget(t, firstRow, c), DIR_UP, t.tabbed ? tb : focusTarget(t, prevRow, c));
		if (t.tabbed)
			for (int k = 0; k < nTabsOf(t); k++) {
				setLink(e, n, tabKey(k), DIR_DOWN, focusTarget(t, firstRow, 0));
				setLink(e, n, tabKey(k), DIR_UP,   focusTarget(t, prevRow, 0));
			}
	}
}

/* MenuEngine's choice rows, from OptionsFrame's numbers: a row starts at 205, or further left
 * when its buttons would end past the right edge its bottom allows (MENU_ROW_RIGHT), and no
 * further. Buttons are 12 apart. */
static float choiceWidthOf(const Row &r, int c)
{
	if (r.widths)
		return r.widths[c];
	return r.fit ? fitWidth(r.names[c]) : 135.0f;
}

/* The space between two buttons of a choice row: 12, or the row's own gap when it has widths. */
static float choiceGapOf(const Row &r)
{
	if (r.widths)
		return r.gap > 0 ? r.gap : 10.0f;
	return 12.0f;
}

/* Where a choice row starts: the row's own x0, or 205 moved left as the engine does. */
static float choiceStartOf(const Row &r, float y)
{
	const float bottom = y + 56.0f;
	const float right = bottom > 392.0f ? 538.0f : 636.0f;
	const int n = rowChoices(r);
	float total = 0;

	if (r.x0 > 0)
		return r.x0;
	for (int c = 0; c < n; c++) {
		total += choiceWidthOf(r, c);
		if (c + 1 < n)
			total += choiceGapOf(r);
	}
	return 205.0f + total > right ? right - total : 205.0f;
}

/* A page's choice labels share one centre: 110, moved left only when a row's buttons were moved
 * left, so the label still clears them by 10. A page with its own labelCx has that centre. */
static float rlabelCxOf(const TestPage &t)
{
	float cx = 110.0f;

	if (t.labelCx > 0)
		return t.labelCx;
	for (int i = 0; i < t.nRows; i++) {
		const Row &r = t.rows[i];
		if (r.kind != ROW_CHOICE || !r.label)
			continue;
		{
			const int lw = fontWidth(r.label, 1.0f, NULL);
			const float c = choiceStartOf(r, 100.0f + i * 60.0f) - 10.0f - (float)(lw - lw / 2);
			if (c < cx)
				cx = c;
		}
	}
	return cx;
}

/* A readout's text is centred on 395, or starts 10 past its label's right edge when centred
 * there would run into the label. */
static float readoutCxOf(const Row &r)
{
	const int lw = fontWidth(r.label, 1.0f, NULL);
	const int vw = fontWidth(r.text, 1.0f, NULL);
	const int labelRight = 160 - lw / 2 + lw;
	float cx = 395.0f;

	if ((int)cx - vw / 2 < labelRight + 10)
		cx = (float)(labelRight + 10 + vw / 2);
	return cx;
}

/* Returns the number of expected controls. Also sets *defaultFocus. */
static int buildExpected(const TestPage &t, Exp *e, Key *defaultFocus)
{
	int n = 0;
	int firstRow = -1;

	if (t.tabbed) {
		for (int k = 0; k < nTabsOf(t); k++) {
			n = addExpButton(e, n, ROLE_TAB, -1, k, TAB_X_LITERAL[k], 20.0f, tabsOf(t)[k].width, 56.0f,
			                 tabsOf(t)[k].text, k == t.curTab);
		}
	} else if (t.title) {
		n = addExpText(e, n, ROLE_TITLE, -1, 0, 320.0f, 44.0f, ALIGN_CENTRE, 1.0f, t.title);
	}

	for (int i = 0; i < t.nRows; i++) {
		const Row &r = t.rows[i];
		const float y = 100.0f + i * 60.0f;
		const float lcx = t.labelCx > 0 ? t.labelCx : 160.0f;	/* a row's label */
		const float ldy = t.labelDy > 0 ? t.labelDy : 20.0f;
		if (firstRow < 0 && rowIsFocusable(r))
			firstRow = i;

		if (r.kind == ROW_CYCLE) {
			n = addExpText(e, n, ROLE_LABEL, i, 0, lcx, y + ldy, ALIGN_CENTRE, 1.0f, r.label);
			n = addExpButton(e, n, ROLE_BUTTON, i, 0, 295.0f, y, 200.0f, 56.0f,
			                 r.names[r.value - r.lo], false);
		} else if (r.kind == ROW_READOUT) {
			n = addExpText(e, n, ROLE_LABEL, i, 0, lcx, y + ldy, ALIGN_CENTRE, 1.0f, r.label);
			n = addExpText(e, n, ROLE_VALUE, i, 0, readoutCxOf(r), y + ldy, ALIGN_CENTRE, 1.0f, r.text);
		} else if (r.kind == ROW_CHOICE) {
			float cx = choiceStartOf(r, y);
			n = addExpText(e, n, ROLE_RLABEL, i, 0, rlabelCxOf(t), y + ldy, ALIGN_CENTRE, 1.0f, r.label);
			for (int c = 0; c < rowChoices(r); c++) {
				float w = choiceWidthOf(r, c);
				/* the last `plain` buttons are actions, never lit */
				n = addExpButton(e, n, ROLE_CHOICE, i, c, cx, y, w, 56.0f, r.names[c],
				                 c < rowChoices(r) - r.plain && c == r.value - r.lo);
				cx += w + choiceGapOf(r);
			}
		} else if (r.kind == ROW_BUTTONS) {
			/* Centred on the screen, or from x0; the gap is the row's own or 10; the label, if any,
			 * left of the buttons. */
			const float gap = r.gap > 0 ? r.gap : 10.0f;
			float total = gap * (r.count - 1);
			float x;
			for (int k = 0; k < r.count; k++)
				total += r.actions[k].width;
			x = r.x0 > 0 ? r.x0 : (640.0f - total) / 2;
			if (r.label)
				n = addExpText(e, n, ROLE_LABEL, i, 0, lcx, y + ldy, ALIGN_CENTRE, 1.0f, r.label);
			for (int k = 0; k < r.count; k++) {
				n = addExpButton(e, n, ROLE_BUTTON, i, k, x, y, r.actions[k].width, 56.0f, r.actions[k].text, r.actions[k].lit);
				x += r.actions[k].width + gap;
			}
		}
	}

	/* Help: the block is centred as a whole, then moved left for the logo's corner. */
	if (t.nHelp > 0) {
		int wTerm = 0, wText = 0, shift = 0;
		double termR;
		float textX, y;
		const float HELP_SCALE = 0.62f;

		for (int h = 0; h < t.nHelp; h++) {
			if (t.help[h].term) {
				int w = fontWidth(t.help[h].term, HELP_SCALE, NULL);
				if (w > wTerm) wTerm = w;
			}
			{
				int w = fontWidth(t.help[h].text, HELP_SCALE, NULL);
				if (w > wText) wText = w;
			}
		}
		termR = (640.0 - (wTerm + 10.0 + wText)) / 2 + wTerm;
		textX = (float)(termR + 10.0);
		y = 100.0f + t.nRows * 60.0f;
		for (int h = 0; h < t.nHelp; h++) {
			int over;
			float bottom;
			if (h > 0 && t.help[h].term) y += 9.0f;
			bottom = y + 24.0f * HELP_SCALE;
			/* MENU_ROW_RIGHT: a line whose lowest pixel is below the logo's top stops at 538. */
			{
				double right = bottom > 392.0f ? 538.0 : 636.0;
				over = (int)(textX + fontWidth(t.help[h].text, HELP_SCALE, NULL) - right);
			}
			if (over > shift) shift = over;
			y += 19.0f;
		}
		if (termR - wTerm - shift < 4.0)
			shift = (int)(termR - wTerm - 4.0);
		termR -= shift;
		textX -= shift;

		y = 100.0f + t.nRows * 60.0f;
		for (int h = 0; h < t.nHelp; h++) {
			if (h > 0 && t.help[h].term) y += 9.0f;
			if (t.help[h].term)
				n = addExpText(e, n, ROLE_HELP_TERM, -1, h, (float)termR, y, ALIGN_RIGHT_TOP, HELP_SCALE, t.help[h].term);
			n = addExpText(e, n, ROLE_HELP_TEXT, -1, h, textX, y, ALIGN_LEFT_TOP, HELP_SCALE, t.help[h].text);
			y += 19.0f;
		}
	}

	if (firstRow >= 0)
		*defaultFocus = focusTarget(t, firstRow, 0);
	else if (t.tabbed)
		*defaultFocus = tabKey(t.curTab);
	else
		*defaultFocus = NOKEY;

	return n;	/* the focus links are added by expectLinks() */
}

static int findCtrl(const Layout &lay, Key k)
{
	for (int i = 0; i < lay.n; i++)
		if (sameKey(keyOf(lay.c[i]), k))
			return i;
	return -1;
}

static const char *keyName(Key k, char *buf, size_t len)
{
	if (k.role < 0) { snprintf(buf, len, "none"); return buf; }
	snprintf(buf, len, "role%d row%d slot%d", k.role, k.row, k.slot);
	return buf;
}

/*  Checks  */

static bool nearly(float a, float b)
{
	return fabs(a - b) < 1e-3f;
}

/* Every control the test expects is in the layout, where it should be, and nothing else is. */
static bool compareLayout(const char *name, const Layout &lay, const Exp *e, int n)
{
	bool ok = true;
	char b1[64];

	if (lay.n != n) {
		char msg[96];
		snprintf(msg, sizeof msg, "%d controls, expected %d", lay.n, n);
		fail(name, msg);
		ok = false;
	}
	for (int i = 0; i < n; i++) {
		int at = findCtrl(lay, e[i].key);
		if (at < 0) {
			char msg[128];
			snprintf(msg, sizeof msg, "missing %s", keyName(e[i].key, b1, sizeof b1));
			fail(name, msg);
			ok = false;
			continue;
		}
		{
			const Control &c = lay.c[at];
			const Rect &r = c.rect;
			char msg[256];
			if (!nearly(r.x, e[i].x) || !nearly(r.y, e[i].y) || !nearly(r.w, e[i].w) || !nearly(r.h, e[i].h)) {
				snprintf(msg, sizeof msg, "%s at (%g,%g %gx%g), expected (%g,%g %gx%g) '%s'",
				         keyName(e[i].key, b1, sizeof b1), r.x, r.y, r.w, r.h, e[i].x, e[i].y, e[i].w, e[i].h,
				         e[i].str ? e[i].str : "");
				fail(name, msg);
				ok = false;
			}
			if (e[i].text) {
				if (c.kind != CTRL_TEXT || !nearly(c.ax, e[i].ax) || !nearly(c.ay, e[i].ay) || c.align != e[i].align) {
					snprintf(msg, sizeof msg, "%s anchor (%g,%g) not (%g,%g) or wrong alignment",
					         keyName(e[i].key, b1, sizeof b1), c.ax, c.ay, e[i].ax, e[i].ay);
					fail(name, msg);
					ok = false;
				}
			}
			if (e[i].str && strcmp(c.text ? c.text : "", e[i].str) != 0) {
				snprintf(msg, sizeof msg, "%s shows '%s', expected '%s'",
				         keyName(e[i].key, b1, sizeof b1), c.text ? c.text : "", e[i].str);
				fail(name, msg);
				ok = false;
			}
			if (e[i].focusable && (c.selected != e[i].selected)) {
				snprintf(msg, sizeof msg, "%s lit=%d, expected %d", keyName(e[i].key, b1, sizeof b1),
				         (int)c.selected, (int)e[i].selected);
				fail(name, msg);
				ok = false;
			}
		}
	}
	return ok;
}

static bool compareLinks(const char *name, const Layout &lay, const Exp *e, int n, Key defaultFocus)
{
	bool ok = true;
	char b1[64], b2[64];
	static const char *dirName[DIR_COUNT] = { "Up", "Down", "Left", "Right" };

	for (int i = 0; i < n; i++) {
		if (!e[i].focusable)
			continue;
		int at = findCtrl(lay, e[i].key);
		if (at < 0)
			continue;
		for (int d = 0; d < DIR_COUNT; d++) {
			Key want = e[i].link[d];
			int got = lay.c[at].link[d];
			if (want.role < 0) {
				if (got >= 0) {
					char msg[160];
					snprintf(msg, sizeof msg, "%s %s links to %s, expected nothing",
					         keyName(e[i].key, b1, sizeof b1), dirName[d],
					         keyName(keyOf(lay.c[got]), b2, sizeof b2));
					fail(name, msg);
					ok = false;
				}
				continue;
			}
			if (got < 0 || !sameKey(keyOf(lay.c[got]), want)) {
				char msg[200];
				snprintf(msg, sizeof msg, "%s %s links to %s, expected %s",
				         keyName(e[i].key, b1, sizeof b1), dirName[d],
				         got < 0 ? "nothing" : keyName(keyOf(lay.c[got]), b2, sizeof b2),
				         keyName(want, b2, sizeof b2));
				fail(name, msg);
				ok = false;
			}
		}
	}
	if (!sameKey(lay.defaultFocus >= 0 ? keyOf(lay.c[lay.defaultFocus]) : NOKEY, defaultFocus)) {
		char msg[128];
		snprintf(msg, sizeof msg, "default focus is %s, expected %s",
		         lay.defaultFocus >= 0 ? keyName(keyOf(lay.c[lay.defaultFocus]), b1, sizeof b1) : "none",
		         keyName(defaultFocus, b2, sizeof b2));
		fail(name, msg);
		ok = false;
	}
	return ok;
}

/* Findings the OptionsFrame pages have, on the real font. They are printed and do not fail
 * the test: fixing one changes a page's picture, and the plan keeps pictures until someone has
 * looked at the change on a Wii (Docs/MENU_FRAMEWORK_PLAN.md, "Decided").
 * Matched by page, problem, and the text of the control (and of the second one, if any).
 * Any finding not listed here is a failure. Every Advanced page lays out with zero problems.
 * The General and Video tabs' findings are the labels the engine's fit rule (16 px a side, plus the font's
 * own +5) calls wider than their Settings buttons: the Settings buttons are narrower than that
 * rule, and the labels still sit inside them with a few pixels to spare (Execute BIOS, 171 px, has
 * a 161 px label). Listed, not changed (Docs/MENU_FRAMEWORK_PLAN.md, "Decided"). The sentinel
 * (page NULL) matches nothing; it stays, since a zero-length array is not C. */
struct Known {
	const char *page;
	int         kind;
	const char *a;
	const char *b;		/* NULL: a finding on one control */
};

static const Known KNOWN[] = {
	{ "General", PROB_TEXT_WIDE, "General", NULL },
	{ "General", PROB_TEXT_WIDE, "Video", NULL },
	{ "General", PROB_TEXT_WIDE, "Audio", NULL },
	{ "General", PROB_TEXT_WIDE, "Saves", NULL },
	{ "General", PROB_TEXT_WIDE, "Advanced", NULL },
	{ "General", PROB_TEXT_WIDE, "Memory", NULL },
	{ "General", PROB_TEXT_WIDE, "HLE", NULL },
	{ "General", PROB_TEXT_WIDE, "SD", NULL },
	{ "General", PROB_TEXT_WIDE, "USB", NULL },
	{ "General", PROB_TEXT_WIDE, "DVD", NULL },
	{ "General", PROB_TEXT_WIDE, "Yes", NULL },
	{ "General", PROB_TEXT_WIDE, "Execute BIOS", NULL },
	{ "General", PROB_TEXT_WIDE, "This Game", NULL },
	{ "Video", PROB_TEXT_WIDE, "General", NULL },
	{ "Video", PROB_TEXT_WIDE, "Video", NULL },
	{ "Video", PROB_TEXT_WIDE, "Audio", NULL },
	{ "Video", PROB_TEXT_WIDE, "Saves", NULL },
	{ "Video", PROB_TEXT_WIDE, "Off", NULL },
	{ "Video", PROB_TEXT_WIDE, "Auto", NULL },
	{ "Video", PROB_TEXT_WIDE, "Force NTSC", NULL },
	{ "Video", PROB_TEXT_WIDE, "Interlaced", NULL },
	{ "Video", PROB_TEXT_WIDE, "240p", NULL },
	{ "Video", PROB_TEXT_WIDE, "Default", NULL },
	{ "Video", PROB_TEXT_WIDE, "Trap", NULL },
	{ "Video", PROB_TEXT_WIDE, "Deflicker", NULL },
	/* The Input tab: the same fit-rule findings on the Settings buttons (the tab row's labels too), and
	 * the label "PSX Controller Type" reaching 1 px into "Standard". */
	{ "Input", PROB_TEXT_WIDE, "General", NULL },
	{ "Input", PROB_TEXT_WIDE, "Video", NULL },
	{ "Input", PROB_TEXT_WIDE, "Audio", NULL },
	{ "Input", PROB_TEXT_WIDE, "Saves", NULL },
	{ "Input", PROB_TEXT_WIDE, "Configure Buttons", NULL },
	{ "Input", PROB_TEXT_WIDE, "Standard", NULL },
	{ "Input", PROB_TEXT_WIDE, "Analog", NULL },
	{ "Input", PROB_TEXT_WIDE, "Stick D-pad", NULL },
	{ "Input", PROB_TEXT_WIDE, "Yes", NULL },
	{ "Input", PROB_TEXT_WIDE, "SD", NULL },
	{ "Input", PROB_TEXT_WIDE, "USB", NULL },
	{ "Input", PROB_OVERLAP, "PSX Controller Type", "Standard" },
	{ NULL, 0, NULL, NULL },
};

static bool isKnown(const char *page, int kind, const Layout &lay, int a, int b)
{
	for (unsigned k = 0; k < COUNT(KNOWN); k++) {
		const Known &x = KNOWN[k];
		if (!x.page || strcmp(x.page, page) != 0 || x.kind != kind || strcmp(x.a, lay.c[a].text) != 0)
			continue;
		if (b < 0 ? x.b != NULL : (x.b == NULL || strcmp(x.b, lay.c[b].text) != 0))
			continue;
		return true;
	}
	return false;
}

static void describe(const Layout &lay, int i, char *buf, size_t len)
{
	const Rect &r = lay.c[i].rect;
	snprintf(buf, len, "'%s' at %g,%g %gx%g", lay.c[i].text, r.x, r.y, r.w, r.h);
}

/* Runs checkLayout and returns how many problems are not known findings. onlyRow >= 0 counts
 * only the problems on that row. With report, each problem is printed. */
static int auditLayout(const Page &p, const Layout &lay, const char *page, int onlyRow, bool report)
{
	Problem probs[MAX_PROBLEMS];
	int n = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	int unknown = 0;

	if (n > MAX_PROBLEMS)
		n = MAX_PROBLEMS;
	for (int i = 0; i < n; i++) {
		const Problem &q = probs[i];
		bool known;
		char d1[160], d2[160];
		if (onlyRow >= 0 && lay.c[q.a].row != onlyRow && (q.b < 0 || lay.c[q.b].row != onlyRow))
			continue;
		known = isKnown(page, q.kind, lay, q.a, q.b);
		if (!known)
			unknown++;
		if (report) {
			describe(lay, q.a, d1, sizeof d1);
			if (q.b >= 0)
				describe(lay, q.b, d2, sizeof d2);
			printf("        %-12s %-30s %s%s%s  [%s]\n", page, problemName(q.kind), d1,
			       q.b >= 0 ? " / " : "", q.b >= 0 ? d2 : "", known ? "known" : "NEW");
		}
	}
	return unknown;
}

/*  Main  */

static Exp EXP[MAX_CONTROLS];

static int checkSymmetry(const char *name, const Layout &lay);

static int testPageList(const TestPage *pages, unsigned count, const char *heading)
{
	int bad = 0;

	printf("%s\n", heading);
	for (unsigned pg = 0; pg < count; pg++) {
		const TestPage &t = pages[pg];
		Row *rows = t.rows;
		Page page = toPage(t, FOCUS_ROWS);
		Layout lay;
		Key defFocus;
		int nExp = buildExpected(t, EXP, &defFocus);
		int before = failures;
		int n;

		n = layoutPage(page, kFont, lay);
		if (n < 0) {
			fail(t.name, "layoutPage refused a page OptionsFrame shows");
			bad++;
			continue;
		}
		expectLinks(t, EXP, nExp);
		compareLayout(t.name, lay, EXP, nExp);
		linkFocus(page, lay);
		compareLinks(t.name, lay, EXP, nExp, defFocus);
		if (checkSymmetry(t.name, lay) != 0)
			fail(t.name, "a one-way focus link that no tab or row-width case explains");

		/* The page must draw clear of everything, under the logo's rules. */
		if (auditLayout(page, lay, t.name, -1, true) != 0)
			fail(t.name, "checkLayout reports a problem that is not a known finding");

		/* Every value of every setting must fit its button and stay clear of its neighbours. */
		for (int i = 0; i < t.nRows; i++) {
			Row &r = rows[i];
			int saved = r.value;
			if (r.kind != ROW_CYCLE && r.kind != ROW_CHOICE)
				continue;
			for (int v = r.lo; v < r.lo + r.count; v++) {
				Layout l2;
				r.value = v;
				if (layoutPage(page, kFont, l2) < 0) {
					fail(t.name, "layoutPage refused a value");
					continue;
				}
				linkFocus(page, l2);
				if (auditLayout(page, l2, t.name, i, false) != 0) {
					char msg[200];
					snprintf(msg, sizeof msg, "row %d, value %d (%s): a problem that is not a known finding",
					         i, v, r.names[v - r.lo]);
					fail(t.name, msg);
				}
			}
			r.value = saved;
		}

		printf("  %-12s %2d rows, %2d controls, %s\n", t.name, t.nRows, lay.n,
		       failures == before ? "ok" : "FAILED");
		bad += failures - before;
	}
	return bad;
}

static int focusableIn(const Layout &lay, int row)
{
	int n = 0;
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].focusable && lay.c[i].row == row)
			n++;
	return n;
}

/* Where a link goes one way only, it must be one of the cases OptionsFrame has by design: into
 * or out of the tab row (only the lit tab links to the first and last rows), or between rows
 * of different widths (a shorter row takes the column of a wider one). Anything else fails. */
static int checkSymmetry(const char *name, const Layout &lay)
{
	static const int pairs[2][2] = { { DIR_DOWN, DIR_UP }, { DIR_RIGHT, DIR_LEFT } };
	int oneWay = 0, unexplained = 0;

	for (int i = 0; i < lay.n; i++) {
		const Control &a = lay.c[i];
		if (!a.focusable)
			continue;
		for (int k = 0; k < 2; k++) {
			int b = a.link[pairs[k][0]];
			if (b < 0 || lay.c[b].link[pairs[k][1]] == i)
				continue;
			oneWay++;
			{
				bool explained = a.role == ROLE_TAB || lay.c[b].role == ROLE_TAB ||
				                 (a.row != lay.c[b].row && focusableIn(lay, a.row) != focusableIn(lay, lay.c[b].row));
				if (!explained) {
					char b1[64], b2[64];
					unexplained++;
					printf("        %-12s one-way link %s -> %s\n", name,
					       keyName(keyOf(a), b1, sizeof b1), keyName(keyOf(lay.c[b]), b2, sizeof b2));
				}
			}
		}
	}
	printf("  %-12s %d one-way link%s, %d not explained by tabs or row widths\n", name, oneWay,
	       oneWay == 1 ? "" : "s", unexplained);
	return unexplained;
}

static void reportGeometry(void)
{
	int totalDiff = 0, totalCross = 0;
	printf("FOCUS_GEOMETRY (CustomizeCoopFrame's rule) on the same pages, against FOCUS_ROWS\n");
	for (unsigned pg = 0; pg < NPAGES; pg++) {
		const TestPage &t = PAGES[pg];
		Layout rowsLay, geoLay;
		Page rowsPage = toPage(t, FOCUS_ROWS), geoPage = toPage(t, FOCUS_GEOMETRY);
		int diff = 0, unreachable = 0, shown = 0, cross = 0;
		char b1[64], b2[64];
		Problem probs[MAX_PROBLEMS];
		int np;

		layoutPage(rowsPage, kFont, rowsLay);
		linkFocus(rowsPage, rowsLay);
		layoutPage(geoPage, kFont, geoLay);
		linkFocus(geoPage, geoLay);

		for (int i = 0; i < geoLay.n; i++) {
			int at;
			if (!geoLay.c[i].focusable)
				continue;
			shown++;
			at = findCtrl(rowsLay, keyOf(geoLay.c[i]));
			for (int d = 0; d < DIR_COUNT; d++) {
				int a = rowsLay.c[at].link[d], g = geoLay.c[i].link[d];
				bool same = (a < 0 && g < 0) ||
				            (a >= 0 && g >= 0 && sameKey(keyOf(rowsLay.c[a]), keyOf(geoLay.c[g])));
				if (!same) {
					diff++;
					/* A left or right move inside one row that geometry sends to another row. */
					if ((d == DIR_LEFT || d == DIR_RIGHT) && a >= 0 && g >= 0 &&
					    rowsLay.c[a].row == geoLay.c[i].row && geoLay.c[g].row != geoLay.c[i].row) {
						cross++;
						if (cross == 1) {
							char b3[64];
							printf("        %-12s %s %s: rows -> %s, geometry -> %s (another row)\n", t.name,
							       keyName(keyOf(geoLay.c[i]), b1, sizeof b1),
							       d == DIR_LEFT ? "Left" : "Right",
							       keyName(keyOf(rowsLay.c[a]), b2, sizeof b2),
							       keyName(keyOf(geoLay.c[g]), b3, sizeof b3));
						}
					}
					if (diff <= 2) {
						char b3[64];
						printf("        %-12s %s %s: rows -> %s, geometry -> %s\n", t.name,
						       keyName(keyOf(geoLay.c[i]), b1, sizeof b1),
						       d == DIR_UP ? "Up" : d == DIR_DOWN ? "Down" : d == DIR_LEFT ? "Left" : "Right",
						       a >= 0 ? keyName(keyOf(rowsLay.c[a]), b2, sizeof b2) : "none",
						       g >= 0 ? keyName(keyOf(geoLay.c[g]), b3, sizeof b3) : "none");
					}
				}
			}
		}
		np = checkLayout(geoPage, kFont, geoLay, probs, MAX_PROBLEMS);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
			if (probs[i].kind == PROB_UNREACHABLE || probs[i].kind == PROB_BAD_LINK)
				unreachable++;
		printf("  %-12s %d focusable, %d links differ from rows (%d left/right moves leave their row), %d not reachable\n",
		       t.name, shown, diff, cross, unreachable);
		totalDiff += diff;
		totalCross += cross;
		if (unreachable)
			fail(t.name, "FOCUS_GEOMETRY leaves a control unreachable");
	}
	printf("  total: %d links differ, %d left/right moves leave their row\n", totalDiff, totalCross);
}

/* checkLayout must catch each thing it claims to. Each case builds one bad page. */
static void testCheckCatches(void)
{
	static const char *const WIDE[] = { "Interpreter with a much longer name than fits" };
	static const char *const NARROW[] = { "On", "Off" };
	static const char *const THREE[]  = { "On", "Off", "Auto" };
	static const ActionButton FOUR[] = { {"A", 180, false}, {"B", 180, false}, {"C", 180, false}, {"D", 180, false} };
	Row rows[5];
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int np;
	bool seen[8];
	Page page;
	const char *tag = "checks";

	printf("checkLayout on deliberately bad pages\n");

	/* A label too long for its column: past the edge and over the button. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE;
	rows[0].label = "This label is far too long for its column at all";
	rows[0].names = NARROW; rows[0].count = 2; rows[0].value = 0;
	page = Page();
	page.rows = rows; page.nRows = 1; page.logo = LOGO_SPOT_BOTTOM;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_PAST_EDGE] || !seen[PROB_OVERLAP]) fail(tag, "long label not reported as past the edge and overlapping");

	/* A value wider than its button. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "Short";
	rows[0].names = WIDE; rows[0].count = 1; rows[0].value = 0;
	page.rows = rows; page.nRows = 1;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_TEXT_WIDE]) fail(tag, "a label wider than its button not reported");

	/* A row of buttons in the logo's corner. A row of buttons is centred on the screen, so
	 * nothing moves it clear. */
	static const ActionButton WIDE3[] = { {"A", 200, false}, {"B", 200, false}, {"C", 200, false} };
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 4; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = NARROW; rows[i].count = 2; }
	/* Three buttons reach x = 610, into the logo's box at the bottom right. */
	rows[4].kind = ROW_BUTTONS; rows[4].count = 3; rows[4].actions = WIDE3;
	page.rows = rows; page.nRows = 5;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_IN_LOGO]) fail(tag, "a row in the logo's box not reported");

	/* The same corner with a choice row of three 135 px buttons, which would reach x = 634 if
	 * left where they start. The engine moves the row left so that it ends at the logo's limit
	 * (538 on that line), and reports nothing. */
	rows[4].kind = ROW_CHOICE; rows[4].label = "Low"; rows[4].names = THREE; rows[4].count = 3;
	page.rows = rows; page.nRows = 5;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	if (np != 0) fail(tag, "a choice row that can move clear of the logo was reported");
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].role == ROLE_CHOICE && lay.c[i].rect.x + lay.c[i].rect.w > 538.0f)
			fail(tag, "a choice row was not moved left of the logo's limit");

	/* An action row wider than the screen. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].count = 4; rows[0].actions = FOUR;
	page.rows = rows; page.nRows = 1;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_PAST_EDGE]) fail(tag, "a button row off the screen not reported");

	/* Two cycle rows. Each one's Up and Down link to the other (the wrap makes both), so cutting
	 * both of the first row's vertical links leaves the second unreachable. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "A"; rows[0].names = NARROW; rows[0].count = 2;
	rows[1].kind = ROW_CYCLE; rows[1].label = "B"; rows[1].names = NARROW; rows[1].count = 2;
	page.rows = rows; page.nRows = 2;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	{
		int first = lay.slot[0][0];
		lay.c[first].link[DIR_DOWN] = -1;
		lay.c[first].link[DIR_UP] = -1;
		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		memset(seen, 0, sizeof seen);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
		if (!seen[PROB_UNREACHABLE]) fail(tag, "a control cut off from focus not reported");
	}

	/* A link to a label, which is not focusable. */
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	{
		int first = lay.slot[0][0];
		int label = -1;
		for (int i = 0; i < lay.n; i++) if (lay.c[i].role == ROLE_LABEL) { label = i; break; }
		lay.c[first].link[DIR_RIGHT] = label;
		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		memset(seen, 0, sizeof seen);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
		if (!seen[PROB_BAD_LINK]) fail(tag, "a focus link to a label not reported");
	}

	/* Things layoutPage refuses rather than laying out past its arrays. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CHOICE; rows[0].names = WIDE; rows[0].count = 5;
	page.rows = rows; page.nRows = 1;
	if (layoutPage(page, kFont, lay) != -1) fail(tag, "a five-button choice row was accepted");
	page.rows = rows; page.nRows = MAX_ROWS + 1;
	if (layoutPage(page, kFont, lay) != -1) fail(tag, "a seventh row was accepted");
}

/*  The General tab's focus, against Settings' own table  */

/* The links SettingsFrame's FRAME_BUTTONS table gives the General tab's buttons (by FRAME_BUTTONS
 * index: up, down, left, right, -1 for none), with the Up and Down that activateSubmenu(SUBMENU_
 * GENERAL) sets on the five tabs: Down to 73 (Advanced), Up to 14 (Save Settings: SD). */
struct OldLink {
	int idx, up, down, left, right;
};
static const OldLink OLD_GENERAL[] = {
	{ 0, 14, 73, 4, 1 }, { 1, 14, 73, 0, 2 }, { 2, 14, 73, 1, 3 }, { 3, 14, 73, 2, 4 }, { 4, 14, 73, 3, 0 },
	{ 7, 73, 11, 10, 8 }, { 8, 73, 11, 7, 9 }, { 9, 75, 12, 8, 10 }, { 10, 75, 12, 9, 7 },
	{ 11, 7, 54, 13, 12 }, { 12, 8, 54, 11, 13 }, { 13, 9, 54, 12, 11 },
	{ 14, 55, 0, 62, 15 }, { 15, 55, 0, 14, 62 }, { 62, 56, 0, 15, 14 },
	{ 54, 11, 55, -1, -1 },
	{ 55, 54, 14, 56, 56 }, { 56, 54, 14, 55, 55 },
	{ 73, 0, 7, 75, 75 }, { 75, 0, 9, 73, 73 },
};

/* The control a FRAME_BUTTONS index is on the General tab. Returns false for an index the tab
 * does not show. */
static bool frameKey(int idx, Key &k)
{
	if (idx >= 0 && idx < 5)   { k = tabKey(idx); return true; }
	if (idx >= 7 && idx <= 10) { k.role = ROLE_CHOICE; k.row = 1; k.slot = idx - 7;  return true; }
	if (idx >= 11 && idx <= 13) { k.role = ROLE_CHOICE; k.row = 2; k.slot = idx - 11; return true; }
	if (idx == 54)             { k.role = ROLE_BUTTON; k.row = 3; k.slot = 0; return true; }
	if (idx == 55 || idx == 56) { k.role = ROLE_CHOICE; k.row = 4; k.slot = idx - 55; return true; }
	if (idx == 14 || idx == 15 || idx == 62) {
		k.role = ROLE_BUTTON; k.row = 5; k.slot = idx == 14 ? 0 : idx == 15 ? 1 : 2;
		return true;
	}
	if (idx == 73 || idx == 75) { k.role = ROLE_BUTTON; k.row = 0; k.slot = idx == 73 ? 0 : 1; return true; }
	return false;
}

static int frameIndexOf(Key want)
{
	for (int i = 0; i < 80; i++) {
		Key k;
		if (frameKey(i, k) && sameKey(k, want))
			return i;
	}
	return -1;
}

/* The General tab's boxes and label anchors as SettingsFrame.cpp's tables write them, read from
 * the numbers themselves (not from the rules above), so that a rule that is wrong fails here. */
static const struct { int idx; float x, y, w, h; } OLD_RECTS[] = {
	{ 0, 25, 20, 110, 56 }, { 1, 155, 20, 100, 56 }, { 2, 275, 20, 100, 56 },
	{ 3, 395, 20, 100, 56 }, { 4, 515, 20, 100, 56 },
	{ 7, 295, 160, 70, 56 }, { 8, 375, 160, 55, 56 }, { 9, 440, 160, 70, 56 }, { 10, 520, 160, 70, 56 },
	{ 11, 295, 220, 75, 56 }, { 12, 380, 220, 75, 56 }, { 13, 465, 220, 171, 56 },
	{ 14, 253, 400, 55, 56 }, { 15, 318, 400, 70, 56 }, { 62, 398, 400, 140, 56 },
	{ 54, 295, 280, 90, 56 },
	{ 55, 295, 340, 75, 56 }, { 56, 380, 340, 75, 56 },
	{ 73, 175, 100, 130, 56 }, { 75, 335, 100, 130, 56 },
};
/* FRAME_TEXTBOXES' label centres, by the row each label belongs to: (150, row top + 28). */
static const float OLD_LABEL_Y[6] = { 0.0f, 188.0f, 248.0f, 308.0f, 368.0f, 428.0f };

static int checkGeneralRects(void)
{
	const TestPage &t = SETTINGS_PAGES[0];
	Page page = toPage(t, FOCUS_ROWS);
	Layout lay;
	int bad = 0;

	if (layoutPage(page, kFont, lay) < 0)
		return 1;
	for (unsigned k = 0; k < COUNT(OLD_RECTS); k++) {
		Key key;
		int at;
		char msg[160];
		if (!frameKey(OLD_RECTS[k].idx, key)) {
			fail("General", "a table button is not on the tab");
			bad++;
			continue;
		}
		at = findCtrl(lay, key);
		if (at < 0) {
			fail("General", "a table button is not in the layout");
			bad++;
			continue;
		}
		{
			const Rect &r = lay.c[at].rect;
			if (!nearly(r.x, OLD_RECTS[k].x) || !nearly(r.y, OLD_RECTS[k].y) ||
			    !nearly(r.w, OLD_RECTS[k].w) || !nearly(r.h, OLD_RECTS[k].h)) {
				snprintf(msg, sizeof msg, "button %d at (%g,%g %gx%g), the table has (%g,%g %gx%g)",
				         OLD_RECTS[k].idx, r.x, r.y, r.w, r.h, OLD_RECTS[k].x, OLD_RECTS[k].y,
				         OLD_RECTS[k].w, OLD_RECTS[k].h);
				fail("General", msg);
				bad++;
			}
		}
	}
	/* The labels: each row's label is centred at x 150, on the row's middle. */
	int labels = 0;
	for (int i = 0; i < lay.n; i++) {
		const Control &c = lay.c[i];
		char msg[160];
		if (c.kind != CTRL_TEXT || (c.role != ROLE_LABEL && c.role != ROLE_RLABEL))
			continue;
		labels++;
		if (!nearly(c.ax, 150.0f) || !nearly(c.ay, OLD_LABEL_Y[c.row]) || c.align != ALIGN_CENTRE) {
			snprintf(msg, sizeof msg, "label '%s' centred at (%g,%g), the table has (150,%g)",
			         c.text, c.ax, c.ay, OLD_LABEL_Y[c.row]);
			fail("General", msg);
			bad++;
		}
	}
	printf("  General: %d table boxes and %d labels checked against the numbers in SettingsFrame.cpp\n",
	       (int)COUNT(OLD_RECTS), labels);
	return bad;
}

/* Prints the General tab's focus links that differ from the table's. This is not a failure: each
 * one is a finding for the port's report (Docs/MENU_FRAMEWORK_PLAN.md), and the look and the
 * focus of the tab stay as they are until someone has decided each. Returns how many differ. */
static int checkGeneralAgainstTable(void)
{
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };
	const TestPage &t = SETTINGS_PAGES[0];
	Page page = toPage(t, FOCUS_ROWS);
	Layout lay;
	int diffs = 0;

	if (layoutPage(page, kFont, lay) < 0) {
		fail("General", "layoutPage refused the General tab");
		return 0;
	}
	linkFocus(page, lay);
	printf("  General: focus links against Settings' table (a finding, not a failure)\n");
	for (unsigned k = 0; k < COUNT(OLD_GENERAL); k++) {
		const OldLink &o = OLD_GENERAL[k];
		const int want[DIR_COUNT] = { o.up, o.down, o.left, o.right };
		Key from;
		int at;

		if (!frameKey(o.idx, from)) {
			fail("General", "the table names a button the tab does not show");
			continue;
		}
		at = findCtrl(lay, from);
		if (at < 0) {
			fail("General", "a button the table shows is not in the layout");
			continue;
		}
		for (int d = 0; d < DIR_COUNT; d++) {
			const int got = lay.c[at].link[d];
			const int gotIdx = got < 0 ? -1 : frameIndexOf(keyOf(lay.c[got]));
			if (gotIdx != want[d]) {
				diffs++;
				printf("        button %3d %-5s table %3d, engine %3d\n", o.idx, DIR[d], want[d], gotIdx);
			}
		}
	}
	printf("  General: %d link%s differ from the table\n", diffs, diffs == 1 ? "" : "s");
	return diffs;
}

/*  The Video tab's boxes, labels and focus, against Settings' own table  */

/* The Video tab's boxes as SettingsFrame.cpp's tables write them: the five tabs (the same as General's,
 * read from the numbers there) and the buttons the tab shows. Dithering (25..27) is hidden on this tab. */
static const struct { int idx; float x, y, w, h; } VIDEO_OLD_RECTS[] = {
	{ 0, 25, 20, 110, 56 }, { 1, 155, 20, 100, 56 }, { 2, 275, 20, 100, 56 },
	{ 3, 395, 20, 100, 56 }, { 4, 515, 20, 100, 56 },
	{ 16, 295, 100, 75, 56 }, { 17, 380, 100, 75, 56 },
	{ 18, 295, 160, 75, 56 }, { 19, 380, 160, 75, 56 }, { 63, 465, 160, 160, 56 },
	{ 20, 295, 220, 75, 56 }, { 21, 380, 220, 75, 56 },
	{ 22, 295, 280, 130, 56 }, { 23, 435, 280, 125, 56 }, { 57, 570, 280, 66, 56 },
	{ 24, 428, 340, 110, 56 }, { 28, 238, 340, 95, 56 }, { 29, 343, 340, 75, 56 },
	{ 78, 245, 400, 150, 56 },
};
/* The labels: textboxes 4, 5, 6, 7 and 9 on the rows 0..4, centred at x 150 on the row's middle. */
static const float VIDEO_OLD_LABEL_Y[5] = { 128.0f, 188.0f, 248.0f, 308.0f, 368.0f };

/* The focus links SettingsFrame's FRAME_BUTTONS table gives the Video tab's buttons (by FRAME_BUTTONS
 * index: up, down, left, right), with the tabs' Down (16) and Up (26, a hidden Dithering button) that
 * activateSubmenu(SUBMENU_VIDEO) sets. */
static const OldLink OLD_VIDEO[] = {
	{ 0, 26, 16, 4, 1 }, { 1, 26, 16, 0, 2 }, { 2, 26, 16, 1, 3 }, { 3, 26, 16, 2, 4 }, { 4, 26, 16, 3, 0 },
	{ 16, 1, 18, 17, 17 }, { 17, 1, 19, 16, 16 },
	{ 18, 16, 20, 63, 19 }, { 19, 17, 21, 18, 63 }, { 63, 17, 21, 19, 18 },
	{ 20, 18, 23, 21, 21 }, { 21, 19, 23, 20, 20 },
	{ 22, 20, 25, 57, 23 }, { 23, 20, 26, 22, 57 }, { 57, 21, 27, 23, 22 },
	{ 24, 23, 78, 29, 28 }, { 28, 22, 78, 24, 29 }, { 29, 22, 78, 28, 24 },
	{ 78, 28, 1, 78, 78 },
};

/* The control a FRAME_BUTTONS index is on the Video tab. Returns false for an index the tab does not show. */
static bool vidKey(int idx, Key &k)
{
	if (idx >= 0 && idx < 5)    { k = tabKey(idx); return true; }
	if (idx == 16 || idx == 17) { k.role = ROLE_CHOICE; k.row = 0; k.slot = idx - 16; return true; }
	if (idx == 18 || idx == 19) { k.role = ROLE_BUTTON; k.row = 1; k.slot = idx - 18; return true; }
	if (idx == 63)              { k.role = ROLE_BUTTON; k.row = 1; k.slot = 2; return true; }
	if (idx == 20 || idx == 21) { k.role = ROLE_CHOICE; k.row = 2; k.slot = idx - 20; return true; }
	if (idx == 22 || idx == 23 || idx == 57) {
		k.role = ROLE_BUTTON; k.row = 3; k.slot = idx == 22 ? 0 : idx == 23 ? 1 : 2; return true;
	}
	if (idx == 28 || idx == 29 || idx == 24) {
		k.role = ROLE_BUTTON; k.row = 4; k.slot = idx == 28 ? 0 : idx == 29 ? 1 : 2; return true;
	}
	if (idx == 78)              { k.role = ROLE_BUTTON; k.row = 5; k.slot = 0; return true; }
	return false;
}

static int vidIndexOf(Key want)
{
	for (int i = 0; i < 80; i++) {
		Key k;
		if (vidKey(i, k) && sameKey(k, want))
			return i;
	}
	return -1;
}

static int checkVideoRects(void)
{
	const TestPage &t = SETTINGS_PAGES[1];
	Page page = toPage(t, FOCUS_ROWS);
	Layout lay;
	int bad = 0, labels = 0;

	if (layoutPage(page, kFont, lay) < 0) {
		fail("Video", "layoutPage refused the Video tab");
		return 1;
	}
	for (unsigned k = 0; k < COUNT(VIDEO_OLD_RECTS); k++) {
		Key key;
		int at;
		char msg[160];
		if (!vidKey(VIDEO_OLD_RECTS[k].idx, key)) {
			fail("Video", "a table button is not on the tab");
			bad++;
			continue;
		}
		at = findCtrl(lay, key);
		if (at < 0) {
			fail("Video", "a table button is not in the layout");
			bad++;
			continue;
		}
		{
			const Rect &r = lay.c[at].rect;
			if (!nearly(r.x, VIDEO_OLD_RECTS[k].x) || !nearly(r.y, VIDEO_OLD_RECTS[k].y) ||
			    !nearly(r.w, VIDEO_OLD_RECTS[k].w) || !nearly(r.h, VIDEO_OLD_RECTS[k].h)) {
				snprintf(msg, sizeof msg, "button %d at (%g,%g %gx%g), the table has (%g,%g %gx%g)",
				         VIDEO_OLD_RECTS[k].idx, r.x, r.y, r.w, r.h, VIDEO_OLD_RECTS[k].x, VIDEO_OLD_RECTS[k].y,
				         VIDEO_OLD_RECTS[k].w, VIDEO_OLD_RECTS[k].h);
				fail("Video", msg);
				bad++;
			}
		}
	}
	/* The labels: each row's label is centred at x 150, on the row's middle. */
	for (int i = 0; i < lay.n; i++) {
		const Control &c = lay.c[i];
		char msg[160];
		if (c.kind != CTRL_TEXT || (c.role != ROLE_LABEL && c.role != ROLE_RLABEL))
			continue;
		labels++;
		if (!nearly(c.ax, 150.0f) || !nearly(c.ay, VIDEO_OLD_LABEL_Y[c.row]) || c.align != ALIGN_CENTRE) {
			snprintf(msg, sizeof msg, "label '%s' centred at (%g,%g), the table has (150,%g)",
			         c.text, c.ax, c.ay, VIDEO_OLD_LABEL_Y[c.row]);
			fail("Video", msg);
			bad++;
		}
	}
	printf("  Video: %d table boxes and %d labels checked against the numbers in SettingsFrame.cpp\n",
	       (int)COUNT(VIDEO_OLD_RECTS), labels);
	return bad;
}

/* Prints the Video tab's focus links that differ from the table's (a finding, not a failure, like
 * General's). Returns how many differ. */
static int checkVideoAgainstTable(void)
{
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };
	const TestPage &t = SETTINGS_PAGES[1];
	Page page = toPage(t, FOCUS_ROWS);
	Layout lay;
	int diffs = 0;

	if (layoutPage(page, kFont, lay) < 0) {
		fail("Video", "layoutPage refused the Video tab");
		return 0;
	}
	linkFocus(page, lay);
	printf("  Video: focus links against Settings' table (a finding, not a failure)\n");
	for (unsigned k = 0; k < COUNT(OLD_VIDEO); k++) {
		const OldLink &o = OLD_VIDEO[k];
		const int want[DIR_COUNT] = { o.up, o.down, o.left, o.right };
		Key from;
		int at;

		if (!vidKey(o.idx, from)) {
			fail("Video", "the table names a button the tab does not show");
			continue;
		}
		at = findCtrl(lay, from);
		if (at < 0) {
			fail("Video", "a button the table shows is not in the layout");
			continue;
		}
		for (int d = 0; d < DIR_COUNT; d++) {
			const int got = lay.c[at].link[d];
			const int gotIdx = got < 0 ? -1 : vidIndexOf(keyOf(lay.c[got]));
			if (gotIdx != want[d]) {
				diffs++;
				printf("        button %3d %-5s table %3d, engine %3d\n", o.idx, DIR[d], want[d], gotIdx);
			}
		}
	}
	printf("  Video: %d link%s differ from the table\n", diffs, diffs == 1 ? "" : "s");
	return diffs;
}

/*  The Input, Audio and Saves tabs' boxes, labels and focus, against Settings' own tables  */

/* One button of a tab as SettingsFrame.cpp's tables give it: its FRAME_BUTTONS index, the control
 * the tab draws it as (role, row, slot), its box, and the links the table gives it (-1: none). A
 * tab row's Up and Down are the ones activateSubmenu() sets for that tab; its Left and Right are the table's. */
struct OldBox {
	int idx;
	int role, row, slot;
	float x, y, w, h;
	int up, down, left, right;
};

/* The index in the table of the control with key want, or -2 when it is not one of the table's (a
 * control the table does not show). */
static int boxIndexOf(const OldBox *b, unsigned n, Key want)
{
	for (unsigned k = 0; k < n; k++) {
		Key key = { b[k].role, b[k].row, b[k].slot };
		if (sameKey(key, want))
			return b[k].idx;
	}
	return -2;
}

/* The boxes and label anchors of a tab, against the layout. labelY is the label centre of each row
 * (the table's label y); every label is centred at x 150. */
static int checkTabBoxes(const char *name, int page, const OldBox *b, unsigned n,
                         const float *labelY, int nLabelY)
{
	Page p = toPage(SETTINGS_PAGES[page], FOCUS_ROWS);
	Layout lay;
	int bad = 0, labels = 0;

	if (layoutPage(p, kFont, lay) < 0) {
		fail(name, "layoutPage refused the tab");
		return 1;
	}
	for (unsigned k = 0; k < n; k++) {
		Key key = { b[k].role, b[k].row, b[k].slot };
		const int at = findCtrl(lay, key);
		char msg[160];
		if (at < 0) {
			snprintf(msg, sizeof msg, "button %d is not in the layout", b[k].idx);
			fail(name, msg);
			bad++;
			continue;
		}
		{
			const Rect &r = lay.c[at].rect;
			if (!nearly(r.x, b[k].x) || !nearly(r.y, b[k].y) ||
			    !nearly(r.w, b[k].w) || !nearly(r.h, b[k].h)) {
				snprintf(msg, sizeof msg, "button %d at (%g,%g %gx%g), the table has (%g,%g %gx%g)",
				         b[k].idx, r.x, r.y, r.w, r.h, b[k].x, b[k].y, b[k].w, b[k].h);
				fail(name, msg);
				bad++;
			}
		}
	}
	for (int i = 0; i < lay.n; i++) {
		const Control &c = lay.c[i];
		char msg[160];
		if (c.kind != CTRL_TEXT || (c.role != ROLE_LABEL && c.role != ROLE_RLABEL))
			continue;
		labels++;
		if (c.row < 0 || c.row >= nLabelY || !nearly(c.ax, 150.0f) || !nearly(c.ay, labelY[c.row]) ||
		    c.align != ALIGN_CENTRE) {
			snprintf(msg, sizeof msg, "label '%s' centred at (%g,%g), the table has (150,%g)",
			         c.text, c.ax, c.ay, c.row >= 0 && c.row < nLabelY ? labelY[c.row] : -1.0f);
			fail(name, msg);
			bad++;
		}
	}
	printf("  %s: %d table boxes and %d labels checked against the numbers in SettingsFrame.cpp\n",
	       name, (int)n, labels);
	return bad;
}

/* Prints the tab's focus links that differ from the table's, as a finding and not a failure, like
 * General's and Video's. "other" is a control the table does not show. Returns how many differ. */
static int checkTabLinks(const char *name, int page, const OldBox *b, unsigned n)
{
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };
	Page p = toPage(SETTINGS_PAGES[page], FOCUS_ROWS);
	Layout lay;
	int diffs = 0;

	if (layoutPage(p, kFont, lay) < 0) {
		fail(name, "layoutPage refused the tab");
		return 0;
	}
	linkFocus(p, lay);
	printf("  %s: focus links against Settings' table (a finding, not a failure)\n", name);
	for (unsigned k = 0; k < n; k++) {
		Key from = { b[k].role, b[k].row, b[k].slot };
		const int at = findCtrl(lay, from);
		const int want[DIR_COUNT] = { b[k].up, b[k].down, b[k].left, b[k].right };
		if (at < 0) {
			fail(name, "a button the table shows is not in the layout");
			continue;
		}
		for (int d = 0; d < DIR_COUNT; d++) {
			const int got = lay.c[at].link[d];
			const int gotIdx = got < 0 ? -1 : boxIndexOf(b, n, keyOf(lay.c[got]));
			if (gotIdx != want[d]) {
				diffs++;
				if (gotIdx == -2)
					printf("        button %3d %-5s table %3d, engine: a control the table does not show\n",
					       b[k].idx, DIR[d], want[d]);
				else
					printf("        button %3d %-5s table %3d, engine %3d\n", b[k].idx, DIR[d], want[d], gotIdx);
			}
		}
	}
	printf("  %s: %d link%s differ from the table\n", name, diffs, diffs == 1 ? "" : "s");
	return diffs;
}

/* The Input tab. Tabs: each tab's own box; Up 38 and Down 30 as activateSubmenu(SUBMENU_INPUT) sets. */
static const OldBox INPUT_BOXES[] = {
	{ 0, ROLE_TAB, -1, 0, 25.0f, 20.0f, 110.0f, 56.0f, 38, 30, 4, 1 },
	{ 1, ROLE_TAB, -1, 1, 155.0f, 20.0f, 100.0f, 56.0f, 38, 30, 0, 2 },
	{ 2, ROLE_TAB, -1, 2, 275.0f, 20.0f, 100.0f, 56.0f, 38, 30, 1, 3 },
	{ 3, ROLE_TAB, -1, 3, 395.0f, 20.0f, 100.0f, 56.0f, 38, 30, 2, 4 },
	{ 4, ROLE_TAB, -1, 4, 515.0f, 20.0f, 100.0f, 56.0f, 38, 30, 3, 0 },
	{ 30, ROLE_BUTTON, 0, 0,  85.0f, 100.0f, 220.0f, 56.0f,  2, 32, 31, 31 },
	{ 31, ROLE_BUTTON, 0, 1, 320.0f, 100.0f, 235.0f, 56.0f,  2, 32, 30, 30 },
	{ 32, ROLE_CHOICE, 1, 0, 271.0f, 160.0f, 105.0f, 56.0f, 30, 59, 79, 33 },
	{ 33, ROLE_CHOICE, 1, 1, 386.0f, 160.0f, 100.0f, 56.0f, 31, 59, 32, 79 },
	{ 79, ROLE_CHOICE, 1, 2, 496.0f, 160.0f, 140.0f, 56.0f, 31, 59, 33, 32 },
	{ 59, ROLE_BUTTON, 2, 0, 295.0f, 220.0f, 135.0f, 56.0f, 32, 34, -1, -1 },
	{ 34, ROLE_CHOICE, 3, 0, 295.0f, 280.0f,  75.0f, 56.0f, 59, 36, 35, 35 },
	{ 35, ROLE_CHOICE, 3, 1, 380.0f, 280.0f,  75.0f, 56.0f, 59, 37, 34, 34 },
	{ 36, ROLE_BUTTON, 4, 0, 295.0f, 340.0f,  55.0f, 56.0f, 34, 38, 37, 37 },
	{ 37, ROLE_BUTTON, 4, 1, 360.0f, 340.0f,  70.0f, 56.0f, 35, 38, 36, 36 },
	{ 38, ROLE_BUTTON, 5, 0, 295.0f, 400.0f, 135.0f, 56.0f, 36,  2, -1, -1 },
};
/* The label centres: each row's top + 28 (textboxes 10, 29, 11, 12 and 13). */
static const float INPUT_LABEL_Y[6] = { 0.0f, 188.0f, 248.0f, 308.0f, 368.0f, 428.0f };

int main(int argc, char **argv)
{
	const char *font = argc > 1 ? argv[1] : "fonts/En.dat";

	if (!loadFont(font)) {
		printf("cannot read the font file '%s' (pass its path as the first argument)\n", font);
		return 2;
	}

	testPageList(PAGES, NPAGES,
	             "OptionsFrame pages (rectangles, focus links, help, logo, every value of every setting)");
	testPageList(SETTINGS_PAGES, NSETTINGS,
	             "Settings tabs on the engine (rectangles, focus links, labels, logo, every value)");
	checkGeneralRects();
	checkGeneralAgainstTable();
	checkVideoRects();
	checkVideoAgainstTable();
	checkTabBoxes("Input", 2, INPUT_BOXES, COUNT(INPUT_BOXES), INPUT_LABEL_Y, COUNT(INPUT_LABEL_Y));
	checkTabLinks("Input", 2, INPUT_BOXES, COUNT(INPUT_BOXES));
	reportGeometry();
	testCheckCatches();

	printf("\n%s: %d check%s failed\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures > 255 ? 255 : failures;
}
