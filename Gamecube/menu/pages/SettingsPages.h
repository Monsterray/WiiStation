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
#include "../MenuEngine.h"
#include "../../wiiSXconfig.h"	/* the values of the choices */

namespace eng = menu::engine;

enum SetTabId { SET_TAB_GENERAL = 0, SET_TAB_VIDEO, SET_TAB_INPUT, SET_TAB_AUDIO, SET_TAB_SAVES, SET_TAB_COUNT };

/* The tab row in order on screen (Video, Input, Saves, Audio, System): left to right, the tab each position shows. */
static const int SET_TAB_AT_POS[SET_TAB_COUNT] = { SET_TAB_VIDEO, SET_TAB_INPUT, SET_TAB_SAVES, SET_TAB_AUDIO, SET_TAB_GENERAL };
static const char *const SET_TAB_NAMES[SET_TAB_COUNT] = { "System", "Video", "Input", "Audio", "Saves" };

/* A segment's value, where it is not a choice's. SEGV_GAME marks an action that needs a loaded game. */
#define SEGV_FLAG	(-1)
#define SEGV_NEXT	(-2)
#define SEGV_GAME	(-3)

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
#define SEG_CYCLE(key)					{ key, key, SEGV_NEXT, NULL, NULL, NULL, NULL, NULL }
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

/* One row of a tab. label is the text on the left; NULL takes the label of the first button's setting.
 * plain counts the last buttons of a choice row that are actions or flags, not choices. noLit: a choice
 * row whose buttons are actions, none shown as the current value. prefix: small cycles, each after its
 * own text (Memcard Type: "1", "2"). */
struct SetRow
{
	eng::RowKind		kind;
	const char			*label;
	SetSeg				seg[4];
	int					plain;
	bool				noLit;
	const char *const	*prefix;
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
	                           SEG_ACT("ExecuteBios", "Execute BIOS", Func_ExecuteBios) }, 1 },
	{ eng::ROW_BUTTONS, "Save Settings", { SEG_ACT("SaveSettingsSD", "SD", Func_SaveSettingsSD),
	                                       SEG_ACT("SaveSettingsUSB", "USB", Func_SaveSettingsUSB),
	                                       SEG_ACT_GAME("SaveSettingsGame", "This Game", Func_SaveSettingsSeparately) } },
	{ eng::ROW_BUTTONS, "Load Settings", { SEG_ACT("LoadSettingsSD", "SD", Func_LoadSettingsSD),
	                                       SEG_ACT("LoadSettingsUSB", "USB", Func_LoadSettingsUSB),
	                                       SEG_ACT_GAME("LoadSettingsGame", "This Game", Func_LoadSettingsGame) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_ACT("lang", Func_SelectLanguage) } },
};

static const eng::FooterHelp SYS_FOOTER[] =
{
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { "Execute BIOS: start the PS1 BIOS shell (memory cards, CD player).", NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
};

/* The Video and Input tabs are on the menu standard too. */
static const SetRow VID_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("AdvancedGraphics", "Advanced", Func_AdvGfxPage) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("ScreenMode", SCREENMODE_4x3), SEG_SET("ScreenMode", SCREENMODE_16x9),
	                           SEG_SET("ScreenMode", SCREENMODE_16x9_PILLARBOX) } },
	{ eng::ROW_BUTTONS, "Display", { SEG_FLAG("Interlaced"), SEG_FLAG_THEN("TVMode", applyOriginalMode) } },
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
	                           SEG_SET("ControllerType", CONTROLLERTYPE_STICKDPAD) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("RumbleEnabled", RUMBLE_DISABLE), SEG_SET("RumbleEnabled", RUMBLE_ENABLE) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE("LightGun") } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE("LoadButtonSlot") } },
	{ eng::ROW_CHOICE, "Save Configs", { SEG_ACT("SaveButtonsSD", "SD", Func_SaveButtonsSD),
	                                     SEG_ACT("SaveButtonsUSB", "USB", Func_SaveButtonsUSB) }, 0, true },	/* two segments that touch; actions, so none is lit */
};

/* The help under the rows: the focused row's (or, with a tab focused, the first row that has one). */
static const eng::FooterHelp VID_FOOTER[] =
{
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { "Interlaced: full height. 240p: half height, no flicker.", NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
};

static const eng::FooterHelp INP_FOOTER[] =
{
	{ { NULL, NULL } },
	{ { "Analog: a DualShock. Stick Pad: the stick moves the D-pad.", NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
};

static const SetRow AUD_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL, { SEG_ACT("AdvancedSound", "Advanced", Func_AdvancedSound) } },
	/* The file keeps the historical "DisableXa" / "DisableCdda" keys (an inverted sense), so those two lit and click by hook. */
	{ eng::ROW_BUTTONS, "Enable", { SEG_FLAG("Audio"), SEG_FLAG_ACT("DisableXa", Func_ToggleXa, litXa),
	                                SEG_FLAG_ACT("DisableCdda", Func_ToggleCdda, litCdda) } },
	/* Sync is two settings, made exclusive by the hooks: no key of its own */
	{ eng::ROW_CHOICE, "Sync", { SEG_PICK("SyncOff", "Off", Func_SoundSyncOff, litSyncOff),
	                             SEG_FLAG_ACT("SoundTempo", Func_SoundSyncTempo, litSyncTempo),
	                             SEG_FLAG_ACT("SoundRateControl", Func_SoundSyncRate, litSyncRate) } },
	{ eng::ROW_BUTTONS, NULL, { SEG_CYCLE_THEN("Interpolation", applyInterpolation) } },
	{ eng::ROW_CHOICE, NULL, { SEG_SET("SoundDriver", SOUND_HW_ACCEL_ON /* DSP */), SEG_SET("SoundDriver", SOUND_HW_ACCEL_OFF /* CPU */) } },
};

static const eng::FooterHelp AUD_FOOTER[] =
{
	{ { NULL, NULL } },
	{ { "Audio: all sound. XA: speech and music streams. CDDA: CD music tracks.", NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
	{ { NULL, NULL } },
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
	const eng::FooterHelp	*footer;	/* footer[row]: the help of the focused row, or NULL */
};

#define SET_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Indexed by SetTabId. */
static const SetTab SET_TABS_DEF[SET_TAB_COUNT] =
{
	{ GEN_ROWS, SET_COUNT(GEN_ROWS), SYS_FOOTER },
	{ VID_ROWS, SET_COUNT(VID_ROWS), VID_FOOTER },
	{ INP_ROWS, SET_COUNT(INP_ROWS), INP_FOOTER },
	{ AUD_ROWS, SET_COUNT(AUD_ROWS), AUD_FOOTER },
	{ SAV_ROWS, SET_COUNT(SAV_ROWS), NULL },
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

#endif
