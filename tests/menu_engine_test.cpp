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

/*  OptionsFrame's tables, copied from Gamecube/menu/OptionsFrame.cpp. The five Advanced tabs (CPU, GPU,
 *  SPU, CD, Input) are standard pages: their mirror is testStdAdvancedTabs().  */

static const char *const RESAMPLER_NAMES[]    = { "Hold", "Linear", "Cubic" };
static const char *const ON_OFF_NAMES[]       = { "Off", "On" };
static const char *const LEGACY_HIFI_NAMES[]  = { "Legacy", "Hi-Fi" };
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

/* Readouts: the text is whatever the emulator reports, so the longest it can be is used. */
static Row MEMORY_ROWS[] = {
	INFO("MEM1 (fast)",        "24576 used, 24576 free KB"),
	INFO("MEM2 heap",          "48128 used, 48128 free KB"),
	INFO("Texture pool",       "999 textures, 9999 + 9999 KB"),
	INFO("Locked cache",       "16 / 16 KB (spu-gauss, tex-tile)"),	/* lc_usage(): the region names */
	INFO("Font glyph cache",   "1024 / 1024 KB"),
	INFO("Recompiler buffer",  "1536 of 1536 KB used"),
	INFO("PS1 RAM + VRAM",     "2048 + 2048 KB"),
	INFO("SPU + BIOS buffers", "64 + 512 KB"),
};

static Row ADVGFX_ROWS[] = {
	RADIO_FIT("Dithering",     DITHER_NAMES, 1),
	CYC("MDEC Chroma",         CHROMA_NAMES, 0),
	CYC("Video Color",        FMV_COLOUR_NAMES, 1),
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
	float            rowDy;		/* 0: 60 (the Settings grid); the Audio and Saves tabs: 70 */
	int              columns;	/* 2: the two-column layout (OptPage::columns) */
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static TestPage PAGES[] = {
	{ "Adv Sound",   "Advanced Sound",    false, 0, SOUND_ROWS,   COUNT(SOUND_ROWS),   NULL,        0 },
	/* one column here; the real two-column page, with all its rows, is testTwoColumns() */
	{ "Adv Graphics","Advanced Graphics", false, 0, ADVGFX_ROWS, COUNT(ADVGFX_ROWS), NULL,        0 },
	{ "Memory",      "Memory",            false, 0, MEMORY_ROWS, COUNT(MEMORY_ROWS), NULL,        0, NULL, 0, 0.0f, 0.0f, 46.0f },
};
#define NPAGES COUNT(PAGES)

/*  The Settings General tab (menupage 1), from SettingsFrame.cpp's FRAME_BUTTONS and
 *  FRAME_TEXTBOXES, and its activateSubmenu() case for SUBMENU_GENERAL.
 *
 *  The rows are the six lines of the tab, top to bottom. Each button's box is the number the
 *  table gives it: 73/75 at y 100, the BIOS four at 160, Yes/No and Execute BIOS at 220, Select
 *  Language at 280, the three Save buttons at 340, the three Load buttons at 400. Labels sit at the centre
 *  of their row (y + 28) and their centre is x 150 (Settings' TextBox, centred). */
static const char *const GEN_BIOS_NAMES[]  = { "HLE", "SD", "USB", "DVD" };
static const char *const GEN_BOOT_NAMES[]  = { "Yes", "No", "Execute BIOS" };
static const float        GEN_BIOS_W[]     = { 70.0f, 55.0f, 70.0f, 70.0f };
static const float        GEN_BOOT_W[]     = { 75.0f, 75.0f, 171.0f };
static const ActionButton GEN_ADV_ACTIONS[]  = { { "Advanced", 130.0f, false }, { "Memory", 130.0f, false } };
static const ActionButton GEN_LANG_ACTIONS[] = { { "En", 90.0f, false } };
static const ActionButton GEN_SAVE_ACTIONS[] = { { "SD", 55.0f, false }, { "USB", 70.0f, false }, { "This Game", 140.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row GENERAL_ROWS[] = {
	{ ROW_BUTTONS, NULL,                 NULL,           0, 2, 0, NULL, false, GEN_ADV_ACTIONS,  0.0f,   30.0f, NULL,       0 },
	{ ROW_CHOICE,  "Select BIOS",        GEN_BIOS_NAMES, 0, 4, 0, NULL, false, NULL,             295.0f,  0.0f, GEN_BIOS_W, 0 },
	{ ROW_CHOICE,  "Boot Through BIOS",  GEN_BOOT_NAMES, 0, 3, 0, NULL, false, NULL,             295.0f,  0.0f, GEN_BOOT_W, 1 },
	{ ROW_BUTTONS, "Select Language",    NULL,           0, 1, 0, NULL, false, GEN_LANG_ACTIONS, 295.0f,  0.0f, NULL,       0 },
	{ ROW_BUTTONS, "Save Settings",      NULL,           0, 3, 0, NULL, false, GEN_SAVE_ACTIONS, 253.0f,  0.0f, NULL,       0 },
	{ ROW_BUTTONS, "Load Settings",      NULL,           0, 3, 0, NULL, false, GEN_SAVE_ACTIONS, 253.0f,  0.0f, NULL,       0 },
};

static const Tab GEN_TABS[] = {
	{ "General", 110.0f }, { "Video", 100.0f }, { "Input", 100.0f }, { "Audio", 100.0f }, { "Saves", 100.0f },
};

/*  The Settings Video tab (menupage 2), from SettingsFrame.cpp's FRAME_BUTTONS, FRAME_TEXTBOXES and
 *  layoutVideo(). The lit buttons are one set state: Show FPS On, Frame Rate Auto with NTSC on,
 *  Frame Skip Off, Interlaced off with 240p on, Trap off with Deflicker on. The button texts are the
 *  state's (Screen Mode 4:3, Bilinear Default). The rows, labels and widths are the table's. */
static const char *const VID_ON_OFF_NAMES[] = { "On", "Off" };
static const float        VID_ON_OFF_W[]    = { 75.0f, 75.0f };
static const ActionButton VID_FRAMERATE_ACTIONS[] = { { "Auto", 130.0f, false }, { "NTSC", 160.0f, true } };
static const ActionButton VID_SCREEN_ACTIONS[] = { { "4:3", 130.0f, false }, { "Interlaced", 125.0f, false }, { "240p", 66.0f, true } };
static const ActionButton VID_FILTER_ACTIONS[] = { { "Default", 95.0f, false }, { "Trap", 75.0f, false }, { "Deflicker", 110.0f, true } };
static const ActionButton VID_ADV_ACTIONS[]    = { { "Advanced", 130.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row VIDEO_ROWS[] = {
	{ ROW_CHOICE,  "Show FPS",    VID_ON_OFF_NAMES, 0, 2, 0, NULL, false, NULL,               295.0f, 0.0f, VID_ON_OFF_W, 0 },
	{ ROW_BUTTONS, "Frame Rate", NULL,             0, 2, 0, NULL, false, VID_FRAMERATE_ACTIONS, 295.0f, 0.0f, NULL,         0 },
	{ ROW_CHOICE,  "Frame Skip",  VID_ON_OFF_NAMES, 0, 2, 1, NULL, false, NULL,               295.0f, 0.0f, VID_ON_OFF_W, 0 },
	{ ROW_BUTTONS, "Screen Mode", NULL,             0, 3, 0, NULL, false, VID_SCREEN_ACTIONS, 295.0f, 0.0f, NULL,         0 },
	{ ROW_BUTTONS, "Filters",     NULL,             0, 3, 0, NULL, false, VID_FILTER_ACTIONS, 295.0f, 0.0f, NULL,         0 },
	{ ROW_BUTTONS, NULL,          NULL,             0, 1, 0, NULL, false, VID_ADV_ACTIONS,    0.0f,   0.0f, NULL,         0 },
};

/*  The Settings Input tab (menupage 3), from SettingsFrame.cpp's FRAME_BUTTONS, FRAME_TEXTBOXES and
 *  layoutInput(). The state is the default one: Standard pad, rumble on, Light Gun off, Auto Load
 *  Default. The rows and widths are the table's; the labels are textboxes 10, 29, 11, 12 and 13. */
static const char *const INP_PSX_NAMES[]    = { "Digital", "Analog", "Stick Pad" };
static const float        INP_PSX_W[]       = { 117.0f,  95.0f, 152.0f };
static const char *const INP_RUMBLE_NAMES[] = { "Yes", "No" };
static const float        INP_RUMBLE_W[]    = { 75.0f, 75.0f };
static const ActionButton INP_CONFIG_ACTIONS[]   = { { "Configure Input", 220.0f, false }, { "Configure Buttons", 235.0f, false } };
static const ActionButton INP_LIGHTGUN_ACTIONS[] = { { "Off", 135.0f, false } };
static const ActionButton INP_SAVEBTN_ACTIONS[]  = { { "SD", 55.0f, false }, { "USB", 70.0f, false } };
static const ActionButton INP_AUTOLOAD_ACTIONS[] = { { "Default", 135.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row SET_INPUT_ROWS[] = {
	{ ROW_BUTTONS, NULL,                  NULL,            0, 2, 0, NULL, false, INP_CONFIG_ACTIONS,  85.0f,  15.0f, NULL,       0 },
	{ ROW_CHOICE,  "PSX Pad Type", INP_PSX_NAMES,   0, 3, 0, NULL, false, NULL,                252.0f,  0.0f, INP_PSX_W,  0 },
	{ ROW_BUTTONS, "Light Gun",           NULL,            0, 1, 0, NULL, false, INP_LIGHTGUN_ACTIONS, 295.0f,  0.0f, NULL,       0 },
	{ ROW_CHOICE,  "Enable Rumble",       INP_RUMBLE_NAMES, 0, 2, 0, NULL, false, NULL,                295.0f,  0.0f, INP_RUMBLE_W, 0 },
	{ ROW_BUTTONS, "Save Configs", NULL,            0, 2, 0, NULL, false, INP_SAVEBTN_ACTIONS,  295.0f,  0.0f, NULL,       0 },
	{ ROW_BUTTONS, "Auto Load Slot",      NULL,            0, 1, 0, NULL, false, INP_AUTOLOAD_ACTIONS, 295.0f,  0.0f, NULL,       0 },
};

/*  The Settings Audio tab (menupage 4). Rows are 70 apart (rowDy). The state: Audio on, XA on, CDDA off,
 *  Sound Driver CPU, Sync Off, Interpolation Simple. Labels are textboxes 14, 17, 25 and 26. */
static const char *const AUD_DSP_NAMES[]  = { "DSP", "CPU" };
static const float        AUD_DSP_W[]     = { 75.0f, 75.0f };
static const char *const AUD_SYNC_NAMES[] = { "Off", "Tempo", "Rate" };
static const float        AUD_SYNC_W[]    = { 75.0f, 90.0f, 75.0f };
static const ActionButton AUD_ENABLE_ACTIONS[] = { { "Audio", 100.0f, true }, { "XA", 75.0f, true }, { "CDDA", 110.0f, false } };
static const ActionButton AUD_INTERP_ACTIONS[] = { { "Simple", 170.0f, false } };
static const ActionButton AUD_ADV_ACTIONS[]    = { { "Advanced", 130.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row AUDIO_ROWS[] = {
	{ ROW_BUTTONS, "Enable",           NULL,           0, 3, 0, NULL, false, AUD_ENABLE_ACTIONS, 295.0f,  0.0f, NULL,      0 },
	{ ROW_BUTTONS, "Interpolation",    NULL,           0, 1, 0, NULL, false, AUD_INTERP_ACTIONS, 295.0f,  0.0f, NULL,      0 },
	{ ROW_CHOICE,  "Sound Driver",     AUD_DSP_NAMES,  0, 2, 1, NULL, false, NULL,               295.0f,  0.0f, AUD_DSP_W, 0 },
	{ ROW_CHOICE,  "Sync",             AUD_SYNC_NAMES, 0, 3, 0, NULL, false, NULL,               295.0f,  0.0f, AUD_SYNC_W, 0 },
	{ ROW_BUTTONS, NULL,               NULL,           0, 1, 0, NULL, false, AUD_ADV_ACTIONS,    0.0f,    0.0f, NULL,      0 },
};

/*  The Settings Saves tab (menupage 5). Rows are 70 apart (rowDy). The state: SD device, card 1 Off,
 *  card 2 Shared, Auto Save Yes, State to SD. Labels are textboxes 18, 23, 19 and 20. */
static const char *const SAV_YESNO_NAMES[] = { "Yes", "No" };
static const float        SAV_YESNO_W[]    = { 75.0f, 75.0f };
static const char *const SAV_STATE_NAMES[] = { "SD", "USB" };
static const float        SAV_STATE_W[]    = { 55.0f, 70.0f };
static const ActionButton SAV_DEVICE_ACTIONS[] = { { "SD", 55.0f, true }, { "USB", 70.0f, false }, { "CardA", 90.0f, false }, { "CardB", 90.0f, false } };
static const ActionButton SAV_CARD_ACTIONS[]   = { { "Off", 105.0f, false }, { "Shared", 105.0f, false } };

/* Row fields: kind, label, names, lo, count, value, text, fit, actions, x0, gap, widths, plain. */
static Row SAVES_ROWS[] = {
	{ ROW_BUTTONS, "Save Memcards To", NULL,             0, 4, 0, NULL, false, SAV_DEVICE_ACTIONS, 295.0f,  0.0f, NULL,      0 },
	{ ROW_BUTTONS, "Memcard Type",        NULL,             0, 2, 0, NULL, false, SAV_CARD_ACTIONS,   323.0f, 38.0f, NULL,      0 },
	{ ROW_CHOICE,  "Memcard Autosave",  SAV_YESNO_NAMES,  0, 2, 0, NULL, false, NULL,               295.0f,  0.0f, SAV_YESNO_W, 0 },
	{ ROW_CHOICE,  "Save States Device",  SAV_STATE_NAMES,  0, 2, 0, NULL, false, NULL,               295.0f,  0.0f, SAV_STATE_W, 0 },
};

/* The General tab is the first Settings tab, so its tab row is lit at 0; Video's is lit at 1. */
static TestPage SETTINGS_PAGES[] = {
	{ "General",     NULL,                true,  0, GENERAL_ROWS, COUNT(GENERAL_ROWS), NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
	{ "Video",       NULL,                true,  1, VIDEO_ROWS,   COUNT(VIDEO_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
	{ "Input",       NULL,                true,  2, SET_INPUT_ROWS,   COUNT(SET_INPUT_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f },
	{ "Audio",       NULL,                true,  3, AUDIO_ROWS,   COUNT(AUDIO_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f, 70.0f },
	{ "Saves",       NULL,                true,  4, SAVES_ROWS,   COUNT(SAVES_ROWS),   NULL,        0,
	  GEN_TABS, COUNT(GEN_TABS), 150.0f, 28.0f, 70.0f },
};
#define NSETTINGS COUNT(SETTINGS_PAGES)

/* The tab row of a page: its own, or the Advanced row. */
static const Tab *tabsOf(const TestPage &t)  { return t.tabs ? t.tabs : OPT_TABS; }
static int        nTabsOf(const TestPage &t) { return t.tabs ? t.nTabs : (int)COUNT(OPT_TABS); }

static Page toPage(const TestPage &t, FocusMode mode)
{
	Page p;
	memset(&p, 0, sizeof p);	/* rowY0 and the other fields the table does not give: zero, as a static page is */
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
	p.rowDy = t.rowDy;
	p.columns = t.columns;
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

/* The row pitch of a page: its own, or the Settings grid's 60. */
static float rowPitch(const TestPage &t)
{
	return t.rowDy > 0 ? t.rowDy : 60.0f;
}

/* Returns the number of expected controls. Also sets *defaultFocus. */
static int buildExpected(const TestPage &t, Exp *e, Key *defaultFocus)
{
	int n = 0;
	int firstRow = -1;

	if (t.tabbed) {
		for (int k = 0; k < nTabsOf(t); k++) {
			n = addExpButton(e, n, ROLE_TAB, -1, k, TAB_X_LITERAL[k], 30.0f, tabsOf(t)[k].width, 56.0f,
			                 tabsOf(t)[k].text, k == t.curTab);
		}
	} else if (t.title) {
		n = addExpText(e, n, ROLE_TITLE, -1, 0, 320.0f, 44.0f, ALIGN_CENTRE, 1.0f, t.title);
	}

	for (int i = 0; i < t.nRows; i++) {
		const Row &r = t.rows[i];
		const float y = 100.0f + i * rowPitch(t);
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
		y = 100.0f + t.nRows * rowPitch(t);
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

		y = 100.0f + t.nRows * rowPitch(t);
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
 * looked at the change on a Wii (Docs/MENU_PLAN.md, "Decided").
 * Matched by page, problem, and the text of the control (and of the second one, if any).
 * Any finding not listed here is a failure. Every Advanced page lays out with zero problems.
 * The General and Video tabs' findings are the labels the engine's fit rule (16 px a side, plus the font's
 * own +5) calls wider than their Settings buttons: the Settings buttons are narrower than that
 * rule, and the labels still sit inside them with a few pixels to spare (Execute BIOS, 171 px, has
 * a 161 px label). Listed, not changed (Docs/MENU_PLAN.md, "Decided"). The sentinel
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
	{ "Video", PROB_TEXT_WIDE, "Interlaced", NULL },
	{ "Video", PROB_TEXT_WIDE, "240p", NULL },
	{ "Video", PROB_TEXT_WIDE, "Default", NULL },
	{ "Video", PROB_TEXT_WIDE, "Trap", NULL },
	{ "Video", PROB_TEXT_WIDE, "Deflicker", NULL },
	{ "Video", PROB_TEXT_WIDE, "Advanced", NULL },
	/* The Filters row sits on the button column, so its last button reaches the logo's box by a few pixels. */
	{ "Video", PROB_IN_LOGO, "Deflicker", NULL },
	/* The Input tab: the same fit-rule findings on the Settings buttons (the tab row's labels too), and
	 * the label "PSX Pad Type" reaching 1 px into "Standard". */
	{ "Input", PROB_TEXT_WIDE, "General", NULL },
	{ "Input", PROB_TEXT_WIDE, "Video", NULL },
	{ "Input", PROB_TEXT_WIDE, "Audio", NULL },
	{ "Input", PROB_TEXT_WIDE, "Saves", NULL },
	{ "Input", PROB_TEXT_WIDE, "Configure Buttons", NULL },
	{ "Input", PROB_TEXT_WIDE, "Digital", NULL },
	{ "Input", PROB_TEXT_WIDE, "Analog", NULL },
	{ "Input", PROB_TEXT_WIDE, "Stick Pad", NULL },
	{ "Input", PROB_TEXT_WIDE, "Yes", NULL },
	{ "Input", PROB_TEXT_WIDE, "SD", NULL },
	{ "Input", PROB_TEXT_WIDE, "USB", NULL },
	{ "Input", PROB_OVERLAP, "PSX Pad Type", "Digital" },
	{ "Audio", PROB_TEXT_WIDE, "General", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Video", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Audio", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Saves", NULL },
	{ "Audio", PROB_TEXT_WIDE, "DSP", NULL },
	{ "Audio", PROB_TEXT_WIDE, "CPU", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Off", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Tempo", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Rate", NULL },
	{ "Audio", PROB_TEXT_WIDE, "Advanced", NULL },
	{ "Saves", PROB_TEXT_WIDE, "General", NULL },
	{ "Saves", PROB_TEXT_WIDE, "Video", NULL },
	{ "Saves", PROB_TEXT_WIDE, "Audio", NULL },
	{ "Saves", PROB_TEXT_WIDE, "Saves", NULL },
	{ "Saves", PROB_TEXT_WIDE, "SD", NULL },
	{ "Saves", PROB_TEXT_WIDE, "USB", NULL },
	{ "Saves", PROB_TEXT_WIDE, "CardA", NULL },
	{ "Saves", PROB_TEXT_WIDE, "CardB", NULL },
	{ "Saves", PROB_TEXT_WIDE, "Shared", NULL },
	{ "Saves", PROB_TEXT_WIDE, "Yes", NULL },
	/* The list pages (the file browser): "Prev" and "Next" are 56 and 59 px in the font, on 70 px
	 * buttons with 16 px of padding a side. The browser has always drawn them so; the look is kept. */
	{ "List", PROB_TEXT_WIDE, "Prev", NULL },
	{ "List", PROB_TEXT_WIDE, "Next", NULL },
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
		/* A two-column page (the CD tab) has no old OptionsFrame arithmetic to re-derive: its grid is
		 * testTwoColumns()'s. It still has to lay out clear, with two-way links, below. */
		if (t.columns < 2) {
			expectLinks(t, EXP, nExp);
			compareLayout(t.name, lay, EXP, nExp);
		}
		linkFocus(page, lay);
		if (t.columns < 2)
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
 * GENERAL) sets on the five tabs: Down to 73 (Advanced), Up to 80 (Load Settings: SD). */
struct OldLink {
	int idx, up, down, left, right;
};
static const OldLink OLD_GENERAL[] = {
	{ 0, 80, 73, 4, 1 }, { 1, 80, 73, 0, 2 }, { 2, 80, 73, 1, 3 }, { 3, 80, 73, 2, 4 }, { 4, 80, 73, 3, 0 },
	{ 7, 73, 11, 10, 8 }, { 8, 73, 11, 7, 9 }, { 9, 75, 12, 8, 10 }, { 10, 75, 12, 9, 7 },
	{ 11, 7, 54, 13, 12 }, { 12, 8, 54, 11, 13 }, { 13, 9, 54, 12, 11 },
	{ 14, 54, 80, 62, 15 }, { 15, 54, 81, 14, 62 }, { 62, 54, 82, 15, 14 },
	{ 80, 14, 0, 82, 81 }, { 81, 15, 0, 80, 82 }, { 82, 62, 0, 81, 80 },
	{ 54, 11, 14, -1, -1 },
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
	if (idx == 14 || idx == 15 || idx == 62) {
		k.role = ROLE_BUTTON; k.row = 4; k.slot = idx == 14 ? 0 : idx == 15 ? 1 : 2;
		return true;
	}
	if (idx >= 80 && idx <= 82) { k.role = ROLE_BUTTON; k.row = 5; k.slot = idx - 80; return true; }
	if (idx == 73 || idx == 75) { k.role = ROLE_BUTTON; k.row = 0; k.slot = idx == 73 ? 0 : 1; return true; }
	return false;
}

static int frameIndexOf(Key want)
{
	for (int i = 0; i < 83; i++) {
		Key k;
		if (frameKey(i, k) && sameKey(k, want))
			return i;
	}
	return -1;
}

/* The General tab's boxes and label anchors as SettingsFrame.cpp's tables write them, read from
 * the numbers themselves (not from the rules above), so that a rule that is wrong fails here. */
static const struct { int idx; float x, y, w, h; } OLD_RECTS[] = {
	{ 0, 25, 30, 110, 56 }, { 1, 155, 30, 100, 56 }, { 2, 275, 30, 100, 56 },
	{ 3, 395, 30, 100, 56 }, { 4, 515, 30, 100, 56 },
	{ 7, 295, 160, 70, 56 }, { 8, 375, 160, 55, 56 }, { 9, 440, 160, 70, 56 }, { 10, 520, 160, 70, 56 },
	{ 11, 295, 220, 75, 56 }, { 12, 380, 220, 75, 56 }, { 13, 465, 220, 171, 56 },
	{ 14, 253, 340, 55, 56 }, { 15, 318, 340, 70, 56 }, { 62, 398, 340, 140, 56 },
	{ 80, 253, 400, 55, 56 }, { 81, 318, 400, 70, 56 }, { 82, 398, 400, 140, 56 },
	{ 54, 295, 280, 90, 56 },
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
 * one is a finding for the port's report (Docs/MENU_PLAN.md), and the look and the
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
	{ 0, 25, 30, 110, 56 }, { 1, 155, 30, 100, 56 }, { 2, 275, 30, 100, 56 },
	{ 3, 395, 30, 100, 56 }, { 4, 515, 30, 100, 56 },
	{ 16, 295, 100, 75, 56 }, { 17, 380, 100, 75, 56 },
	{ 18, 295, 160, 130, 56 }, { 63, 435, 160, 160, 56 },
	{ 20, 295, 220, 75, 56 }, { 21, 380, 220, 75, 56 },
	{ 22, 295, 280, 130, 56 }, { 23, 435, 280, 125, 56 }, { 57, 570, 280, 66, 56 },
	{ 24, 485, 340, 110, 56 }, { 28, 295, 340, 95, 56 }, { 29, 400, 340, 75, 56 },
	{ 78, 255, 400, 130, 56 },
};
/* The labels: textboxes 4, 5, 6, 7 and 9 on the rows 0..4, centred at x 150 on the row's middle. */
static const float VIDEO_OLD_LABEL_Y[5] = { 128.0f, 188.0f, 248.0f, 308.0f, 368.0f };

/* The focus links SettingsFrame's FRAME_BUTTONS table gives the Video tab's buttons (by FRAME_BUTTONS
 * index: up, down, left, right), with the tabs' Down (16) and Up (26, a hidden Dithering button) that
 * activateSubmenu(SUBMENU_VIDEO) sets. */
static const OldLink OLD_VIDEO[] = {
	{ 0, 26, 16, 4, 1 }, { 1, 26, 16, 0, 2 }, { 2, 26, 16, 1, 3 }, { 3, 26, 16, 2, 4 }, { 4, 26, 16, 3, 0 },
	{ 16, 1, 18, 17, 17 }, { 17, 1, 19, 16, 16 },
	{ 18, 16, 20, 63, 63 }, { 63, 17, 21, 18, 18 },
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
	if (idx == 18)              { k.role = ROLE_BUTTON; k.row = 1; k.slot = 0; return true; }
	if (idx == 63)              { k.role = ROLE_BUTTON; k.row = 1; k.slot = 1; return true; }
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
	{ 0, ROLE_TAB, -1, 0, 25.0f, 30.0f, 110.0f, 56.0f, 38, 30, 4, 1 },
	{ 1, ROLE_TAB, -1, 1, 155.0f, 30.0f, 100.0f, 56.0f, 38, 30, 0, 2 },
	{ 2, ROLE_TAB, -1, 2, 275.0f, 30.0f, 100.0f, 56.0f, 38, 30, 1, 3 },
	{ 3, ROLE_TAB, -1, 3, 395.0f, 30.0f, 100.0f, 56.0f, 38, 30, 2, 4 },
	{ 4, ROLE_TAB, -1, 4, 515.0f, 30.0f, 100.0f, 56.0f, 38, 30, 3, 0 },
	{ 30, ROLE_BUTTON, 0, 0,  85.0f, 100.0f, 220.0f, 56.0f,  2, 32, 31, 31 },
	{ 31, ROLE_BUTTON, 0, 1, 320.0f, 100.0f, 235.0f, 56.0f,  2, 32, 30, 30 },
	{ 32, ROLE_CHOICE, 1, 0, 252.0f, 160.0f, 117.0f, 56.0f, 30, 59, 79, 33 },
	{ 33, ROLE_CHOICE, 1, 1, 379.0f, 160.0f,  95.0f, 56.0f, 31, 59, 32, 79 },
	{ 79, ROLE_CHOICE, 1, 2, 484.0f, 160.0f, 152.0f, 56.0f, 31, 59, 33, 32 },
	{ 59, ROLE_BUTTON, 2, 0, 295.0f, 220.0f, 135.0f, 56.0f, 32, 34, -1, -1 },
	{ 34, ROLE_CHOICE, 3, 0, 295.0f, 280.0f,  75.0f, 56.0f, 59, 36, 35, 35 },
	{ 35, ROLE_CHOICE, 3, 1, 380.0f, 280.0f,  75.0f, 56.0f, 59, 37, 34, 34 },
	{ 36, ROLE_BUTTON, 4, 0, 295.0f, 340.0f,  55.0f, 56.0f, 34, 38, 37, 37 },
	{ 37, ROLE_BUTTON, 4, 1, 360.0f, 340.0f,  70.0f, 56.0f, 35, 38, 36, 36 },
	{ 38, ROLE_BUTTON, 5, 0, 295.0f, 400.0f, 135.0f, 56.0f, 36,  2, -1, -1 },
};
/* The label centres: each row's top + 28 (textboxes 10, 29, 11, 12 and 13). */
static const float INPUT_LABEL_Y[6] = { 0.0f, 188.0f, 248.0f, 308.0f, 368.0f, 428.0f };

/* The Audio tab: its tab row's Up is 72 and Down 39. */
static const OldBox AUDIO_BOXES[] = {
	{ 0, ROLE_TAB, -1, 0, 25.0f, 30.0f, 110.0f, 56.0f, 72, 39, 4, 1 },
	{ 1, ROLE_TAB, -1, 1, 155.0f, 30.0f, 100.0f, 56.0f, 72, 39, 0, 2 },
	{ 2, ROLE_TAB, -1, 2, 275.0f, 30.0f, 100.0f, 56.0f, 72, 39, 1, 3 },
	{ 3, ROLE_TAB, -1, 3, 395.0f, 30.0f, 100.0f, 56.0f, 72, 39, 2, 4 },
	{ 4, ROLE_TAB, -1, 4, 515.0f, 30.0f, 100.0f, 56.0f, 72, 39, 3, 0 },
	{ 39, ROLE_BUTTON, 0, 0, 295.0f, 100.0f, 100.0f, 56.0f,  3, 45, 41, 40 },
	{ 40, ROLE_BUTTON, 0, 1, 405.0f, 100.0f,  75.0f, 56.0f,  3, 45, 39, 41 },
	{ 41, ROLE_BUTTON, 0, 2, 490.0f, 100.0f, 110.0f, 56.0f,  3, 45, 40, 39 },
	{ 45, ROLE_BUTTON, 1, 0, 295.0f, 170.0f, 170.0f, 56.0f, 39, 67, -1, -1 },
	{ 67, ROLE_CHOICE, 2, 0, 295.0f, 240.0f,  75.0f, 56.0f, 45, 69, 68, 68 },
	{ 68, ROLE_CHOICE, 2, 1, 380.0f, 240.0f,  75.0f, 56.0f, 45, 69, 67, 67 },
	{ 69, ROLE_CHOICE, 3, 0, 295.0f, 310.0f,  75.0f, 56.0f, 67, 72, 71, 70 },
	{ 70, ROLE_CHOICE, 3, 1, 380.0f, 310.0f,  90.0f, 56.0f, 67, 72, 69, 71 },
	{ 71, ROLE_CHOICE, 3, 2, 480.0f, 310.0f,  75.0f, 56.0f, 68, 72, 70, 69 },
	{ 72, ROLE_BUTTON, 4, 0, 255.0f, 380.0f, 130.0f, 56.0f, 69,  3, -1, -1 },
};
/* The label centres: row top + 28 (textboxes 14, 17, 25 and 26). */
static const float AUDIO_LABEL_Y[5] = { 128.0f, 198.0f, 268.0f, 338.0f, 0.0f };

/* The Saves tab: its tab row's Up is 52 and Down 46. */
static const OldBox SAVES_BOXES[] = {
	{ 0, ROLE_TAB, -1, 0, 25.0f, 30.0f, 110.0f, 56.0f, 52, 46, 4, 1 },
	{ 1, ROLE_TAB, -1, 1, 155.0f, 30.0f, 100.0f, 56.0f, 52, 46, 0, 2 },
	{ 2, ROLE_TAB, -1, 2, 275.0f, 30.0f, 100.0f, 56.0f, 52, 46, 1, 3 },
	{ 3, ROLE_TAB, -1, 3, 395.0f, 30.0f, 100.0f, 56.0f, 52, 46, 2, 4 },
	{ 4, ROLE_TAB, -1, 4, 515.0f, 30.0f, 100.0f, 56.0f, 52, 46, 3, 0 },
	{ 46, ROLE_BUTTON, 0, 0, 295.0f, 100.0f,  55.0f, 56.0f,  4, 76, 49, 47 },
	{ 47, ROLE_BUTTON, 0, 1, 360.0f, 100.0f,  70.0f, 56.0f,  4, 77, 46, 48 },
	{ 48, ROLE_BUTTON, 0, 2, 440.0f, 100.0f,  90.0f, 56.0f,  4, 77, 47, 49 },
	{ 49, ROLE_BUTTON, 0, 3, 540.0f, 100.0f,  90.0f, 56.0f,  4, 77, 48, 46 },
	{ 76, ROLE_BUTTON, 1, 0, 323.0f, 170.0f, 105.0f, 56.0f, 46, 50, 77, 77 },
	{ 77, ROLE_BUTTON, 1, 1, 466.0f, 170.0f, 105.0f, 56.0f, 47, 51, 76, 76 },
	{ 50, ROLE_CHOICE, 2, 0, 295.0f, 240.0f,  75.0f, 56.0f, 76, 52, 51, 51 },
	{ 51, ROLE_CHOICE, 2, 1, 380.0f, 240.0f,  75.0f, 56.0f, 77, 53, 50, 50 },
	{ 52, ROLE_CHOICE, 3, 0, 295.0f, 310.0f,  55.0f, 56.0f, 50,  4, 53, 53 },
	{ 53, ROLE_CHOICE, 3, 1, 360.0f, 310.0f,  70.0f, 56.0f, 51,  4, 52, 52 },
};
/* The label centres: row top + 28 (textboxes 18, 23, 19 and 20). */
static const float SAVES_LABEL_Y[4] = { 128.0f, 198.0f, 268.0f, 338.0f };

/*  Inactive buttons are never focus targets  */

/* Counts the links that point at a control which is not focusable. */
static int linksToInactive(const Layout &lay)
{
	int n = 0;
	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++) {
			const int t = lay.c[i].link[d];
			if (t >= 0 && !lay.c[t].focusable)
				n++;
		}
	return n;
}

/* The control of a row's slot, or -1. */
static int findButton(const Layout &lay, int row, int slot)
{
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].row == row && lay.c[i].slot == slot &&
		    (lay.c[i].role == ROLE_BUTTON || lay.c[i].role == ROLE_CHOICE))
			return i;
	return -1;
}

static void testInactiveNeverFocus(void)
{
	static const char *const THREE[] = { "On", "Off", "Auto" };
	static const char *const TWO[]   = { "Off", "On" };
	static const ActionButton ACT[]  = { {"Save", 100, false}, {"Load", 100, false}, {"This Game", 140, false} };
	static Row rows[4];
	static Layout lay;
	static Page page;
	static const FocusMode modes[] = { FOCUS_ROWS, FOCUS_GEOMETRY };
	static const char *const modeName[] = { "rows", "geometry" };

	for (int m = 0; m < 2; m++) {
		char where[64];
		Problem probs[MAX_PROBLEMS];
		int np;

		memset(rows, 0, sizeof rows);
		/* Row 0: Save (slot 0) inactive. Row 1: the middle choice inactive. Row 2: its only
		 * button inactive, so the whole row is out of the links. Row 3: all active. */
		rows[0].kind = ROW_BUTTONS;  rows[0].actions = ACT; rows[0].count = 3; rows[0].inactive = 1u << 0;
		rows[1].kind = ROW_CHOICE;   rows[1].names = THREE; rows[1].count = 3; rows[1].value = 1;
		rows[1].inactive = 1u << 1;
		rows[2].kind = ROW_CYCLE;    rows[2].names = TWO;   rows[2].count = 2; rows[2].inactive = 1u << 0;
		rows[3].kind = ROW_TOGGLE;   rows[3].names = TWO;   rows[3].count = 2;

		memset(&page, 0, sizeof page);
		page.nRows = 4;
		page.rows = rows;
		page.logo = LOGO_SPOT_NONE;
		page.focus = modes[m];
		snprintf(where, sizeof where, "inactive/%s", modeName[m]);

		if (layoutPage(page, kFont, lay) < 0) {
			fail(where, "the page does not lay out");
			continue;
		}
		linkFocus(page, lay);

		{
			const int save = findButton(lay, 0, 0), load = findButton(lay, 0, 1), game = findButton(lay, 0, 2);
			const int mid = findButton(lay, 1, 1), cycle = findButton(lay, 2, 0), toggle = findButton(lay, 3, 0);

			if (save < 0 || mid < 0 || cycle < 0 || load < 0 || game < 0 || toggle < 0) {
				fail(where, "a button is missing from the layout");
				continue;
			}
			if (lay.c[save].focusable || lay.c[mid].focusable || lay.c[cycle].focusable)
				fail(where, "an inactive button is focusable");
			if (!lay.c[load].focusable || !lay.c[game].focusable || !lay.c[toggle].focusable)
				fail(where, "an active button is not focusable");
			if (lay.defaultFocus != load)
				fail(where, "focus does not start on the first active button");
		}
		if (linksToInactive(lay) != 0)
			fail(where, "a focus link points at an inactive button");

		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
			if (probs[i].kind == PROB_BAD_LINK || probs[i].kind == PROB_UNREACHABLE) {
				fail(where, problemName(probs[i].kind));
				break;
			}
	}
}

/*  The button-row screens: Main, Load ROM and Current ROM (step 3), from their frames' tables  */

/* Each page is written as its frame builds it (MainFrame.cpp, LoadRomFrame.cpp, CurrentRomFrame.cpp):
 * the rows of action buttons, the first row's top, the pitch and the logo. Their boxes and their
 * focus are checked against the numbers the frames had before the port: the boxes as their tables
 * gave them, the links as their FRAME_BUTTONS tables gave them. */

/* Main: seven rows of one button, 200 wide at x 315, the first at y 36 and then 60 apart. */
static const ActionButton MAIN_ACTIONS[] = {
	{ "Load ISO", 200.0f, false }, { "Current ISO", 200.0f, false }, { "Settings", 200.0f, false },
	{ "Players", 200.0f, false },  { "Credits", 200.0f, false },     { "Quit", 200.0f, false },
	{ "Play Game", 200.0f, false },
};
static Row MAIN_ROWS[] = {
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[0], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[1], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[2], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[3], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[4], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[5], 315.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &MAIN_ACTIONS[6], 315.0f, 0.0f, NULL, 0 },
};

/* Load ROM: four rows of one button, 340 wide, centred (x 150), the rows 80 apart from y 100. */
static const ActionButton LOADROM_ACTIONS[] = {
	{ "Load from SD", 340.0f, false }, { "Load from DVD", 340.0f, false },
	{ "Load from USB", 340.0f, false }, { "Load from Samba", 340.0f, false },
};
static Row LOADROM_ROWS[] = {
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &LOADROM_ACTIONS[0], 0.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &LOADROM_ACTIONS[1], 0.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &LOADROM_ACTIONS[2], 0.0f, 0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, &LOADROM_ACTIONS[3], 0.0f, 0.0f, NULL, 0 },
};

/* Current ROM: two columns (x 100 and x 330, 210 wide, 20 apart) on the first two rows, then rows
 * of one, two (Load State and Slot, the Slot 30 lower than its row: dy) and one. Rows 60 apart
 * from y 60. The logo is bottom right (MenuContext's LOGO_BOTTOM_FRAMES). */
static const ActionButton CUR_R0[] = { { "Restart Game", 210.0f, false }, { "Swap CD", 210.0f, false } };
static const ActionButton CUR_R1[] = { { "Load MemCards", 210.0f, false }, { "Save MemCards", 210.0f, false } };
static const ActionButton CUR_R2[] = { { "Show ISO Info", 340.0f, false } };
static const ActionButton CUR_R3[] = { { "Load State", 220.0f, false }, { "Slot 0", 120.0f, false, 30.0f } };
static const ActionButton CUR_R4[] = { { "Save State", 220.0f, false } };
static Row CURRENT_ROWS[] = {
	{ ROW_BUTTONS, NULL, NULL, 0, 2, 0, NULL, false, CUR_R0, 100.0f, 20.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 2, 0, NULL, false, CUR_R1, 100.0f, 20.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, CUR_R2, 150.0f,  0.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 2, 0, NULL, false, CUR_R3, 150.0f, 20.0f, NULL, 0 },
	{ ROW_BUTTONS, NULL, NULL, 0, 1, 0, NULL, false, CUR_R4, 150.0f,  0.0f, NULL, 0 },
};

/* The page a screen's rows make. */
static Page screenPage(Row *rows, int nRows, LogoSpot logo, float rowY0, float rowDy)
{
	Page p;
	memset(&p, 0, sizeof p);
	p.rows = rows;
	p.nRows = nRows;
	p.logo = logo;
	p.focus = FOCUS_ROWS;
	p.rowY0 = rowY0;
	p.rowDy = rowDy;
	return p;
}

/* A frame's table entry: which button (row, slot), its box as the table gave it, and its links by
 * table index (up, down, left, right; -1 for none). */
struct ScreenBox { int idx, row, slot; float x, y, w, h; };
struct ScreenLink { int idx, up, down, left, right; };

static int boxIdxAt(const ScreenBox *b, unsigned n, int row, int slot)
{
	for (unsigned k = 0; k < n; k++)
		if (b[k].row == row && b[k].slot == slot)
			return b[k].idx;
	return -1;
}

static const ScreenBox MAIN_BOXES[] = {
	{ 0, 0, 0, 315.0f,  36.0f, 200.0f, 56.0f }, { 1, 1, 0, 315.0f,  96.0f, 200.0f, 56.0f }, { 2, 2, 0, 315.0f, 156.0f, 200.0f, 56.0f }, { 3, 3, 0, 315.0f, 216.0f, 200.0f, 56.0f }, { 4, 4, 0, 315.0f, 276.0f, 200.0f, 56.0f }, { 5, 5, 0, 315.0f, 336.0f, 200.0f, 56.0f }, { 6, 6, 0, 315.0f, 396.0f, 200.0f, 56.0f },
};
static const ScreenLink MAIN_LINKS[] = {
	{ 0, 6, 1, -1, -1 }, { 1, 0, 2, -1, -1 }, { 2, 1, 3, -1, -1 }, { 3, 2, 4, -1, -1 }, { 4, 3, 5, -1, -1 }, { 5, 4, 6, -1, -1 }, { 6, 5, 0, -1, -1 },
};
static const ScreenBox LOADROM_BOXES[] = {
	{ 0, 0, 0, 150.0f, 100.0f, 340.0f, 56.0f }, { 1, 1, 0, 150.0f, 180.0f, 340.0f, 56.0f },
	{ 2, 2, 0, 150.0f, 260.0f, 340.0f, 56.0f }, { 3, 3, 0, 150.0f, 340.0f, 340.0f, 56.0f },
};
static const ScreenLink LOADROM_LINKS[] = {
	{ 0, 3, 1, -1, -1 }, { 1, 0, 2, -1, -1 }, { 2, 1, 3, -1, -1 }, { 3, 2, 0, -1, -1 },
};
static const ScreenBox CURRENT_BOXES[] = {
	{ 0, 0, 0, 100.0f,  60.0f, 210.0f, 56.0f }, { 1, 0, 1, 330.0f,  60.0f, 210.0f, 56.0f },
	{ 2, 1, 0, 100.0f, 120.0f, 210.0f, 56.0f }, { 3, 1, 1, 330.0f, 120.0f, 210.0f, 56.0f },
	{ 4, 2, 0, 150.0f, 180.0f, 340.0f, 56.0f }, { 5, 3, 0, 150.0f, 240.0f, 220.0f, 56.0f },
	{ 6, 4, 0, 150.0f, 300.0f, 220.0f, 56.0f }, { 7, 3, 1, 390.0f, 270.0f, 120.0f, 56.0f },
};
static const ScreenLink CURRENT_LINKS[] = {
	{ 0, 6, 2, 1, 1 }, { 1, 7, 3, 0, 0 }, { 2, 0, 4, 3, 3 }, { 3, 1, 4, 2, 2 },
	{ 4, 2, 5, -1, -1 }, { 5, 4, 6, 7, 7 }, { 6, 5, 0, 7, 7 }, { 7, 4, 1, 5, 5 },
};
/* The four links the rule does not give, as CurrentRomFrame.cpp's CUR_LINKS sets them. */
static const LinkOverride CURRENT_OVERRIDES[] = {
	{ 0, 1, DIR_UP,    3, 1 }, { 3, 1, DIR_DOWN,  0, 1 },
	{ 4, 0, DIR_LEFT,  3, 1 }, { 4, 0, DIR_RIGHT, 3, 1 },
};

/* Checks one screen: its boxes against the table (a failure), its links against the table (printed,
 * a finding; a failure when linksExact), and checkLayout's problems (a failure). Returns the failures. */
static int checkScreen(const char *name, const Page &page, const ScreenBox *boxes, unsigned nb,
                       const ScreenLink *links, unsigned nl, bool linksExact)
{
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };
	Layout lay;
	Problem probs[16];
	int bad = 0, diffs = 0;

	if (layoutPage(page, kFont, lay) < 0) {
		fail(name, "layoutPage refused the page");
		return 1;
	}
	linkFocus(page, lay);

	for (unsigned k = 0; k < nb; k++) {
		const int at = lay.slot[boxes[k].row][boxes[k].slot];
		char msg[160];
		if (at < 0) {
			fail(name, "a table button is not in the layout");
			bad++;
			continue;
		}
		{
			const Rect &r = lay.c[at].rect;
			if (!nearly(r.x, boxes[k].x) || !nearly(r.y, boxes[k].y) ||
			    !nearly(r.w, boxes[k].w) || !nearly(r.h, boxes[k].h)) {
				snprintf(msg, sizeof msg, "button %d at (%g,%g %gx%g), the table has (%g,%g %gx%g)",
				         boxes[k].idx, r.x, r.y, r.w, r.h, boxes[k].x, boxes[k].y, boxes[k].w, boxes[k].h);
				fail(name, msg);
				bad++;
			}
		}
	}

	for (unsigned k = 0; k < nl; k++) {
		const ScreenLink &o = links[k];
		const int want[DIR_COUNT] = { o.up, o.down, o.left, o.right };
		for (unsigned b = 0; b < nb; b++) {
			if (boxes[b].idx != o.idx)
				continue;
			const int from = lay.slot[boxes[b].row][boxes[b].slot];
			for (int d = 0; d < DIR_COUNT; d++) {
				const int got = lay.c[from].link[d];
				const int gotIdx = got < 0 ? -1 : boxIdxAt(boxes, nb, lay.c[got].row, lay.c[got].slot);
				if (gotIdx != want[d]) {
					diffs++;
					if (linksExact) {
						char msg[160];
						snprintf(msg, sizeof msg, "button %d %s links to %d, the table has %d",
						         o.idx, DIR[d], gotIdx, want[d]);
						fail(name, msg);
						bad++;
					} else {
						printf("        %-10s button %d %-5s table %2d, engine %2d\n",
						       name, o.idx, DIR[d], want[d], gotIdx);
					}
				}
			}
		}
	}

	/* checkLayout: a label wider than its button minus the 16 px each side is a finding, printed
	 * and kept (the look is the frame's, and the label fits the drawn button); anything else fails. */
	{
		const int n = checkLayout(page, kFont, lay, probs, 16);
		for (int k = 0; k < n && k < 16; k++) {
			const int a = probs[k].a;
			char msg[160];
			if (probs[k].kind == PROB_TEXT_WIDE) {
				printf("        %-10s finding: '%s' is wider than its button less 2 x 16 px\n",
				       name, lay.c[a].text);
				continue;
			}
			snprintf(msg, sizeof msg, "checkLayout: %s ('%s')", problemName(probs[k].kind), lay.c[a].text);
			fail(name, msg);
			bad++;
		}
	}
	if (lay.defaultFocus < 0 || lay.defaultFocus != lay.slot[boxes[0].row][boxes[0].slot]) {
		fail(name, "the default focus is not the first button");
		bad++;
	}
	if (linksExact)
		printf("  %-10s %zu buttons against the table, every link as the table has it\n", name, (size_t)nb);
	else
		printf("  %-10s %zu buttons against the table, %d link%s differ from the table (a finding, not a failure)\n",
		       name, (size_t)nb, diffs, diffs == 1 ? "" : "s");
	return bad;
}

/*  Link overrides (Page::overrides), and the D-pad reach of Current ROM  */

/* The controls a D-pad walk from the default focus reaches, by following the links. Returns the
 * count; reached[i] is set for each control reached. */
static int reachFromDefault(const Layout &lay, bool *reached)
{
	int queue[MAX_CONTROLS], qh = 0, qt = 0;

	memset(reached, 0, sizeof(bool) * MAX_CONTROLS);
	if (lay.defaultFocus < 0 || lay.defaultFocus >= lay.n || !lay.c[lay.defaultFocus].focusable)
		return 0;
	reached[lay.defaultFocus] = true;
	queue[qt++] = lay.defaultFocus;
	while (qh < qt) {
		const int u = queue[qh++];
		for (int d = 0; d < DIR_COUNT; d++) {
			const int v = lay.c[u].link[d];
			if (v >= 0 && v < lay.n && lay.c[v].focusable && !reached[v]) {
				reached[v] = true;
				queue[qt++] = v;
			}
		}
	}
	return qt;
}

/* Every button of a screen must be reached from its default focus by the D-pad links. */
static int checkDpadReach(const char *name, const Page &page, const ScreenBox *boxes, unsigned nb)
{
	static bool reached[MAX_CONTROLS];
	Layout lay;
	int bad = 0;

	if (layoutPage(page, kFont, lay) < 0) {
		fail(name, "layoutPage refused the page");
		return 1;
	}
	linkFocus(page, lay);
	{
		const int n = reachFromDefault(lay, reached);
		for (unsigned k = 0; k < nb; k++) {
			const int at = lay.slot[boxes[k].row][boxes[k].slot];
			if (at < 0 || !reached[at]) {
				char msg[120];
				snprintf(msg, sizeof msg, "button %d cannot be reached by the D-pad", boxes[k].idx);
				fail(name, msg);
				bad++;
			}
		}
		printf("  %-10s a D-pad walk from the default focus reaches %d of %zu buttons\n",
		       name, n, (size_t)nb);
	}
	return bad;
}

/* Lays out a page and links it, with the page's overrides. Returns false if it does not lay out. */
static bool layOutLinked(const Page &page, Layout &lay, const char *where)
{
	if (layoutPage(page, kFont, lay) < 0) {
		fail(where, "the page does not lay out");
		return false;
	}
	linkFocus(page, lay);
	return true;
}

/* Three rows of buttons and a readout, one row with an inactive button. The rule links first, then
 * the overrides are checked against it: one applies, and the others each name something that is
 * not a focus target (an inactive button, a readout, a row past the page, a slot past the row's
 * buttons, an inactive source) and must leave the rule's link alone. */
static void testLinkOverrides(void)
{
	static const ActionButton TOP[] = { { "A", 100.0f, false }, { "B", 100.0f, false } };
	static const ActionButton MID[] = { { "C", 100.0f, false }, { "D", 100.0f, false } };
	static const ActionButton LOW[] = { { "E", 100.0f, false } };
	static const LinkOverride OV[] = {
		{ 0, 1, DIR_DOWN,  3, 0 },		/* B down to E: applied */
		{ 0, 0, DIR_DOWN,  1, 1 },		/* A down to D, which is inactive: the rule's C stands */
		{ 0, 0, DIR_RIGHT, 2, 0 },		/* A right to the readout's row (no button): B stands */
		{ 0, 0, DIR_LEFT,  7, 0 },		/* A left to a row past the page: B stands */
		{ 3, 0, DIR_UP,    0, MAX_SLOTS },	/* E up to a slot past the row's buttons: C stands */
		{ 1, 1, DIR_UP,    3, 0 },		/* D up, from an inactive button: nothing to link */
	};
	static Row rows[4];
	static Layout lay;
	static bool reached[MAX_CONTROLS];
	Page page;
	Problem probs[MAX_PROBLEMS];
	const char *where = "overrides";
	int A, B, C, D, E, np;

	printf("Link overrides (Page::overrides)\n");
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].actions = TOP; rows[0].count = 2;
	rows[1].kind = ROW_BUTTONS; rows[1].actions = MID; rows[1].count = 2; rows[1].inactive = 1u << 1;
	rows[2].kind = ROW_READOUT; rows[2].label = "Readout"; rows[2].text = "0";
	rows[3].kind = ROW_BUTTONS; rows[3].actions = LOW; rows[3].count = 1;

	memset(&page, 0, sizeof page);
	page.rows = rows; page.nRows = 4; page.logo = LOGO_SPOT_NONE; page.focus = FOCUS_ROWS;

	/* The rule alone: A and B go down to C (D is inactive), C down to E, and E up to C. */
	if (!layOutLinked(page, lay, where))
		return;
	A = lay.slot[0][0]; B = lay.slot[0][1]; C = lay.slot[1][0]; D = lay.slot[1][1]; E = lay.slot[3][0];
	if (A < 0 || B < 0 || C < 0 || D < 0 || E < 0) {
		fail(where, "a button is missing from the layout");
		return;
	}
	if (lay.c[B].link[DIR_DOWN] != C || lay.c[A].link[DIR_DOWN] != C || lay.c[E].link[DIR_UP] != C)
		fail(where, "the rule's links are not the ones this test expects");

	page.overrides = OV;
	page.nOverrides = (int)COUNT(OV);
	if (!layOutLinked(page, lay, where))
		return;

	if (lay.c[B].link[DIR_DOWN] != E)
		fail(where, "an override that names two focus targets was not applied");
	if (lay.c[A].link[DIR_DOWN] != C)
		fail(where, "an override to an inactive button replaced the rule's link");
	if (lay.c[A].link[DIR_RIGHT] != B || lay.c[A].link[DIR_LEFT] != B)
		fail(where, "an override to a readout or a row past the page replaced the rule's link");
	if (lay.c[E].link[DIR_UP] != C)
		fail(where, "an override to a slot past the row's buttons replaced the rule's link");
	if (lay.c[D].link[DIR_UP] != -1 || lay.c[D].link[DIR_DOWN] != -1 ||
	    lay.c[D].link[DIR_LEFT] != -1 || lay.c[D].link[DIR_RIGHT] != -1)
		fail(where, "an override from an inactive button set a link");
	if (linksToInactive(lay) != 0)
		fail(where, "a focus link points at an inactive button");
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
		if (probs[i].kind == PROB_BAD_LINK || probs[i].kind == PROB_UNREACHABLE) {
			fail(where, problemName(probs[i].kind));
			break;
		}
	if (reachFromDefault(lay, reached) != 4 || !reached[A] || !reached[B] || !reached[C] || !reached[E])
		fail(where, "the overridden page is not reachable from the default focus");

	/* A NULL array is ignored, whatever the count says: the page keeps the rule's links. */
	page.overrides = NULL;
	page.nOverrides = 3;
	if (!layOutLinked(page, lay, where))
		return;
	if (lay.c[B].link[DIR_DOWN] != C)
		fail(where, "a NULL override array was not ignored");
	printf("  overrides: one applied; inactive, readout, out-of-range and inactive-source ones fall back\n");
}

static int checkButtonRowScreens(void)
{
	int bad = 0;
	const Page main    = screenPage(MAIN_ROWS, (int)COUNT(MAIN_ROWS), LOGO_SPOT_TOP, 36.0f, 0.0f);
	const Page loadrom = screenPage(LOADROM_ROWS, (int)COUNT(LOADROM_ROWS), LOGO_SPOT_TOP, 0.0f, 80.0f);
	Page current = screenPage(CURRENT_ROWS, (int)COUNT(CURRENT_ROWS), LOGO_SPOT_BOTTOM, 60.0f, 0.0f);
	current.overrides = CURRENT_OVERRIDES;
	current.nOverrides = (int)COUNT(CURRENT_OVERRIDES);

	printf("Button-row screens (boxes against the frames' tables, focus against their links)\n");
	bad += checkScreen("Main", main, MAIN_BOXES, COUNT(MAIN_BOXES), MAIN_LINKS, COUNT(MAIN_LINKS), false);
	bad += checkScreen("LoadRom", loadrom, LOADROM_BOXES, COUNT(LOADROM_BOXES), LOADROM_LINKS, COUNT(LOADROM_LINKS), false);
	bad += checkScreen("CurrentRom", current, CURRENT_BOXES, COUNT(CURRENT_BOXES), CURRENT_LINKS, COUNT(CURRENT_LINKS), true);
	bad += checkDpadReach("CurrentRom", current, CURRENT_BOXES, COUNT(CURRENT_BOXES));
	return bad;
}

/*  Lists (ROW_LIST, step 4): a window of 10 over 5,000 items  */

/* The item names, made once: the callback hands out pointers into this, as the file browser
 * hands out its directory entries. */
static char listNames[5000][16];
static int  listCalls;

static void listItemFn(void *, int index, ListItem &out)
{
	listCalls++;
	out.text = listNames[index];
	out.folder = (index % 7) == 0;		/* every seventh item is a folder */
}

static const ListButton LIST_PREV = { "Prev",  35.0f, 220.0f, 70.0f, 40.0f };
static const ListButton LIST_NEXT = { "Next", 535.0f, 220.0f, 70.0f, 40.0f };

/* The file browser's geometry: items at x 120, 400 wide, 35 high, 40 apart from y 40. */
static ListSpec listSpec(ListState *st, ListMode mode)
{
	ListSpec sp;
	memset(&sp, 0, sizeof sp);
	sp.state = st;
	sp.mode = mode;
	sp.item = listItemFn;
	sp.ctx = NULL;
	sp.x0 = 120.0f; sp.y0 = 40.0f;
	sp.w = 400.0f; sp.h = 35.0f; sp.pitch = 40.0f;
	sp.prev = LIST_PREV;
	sp.next = LIST_NEXT;
	return sp;
}

/* A page of one list row, and nothing else. */
static void listPageOf(Page &page, Row &row, const ListSpec &spec)
{
	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_TOP;
	page.focus = FOCUS_ROWS;
}

/* The window's item i, as a control index, or -1. */
static int listItemCtl(const Layout &lay, int k)
{
	return (k >= 0 && k < MAX_LIST_SLOTS) ? lay.listItem[k] : -1;
}

static bool checkListWindow(const char *where, const Layout &lay, const ListState &st)
{
	bool ok = true;
	for (int k = 0; k < st.slots; k++) {
		const int c = listItemCtl(lay, k);
		const int index = st.top + k;
		if (c < 0) {
			fail(where, "a window slot has no control");
			return false;
		}
		if (index < st.count) {
			if (!lay.c[c].text || strcmp(lay.c[c].text, listNames[index]) != 0) {
				fail(where, "a shown item has the wrong text");
				ok = false;
			}
			if (!lay.c[c].focusable || lay.c[c].folder != (index % 7 == 0)) {
				fail(where, "a shown item is not an active button, or its folder flag is wrong");
				ok = false;
			}
		} else {
			if (lay.c[c].text[0] != '\0' || lay.c[c].focusable || lay.c[c].folder) {
				fail(where, "a slot past the end of the list is not an empty inactive button");
				ok = false;
			}
		}
		if (!nearly(lay.c[c].rect.x, 120.0f) || !nearly(lay.c[c].rect.w, 400.0f) ||
		    !nearly(lay.c[c].rect.y, 40.0f + 40.0f * k) || !nearly(lay.c[c].rect.h, 35.0f)) {
			fail(where, "an item's box is not the file browser's");
			ok = false;
			break;
		}
	}
	return ok;
}

/* Lays a list page out and links it, after the state has changed. Returns false if it does not lay out. */
static bool layOutList(const Page &page, Layout &lay, const char *where)
{
	listCalls = 0;
	if (layoutPage(page, kFont, lay) < 0) {
		fail(where, "the list page does not lay out");
		return false;
	}
	linkFocus(page, lay);
	if (listCalls > page.rows[0].list->state->slots) {
		fail(where, "the layout asked for more items than the window shows");
		return false;
	}
	return true;
}

/* Checks the window's focus links and the window itself after a layout. */
static void checkListLinks(const char *where, const Layout &lay, const ListState &st, bool paged)
{
	int A[MAX_LIST_SLOTS], n = 0;
	const int prev = lay.slot[0][0], next = lay.slot[0][1];

	for (int k = 0; k < st.slots; k++) {
		const int c = listItemCtl(lay, k);
		if (c >= 0 && lay.c[c].focusable)
			A[n++] = c;
	}
	if (linksToInactive(lay) != 0)
		fail(where, "a focus link points at an inactive control");
	for (int j = 0; j < n; j++) {
		const Control &c = lay.c[A[j]];
		if (paged && n > 1) {
			if (c.link[DIR_UP] != A[(j + n - 1) % n] || c.link[DIR_DOWN] != A[(j + 1) % n])
				fail(where, "a paged window does not wrap up and down round its shown items");
		} else {
			if (c.link[DIR_UP] != (j > 0 ? A[j - 1] : -1) || c.link[DIR_DOWN] != (j + 1 < n ? A[j + 1] : -1))
				fail(where, "a scrolling window does not stop at its ends");
		}
		if (c.link[DIR_LEFT] != (prev >= 0 && lay.c[prev].focusable ? prev : -1) ||
		    c.link[DIR_RIGHT] != (next >= 0 && lay.c[next].focusable ? next : -1))
			fail(where, "an item does not go to Prev and Next");
	}
}

static void testListWindow(void)
{
	static ListState st;
	static ListSpec spec;
	static Row row;
	static Page page;
	static Layout lay;
	static bool reached[MAX_CONTROLS];
	const char *where = "list";

	printf("Lists (a window of 10 over 5,000 items)\n");
	for (int i = 0; i < 5000; i++)
		snprintf(listNames[i], sizeof listNames[i], "file%04d", i);

	/* The first page: ten items, Next active, Prev inactive, the default focus on item 0. */
	st.count = 5000; st.slots = 10; st.top = 0; st.focus = -1;
	spec = listSpec(&st, LIST_PAGED);
	listPageOf(page, row, spec);
	if (!layOutList(page, lay, where))
		return;
	if (listCalls != 10)
		fail(where, "the layout did not ask for exactly the window's items");
	if (!checkListWindow(where, lay, st))
		return;
	if (lay.list != 0 || lay.slot[0][0] < 0 || lay.slot[0][1] < 0)
		fail(where, "the Prev and Next buttons are missing");
	if (lay.c[lay.slot[0][0]].focusable || !lay.c[lay.slot[0][1]].focusable)
		fail(where, "on the first page, Prev is active or Next is not");
	if (lay.defaultFocus != lay.listItem[0])
		fail(where, "the default focus is not the first item");
	if (lay.c[lay.slot[0][0]].link[DIR_RIGHT] != lay.listItem[0])
		fail(where, "an inactive Prev does not keep its link to the first item");
	checkListLinks(where, lay, st, true);
	if (reachFromDefault(lay, reached) != 11)
		fail(where, "the first page's focus does not reach its ten items and Next");
	if (auditLayout(page, lay, "List", -1, true) != 0)
		fail(where, "checkLayout reports a problem with the first page that is not a known one");

	/* Paging with Prev and Next: clamped at both ends, a page at a time. */
	listTurn(st, -1, false);
	if (st.top != 0 || st.focus != 0)
		fail(where, "Prev on the first page moved");
	listTurn(st, 1, false);
	if (st.top != 10 || st.focus != 10)
		fail(where, "Next did not turn to the second page, or focus did not land on its first item");
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;
	if (!lay.c[lay.slot[0][0]].focusable)
		fail(where, "Prev is not active on the second page");
	checkListLinks(where, lay, st, true);

	/* The last page is full (4,990..4,999): Next is inactive there, and Next clamps. */
	for (int i = 0; i < 499; i++)
		listTurn(st, 1, false);
	if (st.top != 4990)
		fail(where, "paging forward did not reach the last full page");
	listTurn(st, 1, false);
	if (st.top != 4990)
		fail(where, "Next on the last page moved");
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;
	if (lay.c[lay.slot[0][1]].focusable || !lay.c[lay.slot[0][0]].focusable)
		fail(where, "on the last page, Next is active or Prev is not");
	/* Focus can stay on a page button that has just gone inactive (the browser's Next, clicked on
	 * the last page): its links stay, so Left still goes to the first item, and Right is none. */
	if (lay.c[lay.slot[0][1]].link[DIR_LEFT] != listItemCtl(lay, 0) ||
	    lay.c[lay.slot[0][1]].link[DIR_RIGHT] != -1)
		fail(where, "an inactive Next lost its links: focus left on it would be stuck");

	/* L and R wrap (listTurn with wrap): from the last page to the first, and back. */
	listTurn(st, 1, true);
	if (st.top != 0 || st.focus != 0)
		fail(where, "R on the last page did not wrap to the first");
	listTurn(st, -1, true);
	if (st.top != 4990 || st.focus != 4990)
		fail(where, "L on the first page did not wrap to the last");

	/* A partial last page (5,005 items: 5,000..5,004 shown, five empty slots). The empty slots
	 * are inactive and the window's links skip them, and the ring is the five shown items. */
	st.count = 5005; st.top = 5000; st.focus = 5000;
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;
	checkListLinks(where, lay, st, true);
	{
		const int c4 = listItemCtl(lay, 4), c0 = listItemCtl(lay, 0);
		if (lay.c[c4].link[DIR_DOWN] != c0)
			fail(where, "the last shown item does not wrap to the first shown item");
	}
	if (reachFromDefault(lay, reached) != 6)
		fail(where, "a partial last page does not reach its five items and Prev");
	listTurn(st, 1, false);
	if (st.top != 5000 || st.count != 5005)
		fail(where, "a partial last page turned past its end");
	st.count = 5000; st.top = 0; st.focus = -1;

	/* Focus inside the window: a focus in the window is the default focus; one outside it is not. */
	st.top = 10; st.focus = 13;
	if (!layOutList(page, lay, where))
		return;
	if (lay.defaultFocus != listItemCtl(lay, 3))
		fail(where, "a focus inside the window is not the default focus");
	st.focus = 99;
	if (!layOutList(page, lay, where))
		return;
	if (lay.defaultFocus != listItemCtl(lay, 0))
		fail(where, "a focus outside the window does not fall back to the first shown item");
	st.top = 0; st.focus = -1;

	/* Errors: a window too small or too large, two lists on one page, a missing state. */
	{
		ListState bad = st;
		ListSpec badSpec;
		Row badRow;
		Page badPage;
		Layout badLay;

		bad.slots = 0;
		badSpec = listSpec(&bad, LIST_PAGED);
		listPageOf(badPage, badRow, badSpec);
		if (layoutPage(badPage, kFont, badLay) >= 0)
			fail(where, "a list with no window was laid out");
		bad.slots = MAX_LIST_SLOTS + 1;
		badSpec = listSpec(&bad, LIST_PAGED);
		listPageOf(badPage, badRow, badSpec);
		if (layoutPage(badPage, kFont, badLay) >= 0)
			fail(where, "a list window past MAX_LIST_SLOTS was laid out");
	}
	{
		Row two[2];
		Page twoPage;
		memset(two, 0, sizeof two);
		two[0].kind = ROW_LIST; two[0].list = &spec;
		two[1].kind = ROW_LIST; two[1].list = &spec;
		memset(&twoPage, 0, sizeof twoPage);
		twoPage.rows = two; twoPage.nRows = 2; twoPage.logo = LOGO_SPOT_TOP;
		if (layoutPage(twoPage, kFont, lay) >= 0)
			fail(where, "two lists on one page were laid out");
	}
	{
		Row nostate;
		ListSpec nospec = spec;
		Page nopage;
		nospec.state = NULL;
		memset(&nostate, 0, sizeof nostate);
		nostate.kind = ROW_LIST; nostate.list = &nospec;
		memset(&nopage, 0, sizeof nopage);
		nopage.rows = &nostate; nopage.nRows = 1;
		if (layoutPage(nopage, kFont, lay) >= 0)
			fail(where, "a list with no state was laid out");
	}

	/* An empty list lays out with nothing focusable, and listTurn does nothing to it. */
	{
		ListState empty = { 0, 10, 0, -1 };
		ListSpec es = listSpec(&empty, LIST_PAGED);
		Row er;
		Page ep;
		listPageOf(ep, er, es);
		if (!layOutList(ep, lay, where))
			return;
		if (lay.defaultFocus != -1 || lay.c[lay.slot[0][1]].focusable || lay.c[lay.slot[0][0]].focusable)
			fail(where, "an empty list has something to focus");
		listTurn(empty, 1, true);
		if (empty.top != 0)
			fail(where, "listTurn moved an empty list");
	}
}

static void testListScroll(void)
{
	static ListState st;
	static ListSpec spec;
	static Row row;
	static Page page;
	static Layout lay;
	static bool reached[MAX_CONTROLS];
	const char *where = "list scroll";

	printf("Lists, LIST_SCROLL (focus off an end scrolls the window)\n");
	st.count = 5000; st.slots = 10; st.top = 0; st.focus = 0;
	spec = listSpec(&st, LIST_SCROLL);
	listPageOf(page, row, spec);
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;
	checkListLinks(where, lay, st, false);
	if (lay.c[lay.slot[0][0]].focusable || lay.c[lay.slot[0][1]].focusable)
		fail(where, "Prev or Next is active in a scrolling list");
	if (reachFromDefault(lay, reached) != 10)
		fail(where, "the scrolling window's focus does not reach its ten items");
	if (auditLayout(page, lay, "List", -1, true) != 0)
		fail(where, "checkLayout reports a problem with the scrolling window that is not a known one");

	/* Inside the window, focus moves and the window stays. */
	for (int i = 0; i < 9; i++)
		if (listScroll(st, 1)) {
			fail(where, "the window scrolled while focus was still inside it");
			break;
		}
	if (st.focus != 9 || st.top != 0)
		fail(where, "focus did not reach the window's last item without scrolling");

	/* One step more leaves the window: it scrolls by one item and focus is its last item. */
	if (!listScroll(st, 1) || st.focus != 10 || st.top != 1)
		fail(where, "focus leaving the window's end did not scroll it by one item");
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;
	if (lay.defaultFocus != listItemCtl(lay, 9))
		fail(where, "after scrolling, focus is not on the window's last item");

	/* To the bottom of the list, then clamped there. */
	while (st.focus < 4999)
		listScroll(st, 1);
	if (st.focus != 4999 || st.top != 4990)
		fail(where, "scrolling to the end did not stop at the last item with the window at the end");
	if (listScroll(st, 1) || st.focus != 4999 || st.top != 4990)
		fail(where, "focus past the last item moved, or the window scrolled");
	if (!layOutList(page, lay, where))
		return;
	if (!checkListWindow(where, lay, st))
		return;

	/* Back up: inside the window nothing scrolls; past its top, the window follows. */
	listScroll(st, -1);
	if (st.focus != 4998 || st.top != 4990)
		fail(where, "moving up inside the window scrolled it");
	while (st.focus > 4990)
		listScroll(st, -1);
	if (st.focus != 4990 || st.top != 4990)
		fail(where, "moving up to the window's first item scrolled it");
	if (!listScroll(st, -1) || st.focus != 4989 || st.top != 4989)
		fail(where, "focus leaving the window's top did not scroll it up by one item");
	while (st.focus > 0)
		listScroll(st, -1);
	if (st.focus != 0 || st.top != 0)
		fail(where, "scrolling back to the first item did not stop at the top");
	if (listScroll(st, -1) || st.focus != 0 || st.top != 0)
		fail(where, "focus above the first item moved, or the window scrolled");

	/* A list shorter than its window never scrolls. */
	{
		ListState shortSt = { 4, 10, 0, 0 };
		int moved = 0;
		for (int i = 0; i < 9; i++)
			if (listScroll(shortSt, 1))
				moved++;
		if (moved != 0 || shortSt.focus != 3 || shortSt.top != 0)
			fail(where, "a list shorter than its window scrolled, or focus passed its last item");
	}

	printf("  scrolling: focus leaving either end of the window scrolls it by one item; the ends clamp\n");
}

static void testListPaging(void)
{
	/* The browser's own turn rule, written out: Prev and Next clamp, L and R wrap, and a page is
	 * ten items. Checked against the arithmetic the browser has always used (current_page,
	 * max_page). */
	static const int COUNTS[] = { 1, 10, 11, 25, 5000, 5005 };
	int bad = 0;

	printf("Lists, page turns against the browser's own arithmetic\n");
	for (unsigned c = 0; c < sizeof COUNTS / sizeof COUNTS[0]; c++) {
		const int maxPage = (COUNTS[c] + 9) / 10;
		for (int start = 0; start < maxPage; start++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				for (int wrap = 0; wrap <= 1; wrap++) {
					ListState s = { COUNTS[c], 10, start * 10, -1 };
					int want, got;
					if (wrap)
						want = ((start + dir) % maxPage + maxPage) % maxPage;
					else
						want = start + dir < 0 ? 0 : (start + dir >= maxPage ? maxPage - 1 : start + dir);
					listTurn(s, dir, wrap != 0);
					got = s.top / 10;
					if (got != want || s.top % 10 != 0 || s.focus != s.top) {
						bad++;
						if (bad <= 5)
							printf("  FAIL  list      count %d page %d dir %d wrap %d: went to %d, want %d\n",
							       COUNTS[c], start, dir, wrap, got, want);
					}
				}
			}
		}
	}
	if (bad)
		fail("list", "page turns differ from the browser's arithmetic (see above)");
	printf("  page turns: %s over %u counts, every page, both directions, wrap and clamp\n",
	       bad ? "MISMATCHES" : "all as the browser", (unsigned)(sizeof COUNTS / sizeof COUNTS[0]));
}

/*  Customize Co-Op (step 5): 24 free buttons and geometry focus, from CustomizeCoopFrame.cpp's tables  */

/* The buttons as CustomizeCoopFrame.cpp had them: six presets, two rows of three (190 x 34, at
 * x 20 + 205 x column, y 12 then 50), and the 18 controller buttons (indicatorRect). Their names are
 * coop.c's, as the frame shows them. */
static const float COOP_IND[18][4] = {
	{140, 130,  80, 40}, {140, 180,  80, 40}, {235, 140,  80, 40}, {325, 140,  80, 40},
	{420, 130,  80, 40}, {420, 180,  80, 40}, { 75, 230,  80, 40}, { 30, 280,  80, 40},
	{120, 280,  80, 40}, { 75, 330,  80, 40}, {485, 230,  80, 40}, {440, 280,  80, 40},
	{530, 280,  80, 40}, {485, 330,  80, 40}, {160, 345, 100, 40}, {380, 345, 100, 40},
	{235, 395,  80, 40}, {325, 395,  80, 40} };
static const char *const COOP_TEXT[24] = {
	"Full pad", "Left half", "Right half", "D-pad+sticks", "Action", "Shoulders",
	"L1", "L2", "Select", "Start", "R1", "R2", "Up", "Left", "Right", "Down",
	"Tri", "Squ", "Cir", "Cro", "L Stick", "R Stick", "L3", "R3" };

static void coopTable(int i, float *x, float *y, float *w, float *h)
{
	if (i < 6) {
		*x = 20.0f + 205.0f * (i % 3);
		*y = i < 3 ? 12.0f : 50.0f;
		*w = 190.0f;
		*h = 34.0f;
	} else {
		const float *r = COOP_IND[i - 6];
		*x = r[0]; *y = r[1]; *w = r[2]; *h = r[3];
	}
}

/* The page as CustomizeCoopFrame builds it: free buttons, geometry focus, the title at y 112, no logo. */
static Page coopPageOf(FreeButton *fb)
{
	Page p;
	memset(&p, 0, sizeof p);
	for (int i = 0; i < 24; i++) {
		coopTable(i, &fb[i].x, &fb[i].y, &fb[i].w, &fb[i].h);
		fb[i].text = COOP_TEXT[i];
		fb[i].lit = false;
		fb[i].inactive = false;
	}
	p.title = "PSX Port 1, player 1: GameCube pad 1";
	p.titleY = 112.0f;
	p.logo = LOGO_SPOT_NONE;
	p.focus = FOCUS_GEOMETRY;
	p.freeButtons = fb;
	p.nFreeButtons = 24;
	return p;
}

/* CustomizeCoopFrame's constructor as it was: each button's nearest other button in each direction,
 * scored as along + 2 x the sideways offset, a button one pixel or less behind not a candidate.
 * Written out again here, in the engine's direction order (up, down, left, right); -1 is none. */
static void coopOldLinks(int want[24][DIR_COUNT])
{
	float cx[24], cy[24];
	for (int j = 0; j < 24; j++) {
		float x, y, w, h;
		coopTable(j, &x, &y, &w, &h);
		cx[j] = x + w / 2;
		cy[j] = y + h / 2;
	}
	for (int i = 0; i < 24; i++)
		for (int d = 0; d < DIR_COUNT; d++) {
			int best = -1;
			float bestCost = 1e9f;
			for (int j = 0; j < 24; j++) {
				const float dx = cx[j] - cx[i], dy = cy[j] - cy[i];
				float along, side;
				if (d == DIR_LEFT)       { along = -dx; side = dy; }
				else if (d == DIR_RIGHT) { along = dx;  side = dy; }
				else if (d == DIR_UP)    { along = -dy; side = dx; }
				else                     { along = dy;  side = dx; }
				if (j == i || along <= 1.0f)
					continue;
				{
					const float cost = along + 2.0f * (side < 0 ? -side : side);
					if (cost < bestCost) { bestCost = cost; best = j; }
				}
			}
			want[i][d] = best;
		}
}

static int checkCoopPage(void)
{
	FreeButton fb[24];
	Page p = coopPageOf(fb);
	Layout lay;
	Problem probs[16];
	int want[24][DIR_COUNT];
	int bad = 0, diffs = 0;
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };

	printf("Customize Co-Op (24 free buttons, geometry focus, the title, no logo)\n");
	if (layoutPage(p, kFont, lay) < 0) {
		fail("CustomizeCoop", "layoutPage refused the page");
		return 1;
	}
	linkFocus(p, lay);

	for (int i = 0; i < 24; i++) {
		const int c = lay.freeCtl[i];
		float x, y, w, h;
		char msg[160];
		coopTable(i, &x, &y, &w, &h);
		if (c < 0 || lay.c[c].role != ROLE_FREE || !lay.c[c].focusable) {
			snprintf(msg, sizeof msg, "free button %d is not a focusable free control", i);
			fail("CustomizeCoop", msg);
			bad++;
			continue;
		}
		if (!nearly(lay.c[c].rect.x, x) || !nearly(lay.c[c].rect.y, y) ||
		    !nearly(lay.c[c].rect.w, w) || !nearly(lay.c[c].rect.h, h)) {
			snprintf(msg, sizeof msg, "button %d at (%g,%g %gx%g), the frame has (%g,%g %gx%g)", i,
			         lay.c[c].rect.x, lay.c[c].rect.y, lay.c[c].rect.w, lay.c[c].rect.h, x, y, w, h);
			fail("CustomizeCoop", msg);
			bad++;
		}
	}

	/* The title: centred on x 320, its anchor at y 112 */
	if (lay.title < 0 || !nearly(lay.c[lay.title].ax, 320.0f) || !nearly(lay.c[lay.title].ay, 112.0f) ||
	    lay.c[lay.title].align != ALIGN_CENTRE) {
		fail("CustomizeCoop", "the title is not centred at (320, 112)");
		bad++;
	}

	/* The links, against the frame's own rule, direction by direction */
	coopOldLinks(want);
	for (int i = 0; i < 24; i++) {
		const int c = lay.freeCtl[i];
		for (int d = 0; d < DIR_COUNT; d++) {
			const int got = lay.c[c].link[d];
			const int gotIdx = got < 0 ? -1 : lay.c[got].slot;	/* a free control's slot is its button */
			if (gotIdx != want[i][d]) {
				char msg[160];
				diffs++;
				snprintf(msg, sizeof msg, "button %d %s links to %d, the frame's rule gives %d",
				         i, DIR[d], gotIdx, want[i][d]);
				fail("CustomizeCoop", msg);
				bad++;
			}
		}
	}

	if (lay.defaultFocus != lay.freeCtl[0]) {
		fail("CustomizeCoop", "the default focus is not Full pad");
		bad++;
	}

	{
		const int n = checkLayout(p, kFont, lay, probs, 16);
		for (int k = 0; k < n && k < 16; k++) {
			char msg[160];
			const int a = probs[k].a;
			if (probs[k].kind == PROB_TEXT_WIDE) {
				printf("        CustomizeCoop finding: '%s' is wider than its button less 2 x 16 px\n",
				       lay.c[a].text);
				continue;
			}
			snprintf(msg, sizeof msg, "checkLayout: %s ('%s')", problemName(probs[k].kind), lay.c[a].text);
			fail("CustomizeCoop", msg);
			bad++;
		}
	}
	printf("  CustomizeCoop  24 buttons against the frame's rectangles, %d link%s differ from the frame's rule\n",
	       diffs, diffs == 1 ? "" : "s");
	return bad;
}

/*  Configure Input (step 5): free buttons and free links, from ConfigureInputFrame.cpp's rectangles and links  */

/* The buttons, by FRAME_BUTTONS index. 0 Automatic and 1 Manual; 2 and 3 the ports' types; 12 and 13 their
 * numbers; 4..11 and 14..21 the multitap slots of port 1 and 2; 22..69 the Co-Op rows (COOP_BTN). The
 * rectangles are layoutTables()'s: columns at x 8 and 324, the port row at y 114, the slots from 206 in
 * steps of 62, the Co-Op players from 206 in steps of 33. Automatic and Manual are as wide as their
 * labels (buttonFitWidth), TAB_GAP apart from x 295. */
static void ciRect(int id, float *x, float *y, float *w, float *h)
{
	const float slotX = 17.0f;		/* (308 - (204 + 10 + 60)) / 2 */
	if (id == 0) { *x = 295.0f; *y = 20.0f; *w = (float)buttonFitWidth(kFont, "Automatic"); *h = 56.0f; return; }
	if (id == 1) { *x = 295.0f + (float)buttonFitWidth(kFont, "Automatic") + 10.0f; *y = 20.0f;
	               *w = (float)buttonFitWidth(kFont, "Manual"); *h = 56.0f; return; }
	if (id == 2 || id == 3) { *x = (id == 2 ? 8.0f : 324.0f) + slotX; *y = 114.0f; *w = 204.0f; *h = 56.0f; return; }
	if (id == 12 || id == 13) { *x = (id == 12 ? 8.0f : 324.0f) + slotX + 204.0f + 10.0f; *y = 114.0f; *w = 60.0f; *h = 56.0f; return; }
	if (id >= 4 && id <= 21) {
		const int i = id <= 11 ? id - 2 : id - 12;		/* 2 + p*4 + sl */
		const int p = (i - 2) / 4, sl = (i - 2) % 4;
		*x = (p ? 324.0f : 8.0f) + slotX + (id >= 14 ? 214.0f : 0.0f);
		*y = 206.0f + 62.0f * sl;
		*w = id >= 14 ? 60.0f : 204.0f;
		*h = 56.0f;
		return;
	}
	{
		const int c = (id - 22) % 3, k = ((id - 22) / 3) % 8, p = (id - 22) / 24;
		static const float coopX[3] = { 32.0f, 104.0f, 256.0f }, coopW[3] = { 66.0f, 146.0f, 52.0f };
		*x = (p ? 324.0f : 8.0f) + coopX[c];
		*y = 206.0f + 33.0f * k;
		*w = coopW[c];
		*h = 30.0f;
	}
}

/* The shown buttons of a state, in FRAME_BUTTONS order, each with its links to ids (-1: none) in the
 * engine's direction order (up, down, left, right). */
struct CiState {
	const char *name;
	int shown[8];			/* ids; -1 ends */
	int active[8];			/* 1 if active */
	int lit;				/* id lit, or -1 */
	int links[8][4];		/* per shown id, the links the frame gives it */
};

/* Automatic (the default): Automatic and Manual active; the ports' buttons are shown and greyed. The frame
 * links Automatic and Manual to each other sideways only. */
static const CiState CI_AUTO = {
	"Automatic", { 0, 1, 2, 3, 12, 13, -1 }, { 1, 1, 0, 0, 0, 0, -1 }, 0,
	{ { -1, -1, 1, 1 }, { -1, -1, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

/* Manual with no port types: the ports' type and number are focus targets, linked as the frame's
 * activateSubmenu links them (lastCol0 2, lastCol1 13, r0 12, r1 13, no multitap or Co-Op rows). */
static const CiState CI_MANUAL = {
	"Manual", { 0, 1, 2, 3, 12, 13, -1 }, { 1, 1, 1, 1, 1, 1, -1 }, 1,
	{ { 2, 2, 1, 1 }, { 13, 3, 0, 0 }, { 0, 0, 13, 12 }, { 1, 1, 12, 13 }, { 0, 0, 2, 3 }, { 1, 1, 3, 2 } },
};

/* Builds the state's page as the frame does: its shown buttons as free buttons, its links as free links. */
static int checkConfigureInputState(const CiState &s)
{
	FreeButton fb[8];
	FreeLink fl[8 * 4];
	int n = 0, nl = 0, bad = 0, diffs = 0;
	int freeOf[70];
	Page p;
	Layout lay;
	Problem probs[16];
	char where[64];
	static const char *const DIR[DIR_COUNT] = { "Up", "Down", "Left", "Right" };

	snprintf(where, sizeof where, "ConfigureInput %s", s.name);
	for (int i = 0; i < 70; i++)
		freeOf[i] = -1;
	for (int k = 0; s.shown[k] >= 0; k++) {
		const int id = s.shown[k];
		ciRect(id, &fb[n].x, &fb[n].y, &fb[n].w, &fb[n].h);
		fb[n].text = "";
		fb[n].lit = (s.lit == id);
		fb[n].inactive = !s.active[k];
		freeOf[id] = n++;
	}
	for (int k = 0; s.shown[k] >= 0; k++)
		for (int d = 0; d < DIR_COUNT; d++) {
			const int to = s.links[k][d];
			if (to < 0 || freeOf[to] < 0) {
				if (to >= 0) { fail(where, "a link to a button the state does not show"); bad++; }
				continue;
			}
			fl[nl].from = freeOf[s.shown[k]];
			fl[nl].dir = (Dir)d;
			fl[nl].to = freeOf[to];
			nl++;
		}
	/* no link where the state gives none: a to of -1 */
	for (int k = 0; s.shown[k] >= 0; k++)
		for (int d = 0; d < DIR_COUNT; d++)
			if (s.links[k][d] < 0) {
				fl[nl].from = freeOf[s.shown[k]];
				fl[nl].dir = (Dir)d;
				fl[nl].to = -1;
				nl++;
			}

	memset(&p, 0, sizeof p);
	p.logo = LOGO_SPOT_NONE;
	p.focus = FOCUS_GEOMETRY;
	p.freeButtons = fb;
	p.nFreeButtons = n;
	p.freeLinks = fl;
	p.nFreeLinks = nl;
	if (layoutPage(p, kFont, lay) < 0) {
		fail(where, "layoutPage refused the page");
		return bad + 1;
	}
	linkFocus(p, lay);

	for (int k = 0; s.shown[k] >= 0; k++) {
		const int id = s.shown[k];
		const int c = lay.freeCtl[freeOf[id]];
		float x, y, w, h;
		char msg[160];
		ciRect(id, &x, &y, &w, &h);
		if (c < 0 || !nearly(lay.c[c].rect.x, x) || !nearly(lay.c[c].rect.y, y) ||
		    !nearly(lay.c[c].rect.w, w) || !nearly(lay.c[c].rect.h, h)) {
			snprintf(msg, sizeof msg, "button %d is not where the table puts it", id);
			fail(where, msg);
			bad++;
			continue;
		}
		if (lay.c[c].focusable != (s.active[k] != 0)) {
			snprintf(msg, sizeof msg, "button %d focus target is %d, the state has it %s", id,
			         (int)lay.c[c].focusable, s.active[k] ? "active" : "inactive");
			fail(where, msg);
			bad++;
		}
		for (int d = 0; d < DIR_COUNT; d++) {
			const int got = lay.c[c].link[d];
			if (s.active[k] == 0)
				continue;
			{
				int gotIdx = -1;
				for (int q = 0; q < n && got >= 0; q++)
					if (lay.freeCtl[q] == got) { gotIdx = q; break; }
				{
					const int want = s.links[k][d];
					const int wantIdx = want < 0 ? -1 : freeOf[want];
					if (gotIdx != wantIdx) {
						char msg2[160];
						diffs++;
						snprintf(msg2, sizeof msg2, "button %d %s links to free %d, the state has free %d",
						         id, DIR[d], gotIdx, wantIdx);
						fail(where, msg2);
						bad++;
					}
				}
			}
		}
	}
	if (lay.defaultFocus != lay.freeCtl[0]) {
		fail(where, "the default focus is not Automatic or the first shown button");
		bad++;
	}
	{
		const int np = checkLayout(p, kFont, lay, probs, 16);
		for (int k = 0; k < np && k < 16; k++) {
			char msg[160];
			if (probs[k].kind == PROB_TEXT_WIDE) {
				printf("        %s finding: '%s' wider than its button less 2 x 16 px\n", where, lay.c[probs[k].a].text);
				continue;
			}
			snprintf(msg, sizeof msg, "checkLayout: %s", problemName(probs[k].kind));
			fail(where, msg);
			bad++;
		}
	}
	printf("  %-22s %d shown buttons against the frame's rectangles and links, %d link%s differ\n",
	       where, n, diffs, diffs == 1 ? "" : "s");
	return bad;
}

static int checkConfigureInput(void)
{
	printf("Configure Input (free buttons, free links; the states the frame gives)\n");
	return checkConfigureInputState(CI_AUTO) + checkConfigureInputState(CI_MANUAL);
}

/*  Configure Buttons (step 5): the 30 buttons and 5 labels at ConfigureButtonsFrame.cpp's places  */

/* The table's rectangles, by FRAME_BUTTONS index (x, y, w, h), and the labels (x, y, scale, centred). */
static const float CB_RECT[30][4] = {
	{ 20, 20, 80, 40 }, { 110, 20, 100, 40 }, { 220, 20, 80, 40 }, { 310, 20, 70, 40 }, { 390, 20, 70, 40 },
	{ 15, 160, 110, 40 }, { 140, 130, 80, 40 }, { 140, 180, 80, 40 }, { 235, 140, 80, 40 }, { 325, 140, 80, 40 },
	{ 420, 130, 80, 40 }, { 420, 180, 80, 40 }, { 75, 230, 80, 40 }, { 30, 280, 80, 40 }, { 120, 280, 80, 40 },
	{ 75, 330, 80, 40 }, { 485, 230, 80, 40 }, { 440, 280, 80, 40 }, { 530, 280, 80, 40 }, { 485, 330, 80, 40 },
	{ 160, 345, 100, 40 }, { 380, 345, 100, 40 }, { 95, 395, 130, 40 }, { 415, 395, 130, 40 }, { 235, 395, 80, 40 },
	{ 325, 395, 80, 40 }, { 325, 340, 50, 40 }, { 265, 340, 50, 40 }, { 515, 160, 110, 40 }, { 480, 20, 140, 40 } };
static const float CB_LABEL[5][4] = {
	{ 320, 100, 1.0f, 1 }, { 70, 145, 0.9f, 1 }, { 318, 323, 0.9f, 1 }, { 565, 145, 0.9f, 1 }, { 520, 310, 0.9f, 1 } };

static int checkConfigureButtons(void)
{
	FreeButton fb[30];
	FreeText ft[5];
	Page p;
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int bad = 0, wide = 0;

	printf("Configure Buttons (30 buttons and 5 labels at the table's places)\n");
	for (int i = 0; i < 30; i++) {
		fb[i].text = "Button";
		fb[i].x = CB_RECT[i][0];
		fb[i].y = CB_RECT[i][1];
		fb[i].w = CB_RECT[i][2];
		fb[i].h = CB_RECT[i][3];
		fb[i].lit = false;
		fb[i].inactive = false;
	}
	for (int t = 0; t < 5; t++) {
		ft[t].text = "label";
		ft[t].ax = CB_LABEL[t][0];
		ft[t].ay = CB_LABEL[t][1];
		ft[t].scale = CB_LABEL[t][2];
		ft[t].centred = CB_LABEL[t][3] != 0;
	}
	memset(&p, 0, sizeof p);
	p.logo = LOGO_SPOT_NONE;
	p.focus = FOCUS_GEOMETRY;
	p.freeButtons = fb;
	p.nFreeButtons = 30;
	p.freeTexts = ft;
	p.nFreeTexts = 5;
	if (layoutPage(p, kFont, lay) < 0) {
		fail("ConfigureButtons", "layoutPage refused the page");
		return 1;
	}
	for (int i = 0; i < 30; i++) {
		const int c = lay.freeCtl[i];
		if (c < 0 || !nearly(lay.c[c].rect.x, CB_RECT[i][0]) || !nearly(lay.c[c].rect.y, CB_RECT[i][1]) ||
		    !nearly(lay.c[c].rect.w, CB_RECT[i][2]) || !nearly(lay.c[c].rect.h, CB_RECT[i][3])) {
			char msg[160];
			snprintf(msg, sizeof msg, "button %d is not at the table's place", i);
			fail("ConfigureButtons", msg);
			bad++;
		}
	}
	{
		int nt = 0;
		for (int k = 0; k < lay.n; k++) {
			if (lay.c[k].role != ROLE_FREE_TEXT)
				continue;
			nt++;
			if (!nearly(lay.c[k].ax, CB_LABEL[lay.c[k].slot][0]) || !nearly(lay.c[k].ay, CB_LABEL[lay.c[k].slot][1])) {
				fail("ConfigureButtons", "a label is not at its anchor");
				bad++;
			}
		}
		if (nt != 5) {
			fail("ConfigureButtons", "not all 5 labels are in the layout");
			bad++;
		}
	}
	/* Findings are printed, not failed: the table's labels sit as the frame has them. A control past an
	 * edge, or one button on another, is a failure. */
	{
		const int np = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
		for (int k = 0; k < np && k < MAX_PROBLEMS; k++) {
			const Problem &q = probs[k];
			const bool buttons = q.b >= 0 && lay.c[q.a].kind == CTRL_BUTTON && lay.c[q.b].kind == CTRL_BUTTON;
			if (q.kind == PROB_UNREACHABLE)
				continue;	/* the focus is the frame's table, not the engine's (see the frame) */
			if (q.kind == PROB_TEXT_WIDE) {
				wide++;
			} else if (q.kind == PROB_PAST_EDGE || (q.kind == PROB_OVERLAP && buttons)) {
				char msg[160];
				snprintf(msg, sizeof msg, "checkLayout: %s", problemName(q.kind));
				fail("ConfigureButtons", msg);
				bad++;
			} else {
				printf("        ConfigureButtons finding: %s ('%s', '%s')\n", problemName(q.kind),
				       lay.c[q.a].text, q.b >= 0 ? lay.c[q.b].text : "");
			}
		}
	}
	if (wide)
		printf("        ConfigureButtons finding: %d label%s wider than the button less 2 x 16 px (the table's look)\n",
		       wide, wide == 1 ? "" : "s");
	printf("  ConfigureButtons  30 buttons and 5 labels at the table's places\n");
	return bad;
}

/*  Edge wrap (Page::wrap)  */

/* Offset of control j from control i along direction d: positive when j lies on d's side. */
static float alongOf(const Layout &lay, int i, int j, int d)
{
	const Rect &A = lay.c[i].rect, &B = lay.c[j].rect;
	const float dx = (B.x + B.w / 2) - (A.x + A.w / 2), dy = (B.y + B.h / 2) - (A.y + A.h / 2);
	switch (d) {
	case DIR_UP:   return -dy;
	case DIR_DOWN: return dy;
	case DIR_LEFT: return -dx;
	default:       return dx;
	}
}

/* j shares a row (for Left and Right) or a column (for Up and Down) with i. */
static bool sharesLine(const Layout &lay, int i, int j, int d)
{
	const Rect &A = lay.c[i].rect, &B = lay.c[j].rect;
	if (d == DIR_LEFT || d == DIR_RIGHT)
		return B.y < A.y + A.h && A.y < B.y + B.h;
	return B.x < A.x + A.w && A.x < B.x + B.w;
}

/* The first wrap stage (the row or column) has a target: a focusable control on i's row or
 * column, beyond i on the side opposite d. */
static bool hasLineWrap(const Layout &lay, int i, int d)
{
	for (int j = 0; j < lay.n; j++)
		if (j != i && lay.c[j].focusable && sharesLine(lay, i, j, d) && alongOf(lay, i, j, d ^ 1) > 1.0f)
			return true;
	return false;
}

static void expectLink(int &bad, const char *where, bool ok, const char *what)
{
	if (!ok) {
		fail(where, what);
		bad++;
	}
}

/* Lays a page out with the wrap flag off and on. With it on, each link the rule made stays the same.
 * Each link the rule left out may now name a control on the same row or column. Returns the wrap count. */
static int wrapLinks(const char *where, Page p, Layout &off, Layout &on, int &bad)
{
	int n = 0;

	p.wrap = false;
	if (layoutPage(p, kFont, off) < 0) {
		fail(where, "the page does not lay out");
		bad++;
		return 0;
	}
	linkFocus(p, off);
	p.wrap = true;
	layoutPage(p, kFont, on);
	linkFocus(p, on);

	for (int i = 0; i < on.n; i++) {
		if (!on.c[i].focusable)
			continue;
		for (int d = 0; d < DIR_COUNT; d++) {
			const int was = off.c[i].link[d], now = on.c[i].link[d];
			const Rect &A = on.c[i].rect;
			if (was >= 0)
				expectLink(bad, where, now == was, "a link the rule made changed with wrap on");
			if (was < 0 && now >= 0) {
				const Rect &B = on.c[now].rect;
				const bool horizontal = d == DIR_LEFT || d == DIR_RIGHT;
				const bool sameLine = horizontal ? (B.y < A.y + A.h && A.y < B.y + B.h)
				                                 : (B.x < A.x + A.w && A.x < B.x + B.w);
				n++;
				/* The row or column first; with none on that side, the farthest control on that side. */
				expectLink(bad, where, on.c[now].focusable && alongOf(on, i, now, d ^ 1) > 1.0f &&
				           (sameLine || !hasLineWrap(on, i, d)),
				           "a wrap link is not a focusable control beyond the origin, on its row or column first");
			}
		}
	}
	return n;
}

static void testEdgeWrap(void)
{
	/* Row 0: A B C D. Row 1: E F, under A and B. In round 1, D is inactive. */
	static FreeButton fb[6];
	static const float at[6][2] = { {20, 20}, {150, 20}, {280, 20}, {410, 20}, {20, 200}, {150, 200} };
	static Layout off, on;
	Page p;
	int bad = 0;
	const char *where = "wrap";

	printf("Edge wrap (Page::wrap): a 6-button page with and without an inactive button, and Configure Buttons\n");
	for (int round = 0; round < 2; round++) {
		for (int i = 0; i < 6; i++) {
			fb[i].text = "Button";
			fb[i].x = at[i][0];
			fb[i].y = at[i][1];
			fb[i].w = 100;
			fb[i].h = 40;
			fb[i].lit = false;
			fb[i].inactive = round == 1 && i == 3;
		}
		memset(&p, 0, sizeof p);
		p.logo = LOGO_SPOT_NONE;
		p.focus = FOCUS_GEOMETRY;
		p.freeButtons = fb;
		p.nFreeButtons = 6;
		wrapLinks(where, p, off, on, bad);

		{
			const int A = on.freeCtl[0], B = on.freeCtl[1], C = on.freeCtl[2];
			const int D = on.freeCtl[3], E = on.freeCtl[4], F = on.freeCtl[5];
			const int far = round == 0 ? D : C;	/* the far end of row 0 from A: D, or C with D inactive */

			expectLink(bad, where, off.c[A].link[DIR_LEFT] == -1 && off.c[A].link[DIR_UP] == -1,
			           "wrap off: an edge has a link");
			expectLink(bad, where, on.c[A].link[DIR_LEFT] == far, "Left from A does not wrap to the far end of row 0");
			expectLink(bad, where, on.c[far].link[DIR_RIGHT] == A, "Right from the far end does not wrap to A");
			expectLink(bad, where, on.c[A].link[DIR_UP] == E, "Up from A does not wrap to the far end of column 0");
			expectLink(bad, where, on.c[E].link[DIR_DOWN] == A, "Down from E does not wrap to A");
			expectLink(bad, where, on.c[E].link[DIR_LEFT] == F, "Left from E does not wrap to the far end of row 1");
			expectLink(bad, where, on.c[A].link[DIR_RIGHT] == B, "Right from A changed");
			if (round == 1) {
				bool toInactive = false;
				for (int i = 0; i < on.n; i++)
					for (int d = 0; d < DIR_COUNT; d++)
						if (on.c[i].link[d] == D)
							toInactive = true;
				expectLink(bad, where, !toInactive, "a link points at an inactive button");
			}
		}
	}

	/* The real table: Next Pad is the leftmost button of the top row, Gun/Mouse the rightmost. */
	{
		static FreeButton cb[30];
		Page q;
		for (int i = 0; i < 30; i++) {
			cb[i].text = "Button";
			cb[i].x = CB_RECT[i][0];
			cb[i].y = CB_RECT[i][1];
			cb[i].w = CB_RECT[i][2];
			cb[i].h = CB_RECT[i][3];
			cb[i].lit = false;
			cb[i].inactive = false;
		}
		memset(&q, 0, sizeof q);
		q.logo = LOGO_SPOT_NONE;
		q.focus = FOCUS_GEOMETRY;
		q.freeButtons = cb;
		q.nFreeButtons = 30;
		const int n = wrapLinks("ConfigureButtons", q, off, on, bad);
		expectLink(bad, "ConfigureButtons", on.c[on.freeCtl[0]].link[DIR_LEFT] == on.freeCtl[29],
		           "Left from Next Pad does not wrap to Gun/Mouse");
		expectLink(bad, "ConfigureButtons", off.c[off.freeCtl[0]].link[DIR_LEFT] == -1,
		           "wrap off: Left from Next Pad has a link");
		printf("        ConfigureButtons: %d links wrap with the flag on\n", n);
	}
	printf("  Edge wrap  far ends, inactive skipped, wrap off unchanged  %s\n", bad ? "FAILED" : "ok");
}

/*  Configure Buttons in every controller state (Page::wrap)  */

/* The states ConfigureButtonsFrame::activateSubmenu sets up: no pad, a plain pad (not a lightgun),
 * a lightgun pad with the gun off, on with no gun mode, and on with Mouse, Just or GunCon. */
enum CbState { CB_NO_PAD, CB_PLAIN_PAD, CB_GUN_OFF, CB_GUN_IDLE, CB_GUN_MOUSE, CB_GUN_JUST, CB_GUN_GUNCON, CB_STATES };
static const char *const CB_STATE_NAME[CB_STATES] = {
	"no controller", "plain pad", "lightgun pad, gun off", "lightgun pad, gun on, no mode",
	"gun on, Mouse", "gun on, Just", "gun on, GunCon" };

/* The buttons a state turns on (visible and active). Button 0, Next Pad, is always on. */
static void cbActive(int state, bool on[30])
{
	int lo = 0, hi = -1;		/* buttons lo..hi are on, besides the extras set below */

	for (int i = 0; i < 30; i++)
		on[i] = false;
	on[0] = true;
	switch (state) {
	case CB_NO_PAD:    break;
	case CB_PLAIN_PAD: lo = 1; hi = 28; break;	/* button 29 (Gun/Mouse) is off: not a lightgun */
	case CB_GUN_OFF:
	case CB_GUN_IDLE:  lo = 1; hi = 29; break;
	case CB_GUN_MOUSE:
		lo = 1; hi = 5;
		on[10] = on[11] = on[19] = on[26] = on[27] = on[28] = on[29] = true;
		break;
	case CB_GUN_JUST:
		lo = 1; hi = 5;
		on[9] = on[17] = on[19] = on[28] = on[29] = true;
		break;
	case CB_GUN_GUNCON:
		lo = 1; hi = 5;
		on[9] = on[18] = on[19] = on[28] = on[29] = true;
		break;
	}
	for (int i = lo; i <= hi; i++)
		on[i] = true;
}

static const char *const DIR_NAME[DIR_COUNT] = { "Up", "Down", "Left", "Right" };

static void testConfigureButtonStates(void)
{
	static FreeButton cb[30];
	static Layout off, on;
	int bad = 0, addedAll = 0, deadBefore = 0, deadAfter = 0;

	printf("Configure Buttons in every controller state (Page::wrap: no dead ends, no link to a hidden or inactive button)\n");
	for (int s = 0; s < CB_STATES; s++) {
		bool act[30];
		Page q;
		int nFocus = 0, added = 0, before = 0, after = 0, deadI = -1, deadD = -1;
		const char *where = CB_STATE_NAME[s];

		cbActive(s, act);
		for (int i = 0; i < 30; i++) {
			cb[i].text = "Button";
			cb[i].x = CB_RECT[i][0];
			cb[i].y = CB_RECT[i][1];
			cb[i].w = CB_RECT[i][2];
			cb[i].h = CB_RECT[i][3];
			cb[i].lit = false;
			cb[i].inactive = !act[i];
		}
		memset(&q, 0, sizeof q);
		q.logo = LOGO_SPOT_NONE;
		q.focus = FOCUS_GEOMETRY;
		q.freeButtons = cb;
		q.nFreeButtons = 30;
		q.wrap = false;
		if (layoutPage(q, kFont, off) < 0) {
			fail(where, "the page does not lay out");
			continue;
		}
		linkFocus(q, off);
		q.wrap = true;
		if (layoutPage(q, kFont, on) < 0) {
			fail(where, "the page does not lay out with wrap");
			continue;
		}
		linkFocus(q, on);

		for (int i = 0; i < on.n; i++) {
			if (!on.c[i].focusable)
				continue;
			nFocus++;
			for (int d = 0; d < DIR_COUNT; d++) {
				const int was = off.c[i].link[d], now = on.c[i].link[d];

				/* the current code: the geometric rule, then the row or column wrap */
				if (was < 0 && !hasLineWrap(on, i, d))
					before++;
				if (was >= 0)
					expectLink(bad, where, now == was, "a link the rule made changed with wrap on");
				if (was < 0 && now >= 0 && hasLineWrap(on, i, d))
					expectLink(bad, where, sharesLine(on, i, now, d) && on.c[now].focusable,
					           "a row or column wrap changed");
				if (was < 0 && now >= 0 && !hasLineWrap(on, i, d))
					added++;
				if (now < 0) {
					if (deadI < 0) {
						deadI = i;
						deadD = d;
					}
					after++;
					continue;
				}
				expectLink(bad, where, on.c[now].focusable, "a link points at a hidden or inactive button");
				if (was < 0)
					expectLink(bad, where, alongOf(on, i, now, d ^ 1) > 1.0f,
					           "a wrap link does not reach beyond the origin on that side");
			}
		}
		if (nFocus >= 2 && deadI >= 0) {
			char msg[96];
			snprintf(msg, sizeof msg, "button %d has no %s target", on.c[deadI].slot, DIR_NAME[deadD]);
			expectLink(bad, where, false, msg);
		} else if (nFocus < 2)	/* a lone focus target has nothing to link to */
			expectLink(bad, where, after == 4 * nFocus, "a lone focus target has a link");
		printf("        %-34s %2d focusable, dead ends %d -> %d, links added %d%s\n", where, nFocus,
		       before, after, added, nFocus < 2 ? " (lone focus target)" : "");
		addedAll += added;
		deadBefore += before;
		deadAfter += after;
	}
	printf("        total: dead ends %d -> %d, links added vs the current code %d\n",
	       deadBefore, deadAfter, addedAll);
	printf("  Configure Buttons states  every focusable button has Left, Right, Up and Down  %s\n",
	       bad ? "FAILED" : "ok");
}

/* The tab buttons (L/R): tabStep goes round with wrap (TabWrap = 1), stops at the ends without. */
static void testTabStep(void)
{
	const int before = failures;
	if (tabStep(4, 5, 1, true) != 0 || tabStep(0, 5, -1, true) != 4 || tabStep(2, 5, 1, true) != 3)
		fail("tabs", "wrap: past the last tab is the first, before the first the last");
	if (tabStep(4, 5, 1, false) != 4 || tabStep(0, 5, -1, false) != 0 || tabStep(2, 5, -1, false) != 1)
		fail("tabs", "no wrap: the ends stop");
	if (tabStep(3, 5, 0, true) != 3 || tabStep(7, 5, 1, true) != 7 || tabStep(0, 0, 1, true) != 0)
		fail("tabs", "no press, or no such tab: the tab stays");
	printf("tab buttons: wrap and stop ... %s\n", failures == before ? "ok" : "FAILED");
}

/* A cycle wraps and its text always comes from its names (the Configure Buttons Slot button
 * once stopped at "Game": it wrote one digit into the label, past the end of "Game"). */
static void testCycle(void)
{
	static const char *const names[] = { "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Game" };
	Cycle c = { names, 5, 0 };
	int before = failures;
	for (unsigned step = 1; step <= 11; step++) {
		const char *got = cycleNext(c);
		if (strcmp(got, names[step % 5]) != 0) {
			printf("cycle step %u: got \"%s\", expected \"%s\"\n", step, got, names[step % 5]);
			failures++;
		}
	}
	printf("cycle: wraps through all names ... %s\n", failures == before ? "ok" : "FAILED");
}

/* The two-column grid: a column's label width is its widest label and its button width its widest
 * fitted button, so every line shares the edges. A cell is a label right-aligned 10 px before its
 * button; the columns are 24 apart and the grid is centred once. Nothing in the 15 px margins or
 * past 636. cells[line] = { left row, right row } (-1 for none), ys[line] the line's top. */
static void checkGrid(const Layout &lay, const Row *rows, const int (*cells)[2], const float *ys, int nLines)
{
	float lw[2] = { 0, 0 }, bw[2] = { 0, 0 }, x0, colX[2];
	bool two = false;

	for (int l = 0; l < nLines; l++)
		for (int c = 0; c < 2; c++) {
			if (cells[l][c] < 0) continue;
			const Row &r = rows[cells[l][c]];
			two = two || c == 1;
			if ((float)fontWidth(r.label, 1.0f, NULL) > lw[c]) lw[c] = (float)fontWidth(r.label, 1.0f, NULL);
			for (int k = 0; k < r.count; k++)
				if (fitWidth(r.names[k]) > bw[c]) bw[c] = fitWidth(r.names[k]);
		}
	x0 = (640.0f - (lw[0] + 10.0f + bw[0] + (two ? 24.0f + lw[1] + 10.0f + bw[1] : 0.0f))) / 2;
	colX[0] = x0 + lw[0] + 10.0f;
	colX[1] = x0 + lw[0] + 10.0f + bw[0] + 24.0f + lw[1] + 10.0f;

	for (int l = 0; l < nLines; l++)
		for (int c = 0; c < 2; c++) {
			const int i = cells[l][c];
			if (i < 0) continue;
			const Key kb = { ROLE_BUTTON, i, 0 }, kl = { ROLE_LABEL, i, 0 };
			const int b = findCtrl(lay, kb), t = findCtrl(lay, kl);
			if (b < 0 || t < 0) { fail("2 columns", "a row lost its label or button"); continue; }
			if (lay.c[b].rect.x != colX[c] || lay.c[b].rect.y != ys[l] || lay.c[b].rect.w != bw[c] ||
			    lay.c[b].rect.h != 56.0f)
				fail("2 columns", "a button is not on the grid");
			if (fabsf(lay.c[t].rect.x + lay.c[t].rect.w - (colX[c] - 10.0f)) > 1.0f || lay.c[t].ay != ys[l] + 20.0f)
				fail("2 columns", "a label does not end 10 px before its button");
			if (lay.c[t].rect.x < 15.0f || colX[c] + bw[c] > 636.0f)
				fail("2 columns", "a cell reaches the margin");
		}
}

/* Page::columns == 2 (Advanced Graphics, as OptionsFrame.cpp has it): Cycle rows fill two columns
 * of label + button, a choice row takes the whole line, focus runs by geometry. */
static void testTwoColumns(void)
{
	static const char *const GAMMA_NAMES[] = { "1.0", "1.1", "1.2", "1.3", "1.4", "1.5" };
	static const char *const DEFLICKER_NAMES[] = { "Normal", "Light", "Strong" };
	static const char *const VIDEOMODE_NAMES[] = { "Auto", "NTSC", "PAL 50", "PAL 60", "480p" };
	static const char *const SOFTMODE_NAMES[] = { "Accurate", "Hi-Fi", "Fast" };
	static Row rows[] = {
		RADIO_FIT("Dithering",       DITHER_NAMES,      1),
		CYC("Video Color",          FMV_COLOUR_NAMES,  1),
		CYC("MDEC Chroma",           CHROMA_NAMES,      0),
		CYC("Gamma",                 GAMMA_NAMES,       0),
		CYC("Filter Tour",           ON_OFF_NAMES,      0),
		CYC("Deflicker",             ON_OFF_NAMES,      0),
		CYC("Deflicker Level",       DEFLICKER_NAMES,   0),
		CYC("Soft Mode",             SOFTMODE_NAMES,    0),
		CYC("Video Mode",            VIDEOMODE_NAMES,   0),
	};
	Page p;
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int before = failures;

	memset(&p, 0, sizeof p);
	p.title = "Advanced Graphics";
	p.rows = rows;
	p.nRows = COUNT(rows);
	p.logo = LOGO_SPOT_BOTTOM;
	p.focus = FOCUS_ROWS;
	p.columns = 2;
	p.rowDy = 56.0f;	/* OptionsFrame's Advanced Graphics: nine rows, five lines, clear of the logo */
	p.rowY0 = 90.0f;

	if (layoutPage(p, kFont, lay) < 0)
		fail("2 columns", "layoutPage refused the page");
	linkFocus(p, lay);
	if (checkLayout(p, kFont, lay, probs, MAX_PROBLEMS) != 0) {
		auditLayout(p, lay, "2 columns", -1, true);
		fail("2 columns", "checkLayout finds a problem on the page");
	}

	/* Lines: Dithering at 90, the four Cycle pairs at 146, 202, 258 and 314. */
	{
		static const int cells[4][2] = { { 1, 2 }, { 3, 4 }, { 5, 6 }, { 7, 8 } };
		static const float ys[4] = { 146.0f, 202.0f, 258.0f, 314.0f };
		checkGrid(lay, rows, cells, ys, 4);
	}
	/* The choice row is where it always was. */
	{
		const Key kc = { ROLE_CHOICE, 0, 0 };
		const int c = findCtrl(lay, kc);
		if (c < 0 || lay.c[c].rect.y != 90.0f)
			fail("2 columns", "the Dithering row moved");
	}
	/* Focus: Down and Up keep the column, Left and Right change it and wrap. */
	{
		static const struct { int row, dir, to; } F[] = {
			{ 1, DIR_RIGHT, 2 }, { 2, DIR_LEFT, 1 }, { 2, DIR_RIGHT, 1 }, { 1, DIR_LEFT, 2 },
			{ 1, DIR_DOWN, 3 },  { 2, DIR_DOWN, 4 }, { 3, DIR_UP, 1 },    { 4, DIR_UP, 2 },
			{ 3, DIR_DOWN, 5 },  { 4, DIR_DOWN, 6 }, { 6, DIR_RIGHT, 5 },
			{ 5, DIR_DOWN, 7 },  { 6, DIR_DOWN, 8 }, { 7, DIR_UP, 5 },    { 8, DIR_UP, 6 },
			{ 7, DIR_DOWN, 0 },  { 8, DIR_DOWN, 0 },	/* the bottom line wraps to the top line, by column */
		};
		for (unsigned k = 0; k < COUNT(F); k++) {
			const Key kf = { ROLE_BUTTON, F[k].row, 0 };
			const int a = findCtrl(lay, kf);
			const int t = a < 0 ? -1 : lay.c[a].link[F[k].dir];
			if (t < 0 || lay.c[t].row != F[k].to) {
				printf("    row %d dir %d -> row %d, want %d\n", F[k].row, F[k].dir, t < 0 ? -1 : lay.c[t].row, F[k].to);
				fail("2 columns", "a focus link goes to the wrong cell");
			}
		}
	}

	p.rowDy = 0.0f;		/* the rest checks the grid on the default pitch */
	p.rowY0 = 0.0f;

	/* An odd run leaves the right cell of its last line empty; a wide row splits the runs. */
	{
		Row odd[] = { CYC("Gamma", GAMMA_NAMES, 0), CYC("Video Color", FMV_COLOUR_NAMES, 0),
		              RADIO_FIT("Dithering", DITHER_NAMES, 1), CYC("Gamma", GAMMA_NAMES, 0),
		              CYC("Gamma", GAMMA_NAMES, 0), CYC("Gamma", GAMMA_NAMES, 0) };
		Page q = p;
		q.rows = odd;
		q.nRows = COUNT(odd);
		q.help = NULL;
		q.nHelp = 0;
		if (layoutPage(q, kFont, lay) < 0)
			fail("2 columns", "layoutPage refused the mixed page");
		linkFocus(q, lay);
		static const int cells[3][2] = { { 0, 1 }, { 3, 4 }, { 5, -1 } };
		static const float ys[3] = { 100.0f, 220.0f, 280.0f };
		checkGrid(lay, odd, cells, ys, 3);	/* the lone cell sits in column 1 of the same grid */
		{	/* the wide row starts on the grid: its first button at column 1's button x */
			const Key wide = { ROLE_CHOICE, 2, 0 }, cell = { ROLE_BUTTON, 0, 0 };
			if (lay.c[findCtrl(lay, wide)].rect.x != lay.c[findCtrl(lay, cell)].rect.x)
				fail("2 columns", "a wide row does not start at column 1's button");
		}
		if (checkLayout(q, kFont, lay, probs, MAX_PROBLEMS) != 0)
			fail("2 columns", "checkLayout finds a problem on the mixed page");
	}

	/* The check still sees a label too long for its cell: it runs over the button beside it. */
	{
		Row bad[] = { CYC("A label far too long to fit in its half of the screen, or even on the whole screen, whatever the font", GAMMA_NAMES, 0) };
		Page q = p;
		bool over = false;
		q.rows = bad;
		q.nRows = 1;
		q.help = NULL;
		q.nHelp = 0;
		layoutPage(q, kFont, lay);
		linkFocus(q, lay);
		for (int i = 0, n = checkLayout(q, kFont, lay, probs, MAX_PROBLEMS); i < n && i < MAX_PROBLEMS; i++)
			if (probs[i].kind == PROB_OVERLAP || probs[i].kind == PROB_PAST_EDGE)
				over = true;
		if (!over)
			fail("2 columns", "checkLayout missed a label wider than its cell");
	}

	/* Columns unset: one row per line, as before. */
	{
		Page q = p;
		const Key k = { ROLE_BUTTON, 2, 0 };
		q.columns = 0;
		layoutPage(q, kFont, lay);
		if (lay.c[findCtrl(lay, k)].rect.x != 295.0f || lay.c[findCtrl(lay, k)].rect.y != 220.0f)
			fail("2 columns", "columns 0 changed the one-column layout");
	}
	printf("two columns: cells, help, focus, checks ... %s\n", failures == before ? "ok" : "FAILED");
}

/*  The standard (Docs/MENU_PLAN.md): its widths, footer and four checks. Each check has a failing and a passing page.  */

static bool stdHas(const Page &p, const Layout &lay, int kind)
{
	Problem probs[MAX_PROBLEMS];
	const int np = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
		if (probs[i].kind == kind)
			return true;
	return false;
}

static Page stdPage(Row *rows, int n, Density d, LogoSpot logo)
{
	Page p;
	memset(&p, 0, sizeof p);
	p.rows = rows;
	p.nRows = n;
	p.standard = d;
	p.logo = logo;
	return p;
}

static void testStandard(void)
{
	static const char *const OFF_ON[]  = { "Off", "On" };
	static const char *const LONG3[]   = { "Interpreter", "Lightrec", "Dynarec with a long name" };
	static const char *const VIDEO[]   = { "Off", "Near", "Bilinear", "Trap" };
	static const ActionButton BTN2[]   = { { "Configure Input", 0, false }, { "Configure Buttons", 0, false } };
	static const ActionButton BTN3[]   = { { "Configure Input", 0, false }, { "Configure Buttons", 0, false }, { "Advanced settings", 0, false } };
	const char *tag = "standard";
	const int before = failures;
	Row rows[8];
	Layout lay;
	Page p;

	/* widths: step 8, equal when they fit, else own text + 16 */
	{
		int w[4], sum;
		bool eq = false;
		const int a = stdStepWidth(kFont, "Off", 32), b = stdButtonWidth(kFont, "Off");
		if (a != b || a % STD_STEP != 0 || a < kFont.width("Off", 1.0f, NULL) + 32)
			fail(tag, "stdButtonWidth is not text + 32 in steps of 8");
		if (stdStepWidth(kFont, "", 32) != 32 || stdStepWidth(kFont, NULL, 16) != 16)
			fail(tag, "an empty text must measure 0 (width = the padding)");
		printf("  text widths: Off %d, Near %d, Bilinear %d, Trap %d\n", kFont.width("Off", 1.0f, NULL),
		       kFont.width("Near", 1.0f, NULL), kFont.width("Bilinear", 1.0f, NULL), kFont.width("Trap", 1.0f, NULL));
		sum = stdSegmentWidths(kFont, OFF_ON, 2, 352, w, &eq);
		if (!eq || w[0] != w[1] || sum != w[0] * 2)
			fail(tag, "two segments that fit in 352 must get equal widths");
		sum = stdSegmentWidths(kFont, VIDEO, 3, 400, w, &eq);
		if (!eq || w[0] != w[1] || w[1] != w[2] || sum != w[0] * 3)
			fail(tag, "three segments that fit in 400 must get equal widths");
		printf("  segments: Off|Near|Bilinear equal %d each (room 400)", w[0]);
		sum = stdSegmentWidths(kFont, VIDEO, 3, 260, w, &eq);
		if (eq || sum != w[0] + w[1] + w[2] || w[0] != stdStepWidth(kFont, "Off", 16) ||
		    w[2] != stdStepWidth(kFont, "Bilinear", 16) || w[0] == w[2])
			fail(tag, "segments that do not fit must get their own text + 16");
		printf(", own %d+%d+%d (room 260)\n", w[0], w[1], w[2]);
	}

	/* the approved Video row: Filters Off | Near | Bilinear, then the separate button Trap, in 352 */
	{
		memset(rows, 0, sizeof rows);
		rows[0].kind = ROW_CHOICE; rows[0].label = "Filters"; rows[0].names = VIDEO; rows[0].count = 4; rows[0].plain = 1;
		p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
		if (layoutPage(p, kFont, lay) < 0) {
			fail(tag, "the Video filters row does not lay out");
		} else {
			const Control &o = lay.c[lay.slot[0][0]], &nr = lay.c[lay.slot[0][1]], &bl = lay.c[lay.slot[0][2]], &tr = lay.c[lay.slot[0][3]];
			printf("  Video row: widths %d+%d+%d, Trap at x %d w %d (ends %d)\n", (int)o.rect.w, (int)nr.rect.w, (int)bl.rect.w,
			       (int)tr.rect.x, (int)tr.rect.w, (int)(tr.rect.x + tr.rect.w));
				/* the equal rule would not leave room for the button: the segments keep their own widths */
				if (o.rect.w != stdStepWidth(kFont, "Off", 16) || nr.rect.w != stdStepWidth(kFont, "Near", 16) ||
				    bl.rect.w != stdStepWidth(kFont, "Bilinear", 16))
					fail(tag, "Video row: the segments must keep their own text + 16 widths (equal widths leave no room for Trap)");
				if (tr.role != ROLE_BUTTON || tr.rect.w != stdStepWidth(kFont, "Trap", 16) ||
				    tr.rect.x != bl.rect.x + bl.rect.w + STD_BTN_GAP)
					fail(tag, "Video row: Trap must be a separate button, its own text + 16 (the row has no room for equal widths), 12 px after the segments");
				if (stdHas(p, lay, PROB_BTN_TOUCH))
					fail(tag, "Video row: the button touches the segments");
				/* Monty's width rule (2026-11-04): a row that does not fit with equal widths gives every part
				 * its own width, separate buttons included: 64+80+112, gap 12, Trap 72 = x 604. */
				if (stdHas(p, lay, PROB_PAST_RIGHT) || (int)(tr.rect.x + tr.rect.w) != 604)
					fail(tag, "Video row: it must end at x 604, inside 616");
			}
		}

		/* The same structure in a row that does fit: Off | Near | Bilinear, then a short button. Equal widths
		 * (3 x 128) would not leave room for it; own widths do. */
		{
			static const char *const NAMES[] = { "Off", "Near", "Bilinear", "Go" };
			memset(rows, 0, sizeof rows);
			rows[0].kind = ROW_CHOICE; rows[0].label = "Filters"; rows[0].names = NAMES; rows[0].count = 4; rows[0].plain = 1;
			p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
			if (layoutPage(p, kFont, lay) < 0) {
				fail(tag, "a choice followed by a button does not lay out");
			} else {
				const Control &bl = lay.c[lay.slot[0][2]], &go = lay.c[lay.slot[0][3]];
				const int room = (int)STD_CTRL_W - stdButtonWidth(kFont, "Go") - (int)STD_BTN_GAP;
				int w[3];
				bool eq = true;
				stdSegmentWidths(kFont, NAMES, 3, room, w, &eq);
				if (eq || bl.rect.w != (float)stdStepWidth(kFont, "Bilinear", 16))
					fail(tag, "choice + button: the equal rule must give way to own widths");
				if (go.rect.x != bl.rect.x + bl.rect.w + STD_BTN_GAP || go.rect.x + go.rect.w > STD_CTRL_X1)
					fail(tag, "choice + button: the button must sit 12 px after the segments, inside 616");
				if (stdHas(p, lay, PROB_PAST_RIGHT) || stdHas(p, lay, PROB_BTN_TOUCH) || stdHas(p, lay, PROB_TEXT_WIDE))
					fail(tag, "choice + button: a fitting row was reported");
			}
		}

	/* PROB_LABEL_WIDE */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "A label that is much too long for the left column"; rows[0].names = OFF_ON; rows[0].count = 2;
	p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_LABEL_WIDE)) fail(tag, "a label wider than 224 px was not reported");
	rows[0].label = "Short";
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_LABEL_WIDE)) fail(tag, "a short label was reported as too wide");

	/* PROB_PAST_RIGHT: a row of separate buttons from 264 reaching past 616; and one that stays inside */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].label = "Input"; rows[0].actions = BTN3; rows[0].count = 3;
	p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_PAST_RIGHT)) fail(tag, "a row reaching past x 616 was not reported");
	rows[0].count = 1;
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_PAST_RIGHT)) fail(tag, "a row inside x 616 was reported as past the right edge");

	/* PROB_IN_STD_LOGO: row 5 of a Regular page is y 368-418; the logo box is 572,384 64x88 */
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 5; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
	rows[5].kind = ROW_CHOICE; rows[5].label = "Wide"; rows[5].names = LONG3; rows[5].count = 3;
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "a control in the logo box of the standard was not reported");
	p.logo = LOGO_SPOT_NONE;
	if (stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "the logo box was checked on a page with no logo");
	p.logo = LOGO_SPOT_BOTTOM;
	rows[5].names = OFF_ON; rows[5].count = 2;
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "a short choice left of the logo box was reported in it");
	if (stdHas(p, lay, PROB_IN_LOGO)) fail(tag, "the legacy logo check ran on a standard page");

	/* PROB_BTN_TOUCH: layoutPage always leaves 12; move one button by hand to touch. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].label = "Input"; rows[0].actions = BTN2; rows[0].count = 2;
	rows[1].kind = ROW_CHOICE; rows[1].label = "Mode"; rows[1].names = VIDEO; rows[1].count = 4; rows[1].plain = 1;
	p = stdPage(rows, 2, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_BTN_TOUCH)) fail(tag, "buttons 12 px apart, and touching segments of one choice, were reported as touching");
	{
		Layout t = lay;
		Control &b = t.c[t.slot[0][1]];
		const Control &a = t.c[t.slot[0][0]];
		b.rect.x = a.rect.x + a.rect.w + 4;
		if (!stdHas(p, t, PROB_BTN_TOUCH)) fail(tag, "two action buttons 4 px apart were not reported");
		t = lay;
		t.c[t.slot[1][3]].rect.x = t.c[t.slot[1][2]].rect.x + t.c[t.slot[1][2]].rect.w;
		if (!stdHas(p, t, PROB_BTN_TOUCH)) fail(tag, "an action button touching a choice segment was not reported");
	}

	/* the density limits: 6 Regular rows, 7 Compact rows; one more is -1 */
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 8; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_NONE);
	if (layoutPage(p, kFont, lay) < 0) fail(tag, "6 Regular rows must fit");
	p.nRows = 7;
	if (layoutPage(p, kFont, lay) != -1) fail(tag, "a 7th Regular row must be refused (-1)");
	p.standard = DENSITY_COMPACT;
	if (layoutPage(p, kFont, lay) < 0) fail(tag, "7 Compact rows must fit");
	p.nRows = 8;
	if (layoutPage(p, kFont, lay) != -1) fail(tag, "an 8th Compact row must be refused (-1)");

	/* the footer */
	{
		static const FooterHelp OK[2] = { { { "One line of help.", NULL } },
		                                  { { "First line of the help for this row.", "Second line of the help." } } };
		static const FooterHelp BAD[2] = { { { "Short.", NULL } },
		                                   { { "A help line that is much too long to fit in the footer, which is 532 pixels wide at 13 px, so it has to be cut or reworded", NULL } } };
		Control c[STD_HELP_LINES];
		memset(rows, 0, sizeof rows);
		for (int i = 0; i < 2; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
		p = stdPage(rows, 2, DENSITY_REGULAR, LOGO_SPOT_NONE);
		if (layoutFooter(p, kFont, 0, c) != 0) fail(tag, "a page with no footer must give 0 lines");
		p.footer = OK;
		if (layoutFooter(p, kFont, 0, c) != 1 || strcmp(c[0].text, "One line of help.") != 0) fail(tag, "a 1-line footer");
		if (layoutFooter(p, kFont, 1, c) != 2) {
			fail(tag, "a 2-line footer must give 2 lines");
		} else if (c[0].role != ROLE_FOOTER || c[0].ax != STD_HELP_X0 || c[0].ay != STD_HELP_Y0 ||
		           c[1].ay != STD_HELP_Y0 + STD_HELP_DY || c[0].align != ALIGN_LEFT_TOP) {
			fail(tag, "footer lines are not at x 24, y 442 and 459, left-aligned");
		}
		if (layoutFooter(p, kFont, 2, c) != 0 || layoutFooter(p, kFont, -1, c) != 0) fail(tag, "a row outside the page must give 0 lines");
		if (checkFooters(p, kFont) != 0) fail(tag, "checkFooters reported a fitting footer");
		p.footer = BAD;
		if (layoutFooter(p, kFont, 1, c) != -1) fail(tag, "a footer line wider than 532 px must give -1");
		if (layoutFooter(p, kFont, 0, c) != 1) fail(tag, "a short footer line beside a bad one must still lay out");
		if (checkFooters(p, kFont) != 1) fail(tag, "checkFooters must count the one bad row");
	}

	printf("standard pages: widths, Video row, four checks, density limits, footer ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/* The Video and Controls tabs as SettingsFrame.cpp builds them (VID_ROWS, INP_ROWS and the words their
 * buttons show), laid out with the real font and checked the way the Wii draws them: no row may pass x 616,
 * touch another control or be wider than its button. Each cycle is one 200 px button. Keep these in step
 * with SettingsFrame.cpp. */

static int stdTabProblems(const char *what, Page &p)
{
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int np, bad = 0;

	if (layoutPage(p, kFont, lay) < 0) {
		fail(what, "the tab does not lay out");
		return 1;
	}
	linkFocus(p, lay);
	np = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) {
		char msg[160];
		snprintf(msg, sizeof msg, "problem %d at control %d (%s): right edge or touch or text width", (int)probs[i].kind,
		         probs[i].a, probs[i].a >= 0 && probs[i].a < lay.n ? lay.c[probs[i].a].text : "");
		fail(what, msg);
		bad++;
	}
	if (checkFooters(p, kFont) != 0) {
		fail(what, "a footer line is too wide");
		bad++;
	}
	return bad;
}

/* The five Advanced tabs as OptionsFrame.cpp builds them on the standard (PLUGIN_ROWS, GPU_ROWS,
 * SPU_ROWS, CD_ROWS, INPUT_ROWS and their footers; the names are GamecubeMain.cpp's OPTIONS table).
 * Every value of every row is laid out with the real font, so a name that does not fit the 352 px
 * (a segment, or a cycle's button) shows up here. Keep in step with OptionsFrame.cpp. */
static void testStdAdvancedTabs(void)
{
	static const char *const TABS5[]  = { "CPU", "GPU", "SPU", "CD", "Input" };
	static const char *const CORE[]   = { "Lightrec", "Interp.", "Dynarec" };
	static const char *const FASTACC[] = { "Fast", "Accurate" };
	static const char *const SPEED[]  = { "Auto", "50%", "75%", "100%", "125%", "150%", "200%", "300%" };
	static const char *const GPU[]    = { "Soft Fast", "Soft Timed", "OpenGX" };
	static const char *const TEXMIN[] = { "Off", "Linear", "Aniso 2x", "Aniso 4x" };
	static const char *const GXCOLOR[] = { "24-bit", "6-bit dither", "AA (240p)" };
	static const char *const GXMASK[] = { "Accurate", "Fast" };
	static const char *const OFF_ON[] = { "Off", "On" };
	static const char *const CDBUF[]  = { "16 KB", "64 KB", "256 KB" };
	static const char *const HUNKS[]  = { "2", "4", "8" };
	static const ActionButton USB[]   = { { "Test", 0, false, 0 } };
	static const FooterHelp CPU_F[] = {
		{{ "Lightrec runs most games best; Interpreter is exact but slow.", "A new core restarts the game." }},
		{{ "Accurate: 3D math and multiply waits as on a PS1.", NULL }},
		{{ "Over 100% eases slowdown in some games, at more Wii CPU time.", "Save it for one game: This Game." }},
	};
	static const FooterHelp GPU_F[] = {
		{{ "OpenGX draws with the Wii's GPU. Soft Fast and Soft Timed draw with the CPU:", "slower, but exact. A change applies at the next game load." }},
		{{ "Accurate: the GPU is busy as long as on a PS1.", NULL }},
		{{ "OpenGX: smooths ground drawn smaller than a pixel. A trial.", NULL }},
		{{ "OpenGX: 6-bit dither, or anti-aliased edges in 240p mode. A trial.", NULL }},
		{{ "Accurate: the PS1 mask bit per pixel. Fast guesses it.", NULL }},
		{{ "On: skips frames when the Wii is too slow.", NULL }},
	};
	static const FooterHelp SPU_F[] = {
		{{ "Emulates the PS1 sound chip on the Wii's CPU.", "The Sound Driver is on the Audio tab; sound quality in Audio, Advanced." }},
	};
	static const FooterHelp CD_F[] = {
		{{ "16 KB is best: the card is read in 32 KB pages, so a larger", "buffer only reads more than the game uses. Next game load." }},
		{{ "A thread reads the next sectors early, so the game waits less", "for the card. Raw bin/cue and .iso only. Next game load." }},
		{{ "Decoded CHD hunks, about 20 KB each. 8 is best.", "Applies at the next game load." }},
		{{ "Shorter CD waits. Some games break with it.", NULL }},
	};
	static const FooterHelp IN_F[] = {
		{{ "Accurate: each byte on the controller ports takes as long as on a PS1.", "Fast is the default." }},
		{{ "Shows what a USB controller sends, button by button, and logs it to usbpad.log.", NULL }},
	};
#define ADV_ROW(r, k, lbl, nm, n) do { memset(&rows[r], 0, sizeof rows[r]); rows[r].kind = (k); rows[r].label = (lbl); rows[r].names = (nm); rows[r].count = (n); } while (0)
	Tab tabs[5];
	Row rows[6];
	Page p;
	int tws[5];

	for (int i = 0; i < 5; i++) {
		tabs[i].text = TABS5[i];
		tabs[i].width = 0;
	}
	{
		const int total = stdTabWidths(kFont, tabs, 5, tws);
		for (int i = 0; i < 5; i++)
			tabs[i].width = (float)tws[i];
		if (((int)MENU_W - total) / 2 < (int)MENU_EDGE)
			fail("Advanced tabs", "the tab strip passes the screen margin");
	}

	for (int pg = 0; pg < 5; pg++) {
		const char *name = TABS5[pg];
		int n = 0;
		Density d = DENSITY_REGULAR;
		const FooterHelp *foot = NULL;

		memset(rows, 0, sizeof rows);
		switch (pg) {
		case 0:
			ADV_ROW(0, ROW_CHOICE, "CPU Core", CORE, 3);
			ADV_ROW(1, ROW_CHOICE, "CPU Timing", FASTACC, 2);
			ADV_ROW(2, ROW_CYCLE, "CPU Speed", SPEED, 8);
			n = 3; foot = CPU_F;
			break;
		case 1:
			ADV_ROW(0, ROW_CYCLE, "GPU Plugin", GPU, 3);
			ADV_ROW(1, ROW_CHOICE, "GPU Timing", FASTACC, 2);
			ADV_ROW(2, ROW_CYCLE, "Far Textures", TEXMIN, 4);
			ADV_ROW(3, ROW_CYCLE, "GX Color", GXCOLOR, 3);
			ADV_ROW(4, ROW_CHOICE, "GX Mask Bit", GXMASK, 2);
			ADV_ROW(5, ROW_CHOICE, "Frame Skip", OFF_ON, 2);
			n = 6; foot = GPU_F;
			break;
		case 2:
			rows[0].kind = ROW_READOUT; rows[0].label = "SPU Plugin"; rows[0].text = "P.E.Op.S. DFSound";
			n = 1; d = DENSITY_TEXT; foot = SPU_F;
			break;
		case 3:
			ADV_ROW(0, ROW_CHOICE, "CD Read Buffer", CDBUF, 3);
			ADV_ROW(1, ROW_CHOICE, "CD Read-Ahead", OFF_ON, 2);
			ADV_ROW(2, ROW_CHOICE, "CHD Hunk Cache", HUNKS, 3);
			ADV_ROW(3, ROW_CHOICE, "Fast Load", OFF_ON, 2);
			n = 4; foot = CD_F;
			break;
		default:
			ADV_ROW(0, ROW_CHOICE, "Port Timing", FASTACC, 2);
			rows[1].kind = ROW_BUTTONS; rows[1].label = "USB Pad"; rows[1].actions = USB; rows[1].count = 1;
			n = 2; foot = IN_F;
			break;
		}
		p = stdPage(rows, n, d, LOGO_SPOT_BOTTOM);
		p.tabs = tabs; p.nTabs = 5; p.curTab = pg; p.footer = foot; p.focus = FOCUS_ROWS;

		/* every value of every Choice and Cycle row */
		for (int r = 0; r < n; r++) {
			if (rows[r].kind != ROW_CHOICE && rows[r].kind != ROW_CYCLE)
				continue;
			for (int v = 0; v < rows[r].count; v++) {
				char what[48];
				rows[r].value = v;
				snprintf(what, sizeof what, "Advanced %s, row %d value %d", name, r, v);
				stdTabProblems(what, p);
			}
			rows[r].value = 0;
		}
		stdTabProblems(name, p);

		/* where each Choice row ends: the standard's column is x 264..616 */
		{
			Layout lay;
			if (layoutPage(p, kFont, lay) >= 0)
				for (int r = 0; r < n; r++) {
					if (rows[r].kind != ROW_CHOICE)
						continue;
					const Control &last = lay.c[lay.slot[r][rows[r].count - 1]];
					const float end = last.rect.x + last.rect.w;
					printf("  %-5s %-15s %d segments, segment 0 is %3.0f px, ends at x %3.0f%s\n", name, rows[r].label, rows[r].count,
					       lay.c[lay.slot[r][0]].rect.w, end, end > 616.0f ? "   DOES NOT FIT (limit 616)" : "");
				}
		}
	}
#undef ADV_ROW
}

static void testStdSettingsTabs(void)
{
	static const char *const TABS[]   = { "Video", "Input", "Saves", "Audio", "System" };
	static const char *const SCREEN[] = { "4:3", "16:9", "Force 16:9" };
	static const char *const FILTER[] = { "Off", "Near", "Bilinear", "Trap" };
	static const char *const OFF_ON[] = { "Off", "On" };
	static const char *const PAD[]    = { "Digital", "Analog", "Stick Pad" };
	static const char *const SDUSB[]  = { "SD", "USB" };
	static const ActionButton ADV[]   = { { "Advanced", 0, false } };
	static const ActionButton DISP[]  = { { "Interlaced", 0, true }, { "240p", 0, false } };
	static const ActionButton RATE[]  = { { "Auto", 200, false }, { "NTSC", 0, false } };
	static const ActionButton CFG[]   = { { "Configure Input", 0, false }, { "Configure Buttons", 0, false } };
	static const ActionButton CYC[]   = { { "Off", 200, false } };
	static const FooterHelp VF[]      = { {{ NULL, NULL }}, {{ NULL, NULL }}, {{ "Interlaced: full height. 240p: half height, no flicker.", NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }} };
	static const FooterHelp CF[]      = { {{ NULL, NULL }}, {{ "Analog: a DualShock. Stick Pad: the stick moves the D-pad.", NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }} };
	const int before = failures;
	Tab tabs[5];
	Row rows[6];
	Page p;

	for (int i = 0; i < 5; i++) {
		tabs[i].text = TABS[i];
		tabs[i].width = 0;
	}
	{
		int tws[5];
		const int total = stdTabWidths(kFont, tabs, 5, tws);
		const int x0 = ((int)MENU_W - total) / 2;
		for (int i = 0; i < 5; i++)
			tabs[i].width = (float)tws[i];
		printf("  tab strip: widths %d %d %d %d %d, total %d, x %d..%d\n", tws[0], tws[1], tws[2], tws[3], tws[4],
		       total, x0, x0 + total);
		if (x0 < (int)MENU_EDGE || x0 + total > (int)MENU_W - (int)MENU_EDGE)
			fail("Settings tabs", "the tab strip passes the screen margin: shorten a tab word");
	}

	/* Video */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].actions = ADV; rows[0].count = 1;
	rows[1].kind = ROW_CHOICE;  rows[1].label = "Screen Mode"; rows[1].names = SCREEN; rows[1].count = 3;
	rows[2].kind = ROW_BUTTONS; rows[2].label = "Display"; rows[2].actions = DISP; rows[2].count = 2;
	rows[3].kind = ROW_BUTTONS; rows[3].label = "Frame Rate"; rows[3].actions = RATE; rows[3].count = 2;
	rows[4].kind = ROW_CHOICE;  rows[4].label = "Filters"; rows[4].names = FILTER; rows[4].count = 4; rows[4].plain = 1;
	rows[5].kind = ROW_CHOICE;  rows[5].label = "Show FPS"; rows[5].names = OFF_ON; rows[5].count = 2;
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
	p.tabs = tabs; p.nTabs = 5; p.curTab = 0; p.footer = VF; p.focus = FOCUS_ROWS;
	stdTabProblems("Video tab", p);

	/* Controls */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].actions = CFG; rows[0].count = 2;
	rows[1].kind = ROW_CHOICE;  rows[1].label = "PSX Pad Type"; rows[1].names = PAD; rows[1].count = 3;
	rows[2].kind = ROW_CHOICE;  rows[2].label = "Enable Rumble"; rows[2].names = OFF_ON; rows[2].count = 2;
	rows[3].kind = ROW_BUTTONS; rows[3].label = "Light Gun"; rows[3].actions = CYC; rows[3].count = 1;
	rows[4].kind = ROW_BUTTONS; rows[4].label = "Auto Load Slot"; rows[4].actions = CYC; rows[4].count = 1;
	rows[5].kind = ROW_CHOICE;  rows[5].label = "Save Configs"; rows[5].names = SDUSB; rows[5].count = 2;
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
	p.tabs = tabs; p.nTabs = 5; p.curTab = 1; p.footer = CF; p.focus = FOCUS_ROWS;
	{
		Layout pl;
		if (layoutPage(p, kFont, pl) >= 0) {
			const Control &a = pl.c[pl.slot[1][0]], &b = pl.c[pl.slot[1][1]], &c = pl.c[pl.slot[1][2]];
			printf("  PSX Pad Type: text %d %d %d; widths %d+%d+%d = %d, x %d..%d\n",
			       kFont.width("Digital", 1.0f, NULL), kFont.width("Analog", 1.0f, NULL), kFont.width("Stick Pad", 1.0f, NULL),
			       (int)a.rect.w, (int)b.rect.w, (int)c.rect.w, (int)(a.rect.w + b.rect.w + c.rect.w),
			       (int)a.rect.x, (int)(c.rect.x + c.rect.w));
		}
	}
	stdTabProblems("Controls tab", p);

	/* Saves: Memcard Type is two small cycles, each after "1" or "2" (Row::prefix) */
	{
		static const char *const AUTO[]   = { "Off", "On" };
		static const char *const DEVICE[] = { "SD", "USB", "CardA", "CardB" };
		static const char *const PRE[]    = { "1", "2" };
		static const char *const CARD[]   = { "Off", "Shared", "Game" };
		for (int t = 0; t < 3; t++) {
			ActionButton cyc[2];
			Layout pl;
			cyc[0].text = CARD[t]; cyc[0].width = 0; cyc[0].lit = false; cyc[0].dy = 0;
			cyc[1] = cyc[0];
			memset(rows, 0, sizeof rows);
			rows[0].kind = ROW_CHOICE;  rows[0].label = "Memcard Autosave"; rows[0].names = AUTO; rows[0].count = 2;
			rows[1].kind = ROW_CHOICE;  rows[1].label = "Save Memcards To"; rows[1].names = DEVICE; rows[1].count = 4;
			rows[2].kind = ROW_BUTTONS; rows[2].label = "Memcard Type"; rows[2].actions = cyc; rows[2].count = 2; rows[2].prefix = PRE;
			rows[3].kind = ROW_CHOICE;  rows[3].label = "Save States Device"; rows[3].names = SDUSB; rows[3].count = 2;
			p = stdPage(rows, 4, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
			p.tabs = tabs; p.nTabs = 5; p.curTab = 2; p.focus = FOCUS_ROWS;
			if (t == 0 && layoutPage(p, kFont, pl) >= 0) {
				const Control &d0 = pl.c[pl.slot[1][0]], &d3 = pl.c[pl.slot[1][3]];
				const Control &y0 = pl.c[pl.slot[2][0]], &y1 = pl.c[pl.slot[2][1]];
				printf("  Save Memcards To: text %d %d %d %d; widths %d %d %d %d, x %d..%d\n",
				       kFont.width("SD", 1.0f, NULL), kFont.width("USB", 1.0f, NULL), kFont.width("CardA", 1.0f, NULL),
				       kFont.width("CardB", 1.0f, NULL), (int)pl.c[pl.slot[1][0]].rect.w, (int)pl.c[pl.slot[1][1]].rect.w,
				       (int)pl.c[pl.slot[1][2]].rect.w, (int)d3.rect.w, (int)d0.rect.x, (int)(d3.rect.x + d3.rect.w));
				printf("  Memcard Type: cycle 1 at x %d..%d, cycle 2 at x %d..%d; labels %d %d %d %d\n",
				       (int)y0.rect.x, (int)(y0.rect.x + y0.rect.w), (int)y1.rect.x, (int)(y1.rect.x + y1.rect.w),
				       kFont.width("Memcard Autosave", 1.0f, NULL), kFont.width("Save Memcards To", 1.0f, NULL),
				       kFont.width("Memcard Type", 1.0f, NULL), kFont.width("Save States Device", 1.0f, NULL));
			}
			if (t == 0 && layoutPage(p, kFont, pl) >= 0) {
				/* a standard page: Down from any Save Memcards To button lands on Memcard Type's first */
				linkFocus(p, pl);
				for (int k = 0; k < 4; k++)
					if (pl.c[pl.slot[1][k]].link[DIR_DOWN] != pl.slot[2][0])
						fail("Saves tab", "Down from Save Memcards To does not land on the leftmost Memcard Type button");
			}
			stdTabProblems("Saves tab", p);
		}
	}

	/* Audio: Advanced centered, the Enable flags, Sync, the Interpolation cycle, Sound Driver */
	{
		static const char *const SYNC[]   = { "Off", "Tempo", "Rate" };
		static const char *const DRIVER[] = { "DSP", "CPU" };
		static const char *const INTERP[] = { "Simple", "Gaussian" };
		static const ActionButton ENABLE[] = { { "Audio", 0, true }, { "XA", 0, true }, { "CDDA", 0, true } };
		static const FooterHelp AF[] = { {{ NULL, NULL }}, {{ "Audio: all sound. XA: speech and music streams. CDDA: CD music tracks.", NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }} };
		for (int t = 0; t < 2; t++) {
			ActionButton cyc[1];
			Layout pl;
			cyc[0].text = INTERP[t]; cyc[0].width = 200; cyc[0].lit = false; cyc[0].dy = 0;
			memset(rows, 0, sizeof rows);
			rows[0].kind = ROW_BUTTONS; rows[0].actions = ADV; rows[0].count = 1;
			rows[1].kind = ROW_BUTTONS; rows[1].label = "Enable"; rows[1].actions = ENABLE; rows[1].count = 3;
			rows[2].kind = ROW_CHOICE;  rows[2].label = "Sync"; rows[2].names = SYNC; rows[2].count = 3;
			rows[3].kind = ROW_BUTTONS; rows[3].label = "Interpolation"; rows[3].actions = cyc; rows[3].count = 1;
			rows[4].kind = ROW_CHOICE;  rows[4].label = "Sound Driver"; rows[4].names = DRIVER; rows[4].count = 2;
			p = stdPage(rows, 5, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
			p.tabs = tabs; p.nTabs = 5; p.curTab = 3; p.footer = AF; p.focus = FOCUS_ROWS;
			if (t == 0 && layoutPage(p, kFont, pl) >= 0) {
				const Control &e0 = pl.c[pl.slot[1][0]], &e2 = pl.c[pl.slot[1][2]];
				const Control &s0 = pl.c[pl.slot[2][0]], &s2 = pl.c[pl.slot[2][2]];
				const Control &i0 = pl.c[pl.slot[3][0]], &d1 = pl.c[pl.slot[4][1]];
				printf("  Audio: Enable x %d..%d, Sync x %d..%d, Interpolation x %d..%d, Sound Driver ends x %d; labels %d %d %d %d\n",
				       (int)e0.rect.x, (int)(e2.rect.x + e2.rect.w), (int)s0.rect.x, (int)(s2.rect.x + s2.rect.w),
				       (int)i0.rect.x, (int)(i0.rect.x + i0.rect.w), (int)(d1.rect.x + d1.rect.w),
				       kFont.width("Enable", 1.0f, NULL), kFont.width("Sync", 1.0f, NULL),
				       kFont.width("Interpolation", 1.0f, NULL), kFont.width("Sound Driver", 1.0f, NULL));
			}
			stdTabProblems("Audio tab", p);
		}
	}

	/* System: Advanced, Achievements, Memory; Select BIOS; Boot Through BIOS + Execute BIOS; Save and Load
	 * Settings (actions); Select Language (a cycle, the last row) */
	{
		static const char *const BIOS[]  = { "HLE", "SD", "USB", "DVD" };
		static const char *const BOOT[]  = { "Off", "On", "Execute BIOS" };
		static const char *const LANGS[] = { "En", "Chs", "Kr", "Es", "Pte", "It", "De", "Cht", "Jp", "Fr", "Br", "Ca", "Tu" };
		static const ActionButton TOP[]  = { { "Advanced", 0, false }, { "Achievements", 0, false }, { "Memory", 0, false } };
		static const ActionButton SAVE[] = { { "SD", 0, false }, { "USB", 0, false }, { "This Game", 0, false } };
		static const FooterHelp SF[] = { {{ NULL, NULL }}, {{ NULL, NULL }}, {{ "Execute BIOS: start the PS1 BIOS shell (memory cards, CD player).", NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }}, {{ NULL, NULL }} };
		for (int t = 0; t < 13; t++) {
			ActionButton cyc[1];
			ActionButton boot[3];
			Layout pl;
			cyc[0].text = LANGS[t]; cyc[0].width = 200; cyc[0].lit = false; cyc[0].dy = 0;
			boot[0] = boot[1] = boot[2] = cyc[0]; boot[0].width = boot[1].width = boot[2].width = 0;
			boot[2].text = BOOT[2];
			memset(rows, 0, sizeof rows);
			rows[0].kind = ROW_BUTTONS; rows[0].actions = TOP; rows[0].count = 3;
			rows[1].kind = ROW_CHOICE;  rows[1].label = "Select BIOS"; rows[1].names = BIOS; rows[1].count = 4;
			rows[2].kind = ROW_CHOICE;  rows[2].label = "Boot Through BIOS"; rows[2].names = BOOT; rows[2].count = 3; rows[2].plain = 1; rows[2].actions = boot;
			rows[3].kind = ROW_BUTTONS; rows[3].label = "Save Settings"; rows[3].actions = SAVE; rows[3].count = 3;
			rows[4].kind = ROW_BUTTONS; rows[4].label = "Load Settings"; rows[4].actions = SAVE; rows[4].count = 3;
			rows[5].kind = ROW_BUTTONS; rows[5].label = "Select Language"; rows[5].actions = cyc; rows[5].count = 1;
			p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
			p.tabs = tabs; p.nTabs = 5; p.curTab = 4; p.footer = SF; p.focus = FOCUS_ROWS;
			if (t == 0 && layoutPage(p, kFont, pl) >= 0) {
				const Control &t0 = pl.c[pl.slot[0][0]], &t2 = pl.c[pl.slot[0][2]];
				const Control &b0 = pl.c[pl.slot[1][0]], &b3 = pl.c[pl.slot[1][3]];
				const Control &o0 = pl.c[pl.slot[2][0]], &ox = pl.c[pl.slot[2][2]];
				const Control &s0 = pl.c[pl.slot[3][0]], &s2 = pl.c[pl.slot[3][2]];
				const Control &l0 = pl.c[pl.slot[5][0]];
				printf("  System: top x %d..%d; BIOS x %d..%d; Boot x %d..%d (Execute %d..%d); Save x %d..%d; Language x %d..%d y %d..%d;"
				       " labels %d %d %d %d %d\n",
				       (int)t0.rect.x, (int)(t2.rect.x + t2.rect.w), (int)b0.rect.x, (int)(b3.rect.x + b3.rect.w),
				       (int)o0.rect.x, (int)(ox.rect.x + ox.rect.w), (int)ox.rect.x, (int)(ox.rect.x + ox.rect.w),
				       (int)s0.rect.x, (int)(s2.rect.x + s2.rect.w), (int)l0.rect.x, (int)(l0.rect.x + l0.rect.w),
				       (int)l0.rect.y, (int)(l0.rect.y + l0.rect.h),
				       kFont.width("Select BIOS", 1.0f, NULL), kFont.width("Boot Through BIOS", 1.0f, NULL),
				       kFont.width("Save Settings", 1.0f, NULL), kFont.width("Load Settings", 1.0f, NULL),
				       kFont.width("Select Language", 1.0f, NULL));
			}
			stdTabProblems("System tab", p);
		}
	}

	/* the overflow must be caught: the old "Force NTSC" after a 200 px cycle ends at x 644 */
	memset(rows, 0, sizeof rows);
	{
		static const ActionButton OLD[] = { { "Auto", 200, false }, { "Force NTSC", 0, false } };
		Layout lay;
		rows[0].kind = ROW_BUTTONS; rows[0].label = "Frame Rate"; rows[0].actions = OLD; rows[0].count = 2;
		p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
		if (layoutPage(p, kFont, lay) < 0 || !stdHas(p, lay, PROB_PAST_RIGHT))
			fail("Video tab", "the check does not catch Frame Rate + Force NTSC passing x 616");
	}

	printf("standard Settings tabs: Video, Controls, Saves, Audio and System on the real font, nothing past x 616 ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/*  The explorer (DENSITY_EXPLORER, menu standard step 3p slice 1): the file browser's page  */

struct ExpItem {
	const char *text;
	bool        folder;
	const char *tag;
	const char *note;
};

static void expItemFn(void *ctx, int index, ListItem &out)
{
	const ExpItem &e = ((const ExpItem *)ctx)[index];
	out.text = e.text;
	out.folder = e.folder;
	out.tag = e.tag;
	out.note = e.note;
}

/* The control of a given text role on window slot k, or -1. */
static int expText(const Layout &lay, CtrlRole role, int k)
{
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].kind == CTRL_TEXT && lay.c[i].role == role && lay.c[i].slot == k)
			return i;
	return -1;
}

static void expCheckPage(const char *where, const Page &page, const Layout &lay, const ListState &st,
                         const ExpItem *items, int nTabs)
{
	Problem probs[MAX_PROBLEMS];
	const int np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	const int top = st.top;

	if (np != 0) {
		char buf[96];
		sprintf(buf, "checkLayout found %d problem(s); first: %s", np, problemName(probs[0].kind));
		fail(where, buf);
	}

	for (int i = 0; i < lay.n; i++) {
		const Rect &r = lay.c[i].rect;
		if (r.x + r.w > 616.0f + 0.01f)
			fail(where, "a control reaches past x 616");
	}
	/* The explorer carries no logo: its last row and the Next arrow (x to 616, y to 398) and its tab strip
	 * would meet the standard's logo box (x 572-636, y 384-472) or the top-right one (x 534-606, y 34-106).
	 * With LOGO_SPOT_NONE no logo is drawn and checkLayout's logo checks have nothing to test. */
	if (page.logo != LOGO_SPOT_NONE)
		fail(where, "the explorer page has a logo");

	for (int k = 0; k < st.slots; k++) {
		const int c = lay.listItem[k];
		const int index = top + k;
		if (c < 0) { fail(where, "a window slot has no control"); return; }
		if (!nearly(lay.c[c].rect.x, (float)STD_LIST_X0) || !nearly(lay.c[c].rect.w, (float)STD_LIST_W) ||
		    !nearly(lay.c[c].rect.y, (float)STD_LIST_Y0 + (float)STD_LIST_PITCH * (float)k) ||
		    !nearly(lay.c[c].rect.h, (float)STD_LIST_H))
			fail(where, "a row's box is not 576 x STD_LIST_H at STD_LIST_PITCH from y 126");
		if (k + 1 < st.slots && lay.listItem[k + 1] >= 0) {
			const float gap = lay.c[lay.listItem[k + 1]].rect.y - (lay.c[c].rect.y + lay.c[c].rect.h);
			if (gap < -0.01f)
				fail(where, "two list rows overlap");
			if (gap > 0.01f)
				fail(where, "two list rows have a gap (the explorer rows touch)");
		}
		if (index >= st.count) {
			if (lay.c[c].focusable)
				fail(where, "a row past the end of the list is focusable");
			continue;
		}
		{
			const ExpItem &e = items[index];
			const Control &it = lay.c[c];
			const int tg = expText(lay, ROLE_LIST_TAG, k);
			const int nt = expText(lay, ROLE_LIST_NOTE, k);
			const float wantX = e.folder ? (float)STD_LIST_FOLDER_X : (float)STD_LIST_NAME_X;
			const int nameW = fontWidth(it.text, grid::TEXT_SCALE, NULL);

			if (!it.focusable || it.folder != e.folder)
				fail(where, "a row is not focusable, or its folder flag is wrong");
			if (!it.full || strcmp(it.full, e.text) != 0)
				fail(where, "Control::full is not the whole name");
			if (!nearly(it.textX, wantX))
				fail(where, "a name does not start at the folder or game x");
			if ((e.tag != NULL) != (tg >= 0) || (e.note != NULL) != (nt >= 0))
				fail(where, "a tag or note control is missing, or there is one for nothing");
			if (nameW > (int)(it.textR - it.textX))
				fail(where, "a cut name is wider than its room");
			if (tg >= 0) {
				const Rect &t = lay.c[tg].rect;
				if (!nearly(t.x + t.w, (float)STD_LIST_TAG_R))
					fail(where, "a tag does not end at the tag's right edge");
				if (it.textR > t.x - (float)STD_LIST_NAME_GAP + 0.01f)
					fail(where, "a name's room is not 16 px short of its tag");
			}
			if (nt >= 0) {
				const Rect &n = lay.c[nt].rect;
				const float wantNoteR = tg >= 0 ? lay.c[tg].rect.x - (float)STD_LIST_NOTE_GAP : (float)STD_LIST_NOTE_R;
				if (!nearly(n.x + n.w, wantNoteR))
					fail(where, "a note does not end 10 px left of its tag (or at the note edge without a tag)");
				if (it.textR > n.x - (float)STD_LIST_NAME_GAP + 0.01f)
					fail(where, "a name's room is not 16 px short of its note");
				if (tg >= 0 && n.x + n.w > lay.c[tg].rect.x)
					fail(where, "a note is not left of its tag");
			}
			if (strcmp(it.text, e.text) != 0) {
				const size_t n = strlen(it.text);
				if (n < 3 || strcmp(it.text + n - 3, "...") != 0 || strncmp(it.text, e.text, n - 3) != 0)
					fail(where, "a cut name is not a prefix of the name followed by \"...\"");
			}
		}
	}
	(void)nTabs;
}

static void expCheckLinks(const char *where, const Page &page, const Layout &lay, const ListState &st)
{
	int first = -1, last = -1, nAct = 0;
	const int prev = lay.slot[0][0], next = lay.slot[0][1];
	const int def = lay.defaultFocus;

	for (int k = 0; k < st.slots; k++) {
		const int c = lay.listItem[k];
		if (c >= 0 && lay.c[c].focusable) {
			if (first < 0) first = c;
			last = c;
			nAct++;
		}
	}
	if (first < 0) { fail(where, "no focusable row"); return; }

	if (def < 0 || lay.c[def].role != ROLE_LIST_ITEM)
		fail(where, "the default focus is not a row");
	for (int t = 0; t < page.nTabs; t++) {
		const int c = lay.tab[t];
		if (c < 0 || !lay.c[c].focusable) { fail(where, "a tab is not focusable"); continue; }
		if (lay.c[c].link[DIR_DOWN] != def)
			fail(where, "Down from a tab does not go to the default row");
		if (lay.c[c].link[DIR_UP] != -1)
			fail(where, "a tab has an Up link");
		if (lay.c[c].link[DIR_RIGHT] != lay.tab[(t + 1) % page.nTabs] ||
		    lay.c[c].link[DIR_LEFT] != lay.tab[(t + page.nTabs - 1) % page.nTabs])
			fail(where, "Left or Right on a tab does not go to the next or previous tab");
	}
	if (lay.c[first].link[DIR_UP] != lay.tab[page.curTab])
		fail(where, "Up from the first row does not go to the lit tab");
	if (lay.c[last].link[DIR_DOWN] != first)
		fail(where, "Down from the last row does not wrap to the first");
	for (int k = 0, prevC = -1; k < st.slots; k++) {
		const int c = lay.listItem[k];
		if (c < 0 || !lay.c[c].focusable)
			continue;
		if (prevC >= 0 && (lay.c[c].link[DIR_UP] != prevC || lay.c[prevC].link[DIR_DOWN] != c))
			fail(where, "two neighbouring rows are not linked Up and Down");
		prevC = c;
		if (prev >= 0 && lay.c[prev].focusable && lay.c[c].link[DIR_LEFT] != prev)
			fail(where, "Left from a row does not go to the Prev arrow");
		if (next >= 0 && lay.c[next].focusable && lay.c[c].link[DIR_RIGHT] != next)
			fail(where, "Right from a row does not go to the Next arrow");
	}
	/* the arrows keep their links while inactive (focus stays on one after A turns the page) */
	if (prev < 0 || next < 0) { fail(where, "an arrow is missing"); return; }
	if (lay.c[prev].link[DIR_RIGHT] != first || lay.c[prev].link[DIR_LEFT] != first ||
	    lay.c[prev].link[DIR_DOWN] != first)
		fail(where, "the Prev arrow does not go back to the first row (Down, Left, Right)");
	if (lay.c[prev].link[DIR_UP] != lay.tab[page.curTab])
		fail(where, "Up from the Prev arrow does not go to the lit tab");
	if (lay.c[next].link[DIR_UP] != last || lay.c[next].link[DIR_LEFT] != last)
		fail(where, "Up and Left from the Next arrow do not go to the last row");
	if (lay.c[next].link[DIR_DOWN] != -1 || lay.c[next].link[DIR_RIGHT] != -1)
		fail(where, "the Next arrow has a Down or Right link");
	(void)nAct;
}

static void testStdExplorer(void)
{
	static const char *const TABS[] = { "SD", "USB", "DVD", "Network", "Recent" };
	static const char *const LONG_NAME =
	    "Final Fantasy VII International (Disc 1) (Rev 2) (Special Limited Edition With A Name Far Too Long For One Row).chd";
	static const char *const LONG_FOLDER =
	    "A folder with an absurdly long name that goes on and on and on past every limit the row has, "
	    "so that the buffer of the cut name is the thing that ends it, long before the text itself does ......";
	static const char *const UTF8_NAME =
	    "Caf\xC3\xA9 \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
	    "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
	    "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 end";
	static const ExpItem ITEMS[] = {
		{ "..",                      true,  NULL,  NULL },
		{ "Games",                   true,  NULL,  NULL },
		{ "Crash Bandicoot 3",       false, "BIN", NULL },
		{ "Final Fantasy VII",       false, "CHD", "3 discs" },
		{ LONG_NAME,                 false, "CHD", "3 discs" },
		{ LONG_FOLDER,               true,  NULL,  NULL },
		{ UTF8_NAME,                 false, "PBP", "12 Mar" },
		{ "Spyro 2",                 false, "ISO", NULL },
		{ "Tekken 3",                false, NULL,  "2 discs" },
		{ "Gex",                     false, "CUE", NULL },
		{ "Vagrant Story",           false, "ECM", NULL },
	};
	const int before = failures;
	Tab tabs[5];
	Row row;
	Page page;
	Layout lay;
	ListState st;
	ListSpec spec;

	for (int i = 0; i < 5; i++) { tabs[i].text = TABS[i]; tabs[i].width = 0; }

	memset(&spec, 0, sizeof spec);
	st.count = (int)COUNT(ITEMS);
	st.slots = STD_LIST_ROWS;
	st.top = 0;
	st.focus = -1;
	spec.state = &st;
	spec.mode = LIST_PAGED;
	spec.item = expItemFn;
	spec.ctx = (void *)ITEMS;
	spec.prev.text = "^";
	spec.next.text = "v";		/* the geometry fields stay 0: the explorer ignores them */

	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.tabs = tabs;
	page.nTabs = 5;
	page.curTab = 1;
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_NONE;
	page.focus = FOCUS_ROWS;
	page.standard = DENSITY_EXPLORER;

	/* page 1 of 2: every item kind, the long names cut, the tag rightmost */
	if (layoutPage(page, kFont, lay) < 0) {
		fail("Explorer", "the explorer page does not lay out");
		return;
	}
	linkFocus(page, lay);
	expCheckPage("Explorer p1", page, lay, st, ITEMS, 5);
	expCheckLinks("Explorer p1", page, lay, st);
	{
		const Control &dd = lay.c[lay.listItem[0]];
		const Control &gm = lay.c[lay.listItem[2]];
		const Control &lg = lay.c[lay.listItem[4]];
		const Control &lf = lay.c[lay.listItem[5]];
		const Control &u8 = lay.c[lay.listItem[6]];
		if (!dd.folder || strcmp(dd.text, "..") != 0 || !nearly(dd.textX, (float)STD_LIST_FOLDER_X))
			fail("Explorer p1", "the \"..\" row is not a folder row with its text whole");
		if (gm.folder || strcmp(gm.text, "Crash Bandicoot 3") != 0)
			fail("Explorer p1", "a short game name was changed");
		if (strcmp(lg.text, LONG_NAME) == 0 || strlen(lg.text) < 4 || strcmp(lg.text + strlen(lg.text) - 3, "...") != 0)
			fail("Explorer p1", "a long game name was not cut with \"...\"");
		if (strcmp(lf.text, LONG_FOLDER) == 0 || strcmp(lf.text + strlen(lf.text) - 3, "...") != 0 ||
		    strlen(lf.text) > (size_t)STD_CUT_LEN - 1)
			fail("Explorer p1", "a long folder name was not cut with \"...\" inside the buffer");
		/* the cut never splits a UTF-8 sequence: every 0xC3 is followed by 0xA9 */
		for (const unsigned char *s = (const unsigned char *)u8.text; *s; s++) {
			if (*s == 0xC3 && s[1] != 0xA9)
				fail("Explorer p1", "a cut name splits a UTF-8 character");
			if (*s >= 0x80 && *s != 0xC3 && *s != 0xA9)
				fail("Explorer p1", "a cut name has a stray byte");
		}
		if (strcmp(u8.text, UTF8_NAME) == 0)
			fail("Explorer p1", "the UTF-8 name was not cut");
		/* a row with a tag leaves less room than one without */
		if (!(lay.c[lay.listItem[3]].textR < lay.c[lay.listItem[1]].textR))
			fail("Explorer p1", "a row with a tag and a note has no less room than a bare one");
		/* a row's tag and note sit on its own row */
		{
			const int tg = expText(lay, ROLE_LIST_TAG, 3), nt = expText(lay, ROLE_LIST_NOTE, 3);
			if (tg < 0 || nt < 0 || lay.c[tg].rect.y < lay.c[lay.listItem[3]].rect.y ||
			    lay.c[tg].rect.y + lay.c[tg].rect.h > lay.c[lay.listItem[3]].rect.y + (float)STD_LIST_H)
				fail("Explorer p1", "a tag is not inside its row's height");
		}
	}
	/* Page arrows: Prev off at the top, Next on */
	if (lay.slot[0][0] < 0 || lay.c[lay.slot[0][0]].focusable || lay.slot[0][1] < 0 || !lay.c[lay.slot[0][1]].focusable)
		fail("Explorer p1", "Prev should be inactive and Next active on the first page");
	if (!nearly(lay.c[lay.slot[0][0]].rect.x, (float)STD_SCROLL_X) || !nearly(lay.c[lay.slot[0][0]].rect.w, (float)STD_SCROLL_W) ||
	    !nearly(lay.c[lay.slot[0][0]].rect.y, (float)STD_LIST_Y0) ||
	    !nearly(lay.c[lay.slot[0][1]].rect.y + lay.c[lay.slot[0][1]].rect.h,
	            (float)STD_LIST_Y0 + (float)(STD_LIST_ROWS - 1) * (float)STD_LIST_PITCH + (float)STD_LIST_H))
		fail("Explorer p1", "the arrows are not at the two ends of the scroll column");

	/* the tab strip: five equal tabs do not fit, so each tab has its own width (text + 16), gap 12, centred,
	 * inside x 24..616 (Monty 2026-11-05) */
	for (int t = 1; t < 5; t++) {
		const float gap = lay.c[lay.tab[t]].rect.x - (lay.c[lay.tab[t - 1]].rect.x + lay.c[lay.tab[t - 1]].rect.w);
		if (!nearly(gap, (float)STD_TAB_GAP))
			fail("Explorer tabs", "the gap between two tabs is not 12");
	}
	if (lay.c[lay.tab[0]].rect.x < 24.0f - 0.01f || lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w > 616.0f + 0.01f)
		fail("Explorer tabs", "the tab strip is not inside x 24..616");
	if (fabsf(lay.c[lay.tab[0]].rect.x - ((float)MENU_W - (lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w))) > 1.01f)
		fail("Explorer tabs", "the tab strip is not centred");
	printf("  explorer tab widths: %d %d %d %d %d (x %d..%d)\n", (int)lay.c[lay.tab[0]].rect.w, (int)lay.c[lay.tab[1]].rect.w,
	       (int)lay.c[lay.tab[2]].rect.w, (int)lay.c[lay.tab[3]].rect.w, (int)lay.c[lay.tab[4]].rect.w,
	       (int)lay.c[lay.tab[0]].rect.x, (int)(lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w));

	/* the state's own focus: Down from a tab goes to that row */
	st.focus = 3;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	if (lay.defaultFocus != lay.listItem[3] || lay.c[lay.tab[0]].link[DIR_DOWN] != lay.listItem[3])
		fail("Explorer focus", "Down from a tab does not go to the row the state names");
	if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.tab[1])
		fail("Explorer focus", "Up from the first row does not go to the lit tab");
	st.focus = -1;

	/* page 2 of 2: four rows, Prev on, Next off, the rest of the window inactive */
	st.top = 7;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	expCheckPage("Explorer p2", page, lay, st, ITEMS, 5);
	expCheckLinks("Explorer p2", page, lay, st);
	if (!lay.c[lay.slot[0][0]].focusable || lay.c[lay.slot[0][1]].focusable)
		fail("Explorer p2", "Prev should be active and Next inactive on the last page");
	for (int k = 4; k < 7; k++)
		if (lay.c[lay.listItem[k]].focusable || lay.c[lay.listItem[k]].text[0] != '\0')
			fail("Explorer p2", "a row past the end is not empty and inactive");
	st.top = 0;

	/* the last page with fewer items (page 2 above: 4 rows): nothing past them is focusable, no link
	 * reaches it, and the arrows' links keep going to the shown rows */
	st.top = 7;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++) {
			const int to = lay.c[i].link[d];
			for (int k = 4; k < 7; k++)
				if (to == lay.listItem[k])
					fail("Explorer p2", "a link goes to a row past the end of the list");
		}
	if (lay.c[lay.slot[0][1]].link[DIR_UP] != lay.listItem[3] || lay.c[lay.slot[0][1]].link[DIR_LEFT] != lay.listItem[3])
		fail("Explorer p2", "the Next arrow does not go up to the last shown row");
	st.top = 0;

	/* a list shorter than the window (3 items): both arrows off, the rest of the rows off, links to the 3 */
	{
		ListState ss = st;
		ListSpec ssp = spec;
		Row srow = row;
		Page sp = page;
		ss.count = 3;
		ssp.state = &ss;
		srow.list = &ssp;
		sp.rows = &srow;
		layoutPage(sp, kFont, lay);
		linkFocus(sp, lay);
		expCheckLinks("Explorer short", sp, lay, ss);
		if (lay.c[lay.slot[0][0]].focusable || lay.c[lay.slot[0][1]].focusable)
			fail("Explorer short", "an arrow is active on a list that fits the window");
		for (int k = 0; k < 7; k++)
			if (lay.c[lay.listItem[k]].focusable != (k < 3))
				fail("Explorer short", "the rows past the 3 items are not off, or a shown one is");
	}

	/* the other tab lit: Up from the first row follows it */
	page.curTab = 4;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.tab[4])
		fail("Explorer tab 4", "Up from the first row does not follow the lit tab");
	page.curTab = 1;

	/* an empty list (the Recent tab with nothing in it): still lays out, tabs reachable, no problem */
	{
		ListState es = st;
		ListSpec esp = spec;
		Row erow = row;
		Page ep = page;
		Problem probs[MAX_PROBLEMS];
		es.count = 0;
		esp.state = &es;
		erow.list = &esp;
		ep.rows = &erow;
		if (layoutPage(ep, kFont, lay) < 0) {
			fail("Explorer empty", "an empty list does not lay out");
		} else {
			linkFocus(ep, lay);
			if (checkLayout(ep, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Explorer empty", "an empty list has layout problems");
			if (lay.defaultFocus < 0 || lay.c[lay.defaultFocus].role != ROLE_TAB)
				fail("Explorer empty", "an empty list's default focus is not a tab");
		}
	}

	/* no tabs (Accounts mode): a title instead, the rows' first Up has nowhere to go */
	{
		Page ap = page;
		ap.tabs = NULL;
		ap.nTabs = 0;
		ap.title = "Accounts";
		if (layoutPage(ap, kFont, lay) < 0) {
			fail("Explorer title", "the explorer with a title does not lay out");
		} else {
			Problem probs[MAX_PROBLEMS];
			linkFocus(ap, lay);
			if (checkLayout(ap, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Explorer title", "the explorer with a title has layout problems");
			if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.listItem[STD_LIST_ROWS - 1])
				fail("Explorer title", "Up from the first row does not wrap to the last");
		}
	}

	/* the Players account page (AccountFrame): "Account for <Mii>", a few plain rows, no tags, no tabs */
	{
		static const ExpItem ACC[] = {
			{ "None: earn Local", false, NULL, NULL },
			{ "Ann", false, NULL, NULL },
			{ "Bob", false, NULL, NULL },
			{ "This Wii's account", false, NULL, NULL },
		};
		ListState as;
		ListSpec asp;
		Row arow;
		Page ap = page;
		Problem probs[MAX_PROBLEMS];
		memset(&asp, 0, sizeof asp);
		as.count = (int)COUNT(ACC);
		as.slots = STD_LIST_ROWS;
		as.top = 0;
		as.focus = -1;
		asp.state = &as;
		asp.mode = LIST_PAGED;
		asp.item = expItemFn;
		asp.ctx = (void *)ACC;
		asp.prev.text = "^";
		asp.next.text = "v";
		memset(&arow, 0, sizeof arow);
		arow.kind = ROW_LIST;
		arow.list = &asp;
		ap.rows = &arow;
		ap.tabs = NULL;
		ap.nTabs = 0;
		ap.title = "Account for Player One";
		if (layoutPage(ap, kFont, lay) < 0) {
			fail("Account page", "the account page does not lay out");
		} else {
			linkFocus(ap, lay);
			if (checkLayout(ap, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Account page", "the account page has layout problems");
			if (lay.defaultFocus != lay.listItem[0])
				fail("Account page", "the default focus is not the first row");
			for (int k = 0; k < STD_LIST_ROWS; k++)
				if (lay.c[lay.listItem[k]].focusable != (k < 4))
					fail("Account page", "the rows past the 4 accounts are not off, or a shown one is");
		}
	}

	/* what the explorer refuses */
	{
		Row two[2];
		Page bad = page;
		two[0] = row; two[1] = row;
		bad.rows = two; bad.nRows = 2;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "two list rows were accepted");
		bad = page;
		bad.standard = DENSITY_REGULAR;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "a list on a non-explorer density was accepted");
		bad = page;
		st.slots = STD_LIST_ROWS + 1;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "STD_LIST_ROWS + 1 rows were accepted");
		st.slots = STD_LIST_ROWS;
	}

	printf("standard explorer: every item kind, cut names, tag and note, links, arrows, nothing past x 616 ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/* The explorer's letter strip (stripLetter, stripMask, stripFirst, stripStep, stripCell, stripHit) and
 * the scroll thumb (scrollThumb, scrollDragPage). */
static const char *const kStripNames[] = {
	"..", "Formats", "Tests", "Alpha (USA).cue", "alpine.chd", "Castlevania (USA).chd", "Crash 3.cue",
	"Zelda.iso", "1942.cue", "[demo].bin", "\xC3\x89t\xC3\xA9.chd", "Final Fantasy VIII (USA).pbp",
	"Frogger.pbp", "Spyro (USA).chd"
};

static const char *stripNameAt(void *, int i)
{
	return kStripNames[i];
}

/* The facts strip: the panel under the list, the title line, six fact cells in two columns. */
/* A list item with dim set (Recent: its device is not there) lays out dim, and no other row does. */
static const bool DIM_ROWS[4] = { false, true, false, true };
static void dimItemFn(void *, int index, ListItem &out)
{
	static const char *const names[4] = { "Crash 3", "FF8 (usb)", "Gex", "Spyro (smb)" };
	out.text = names[index];
	out.folder = false;
	out.dim = DIM_ROWS[index];
	out.tag = "BIN";
	out.note = index == 1 ? "USB, 2026-09-28" : "today";
}

static void testCountText(void)
{
	const int before = failures;
	char out[64];
	const float s = (float)STD_PATH_PX / 24.0f;
	const int room = 440;		/* what FileBrowserFrame gives the count: the path keeps the rest of x 24..616 */
	struct Case { int folders, games, hint, cell; const char *want; };
	static const Case cases[] = {
		{ 0, 42, COUNT_HINT_AZ,      -1, "42 games, A-Z" },
		{ 0, 1,  COUNT_HINT_AZ,      -1, "1 game, A-Z" },
		{ 0, 42, COUNT_HINT_FOLDERS, -1, "42 games, folders first" },
		{ 1, 7,  COUNT_HINT_FOLDERS, -1, "1 folder, 7 games, folders first" },
		{ 3, 0,  COUNT_HINT_AZ,      -1, "3 folders, A-Z" },
		{ 0, 9,  COUNT_HINT_NEWEST,  -1, "9 games, newest first" },
		{ 0, 3,  COUNT_HINT_NEWEST,   3, "3 games, C" },
		{ 0, 2,  COUNT_HINT_NEWEST,   0, "2 games, #" },
		{ 0, 5,  COUNT_HINT_NONE,    -1, "5 games" },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		const Case &c = cases[i];
		countText(kFont, s, room, c.folders, c.games, c.hint, c.cell, out, sizeof out);
		if (strcmp(out, c.want)) {
			char msg[160];
			snprintf(msg, sizeof msg, "case %u: got \"%s\", want \"%s\"", i, out, c.want);
			fail("countText", msg);
		}
	}
	/* the worst case fits the room, and a tiny room drops the hint, then cuts with ... */
	countText(kFont, s, room, 20, 100, COUNT_HINT_FOLDERS, -1, out, sizeof out);
	if (fontWidth(out, s, NULL) > room || strcmp(out, "20 folders, 100 games, folders first"))
		fail("countText", "the longest count does not fit x 24..616 with the path's share");
	countText(kFont, s, fontWidth("42 games", s, NULL) + 2, 0, 42, COUNT_HINT_AZ, -1, out, sizeof out);
	if (strcmp(out, "42 games"))
		fail("countText", "a hint that does not fit is not dropped");
	countText(kFont, s, 40, 0, 42, COUNT_HINT_AZ, -1, out, sizeof out);
	if (fontWidth(out, s, NULL) > 40)
		fail("countText", "a tiny room is not cut");
	printf("explorer count text with sort hint, Recent hint, letter filter, fits the room %s\n",
	       failures == before ? "ok" : "FAILED");
}

static void testStdDim(void)
{
	const int before = failures;
	static const char *const TABS[] = { "SD", "USB", "DVD", "Network", "Recent" };
	Tab tabs[5];
	Row row;
	Page page;
	Layout lay;
	ListState st;
	ListSpec spec;

	for (int i = 0; i < 5; i++) { tabs[i].text = TABS[i]; tabs[i].width = 0; }
	memset(&spec, 0, sizeof spec);
	st.count = 4;
	st.slots = STD_LIST_ROWS;
	st.top = 0;
	st.focus = -1;
	spec.state = &st;
	spec.mode = LIST_PAGED;
	spec.item = dimItemFn;
	spec.prev.text = "^";
	spec.next.text = "v";
	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.tabs = tabs;
	page.nTabs = 5;
	page.curTab = 4;
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_NONE;
	page.focus = FOCUS_ROWS;
	page.standard = DENSITY_EXPLORER;

	if (layoutPage(page, kFont, lay) < 0) {
		fail("Dim", "the page does not lay out");
		return;
	}
	for (int k = 0; k < st.slots; k++) {
		const bool want = k < 4 && DIM_ROWS[k];
		if (lay.c[lay.listItem[k]].dim != want)
			fail("Dim", "a row's dim flag is not the item's (or an empty slot is dim)");
	}
	/* the other lists never dim: the older list layout, with an item that leaves dim unset */
	if (failures == before)
		printf("Dim: ok\n");
}

static void testStdFacts(void)
{
	const int before = failures;
	FactsBox b;
	factsLayout(b);

	/* the path and count line clears the letter strip (the 2 px move), and the panel clears the list */
	if ((int)(STD_PATH_Y + STD_PATH_PX) > (int)STD_STRIP_Y)
		fail("Facts", "the path line (y + size) reaches the letter strip");
	if (b.y0 < (int)(STD_LIST_Y0 + (STD_LIST_ROWS - 1) * STD_LIST_PITCH + STD_LIST_H) + 10)
		fail("Facts", "the panel is less than 10 px below the last list row");
	if (b.y1 > 472 || b.x0 < 24 || b.x1 > 616 || b.y0 >= b.y1 || b.x0 >= b.x1)
		fail("Facts", "the panel leaves the 24..616 x 472 safe area or is empty");

	/* the title: inside the panel, above the first fact line */
	if (b.titleY < b.y0 || b.titleY + (int)STD_FACTS_TITLE_PX >= b.cell[0].y)
		fail("Facts", "the title is outside the panel or touches the first fact line");
	if (b.titleX < b.x0 || b.titleX + b.titleRoom > b.x1)
		fail("Facts", "the title's room leaves the panel");

	/* the cells: inside the panel, lines do not overlap, columns do not meet */
	for (int k = 0; k < STD_FACTS_N; k++) {
		const FactsCell &c = b.cell[k];
		if (c.y < b.y0 || c.y + (int)STD_FACTS_PX > b.y1)
			fail("Facts", "a fact line is outside the panel");
		if (c.valX + c.valRoom > b.x1)
			fail("Facts", "a value's room leaves the panel");
		if (c.keyR >= c.valX || c.keyR - 100 < b.x0)
			fail("Facts", "a key and its value are not in order or the key leaves the panel");
		if (k % 3 > 0 && c.y - b.cell[k - 1].y < (int)STD_FACTS_PX)
			fail("Facts", "two fact lines overlap (pitch below the text size)");
		if (k % 3 == 0 && c.y != b.cell[0].y)
			fail("Facts", "the columns' first lines are not level");
	}
	{
		const int keyW = fontWidth("Achievements", (float)STD_FACTS_PX / 24.0f, NULL);
		const int col1ValueEnd = b.cell[0].valX + b.cell[0].valRoom;
		const int col2KeyLeft = b.cell[3].keyR - keyW;
		if (col1ValueEnd >= col2KeyLeft)
			fail("Facts", "the first column's values reach the second column's keys");
	}

	/* fitText: a short name as it is, a long one cut with "..." inside the room */
	{
		char buf[128];
		const float s = (float)STD_FACTS_TITLE_PX / 24.0f;
		const char *shortName = "Spyro 2.chd";
		const char *longName = "Final Fantasy VII - International Edition (Japan) (Disc 1) (Rev 1) (Alt) (Beta 3) - Final Fantasy VII - International Edition (Japan) (Disc 2) (Rev 2) (Alt 2) (Beta 4).chd";
		const char *r = fitText(kFont, shortName, s, b.titleRoom, buf, sizeof buf);
		if (strcmp(r, shortName))
			fail("Facts fitText", "a name that fits was changed");
		r = fitText(kFont, longName, s, b.titleRoom, buf, sizeof buf);
		const int n = (int)strlen(r);
		if (!strcmp(r, longName) || n < 4 || strcmp(r + n - 3, "...") || fontWidth(r, s, NULL) > b.titleRoom)
			fail("Facts fitText", "a long name is not cut with ... inside the room");
	}
	printf("standard explorer facts strip: panel, title, 2 columns, path clears the strip, fitText ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

static void testStdStrip(void)
{
	const int before = failures;
	const int n = (int)COUNT(kStripNames);

	/* the letter of a name */
	if (stripLetter("Alpha") != 1 || stripLetter("alpha") != 1 || stripLetter("zelda") != 26 ||
	    stripLetter("Z") != 26)
		fail("Strip letter", "A, a or Z is on the wrong cell");
	if (stripLetter("1942") != 0 || stripLetter("[demo]") != 0 || stripLetter("\xC3\x89t\xC3\xA9") != 0 ||
	    stripLetter(".hidden") != 0)
		fail("Strip letter", "a digit, a bracket, a UTF-8 name or a dot file is not '#'");
	if (stripLetter("..") != -1 || stripLetter(".") != -1 || stripLetter("") != -1 || stripLetter(NULL) != -1)
		fail("Strip letter", "the up row, an empty name or NULL has a cell");
	if (stripChar(0) != '#' || stripChar(1) != 'A' || stripChar(26) != 'Z' || stripChar(3) != 'C')
		fail("Strip letter", "stripChar is wrong");

	/* dimmed letters: the mask holds exactly the cells with a name */
	const unsigned mask = stripMask(n, stripNameAt, NULL);
	const char *have = "#ACFSTZ";
	for (int k = 0; k < STRIP_CELLS; k++) {
		const bool want = strchr(have, stripChar(k)) != NULL;
		if (((mask >> k) & 1u) != (want ? 1u : 0u)) {
			char msg[64];
			snprintf(msg, sizeof msg, "cell %c is %s", stripChar(k), want ? "dimmed" : "lit");
			fail("Strip mask", msg);
		}
	}
	if (stripMask(0, stripNameAt, NULL) != 0)
		fail("Strip mask", "an empty list has letters");

	/* jump targets: the first item of the letter, in list order */
	if (stripFirst(n, stripNameAt, NULL, 1) != 3)
		fail("Strip jump", "A is not the first A row (Alpha)");
	if (stripFirst(n, stripNameAt, NULL, 3) != 5)
		fail("Strip jump", "C is not Castlevania");
	if (stripFirst(n, stripNameAt, NULL, 6) != 1)
		fail("Strip jump", "F is not the first F row (the Formats folder)");
	if (stripFirst(n, stripNameAt, NULL, 0) != 8)
		fail("Strip jump", "# is not 1942 (the up row is no '#')");
	if (stripFirst(n, stripNameAt, NULL, 2) != -1 || stripFirst(n, stripNameAt, NULL, 25) != -1)
		fail("Strip jump", "a letter with no entry has a target");
	if (stripFirst(n, stripNameAt, NULL, 26) != 7 || stripFirst(n, stripNameAt, NULL, 19) != 13)
		fail("Strip jump", "Z or S is not at its row");

	/* links: the C stick walks the letters that have entries, both ways, and stops at the ends */
	int chain[STRIP_CELLS], len = 0, cur = -1;
	while ((cur = stripStep(mask, cur, 1)) >= 0 && len < STRIP_CELLS)
		chain[len++] = cur;
	if (len != 7 || chain[0] != 0 || chain[1] != 1 || chain[2] != 3 || chain[3] != 6 || chain[4] != 19 ||
	    chain[5] != 20 || chain[6] != 26)
		fail("Strip links", "the forward chain is not #, A, C, F, S, T, Z");
	cur = -1; len = 0;
	while ((cur = stripStep(mask, cur, -1)) >= 0 && len < STRIP_CELLS)
		chain[len++] = cur;
	if (len != 7 || chain[0] != 26 || chain[6] != 0)
		fail("Strip links", "the backward chain is not Z .. #");
	if (stripStep(mask, 26, 1) != -1 || stripStep(mask, 0, -1) != -1)
		fail("Strip links", "a step past an end did not stop");
	if (stripStep(mask, 3, 1) != 6 || stripStep(mask, 6, -1) != 3 || stripStep(mask, 2, 1) != 3 ||
	    stripStep(mask, 4, -1) != 3)
		fail("Strip links", "a step from a letter or from a dimmed one skips wrongly");
	if (stripStep(0, -1, 1) != -1 || stripStep(0, 5, -1) != -1)
		fail("Strip links", "an empty strip has a step");
	if (stripStep(mask, 3, 0) != -1)
		fail("Strip links", "direction 0 moves");

	/* geometry: 27 cells inside x 24..616, y 94-118, in order, never touching */
	int px = (int)STD_STRIP_X0 - 1;
	for (int k = 0; k < STRIP_CELLS; k++) {
		int x, w;
		stripCell(k, &x, &w);
		if (w < 16 || x <= px || x + w > (int)STD_STRIP_X1 || x < (int)STD_STRIP_X0) {
			char msg[64];
			snprintf(msg, sizeof msg, "cell %c at x %d w %d", stripChar(k), x, w);
			fail("Strip fit", msg);
		}
		if (k > 0 && x - px < STD_STRIP_GAP)
			fail("Strip fit", "two cells touch");
		px = x + w;
		/* the centre and the first pixel hit their own cell; the gap belongs to the cell before it */
		if (stripHit(x, (int)STD_STRIP_Y) != k || stripHit(x + w / 2, (int)STD_STRIP_Y + 10) != k ||
		    stripHit(x + w + STD_STRIP_GAP - 1, (int)STD_STRIP_Y) != k)
			fail("Strip fit", "a point on a cell hits another cell");
	}
	if ((int)STD_STRIP_Y != 94 || (int)(STD_STRIP_Y + STD_STRIP_H) != 118)
		fail("Strip fit", "the strip is not at y 94-118");
	{
		int x, w;
		stripCell(STRIP_CELLS - 1, &x, &w);
		if (x + w + STD_STRIP_GAP != (int)STD_STRIP_X1)
			fail("Strip fit", "the cells do not fill x 24..616");
	}
	/* outside the strip: above, below, left of it, right of it */
	if (stripHit(100, 93) != -1 || stripHit(100, 118) != -1 || stripHit(23, 100) != -1 || stripHit(616, 100) != -1)
		fail("Strip hit", "a point outside the strip hits a cell");
	/* it is below the tab strip (y 18-70) and above the first row (y 126) */
	if ((int)STD_STRIP_Y < (int)(STD_TAB_Y + STD_TAB_H) || (int)(STD_STRIP_Y + STD_STRIP_H) > (int)STD_LIST_Y0)
		fail("Strip fit", "the strip meets the tabs or the first row");

	/* the scroll thumb: by page, and a drag maps back to the page it was drawn for */
	{
		const int y0 = 152, y1 = 376;
		ListState s = { 20, 7, 0, 0 };		/* 3 pages */
		Thumb t = scrollThumb(s, y0, y1);
		if (!t.live || t.y != y0 || t.h != (y1 - y0) * 7 / 20)
			fail("Scroll thumb", "page 1 of 3: not live, not at the top, or the wrong height");
		for (int page = 0; page < 3; page++) {
			s.top = page * 7;
			t = scrollThumb(s, y0, y1);
			if (t.y < y0 || t.y + t.h > y1)
				fail("Scroll thumb", "the thumb leaves the track");
			if (scrollDragPage(s, y0, y1, t.y) != page)
				fail("Scroll thumb", "a drag to where the thumb is changes the page");
		}
		s.top = 14;
		t = scrollThumb(s, y0, y1);
		if (t.y + t.h != y1)
			fail("Scroll thumb", "the last page's thumb is not at the bottom");
		if (scrollDragPage(s, y0, y1, -50) != 0 || scrollDragPage(s, y0, y1, 9999) != 2)
			fail("Scroll thumb", "a drag past the track ends does not clamp to the first and last page");
		s.top = 0;
		const int mid = y0 + ((y1 - y0) - t.h) / 2;
		if (scrollDragPage(s, y0, y1, mid) != 1)
			fail("Scroll thumb", "a drag to the middle is not the middle page");
		ListState all = { 5, 7, 0, 0 };		/* fits: no thumb to drag */
		t = scrollThumb(all, y0, y1);
		if (t.live || t.y != y0 || t.h != y1 - y0 || scrollDragPage(all, y0, y1, 200) != 0)
			fail("Scroll thumb", "a list that fits has a live thumb");
		ListState none = { 0, 7, 0, -1 };
		if (scrollThumb(none, y0, y1).live || scrollDragPage(none, y0, y1, 200) != 0)
			fail("Scroll thumb", "an empty list has a live thumb");
		ListState big = { 450, 7, 63 * 7, 0 };	/* 450 stubs: the 16 px floor */
		t = scrollThumb(big, y0, y1);
		if (t.h != 16 || t.y < y0 || t.y + t.h > y1)
			fail("Scroll thumb", "a long list's thumb is not 16 px inside the track");
		if (scrollDragPage(big, y0, y1, t.y) != 63)
			fail("Scroll thumb", "a long list: a drag to the thumb is another page");
	}

	printf("standard explorer letter strip: letters, dim, jump targets, links, fit in 24..616, scroll thumb ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

int main(int argc, char **argv)
{
	const char *font = argc > 1 ? argv[1] : "fonts/En.dat";

	if (!loadFont(font)) {
		printf("cannot read the font file '%s' (pass its path as the first argument)\n", font);
		return 2;
	}

	testCycle();
	testTabStep();
	testTwoColumns();
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
	checkTabBoxes("Audio", 3, AUDIO_BOXES, COUNT(AUDIO_BOXES), AUDIO_LABEL_Y, COUNT(AUDIO_LABEL_Y));
	checkTabLinks("Audio", 3, AUDIO_BOXES, COUNT(AUDIO_BOXES));
	checkTabBoxes("Saves", 4, SAVES_BOXES, COUNT(SAVES_BOXES), SAVES_LABEL_Y, COUNT(SAVES_LABEL_Y));
	checkTabLinks("Saves", 4, SAVES_BOXES, COUNT(SAVES_BOXES));
	checkButtonRowScreens();
	checkCoopPage();
	checkConfigureInput();
	checkConfigureButtons();
	testEdgeWrap();
	testConfigureButtonStates();
	testLinkOverrides();
	reportGeometry();
	testCheckCatches();
	testInactiveNeverFocus();
	testListWindow();
	testListScroll();
	testListPaging();
	testStandard();
	testStdSettingsTabs();
	testStdAdvancedTabs();
	testStdExplorer();
	testStdStrip();
	testStdFacts();
	testStdDim();
	testCountText();

	printf("\n%s: %d check%s failed\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures > 255 ? 255 : failures;
}
