/**
 * WiiStation - pages/SettingsPages.h
 *
 * The five Settings tabs (System, Video, Input, Audio, Saves) as plain tables: for each tab its rows, for
 * each row its buttons ("segments"), and what each button does. SettingsFrame.cpp (which defines the hooks
 * and builds the engine page) and the host test tests/menu_pages_test.cpp (which stubs the hooks) include
 * this one file, so the test checks the tables the menu runs.
 *
 * A button is bound to a setting by its key (the OPTIONS table, GamecubeMain.cpp). The key gives its text
 * (a choice's name, or the setting's label for a flag), its lit state, and what a click does:
 *   SEG_SET   a choice: a click sets the setting to that value; lit when the setting holds it
 *   SEGV_FLAG  a flag: a click flips the setting between its lowest and highest value; lit when on
 *   SEG_CYCLE a cycle: a click steps the setting to the next value, wrapping; shows the current value's name
 *   SEG_ACT   an action with no setting: a click runs its function
 * A segment may add a function that replaces the generic click (act: the setting needs more than a write,
 * e.g. the BIOS and the language), a function that decides lit (lit) or text (textFn) where the setting alone
 * does not, and a function that runs after the generic click (apply: tell the emulator).
 *
 * A script presses a button by its name: "menuclick FPS 1" sets Show FPS to 1 (a choice presses the segment
 * with that value); "menuclick SaveSettingsSD 1" presses an action once (any other button n times).
 * A segment's name is its key, or an action's own name.
 *
 * Nothing here needs the Wii: the hooks are declared, not defined.
**/

#ifndef SETTINGSPAGES_H
#define SETTINGSPAGES_H

#include <stddef.h>
#include <string.h>
#include "../MenuEngine.h"
#include "../MenuTypes.h"
#include "../../wiiSXconfig.h"	/* the values of the choices */

namespace eng = menu::engine;
namespace menu { namespace glue { class PageView; } }

enum SetTabId { SET_TAB_GENERAL = 0, SET_TAB_VIDEO, SET_TAB_INPUT, SET_TAB_AUDIO, SET_TAB_SAVES, SET_TAB_COUNT };

/* The tab row in order on screen (Video, Input, Saves, Audio, System): left to right, the tab each position shows. */
static const int SET_TAB_AT_POS[SET_TAB_COUNT] = { SET_TAB_VIDEO, SET_TAB_INPUT, SET_TAB_SAVES, SET_TAB_AUDIO, SET_TAB_GENERAL };
static const char *const SET_TAB_NAMES[SET_TAB_COUNT] = { "System", "Video", "Input", "Audio", "Saves" };

/* Where the Settings frame puts the logo (SettingsFrame::logoSpot): the tab strip reaches x=615. */
static const eng::LogoSpot SET_LOGO = eng::LOGO_SPOT_BOTTOM;

/* A segment's value, where it is not a choice's. SEGV_GAME marks an action that needs a loaded game. */
#define SEGV_FLAG	(-1)
#define SEGV_NEXT	(-2)
#define SEGV_GAME	(-3)
#define SEGV_EACH	(-4)	/* one button per value of the setting (a radio row): the row has this one seg, the slot is the value */

struct SetSeg
{
	const char	*name;		/* what a script calls it; NULL ends a row's list */
	const char	*key;		/* the setting it edits, or NULL for an action */
	int			value;		/* SEG_SET: the choice; else SEGV_FLAG, SEGV_NEXT, or (action) 0 or SEGV_GAME */
	const char	*text;		/* an action's text, or a text that replaces the setting's */
	const char	*(*textFn)(void);	/* the text, when it is worked out (the memory card types) */
	void		(*act)(void);		/* replaces the generic click */
	bool		(*lit)(void);		/* replaces the generic lit test */
	void		(*apply)(void);		/* runs after the generic click */
};

#define SEG_SET(key, v)					{ key, key, v, NULL, NULL, NULL, NULL, NULL }
#define SEG_SET_AS_THEN(key, v, text, apply)	{ key, key, v, text, NULL, NULL, NULL, apply }
#define SEG_SET_THEN(key, v, apply)		{ key, key, v, NULL, NULL, NULL, NULL, apply }
#define SEG_SET_ACT(key, v, act)		{ key, key, v, NULL, NULL, act, NULL, NULL }
#define SEG_FLAG(key)					{ key, key, SEGV_FLAG, NULL, NULL, NULL, NULL, NULL }
#define SEG_FLAG_THEN(key, apply)		{ key, key, SEGV_FLAG, NULL, NULL, NULL, NULL, apply }
#define SEG_FLAG_ACT(key, act, lit)		{ key, key, SEGV_FLAG, NULL, NULL, act, lit, NULL }
#define SEG_EACH(key, apply)			{ key, key, SEGV_EACH, NULL, NULL, NULL, NULL, apply }
#define SEG_CYCLE(key)				{ key, key, SEGV_NEXT, NULL, NULL, NULL, NULL, NULL }
#define SEG_CYCLE_THEN(key, apply)		{ key, key, SEGV_NEXT, NULL, NULL, NULL, NULL, apply }
#define SEG_CYCLE_ACT(key, act)			{ key, key, SEGV_NEXT, NULL, NULL, act, NULL, NULL }
#define SEG_CYCLE_TEXT(key, act, textFn)	{ key, key, SEGV_NEXT, NULL, textFn, act, NULL, NULL }
#define SEG_ACT(name, text, act)		{ name, NULL, 0, text, NULL, act, NULL, NULL }
#define SEG_ACT_GAME(name, text, act)	{ name, NULL, SEGV_GAME, text, NULL, act, NULL, NULL }
#define SEG_PICK(name, text, act, lit)	{ name, NULL, 0, text, NULL, act, lit, NULL }

/* The hooks the tables name. Defined in SettingsFrame.cpp; the host test defines empty ones. */
void Func_PluginsPage(void);
void Func_AchievementsPage(void);
void Func_MemoryPage(void);
void Func_AdvGfxPage(void);
void Func_AdvancedSound(void);
void Func_BiosSelectHLE(void);
void Func_BiosSelectSD(void);
void Func_BiosSelectUSB(void);
void Func_BiosSelectDVD(void);
void Func_ExecuteBios(void);
void Func_SaveSettingsSD(void);
void Func_SaveSettingsUSB(void);
void Func_SaveSettingsSeparately(void);
void Func_LoadSettingsSD(void);
void Func_LoadSettingsUSB(void);
void Func_LoadSettingsGame(void);
void Func_SelectLanguage(void);
void Func_FrameRateCycle(void);
void Func_ForceNTSC(void);
void Func_ConfigureInput(void);
void Func_ConfigureButtons(void);
void Func_SaveButtonsSD(void);
void Func_SaveButtonsUSB(void);
void Func_ToggleXa(void);
void Func_ToggleCdda(void);
void Func_SoundSyncOff(void);
void Func_SoundSyncTempo(void);
void Func_SoundSyncRate(void);
void Func_CardType1(void);
void Func_CardType2(void);
void Func_UsbPadTest(void);
bool litXa(void);
bool litCdda(void);
bool litSyncOff(void);
bool litSyncTempo(void);
bool litSyncRate(void);
const char *textCard0(void);
const char *textCard1(void);
void applyOriginalMode(void);
void applyTrapFilter(void);
void applyBilinear(void);
void applyInterpolation(void);

/* One row of a page, a Settings tab's or an Options page's. kind says what it is: ROW_BUTTONS (buttons, each
 * its own seg), ROW_CHOICE (segs side by side, one lit), ROW_CYCLE (one button stepping a setting: seg[0]
 * is its SEG_CYCLE*), ROW_READOUT (a label and the text info writes; no seg). A radio row of an Options page
 * is ROW_CHOICE with the one seg SEG_EACH. label is the text on the left; NULL takes the label of the first
 * button's setting. plain counts the last buttons of a choice row that are actions or flags, not choices.
 * noLit: a choice row whose buttons are actions, none shown as the current value. prefix: small cycles, each
 * after its own text (Memcard Type: "1", "2"). help: the one or two lines under the rows while the row has
 * focus, or NULL for none. fit: each button of a radio row as wide as its own word. */
struct SetRow
{
	eng::RowKind		kind;
	const char			*label;
	SetSeg				seg[4];
	int					plain;
	bool				noLit;
	const char *const	*prefix;
	const char			*help[2];
	bool				fit;
	void				(*info)(char *buf, int len);	/* ROW_READOUT: writes the right-hand text */
};

/* The System tab is on the menu standard: the engine takes every position and width from the text. The
 * rows are in the display order: Advanced..Memory, Select BIOS, Boot Through BIOS (and Execute BIOS),
 * Save Settings, Load Settings, Select Language. This Game is drawn always, inactive with no game loaded. */
static const SetRow GEN_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("AdvancedPages", "Advanced", Func_PluginsPage),
	                            SEG_ACT("AchievementsPage", "Achievements", Func_AchievementsPage),
	                            SEG_ACT("MemoryPage", "Memory", Func_MemoryPage) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET_ACT("BiosDevice", BIOSDEVICE_HLE, Func_BiosSelectHLE),
	                           SEG_SET_ACT("BiosDevice", BIOSDEVICE_SD, Func_BiosSelectSD),
	                           SEG_SET_ACT("BiosDevice", BIOSDEVICE_USB, Func_BiosSelectUSB),
	                           SEG_ACT("BiosDvd", "DVD", Func_BiosSelectDVD) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("BootThruBios", BOOTTHRUBIOS_NO), SEG_SET("BootThruBios", BOOTTHRUBIOS_YES),
	                           SEG_ACT("ExecuteBios", "Execute BIOS", Func_ExecuteBios) }, 1, false, NULL, { "Boot Through BIOS: runs the BIOS start screen before a game (not HLE).", "Execute BIOS: ends the game, starts the BIOS shell. Needs a BIOS file." } },
	{ eng::ROW_BUTTONS, "Save Settings", { SEG_ACT("SaveSettingsSD", "SD", Func_SaveSettingsSD),
	                                       SEG_ACT("SaveSettingsUSB", "USB", Func_SaveSettingsUSB),
	                                       SEG_ACT_GAME("SaveSettingsGame", "This Game", Func_SaveSettingsSeparately) } },
	{ eng::ROW_BUTTONS, "Load Settings", { SEG_ACT("LoadSettingsSD", "SD", Func_LoadSettingsSD),
	                                       SEG_ACT("LoadSettingsUSB", "USB", Func_LoadSettingsUSB),
	                                       SEG_ACT_GAME("LoadSettingsGame", "This Game", Func_LoadSettingsGame) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_ACT("lang", Func_SelectLanguage) } },
};

/* The Video and Input tabs are on the menu standard too. */
static const SetRow VID_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("AdvancedGraphics", "Advanced", Func_AdvGfxPage) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("ScreenMode", SCREENMODE_4x3), SEG_SET("ScreenMode", SCREENMODE_16x9),
	                           SEG_SET("ScreenMode", SCREENMODE_16x9_PILLARBOX) } },
	{ eng::ROW_BUTTONS, "Display", { SEG_FLAG("Interlaced"), SEG_FLAG_THEN("TVMode", applyOriginalMode) }, 0, false, NULL, { "Interlaced: 480-line pictures drawn one field at a time.", "240p: pictures of 288 lines or fewer show at 240p, on resume." } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_ACT("FrameRate", Func_FrameRateCycle), SEG_FLAG_ACT("ForceNTSC", Func_ForceNTSC, NULL) } },
	{ eng::ROW_CHOICE, "Filters", { SEG_SET_AS_THEN("BilinearFilter", BILINEARFILTER_DISABLE, "Off", applyBilinear),
	                                SEG_SET_THEN("BilinearFilter", BILINEARFILTER_NEAR, applyBilinear),
	                                SEG_SET_THEN("BilinearFilter", BILINEARFILTER_ENABLE, applyBilinear),
	                                SEG_FLAG_THEN("TrapFilter", applyTrapFilter) }, 1 },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("FPS", FPS_HIDE), SEG_SET("FPS", FPS_SHOW) } },
};

static const SetRow INP_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("ConfigureInput", "Configure Input", Func_ConfigureInput),
	                            SEG_ACT("ConfigureButtons", "Configure Buttons", Func_ConfigureButtons) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("ControllerType", CONTROLLERTYPE_STANDARD), SEG_SET("ControllerType", CONTROLLERTYPE_ANALOG),
	                           SEG_SET("ControllerType", CONTROLLERTYPE_STICKDPAD) }, 0, false, NULL, { "Digital: plain pad. Analog: DualShock. Stick Pad: left stick also presses the D-pad." } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("RumbleEnabled", RUMBLE_DISABLE), SEG_SET("RumbleEnabled", RUMBLE_ENABLE) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE("LightGun") } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE("LoadButtonSlot") } },
	{ eng::ROW_CHOICE, "Save Configs", { SEG_ACT("SaveButtonsSD", "SD", Func_SaveButtonsSD),
	                                     SEG_ACT("SaveButtonsUSB", "USB", Func_SaveButtonsUSB) }, 0, true },	/* two segments that touch; actions, so none is lit */
};

static const SetRow AUD_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("AdvancedSound", "Advanced", Func_AdvancedSound) } },
	/* The file keeps the historical "DisableXa" / "DisableCdda" keys (an inverted sense), so those two lit and click by hook. */
	{ eng::ROW_BUTTONS, "Enable", { SEG_FLAG("Audio"), SEG_FLAG_ACT("DisableXa", Func_ToggleXa, litXa),
	                                SEG_FLAG_ACT("DisableCdda", Func_ToggleCdda, litCdda) }, 0, false, NULL, { "Audio: all sound. XA: the disc's speech and music streams.", "CDDA: the CD music tracks. A lit button plays; an unlit one is muted." } },
	/* Sync is two settings, made exclusive by the hooks: no key of its own */
	{ eng::ROW_CHOICE, "Sync", { SEG_PICK("SyncOff", "Off", Func_SoundSyncOff, litSyncOff),
	                             SEG_FLAG_ACT("SoundTempo", Func_SoundSyncTempo, litSyncTempo),
	                             SEG_FLAG_ACT("SoundRateControl", Func_SoundSyncRate, litSyncRate) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_THEN("Interpolation", applyInterpolation) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("SoundDriver", SOUND_HW_ACCEL_ON /* DSP */), SEG_SET("SoundDriver", SOUND_HW_ACCEL_OFF /* CPU */) } },
};

/* Memcard Type is two small cycles, one per card, each after its card number (the engine's Row::prefix). */
static const char *const SAV_PREFIX[2] = { "1", "2" };

static const SetRow SAV_ROWS[] =
{
	{ eng::ROW_CHOICE, NULL, { SEG_SET("AutoSave", AUTOSAVE_DISABLE), SEG_SET("AutoSave", AUTOSAVE_ENABLE) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("NativeDevice", NATIVESAVEDEVICE_SD), SEG_SET("NativeDevice", NATIVESAVEDEVICE_USB),
	                           SEG_SET("NativeDevice", NATIVESAVEDEVICE_CARDA), SEG_SET("NativeDevice", NATIVESAVEDEVICE_CARDB) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_TEXT("Memcard0File", Func_CardType1, textCard0),
	                            SEG_CYCLE_TEXT("Memcard1File", Func_CardType2, textCard1) }, 0, false, SAV_PREFIX },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("StatesDevice", SAVESTATEDEVICE_SD), SEG_SET("StatesDevice", SAVESTATEDEVICE_USB) } },
};

struct SetTab
{
	const SetRow			*rows;
	int						nRows;
};

#define SET_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Indexed by SetTabId. */
static const SetTab SET_TABS_DEF[SET_TAB_COUNT] =
{
	{ GEN_ROWS, SET_COUNT(GEN_ROWS) },
	{ VID_ROWS, SET_COUNT(VID_ROWS) },
	{ INP_ROWS, SET_COUNT(INP_ROWS) },
	{ AUD_ROWS, SET_COUNT(AUD_ROWS) },
	{ SAV_ROWS, SET_COUNT(SAV_ROWS) },
};

/* Pressed by a script only: no tab shows them. */
static const SetSeg SET_SCRIPT_ONLY[] =
{
	SEG_ACT("TestUsbPad", "Test USB Pad", Func_UsbPadTest),	/* the page is on Advanced > Input too; B here goes back to Settings */
};

/* How many buttons a row has (the list ends at a segment with no name). */
static inline int setRowSegs(const SetRow &r)
{
	int n = 0;
	while (n < 4 && r.seg[n].name)
		n++;
	return n;
}

/* How many buttons a choice row shows for a setting: one per value, at most the engine's slots. A row that
 * declares more values than there are slots would silently lose the rest; the clamp is the documented limit. */
static inline int choiceCount(const SettingOption *s)
{
	int n = (int)s->max - (int)s->min + 1;
	if (n < 1) n = 1;
	if (n > eng::MAX_SLOTS) n = eng::MAX_SLOTS;
	return n;
}

/* How many buttons a row has on screen: a readout none, a cycle one, a SEG_EACH row one per value of its
 * setting (set), any other row one per seg. */
static inline int rowSlots(const SetRow &r, const SettingOption *set)
{
	if (r.kind == eng::ROW_READOUT) return 0;
	if (r.kind == eng::ROW_CYCLE) return 1;
	return r.seg[0].value == SEGV_EACH ? choiceCount(set) : setRowSegs(r);
}

/* The shared reader and writer of a row's buttons (SettingsFrame.cpp). The Settings tabs and the Options
 * pages both read and click through these: slot is the button's place in its row (the value of a SEG_EACH
 * button; any other button is its own seg, so 0 will do). */
const SettingOption *segSetting(const SetSeg &g);
const char *segText(const SetSeg &g, int slot);
bool segLit(const SetSeg &g, int slot);
void segPress(const SetSeg &g, int slot);
/* A script's "menuclick name n" on one button: false when g is not that button (see SettingsFrame.cpp). */
bool scriptPress(const SetSeg &g, const char *name, int n);
/* The text and lit state of every button of rows[0..n) (and the readouts) follow the variables. */
void syncRows(menu::glue::PageView &view, const SetRow *rows, int n);

/* ---- The engine page of a table of rows: the Wii's frames and the host test build it here ---- */

/* What the page asks of the running program: a button's text and lit state, the setting a button edits. */
struct SetBind
{
	const char	*(*text)(const SetSeg &, int slot);
	bool		(*lit)(const SetSeg &, int slot);
	const SettingOption	*(*setting)(const SetSeg &);	/* the setting of the button, or NULL */
	const char	*(*readout)(int row);					/* ROW_READOUT: the text of row */
	bool		gameLoaded;
	bool		settingHelp;	/* a row with no help of its own shows its setting's (the OPTIONS table) */
};

struct SetPage
{
	eng::Row			rows[eng::MAX_ROWS];
	eng::FooterHelp		footer[eng::MAX_ROWS];	/* row r's help, from SetRow::help */
	eng::ActionButton	actions[eng::MAX_ROWS][4];
	const char			*names[eng::MAX_ROWS][4];
	eng::Tab			tabs[eng::MAX_TABS];
	eng::Page			page;
};

/* The place of tab id in the tab row. */
static inline int setTabPos(int tabId)
{
	for (int p = 0; p < SET_TAB_COUNT; p++)
		if (SET_TAB_AT_POS[p] == tabId)
			return p;
	return 0;
}

/* Describes row r of rows as an engine row: the text of every button and which are lit, as bind gives them. */
static inline void buildRow(const SetRow &s, int r, const SetBind &bind, SetPage &o)
{
	eng::Row &row = o.rows[r];
	const SettingOption *set = s.kind == eng::ROW_READOUT ? NULL : bind.setting(s.seg[0]);
	const int n = rowSlots(s, set);
	const int nChoices = s.kind == eng::ROW_CHOICE ? n - s.plain : n;
	bool picked = false;

	row.kind    = s.kind;
	row.label   = s.label ? s.label : set ? set->label : NULL;
	row.plain   = s.plain;
	row.prefix  = s.prefix;
	row.fit     = s.fit;
	o.footer[r].line[0] = s.help[0] ? s.help[0] : bind.settingHelp && set ? set->help : NULL;
	o.footer[r].line[1] = s.help[1];
	if (s.kind == eng::ROW_READOUT)
	{
		row.text = bind.readout(r);
		return;
	}
	if (s.kind == eng::ROW_CYCLE || s.seg[0].value == SEGV_EACH)
	{
		/* the engine takes the names and the current value of the setting itself */
		row.names = set->choices;
		row.lo    = set->min;
		row.count = s.kind == eng::ROW_CYCLE ? set->max - set->min + 1 : n;
		row.value = *set->value;
		return;
	}
	row.names   = o.names[r];
	row.count   = n;
	row.actions = o.actions[r];
	for (int c = 0; c < n; c++)
	{
		const SetSeg &g = s.seg[c];
		o.names[r][c] = o.actions[r][c].text = bind.text(g, c);
		/* a cycle has the width of the longest name (the standard's small cycles take their own) */
		o.actions[r][c].width = g.value == SEGV_NEXT && !s.prefix ? STD_CYCLE_W : 0.0f;
		const bool lit = bind.lit(g, c);
		if (s.kind == eng::ROW_CHOICE && c < nChoices)
		{
			if (lit && !picked)
			{
				row.value = c;
				picked = true;
			}
		}
		else
			o.actions[r][c].lit = lit && !s.noLit;
		/* drawn always, inactive with no game loaded */
		if (g.value == SEGV_GAME && !bind.gameLoaded)
			row.inactive |= 1u << c;
	}
}

/* Describes tab tabId as an engine page. */
static inline void buildSettingsPage(int tabId, const SetBind &bind, SetPage &o)
{
	const SetTab &tab = SET_TABS_DEF[tabId];
	memset(&o, 0, sizeof o);
	for (int r = 0; r < tab.nRows; r++)
		buildRow(tab.rows[r], r, bind, o);
	for (int t = 0; t < SET_TAB_COUNT; t++)
	{
		o.tabs[t].text = SET_TAB_NAMES[SET_TAB_AT_POS[t]];	/* a standard page measures the text: no width */
	}
	o.page.title      = NULL;
	o.page.tabs       = o.tabs;
	o.page.nTabs      = SET_TAB_COUNT;
	o.page.curTab     = setTabPos(tabId);
	o.page.rows       = o.rows;
	o.page.nRows      = tab.nRows;
	o.page.logo       = SET_LOGO;
	o.page.focus      = eng::FOCUS_ROWS;
	o.page.standard   = eng::DENSITY_REGULAR;
	o.page.footer     = o.footer;
}

#endif
