/**
 * WiiSX - SettingsFrame.cpp
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

#include <sys/stat.h>

#include "MenuContext.h"
#include "../perf_prof.h"
#include "SettingsFrame.h"
#include "OptionsFrame.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "../libgui/Button.h"
#include "../libgui/TextBox.h"
#include "../libgui/IPLFont.h"
#include <string.h>
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"
#include "../libgui/MessageBox.h"
#include "../libgui/gui2/gettext.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"
#include "../vm/vm.h"

#ifdef SHOW_DEBUG
#include "../DEBUG.h"
#endif // SHOW_DEBUG
extern "C" {
#include "../gc_input/controller.h"
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../fileBrowser/fileBrowser-CARD.h"
#include "../../gpu.h"
#include "../../deps/opengx/GL/gl.h"
}

extern void Func_SetPlayGame();

void Func_TabGeneral();
void Func_TabVideo();
void Func_TabInput();
void Func_TabAudio();
void Func_TabSaves();

void Func_CpuInterp();
void Func_CpuDynarec();
void Func_CpuLightrec();
void Func_BiosSelectHLE();
void Func_BiosSelectSD();
void Func_BiosSelectUSB();
void Func_BiosSelectDVD();
void Func_BootBiosYes();
void Func_BootBiosNo();
void Func_ExecuteBios();
void Func_SelectLanguage();
void Func_SaveSettingsSD();
void Func_SaveSettingsUSB();
void Func_SaveSettingsSeparately();

void Func_ShowFpsOn();
void Func_ShowFpsOff();
void Func_FpsLimitAuto();
void Func_FpsLimitOff();
void Func_FrameSkipOn();
void Func_FrameSkipOff();
void Func_ScreenMode();
void Func_Interlaced();
void Func_DeflickerFilter();
void Func_Screen240p();
void Func_BilinearFilter();
void Func_TrapFilter();
void Func_DitheringNone();
void Func_DitheringDefault();
void Func_DitheringAlways();
void Func_ScalingNone();
void Func_Scaling2xSai();
void Func_ForceNTSC();

void Func_ConfigureInput();
void Func_ConfigureButtons();
void Func_PsxTypeStandard();
void Func_PsxTypeAnalog();
void Func_PsxTypeStickDpad();
void Func_PsxTypeLightgun();
void Func_EnableRumbleYes();
void Func_EnableRumbleNo();
void Func_SaveButtonsSD();
void Func_SaveButtonsUSB();
void Func_SetButtonLoad();
void Func_ToggleButtonLoad();

void Func_ToggleAudio();
void Func_ToggleXa();
void Func_ToggleCdda();
void Func_PluginsPage();
void Func_CDPage();
void Func_AdvGfxPage();
void Func_MemoryPage();
void Func_SoundHwAccelYes();
void Func_SoundHwAccelNo();
void Func_SoundSyncOff();
void Func_SoundSyncTempo();
void Func_SoundSyncRate();
void Func_AdvancedSound();
void Func_InterpolationToggle();

void Func_MemcardSaveSD();
void Func_MemcardSaveUSB();
void Func_MemcardSaveCardA();
void Func_MemcardSaveCardB();
void Func_AutoSaveYes();
void Func_AutoSaveNo();
void Func_SaveStateSD();
void Func_SaveStateUSB();
void Func_Memcard1();
void Func_Memcard2();
void Func_CardType1();
void Func_CardType2();
static int cardType(int card);   /* Off, Shared or Game, from the two settings */


void Func_FastloadYes();
void Func_FastloadNo();

void Func_ReturnFromSettingsFrame();
void SetFrameLimit();

void Func_UseOldSoftGpu();
void Func_UseNewSoftGpu();
void Func_UseOpenGxGpu();

extern "C" void VIDEO_SetTrapFilter(bool enable);

extern BOOL hasLoadedISO;
extern volatile int stop;
extern char menuActive;
extern char fastLoad;   /* the Fast Load setting, read by the General tab */

extern "C" {
void SysReset();
int SysInit();
void SysClose();
void SysStartCPU();
void CheckCdrom();
void CheckPsxType();
void LoadCdrom();
void pauseAudio(void);  void pauseInput(void);
void resumeAudio(void); void resumeInput(void);
void IplFont_loadFontFile(FILE* fontFile);
FILE* IplFont_getFontFile(char* sdUsb);
void psxResetRcntRate();
void pl_chg_psxtype(int is_pal_);
void gpuChangePsxType();
void setSpuInterpolation(int spuInterpolation);
void setSpuTempo(int soundTempo);
}

#define NUM_FRAME_BUTTONS 80
#define NUM_TAB_BUTTONS 5
#define FRAME_BUTTONS settingsFrameButtons
#define FRAME_STRINGS settingsFrameStrings
#define NUM_FREE_TEXTS 2
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/*
The tabs. Each one is a page for the menu engine: its rows are in the tables below, and the
engine places every button and label. What each row holds:

General:  Advanced, Memory; Select BIOS; Boot Through BIOS (and Execute BIOS); Select Language;
          Fast Load; Save Settings. The CPU core and GPU plugin are on the Advanced pages.
Video:    Show FPS; FPS Limit; Frame Skip; Screen Mode; Filters; Advanced (Advanced Graphics).
Input:    Configure Input and Button Mappings; PSX Controller Type; Light Gun; Enable Rumble;
          Save Button Configs; Auto Load Slot.
Audio:    Enable; Interpolation; DSP Sound; Sync; Advanced (Advanced Sound).
Saves:    Memcard Save Device; Memcard Type; Auto Save Memcards; Save State Device.

Never remove or move an entry of FRAME_BUTTONS. Its index is what a script's "menuclick" names
(PadWiiSX.c). Entries that no tab shows are kept for that reason.
*/
static char FRAME_STRINGS[99][24] =
	{ "General",
	  "Video",
	  "Input",
	  "Audio",
	  "Saves",
	//Strings for General tab (starting at FRAME_STRINGS[5])
	  "Select CPU Core",
	  "Select BIOS",
	  "Boot Through BIOS",
	  "Execute BIOS",
	  "Save Settings",
	  "Interpreter",
	  "Dynarec",
	  "HLE",
	  "SD",
	  "USB",
	  "DVD",
	  "Yes",
	  "No",
	//Strings for Video tab (starting at FRAME_STRINGS[18])..was[])
	  "Show FPS",
	  "Limit FPS",
	  "Frame Skip",
	  "Screen Mode",
	  "Dithering",
	  "Filters",
	  "On",
	  "Off",
	  "Auto",
	  "4:3",
	  "16:9",
	  "Force 16:9",
	  "None",
	  "2xSaI",
	  "Default",
	  "Always",
	//Strings for Input tab (starting at FRAME_STRINGS[34]..was[])
	  "Configure Input",
	  "Configure Buttons",
	  "PSX Controller Type",
	  "Enable Rumble",
	  "Standard",
	  "Analog",
	  "Save Button Configs",
	  "Auto Load Slot",
	  "Default",
	//Strings for Audio tab (starting at FRAME_STRINGS[43]) ..was[47]
	  "Disable Audio",
	  "Disable XA",
	  "Disable CDDA",
	  "Interpolation",
	  "Simple",		// [47] spuInterpolation == SIMPLE_INTERPOLATION (1)
	  "Gaussian",	// [48] spuInterpolation == GAUSSI_INTERPOLATION (2)
	  "unused1",	// [49] and [50] are unreachable: the label is FRAME_STRINGS[46 + spuInterpolation]
	  "unused2",
	//Strings for Saves tab (starting at FRAME_STRINGS[51]) ..was[55]
	  "Memcard Save Device",
	  "Auto Save Memcards",
	  "Save States Device",
	  "CardA",
	  "CardB",
      // Strings for display language (starting at FRAME_STRINGS[56]) ..was[62]
      "Select Language",
      "En", // English
      "Chs", // Simplified Chinese
      "Kr", // Korean
      "Es", // SPANISH
      "Pte", // PORTUGUESE
      "It", // ITALIAN
      // Strings for display Fast Load (starting at FRAME_STRINGS[63]) ..was[78]
      "Fast Load",
	  "240p",
	  "Bilinear",
	  "Trap",
	  "Interlaced",
	  "Deflicker",
	  "Lightrec",
	  "Off",					// [70] light gun: none, the pad is a pad
	  "GunCon",
	  "Justifier",
	  "Mouse",
	  "Memcard 1",
	  "Memcard 2",
	  "Memcard Type",
	  "This Game",
	  "Force NTSC",
	//Appended so no existing FRAME_STRINGS index moves
	  "DSP Sound",			// [79] audio tab: hardware-accelerated sound
	  "Sync",				// [80] audio tab: how the output keeps pace with the mixer
	  "Tempo",				// [81] Sync: the legacy mixer-clock pull-back (SoundTempo)
	  "Rate",				// [82] Sync: playback-rate nudge at the output (SoundRateControl)
	  "Advanced",			// [83] audio tab: the Advanced Sound page
	  "Advanced",			// [84] general tab: the Advanced pages (tabs CPU, GPU, SPU, CD, Input)
	  "CD",					// [85] general tab: the CD page (not shown: CD is a tab of Advanced)
	  "Memory",				// [86] general tab: the Memory page
	  "Enable",				// [87] audio tab: label of the three source toggles
	  "Audio",				// [88] Enable: the sound output as a whole
	  "XA",					// [89] Enable: the disc's XA streams
	  "CDDA",				// [90] Enable: Red Book audio tracks
	  "1",					// [91] saves tab: which card the first Memcard Type button is
	  "Off",				// [92] the card is not there at all
	  "Shared",				// [93] one card file every game opens
	  "Game",				// [94] one card file per game
	  "2",					// [95] saves tab: which card the second button is
	  "Advanced",			// [96] video tab: the Advanced Graphics page
	  "Light Gun",			// [97] input tab: the light gun row
	  "Stick D-pad"			// [98] input tab: controller type, the stick drives the D-pad
      };

static char LANG_STRINGS[13][24] =
	{ "En", // English
      "Chs", // Simplified Chinese
      "Kr", // Korean
      "Es", // SPANISH
      "Pte", // PORTUGUESE
      "It", // ITALIAN
      "De", // GERMAN
      "Cht", // TRAD_CHINESE
      "Jp", // JAPANESE
      "Fr", // FRENCH
      "Br", // BRAZILIAN_PORTUGUESE
      "Ca", // CATALAN
      "Tu" // TURKISH
      };

static char GPU_PLUGIN_STRINGS[4][24] =
    { "GPU Plugin",
      "Soft Fast",
      "Soft Timed",
      "OpenGX"
      };

// Texture Filters
static char TEXTURE_FILTER_STRINGS[3][24] =
    { "Default",
      "Near",
      "Bilinear"
      };

/* The tab row. Its widths are shared with OptionsFrame, whose Advanced tabs sit in the same row. */
const float settingsTabWidths[NUM_TAB_BUTTONS] = { 110.0f, 100.0f, 100.0f, 100.0f, 100.0f };

/* One button: its style, its text (some handlers change it), and what a click and Back do. */
struct ButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =
{ //	button	style	string		click	return
	//Buttons for Tabs (starts at button[0])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[0],	Func_TabGeneral,		Func_ReturnFromSettingsFrame }, // General tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[1],	Func_TabVideo,			Func_ReturnFromSettingsFrame }, // Video tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[2],	Func_TabInput,			Func_ReturnFromSettingsFrame }, // Input tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[3],	Func_TabAudio,			Func_ReturnFromSettingsFrame }, // Audio tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[4],	Func_TabSaves,			Func_ReturnFromSettingsFrame }, // Saves tab
	//Buttons for General Tab (starts at button[5])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[10],	Func_CpuInterp,			Func_ReturnFromSettingsFrame }, // CPU: Interp
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[69],	Func_CpuLightrec,		Func_ReturnFromSettingsFrame }, // CPU: Lightrec
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[12],	Func_BiosSelectHLE,		Func_ReturnFromSettingsFrame }, // Bios: HLE
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	Func_BiosSelectSD,		Func_ReturnFromSettingsFrame }, // Bios: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	Func_BiosSelectUSB,		Func_ReturnFromSettingsFrame }, // Bios: USB
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[15],	Func_BiosSelectDVD,		Func_ReturnFromSettingsFrame }, // Bios: DVD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_BootBiosYes,		Func_ReturnFromSettingsFrame }, // Boot Thru Bios: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_BootBiosNo,		Func_ReturnFromSettingsFrame }, // Boot Thru Bios: No
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[8],	Func_ExecuteBios,		Func_ReturnFromSettingsFrame }, // Execute Bios

	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[13],	Func_SaveSettingsSD,	Func_ReturnFromSettingsFrame }, // Save Settings: SD
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[14],	Func_SaveSettingsUSB,	Func_ReturnFromSettingsFrame }, // Save Settings: USB

	//Buttons for Video Tab (starts at button[16])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[24],	Func_ShowFpsOn,			Func_ReturnFromSettingsFrame }, // Show FPS: On
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	Func_ShowFpsOff,		Func_ReturnFromSettingsFrame }, // Show FPS: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[26],	Func_FpsLimitAuto,		Func_ReturnFromSettingsFrame }, // FPS Limit: Auto
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	Func_FpsLimitOff,		Func_ReturnFromSettingsFrame }, // FPS Limit: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[24],	Func_FrameSkipOn,		Func_ReturnFromSettingsFrame }, // Frame Skip: On
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	Func_FrameSkipOff,		Func_ReturnFromSettingsFrame }, // Frame Skip: Off
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[27],	Func_ScreenMode,		Func_ReturnFromSettingsFrame }, // ScreenMode: 4:3/16:9/Force16:9
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[67],	Func_Interlaced,		Func_ReturnFromSettingsFrame }, // Interlaced Mode
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[68],	Func_DeflickerFilter,	Func_ReturnFromSettingsFrame }, // Filters: Deflicker Filter
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[30],	Func_DitheringNone,		Func_ReturnFromSettingsFrame }, // Dithering: None
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[32],	Func_DitheringDefault,	Func_ReturnFromSettingsFrame }, // Dithering: Game Dependent
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[33],	Func_DitheringAlways,	Func_ReturnFromSettingsFrame }, // Dithering: Always
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[65],	Func_BilinearFilter,	Func_ReturnFromSettingsFrame }, // Filters: Bilinear Filter
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[66],	Func_TrapFilter,		Func_ReturnFromSettingsFrame }, // Filters: Trap Filter
	//Buttons for Input Tab (starts at button[30])
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[34],	Func_ConfigureInput,	Func_ReturnFromSettingsFrame }, // Configure Input Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[35],	Func_ConfigureButtons,	Func_ReturnFromSettingsFrame }, // Configure Button Mappings
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[38],	Func_PsxTypeStandard,	Func_ReturnFromSettingsFrame }, // PSX Controller Type: Standard
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[39],	Func_PsxTypeAnalog,		Func_ReturnFromSettingsFrame }, // PSX Controller Type: Analog
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_EnableRumbleYes,	Func_ReturnFromSettingsFrame }, // Enable Rumble: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_EnableRumbleNo,	Func_ReturnFromSettingsFrame }, // Enable Rumble: No
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[13],	Func_SaveButtonsSD,		Func_ReturnFromSettingsFrame }, // Save Button Mappings: SD
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[14],	Func_SaveButtonsUSB,	Func_ReturnFromSettingsFrame }, // Save Button Mappings: USB
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[42],	Func_ToggleButtonLoad,	Func_ReturnFromSettingsFrame }, // Auto Load Button Config Slot: Default,1,2,3,4
	//Buttons for Audio Tab (starts at button[39]) ..was[45]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[88],	Func_ToggleAudio,		Func_ReturnFromSettingsFrame }, // Enable: Audio
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[89],	Func_ToggleXa,			Func_ReturnFromSettingsFrame }, // Enable: XA
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[90],	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // Enable: CDDA
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_ToggleXa,			Func_ReturnFromSettingsFrame }, // (spare, never shown: the Enable row replaced the three Yes/No pairs; kept so no later index moves)
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // (spare, never shown)
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // (spare, never shown)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[47],	Func_InterpolationToggle,	Func_ReturnFromSettingsFrame }, // Interpolation: Simple/Gaussian
	//Buttons for Saves Tab (starts at button[46]) ..was[54]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	Func_MemcardSaveSD,		Func_ReturnFromSettingsFrame }, // Memcard Save: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	Func_MemcardSaveUSB,	Func_ReturnFromSettingsFrame }, // Memcard Save: USB
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[54],	Func_MemcardSaveCardA,	Func_ReturnFromSettingsFrame }, // Memcard Save: Card A
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[55],	Func_MemcardSaveCardB,	Func_ReturnFromSettingsFrame }, // Memcard Save: Card B
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_AutoSaveYes,		Func_ReturnFromSettingsFrame }, // Auto Save Memcards: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_AutoSaveNo,		Func_ReturnFromSettingsFrame }, // Auto Save Memcards: No
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	Func_SaveStateSD,		Func_ReturnFromSettingsFrame }, // Save State: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	Func_SaveStateUSB,		Func_ReturnFromSettingsFrame }, // Save State: USB
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[57],	Func_SelectLanguage,	Func_ReturnFromSettingsFrame }, // Select Language: En
    //Buttons for Saves Tab (starts at button[55]) ..was[61]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_FastloadYes,		Func_ReturnFromSettingsFrame }, // Fast load: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_FastloadNo,		Func_ReturnFromSettingsFrame }, // Fast load: No
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[64],	Func_Screen240p,		Func_ReturnFromSettingsFrame },  // ScreenMode: 240p
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[11],	Func_CpuDynarec,		Func_ReturnFromSettingsFrame },  // CPU: Dynarec
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[70],	Func_PsxTypeLightgun,	Func_ReturnFromSettingsFrame },  // Light Gun: Off/GunCon/Justifier/Mouse
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[74],	Func_Memcard1,			Func_ReturnFromSettingsFrame },  // Memcard 1 toggle
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[75],	Func_Memcard2,			Func_ReturnFromSettingsFrame },  // Memcard 2 toggle

	//Buttons for ... (starts at button[62]) ..was[65]
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[77],	Func_SaveSettingsSeparately,	Func_ReturnFromSettingsFrame }, // Save Settings: Separately
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[78],	Func_ForceNTSC,			Func_ReturnFromSettingsFrame },  // Force NTSC toggle

	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[1],	Func_UseOldSoftGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: Soft Fast
	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[2],	Func_UseNewSoftGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: Soft Timed
	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[3],	Func_UseOpenGxGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: OpenGX

	//Audio tab, appended at the end (starts at button[67]) so no existing index moves
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	Func_SoundHwAccelYes,	Func_ReturnFromSettingsFrame }, // DSP Sound: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	Func_SoundHwAccelNo,	Func_ReturnFromSettingsFrame }, // DSP Sound: No
	//Audio tab, Sync group (buttons 69..71): like Dithering, one of three is selected. The trio
	//starts left of the other rows' buttons (the "Sync" label is short) and leaves a gap before
	//Advanced, which sits in the row's right corner (right edge 645, the tab's widest row).
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	Func_SoundSyncOff,		Func_ReturnFromSettingsFrame }, // Sync: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[81],	Func_SoundSyncTempo,	Func_ReturnFromSettingsFrame }, // Sync: Tempo
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[82],	Func_SoundSyncRate,		Func_ReturnFromSettingsFrame }, // Sync: Rate
	//Audio tab, Advanced (button 72): opens the Advanced Sound page (AdvancedSoundFrame.cpp)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[83],	Func_AdvancedSound,		Func_ReturnFromSettingsFrame }, // Advanced: opens the Advanced Sound page
	//General tab: one row of sub-pages. The CPU core and GPU plugin moved to Plugins and the
	//CD settings to the CD page; buttons 5/6/58 and 64/65/66 stay in this table but are not shown.
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[84],	Func_PluginsPage,		Func_ReturnFromSettingsFrame }, // Advanced pages
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[85],	Func_CDPage,			Func_ReturnFromSettingsFrame }, // CD page
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[86],	Func_MemoryPage,		Func_ReturnFromSettingsFrame }, // Memory page
	//Saves tab: which file each memory card lives in (buttons 76 and 77)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[94],	Func_CardType1,			Func_ReturnFromSettingsFrame }, // Memcard 1 type: Off/Shared/Game
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[93],	Func_CardType2,			Func_ReturnFromSettingsFrame }, // Memcard 2 type: Off/Shared/Game
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[96],	Func_AdvGfxPage,		Func_ReturnFromSettingsFrame }, // Video tab: Advanced Graphics page
	//Input tab: the third controller type (button 79), appended so no index moves
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[98],	Func_PsxTypeStickDpad,	Func_ReturnFromSettingsFrame }, // PSX Controller Type: Stick D-pad
};

/*  The tabs, on the menu engine  */

namespace eng = menu::engine;

/* One row of a tab. button[] holds FRAME_BUTTONS indexes, -1 at the end of the row.
 * label is the text on the left, or NULL. x0 is the first button's left edge. 0 centres the row.
 * gap 0 means TAB_GAP. plain counts the last buttons of a choice row: actions, never lit. */
struct SetRow
{
	eng::RowKind	kind;
	const char		*label;
	int				button[4];
	float			width[4];
	float			x0;
	float			gap;
	int				plain;
};

/* One tab, as the engine's page. rowDy 0 means the default pitch (60). prepare sets what the
 * buttons show and which are lit. freeTexts are texts at fixed points, or NULL. */
struct SetTab
{
	int						curTab;
	const SetRow			*rows;
	int						nRows;
	float					rowDy;
	void					(*prepare)(void);
	const eng::FreeText		*freeTexts;
	int						nFreeTexts;
};

static int rowButtons(const SetRow &s)
{
	int n = 0;
	while (n < 4 && s.button[n] >= 0)
		n++;
	return n;
}

/* The engine lays out one tab at a time, so one set of arrays serves all five tabs. */
static eng::Row				SET_ROWS[eng::MAX_ROWS];
static eng::ActionButton	SET_ACTIONS[eng::MAX_ROWS][4];
static const char			*SET_TEXT[eng::MAX_ROWS][4];
static eng::Tab				SET_TABS[NUM_TAB_BUTTONS];
static eng::Page			SET_PAGE;
static eng::Layout			SET_LAYOUT;
static const SetRow			*setRows;		/* the rows of the tab on show */

/* Each row's label and each free text is a TextBox. The layout moves them. they keep their place. */
static char					*rowLabelString[eng::MAX_ROWS];
static menu::TextBox		*rowLabel[eng::MAX_ROWS];
static char					*freeString[NUM_FREE_TEXTS];
static menu::TextBox		*freeBox[NUM_FREE_TEXTS];

extern "C" void SysPrintf(const char *fmt, ...);

/* Text widths for the engine: IplFont, the font every label is drawn with. */
static int setWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}
static int setHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}
static const eng::Metrics SET_METRICS = { setWidth, setHeight, NULL };

/* The FRAME_BUTTONS entry a control is drawn with. A tab's control is the tab button itself. */
static menu::Button *buttonOf(const eng::Control &c)
{
	return FRAME_BUTTONS[c.role == eng::ROLE_TAB ? c.slot : setRows[c.row].button[c.slot]].button;
}

/* A button at the engine's rectangle, shown. Its lit state comes from the engine. A button the
 * engine does not make a focus target (This Game with no game loaded) is drawn, but inactive. */
static void placeButton(SettingsFrame *frame, menu::Button *b, const eng::Control &c)
{
	b->setBounds(c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	b->setVisible(true);
	b->setActive(c.focusable);
	b->setSelected(c.selected);
	menu::Cursor::getInstance().moveComponent(frame, b,
		c.rect.x, c.rect.x + c.rect.w, c.rect.y, c.rect.y + c.rect.h);
}

/* The engine's focus links, on the buttons. A link of -1 is no link. */
static void linkButtons(const eng::Layout &lay)
{
	static const int DIRS[eng::DIR_COUNT] = {
		menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
		menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
	};
	for (int i = 0; i < lay.n; i++)
	{
		const eng::Control &c = lay.c[i];
		if (!c.focusable)
			continue;
		for (int d = 0; d < eng::DIR_COUNT; d++)
		{
			const int to = c.link[d];
			buttonOf(c)->setNextFocus(DIRS[d], to >= 0 ? buttonOf(lay.c[to]) : NULL);
		}
	}
}

/*  What the buttons show, and which are lit: set before the layout  */

/* Each prepare runs after the rows are cleared and before the button texts are read. A choice
 * row's value is the index of its lit button. An action's lit flag lights that button. */

static void prepGeneral(void)
{
	FRAME_BUTTONS[54].buttonString = LANG_STRINGS[(int)lang];
	SET_ROWS[1].value = biosDevice;
	SET_ROWS[2].value = LoadCdBios == BOOTTHRUBIOS_YES ? 0 : 1;
	SET_ROWS[4].value = fastLoad == 1 ? 0 : 1;
	/* This Game (row 5, button 2) is drawn always, and inactive with no game loaded. */
	SET_ROWS[5].inactive = hasLoadedISO ? 0 : 1u << 2;
}

static void prepVideo(void)
{
	FRAME_BUTTONS[22].buttonString = FRAME_STRINGS[27 + screenMode];
	FRAME_BUTTONS[28].buttonString = TEXTURE_FILTER_STRINGS[(int)bilinearFilter];
	SET_ROWS[0].value = showFPSonScreen == FPS_SHOW ? 0 : 1;
	SET_ROWS[2].value = frameSkip == FRAMESKIP_ENABLE ? 0 : 1;
	SET_ACTIONS[1][0].lit = frameLimit[1] == FRAMELIMIT_AUTO;
	SET_ACTIONS[1][1].lit = frameLimit[1] != FRAMELIMIT_AUTO;
	SET_ACTIONS[1][2].lit = forceNTSC == FORCENTSC_ENABLE;
	SET_ACTIONS[3][1].lit = interlacedMode == INTERLACED_ENABLE;
	SET_ACTIONS[3][2].lit = originalMode == ORIGINALMODE_ENABLE;
	SET_ACTIONS[4][1].lit = trapFilter == TRAPFILTER_ENABLE;
	SET_ACTIONS[4][2].lit = deflickerFilter == DEFLICKER_ENABLE;
}

static void prepInput(void)
{
	FRAME_BUTTONS[59].buttonString = FRAME_STRINGS[70 + lightGun];
	SET_ROWS[1].value = controllerType;
	SET_ROWS[3].value = rumbleEnabled == RUMBLE_ENABLE ? 0 : 1;
	SET_ACTIONS[2][0].lit = lightGun != LIGHTGUN_DISABLE;
}

static void prepAudio(void)
{
	FRAME_BUTTONS[45].buttonString = FRAME_STRINGS[46 + spuInterpolation];
	/* Each source toggle is lit when that source plays. The file keeps the inverted "Disable" keys. */
	SET_ACTIONS[0][0].lit = audioEnabled == AUDIO_ENABLE;
	SET_ACTIONS[0][1].lit = Config.Xa != XA_DISABLE;
	SET_ACTIONS[0][2].lit = Config.Cdda != CDDA_DISABLE;
	SET_ROWS[2].value = soundHwAccel == SOUND_HW_ACCEL_ON ? 0 : 1;
	SET_ROWS[3].value = soundRateControl ? 2 : (soundTempo ? 1 : 0);
}

static void prepSaves(void)
{
	for (int i = 76; i <= 77; i++)
		FRAME_BUTTONS[i].buttonString = FRAME_STRINGS[92 + cardType(i - 76)];
	for (int k = 0; k < 4; k++)
		SET_ACTIONS[0][k].lit = nativeSaveDevice == k;
	SET_ROWS[2].value = autoSave == AUTOSAVE_ENABLE ? 0 : 1;
	SET_ROWS[3].value = saveStateDevice == SAVESTATEDEVICE_SD ? 0 : 1;
}

/*  The rows of each tab, top to bottom  */

static const SetRow GEN_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL,            { 73, 75, -1, -1 }, { 130, 130,   0, 0 }, 0.0f,   30.0f, 0 },	/* Advanced, Memory */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[6],  {  7,  8,  9, 10 }, {  70,  55,  70, 70 }, 295.0f, 0.0f,  0 },	/* Select BIOS: HLE, SD, USB, DVD */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[7],  { 11, 12, 13, -1 }, {  75,  75, 171,  0 }, 295.0f, 0.0f,  1 },	/* Boot Through BIOS: Yes, No, Execute BIOS */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[56], { 54, -1, -1, -1 }, {  90,   0,   0,  0 }, 295.0f, 0.0f,  0 },	/* Select Language */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[63], { 55, 56, -1, -1 }, {  75,  75,   0,  0 }, 295.0f, 0.0f,  0 },	/* Fast Load: Yes, No */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[9],  { 14, 15, 62, -1 }, {  55,  70, 140,  0 }, 253.0f, 0.0f,  0 },	/* Save Settings: SD, USB, This Game */
};

static const SetRow VID_ROWS[] =
{
	{ eng::ROW_CHOICE,  FRAME_STRINGS[18], { 16, 17, -1, -1 }, {  75,  75,   0,  0 }, 295.0f, 0.0f, 0 },	/* Show FPS: On, Off */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[19], { 18, 19, 63, -1 }, {  75,  75, 160,  0 }, 295.0f, 0.0f, 0 },	/* FPS Limit: Auto, Off. Force NTSC (a toggle) */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[20], { 20, 21, -1, -1 }, {  75,  75,   0,  0 }, 295.0f, 0.0f, 0 },	/* Frame Skip: On, Off */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[21], { 22, 23, 57, -1 }, { 130, 125,  66,  0 }, 295.0f, 0.0f, 0 },	/* Screen Mode (a cycle). Interlaced, 240p (toggles) */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[23], { 28, 29, 24, -1 }, {  95,  75, 110,  0 }, 238.0f, 0.0f, 0 },	/* Filters: Bilinear (a cycle). Trap, Deflicker (toggles) */
	{ eng::ROW_BUTTONS, NULL,              { 78, -1, -1, -1 }, { 150,   0,   0,  0 }, 245.0f, 0.0f, 0 },	/* Advanced Graphics (an action) */
};

static const SetRow INP_ROWS[] =
{
	{ eng::ROW_BUTTONS, NULL,              { 30, 31, -1, -1 }, { 220, 235,   0, 0 }, 85.0f,  15.0f, 0 },	/* Configure Input, Configure Button Mappings */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[36], { 32, 33, 79, -1 }, { 105, 100, 140, 0 }, 271.0f,  0.0f, 0 },	/* PSX Controller Type: Standard, Analog, Stick D-pad */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[97], { 59, -1, -1, -1 }, { 135,   0,   0, 0 }, 295.0f,  0.0f, 0 },	/* Light Gun (a cycle, lit when a gun is on) */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[37], { 34, 35, -1, -1 }, {  75,  75,   0, 0 }, 295.0f,  0.0f, 0 },	/* Enable Rumble: Yes, No */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[40], { 36, 37, -1, -1 }, {  55,  70,   0, 0 }, 295.0f,  0.0f, 0 },	/* Save Button Configs: SD, USB */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[41], { 38, -1, -1, -1 }, { 135,   0,   0, 0 }, 295.0f,  0.0f, 0 },	/* Auto Load Slot (an action) */
};

static const SetRow AUD_ROWS[] =
{
	{ eng::ROW_BUTTONS, FRAME_STRINGS[87], { 39, 40, 41, -1 }, { 100,  75, 110, 0 }, 295.0f, 0.0f, 0 },	/* Enable: Audio, XA, CDDA (toggles) */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[46], { 45, -1, -1, -1 }, { 170,   0,   0, 0 }, 295.0f, 0.0f, 0 },	/* Interpolation: Simple/Gaussian (an action) */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[79], { 67, 68, -1, -1 }, {  75,  75,   0, 0 }, 295.0f, 0.0f, 0 },	/* DSP Sound: Yes, No */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[80], { 69, 70, 71, -1 }, {  60,  90,  70, 0 }, 295.0f, 0.0f, 0 },	/* Sync: Off, Tempo, Rate */
	{ eng::ROW_BUTTONS, NULL,              { 72, -1, -1, -1 }, { 125,   0,   0, 0 }, 258.0f, 0.0f, 0 },	/* Advanced: the Advanced Sound page */
};

static const SetRow SAV_ROWS[] =
{
	{ eng::ROW_BUTTONS, FRAME_STRINGS[51], { 46, 47, 48, 49 }, { 55, 70, 90, 90 }, 295.0f, 0.0f,  0 },	/* Memcard Save Device: SD, USB, Card A, Card B */
	{ eng::ROW_BUTTONS, FRAME_STRINGS[76], { 76, 77, -1, -1 }, { 105, 105, 0, 0 }, 295.0f, 30.0f, 0 },	/* Memcard Type: card 1, card 2 (their text is the card's type) */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[52], { 50, 51, -1, -1 }, {  75,  75, 0, 0 }, 295.0f, 0.0f,  0 },	/* Auto Save Memcards: Yes, No */
	{ eng::ROW_CHOICE,  FRAME_STRINGS[53], { 52, 53, -1, -1 }, {  55,  70, 0, 0 }, 295.0f, 0.0f,  0 },	/* Save State Device: SD, USB */
};

/* The card numbers "1" and "2", beside the Memcard Type buttons. */
static const eng::FreeText SAV_FREE[NUM_FREE_TEXTS] =
{
	{ FRAME_STRINGS[91], 285.0f, 198.0f, 1.0f, true },
	{ FRAME_STRINGS[95], 420.0f, 198.0f, 1.0f, true },
};

/* The five tabs. The Audio and Saves tabs have rows 70 apart. */
static const SetTab SET_PAGES[NUM_TAB_BUTTONS] =
{
	{ SettingsFrame::SUBMENU_GENERAL, GEN_ROWS, COUNT(GEN_ROWS), 0.0f,  prepGeneral, NULL,    0 },
	{ SettingsFrame::SUBMENU_VIDEO,   VID_ROWS, COUNT(VID_ROWS), 0.0f,  prepVideo,   NULL,    0 },
	{ SettingsFrame::SUBMENU_INPUT,   INP_ROWS, COUNT(INP_ROWS), 0.0f,  prepInput,   NULL,    0 },
	{ SettingsFrame::SUBMENU_AUDIO,   AUD_ROWS, COUNT(AUD_ROWS), 70.0f, prepAudio,   NULL,    0 },
	{ SettingsFrame::SUBMENU_SAVES,   SAV_ROWS, COUNT(SAV_ROWS), 70.0f, prepSaves,   SAV_FREE, COUNT(SAV_FREE) },
};

/*  Laying a tab out  */

/* Lays one tab out on the engine and places its buttons, labels and texts. then links the focus.
 * Done when the tab is shown, never per frame. */
static void layoutTab(SettingsFrame *frame, const SetTab &tab)
{
	setRows = tab.rows;
	for (int r = 0; r < tab.nRows; r++)
	{
		const SetRow &s = tab.rows[r];
		eng::Row &row = SET_ROWS[r];
		memset(&row, 0, sizeof row);
		memset(SET_ACTIONS[r], 0, sizeof SET_ACTIONS[r]);
		row.kind    = s.kind;
		row.label   = s.label;
		row.names   = SET_TEXT[r];
		row.count   = rowButtons(s);
		row.actions = SET_ACTIONS[r];
		row.x0      = s.x0;
		row.gap     = s.gap;
		row.plain   = s.plain;
		row.widths  = s.kind == eng::ROW_CHOICE ? s.width : NULL;
		for (int c = 0; c < 4; c++)
			SET_ACTIONS[r][c].width = s.width[c];
	}
	if (tab.prepare)
		tab.prepare();
	for (int r = 0; r < tab.nRows; r++)
		for (int c = 0; c < SET_ROWS[r].count; c++)
		{
			SET_TEXT[r][c] = FRAME_BUTTONS[tab.rows[r].button[c]].buttonString;
			SET_ACTIONS[r][c].text = SET_TEXT[r][c];
		}

	for (int t = 0; t < NUM_TAB_BUTTONS; t++)
	{
		SET_TABS[t].text  = FRAME_BUTTONS[t].buttonString;
		SET_TABS[t].width = settingsTabWidths[t];
	}
	SET_PAGE.tabs       = SET_TABS;
	SET_PAGE.nTabs      = NUM_TAB_BUTTONS;
	SET_PAGE.curTab     = tab.curTab;
	SET_PAGE.rows       = SET_ROWS;
	SET_PAGE.nRows      = tab.nRows;
	SET_PAGE.logo       = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_SETTINGS is in LOGO_BOTTOM_FRAMES */
	SET_PAGE.focus      = eng::FOCUS_ROWS;
	SET_PAGE.labelCx    = 150.0f;	/* the Settings labels: centred at x 150 */
	SET_PAGE.labelDy    = 28.0f;	/* on the middle of a 56-high row */
	SET_PAGE.rowDy      = tab.rowDy;
	SET_PAGE.freeTexts  = tab.freeTexts;
	SET_PAGE.nFreeTexts = tab.nFreeTexts;

	if (eng::layoutPage(SET_PAGE, SET_METRICS, SET_LAYOUT) < 0)
	{
		/* The tables are fixed, so this is a programming error. The tab stays hidden. */
		SysPrintf("SettingsFrame: tab %d does not fit the menu engine\n", tab.curTab);
		return;
	}
	eng::linkFocus(SET_PAGE, SET_LAYOUT);

	for (int i = 0; i < SET_LAYOUT.n; i++)
	{
		const eng::Control &c = SET_LAYOUT.c[i];
		switch (c.role)
		{
		case eng::ROLE_TAB:
		case eng::ROLE_BUTTON:
		case eng::ROLE_CHOICE:
			placeButton(frame, buttonOf(c), c);
			break;
		case eng::ROLE_LABEL:
		case eng::ROLE_RLABEL:
			rowLabelString[c.row] = (char*)c.text;
			rowLabel[c.row]->setPosition(c.ax, c.ay);
			rowLabel[c.row]->setVisible(true);
			break;
		case eng::ROLE_FREE_TEXT:
			freeString[c.slot] = (char*)c.text;
			freeBox[c.slot]->setPosition(c.ax, c.ay);
			freeBox[c.slot]->setVisible(true);
			break;
		default:
			break;
		}
	}
	linkButtons(SET_LAYOUT);
}
SettingsFrame::SettingsFrame()
		: activeSubmenu(SUBMENU_GENERAL)
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		/* The position is the engine's: layoutTab() sets it on each shown button. */
		FRAME_BUTTONS[i].button = new menu::Button(FRAME_BUTTONS[i].buttonStyle, &FRAME_BUTTONS[i].buttonString,
										0.0f, 0.0f, 0.0f, 0.0f);
		FRAME_BUTTONS[i].button->setActive(true);
		if (FRAME_BUTTONS[i].clickedFunc) FRAME_BUTTONS[i].button->setClicked(FRAME_BUTTONS[i].clickedFunc);
		if (FRAME_BUTTONS[i].returnFunc) FRAME_BUTTONS[i].button->setReturn(FRAME_BUTTONS[i].returnFunc);
		add(FRAME_BUTTONS[i].button);
		menu::Cursor::getInstance().addComponent(this, FRAME_BUTTONS[i].button, 0.0f, 0.0f, 0.0f, 0.0f);
	}

	for (int i = 0; i < eng::MAX_ROWS; i++)
	{
		rowLabelString[i] = (char*)"";
		rowLabel[i] = new menu::TextBox(&rowLabelString[i], 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
		add(rowLabel[i]);
	}
	for (int i = 0; i < NUM_FREE_TEXTS; i++)
	{
		freeString[i] = (char*)"";
		freeBox[i] = new menu::TextBox(&freeString[i], 0.0f, 0.0f, eng::grid::TEXT_SCALE, true);
		add(freeBox[i]);
	}

	setDefaultFocus(FRAME_BUTTONS[0].button);
	setBackFunc(Func_ReturnFromSettingsFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_GENERAL);
}

SettingsFrame::~SettingsFrame()
{
	for (int i = 0; i < eng::MAX_ROWS; i++)
		delete rowLabel[i];
	for (int i = 0; i < NUM_FREE_TEXTS; i++)
		delete freeBox[i];
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, FRAME_BUTTONS[i].button);
		delete FRAME_BUTTONS[i].button;
	}
}

void SettingsFrame::activateSubmenu(int submenu)
{
	activeSubmenu = submenu;

	/* Hidden first. The layout then shows what this tab has. */
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		FRAME_BUTTONS[i].button->setVisible(false);
		FRAME_BUTTONS[i].button->setSelected(false);
		FRAME_BUTTONS[i].button->setActive(false);
	}
	for (int i = 0; i < eng::MAX_ROWS; i++)
		rowLabel[i]->setVisible(false);
	for (int i = 0; i < NUM_FREE_TEXTS; i++)
		freeBox[i]->setVisible(false);

	/* The Auto Load Slot's text is set before the Input tab's layout reads it. */
	if (submenu == SUBMENU_INPUT)
		Func_SetButtonLoad();
	layoutTab(this, SET_PAGES[submenu]);

	/* Focus starts on the tab's own button, as it always has. */
	setDefaultFocus(FRAME_BUTTONS[submenu].button);
}

void SettingsFrame::drawChildren(menu::Graphics &gfx)
{
	if(isVisible())
	{
#ifdef HW_RVL
		WPADData* wiiPad = menu::Input::getInstance().getWpad();
#endif
		for (int i=0; i<4; i++)
		{
			u16 currentButtonsGC = PAD_ButtonsHeld(i);
			if (currentButtonsGC ^ previousButtonsGC[i])
			{
				u16 currentButtonsDownGC = (currentButtonsGC ^ previousButtonsGC[i]) & currentButtonsGC;
				previousButtonsGC[i] = currentButtonsGC;
				if (currentButtonsDownGC & PAD_TRIGGER_R)
				{
					//move to next tab
					if(activeSubmenu < SUBMENU_SAVES)
					{
						activateSubmenu(activeSubmenu+1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
					break;
				}
				else if (currentButtonsDownGC & PAD_TRIGGER_L)
				{
					//move to the previous tab
					if(activeSubmenu > SUBMENU_GENERAL)
					{
						activateSubmenu(activeSubmenu-1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
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
					if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_R)
					{
						//move to next tab
						if(activeSubmenu < SUBMENU_SAVES)
						{
							activateSubmenu(activeSubmenu+1);
							menu::Focus::getInstance().clearPrimaryFocus();
						}
						break;
					}
					else if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_L)
					{
						//move to the previous tab
						if(activeSubmenu > SUBMENU_GENERAL)
						{
							activateSubmenu(activeSubmenu-1);
							menu::Focus::getInstance().clearPrimaryFocus();
						}
						break;
					}
				}
				else
				{
					if (currentButtonsDownWii & WPAD_BUTTON_PLUS)
					{
						//move to next tab
						if(activeSubmenu < SUBMENU_SAVES)
						{
							activateSubmenu(activeSubmenu+1);
							menu::Focus::getInstance().clearPrimaryFocus();
						}
						break;
					}
					else if (currentButtonsDownWii & WPAD_BUTTON_MINUS)
					{
						//move to the previous tab
						if(activeSubmenu > SUBMENU_GENERAL)
						{
							activateSubmenu(activeSubmenu-1);
							menu::Focus::getInstance().clearPrimaryFocus();
						}
						break;
					}
				}
			}
			else if (WUPC_ButtonsHeld(i) ^ previousButtonsWii[i])
			{
				u32 wupcHeld = WUPC_ButtonsHeld(i);
				u32 currentButtonsDownWii = (wupcHeld ^ previousButtonsWii[i]) & wupcHeld;
				previousButtonsWii[i] = wupcHeld;

				if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_R)
				{
					//move to next tab
					if (activeSubmenu < SUBMENU_SAVES)
					{
						activateSubmenu(activeSubmenu + 1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
					break;
				}
				else if (currentButtonsDownWii & WPAD_CLASSIC_BUTTON_FULL_L)
				{
					//move to the previous tab
					if (activeSubmenu > SUBMENU_GENERAL)
					{
						activateSubmenu(activeSubmenu - 1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
					break;
				}
			}
			else if (i == 0 && WiiDRC_ButtonsHeld() ^ previousButtonsWii[i])
			{
				u16 wiidrcHeld = WiiDRC_ButtonsHeld();
				u16 currentButtonsDownWii = (wiidrcHeld ^ previousButtonsWii[i]) & wiidrcHeld;
				previousButtonsWii[i] = wiidrcHeld;

				if (currentButtonsDownWii & WIIDRC_BUTTON_ZL)
				{
					//move to next tab
					if (activeSubmenu < SUBMENU_SAVES)
					{
						activateSubmenu(activeSubmenu + 1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
					break;
				}
				else if (currentButtonsDownWii & WIIDRC_BUTTON_ZR)
				{
					//move to the previous tab
					if (activeSubmenu > SUBMENU_GENERAL)
					{
						activateSubmenu(activeSubmenu - 1);
						menu::Focus::getInstance().clearPrimaryFocus();
					}
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

extern MenuContext *pMenuContext;

void Func_TabGeneral()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_GENERAL);
}

void Func_TabVideo()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_VIDEO);
}

void Func_TabInput()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_INPUT);
}

void Func_TabAudio()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_AUDIO);
}

void Func_TabSaves()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_SETTINGS,SettingsFrame::SUBMENU_SAVES);
}

#define ChangeCpu(cpuCore, btnIdx) \
    FRAME_BUTTONS[5].button->setSelected(false); \
    FRAME_BUTTONS[6].button->setSelected(false); \
    FRAME_BUTTONS[58].button->setSelected(false); \
    FRAME_BUTTONS[btnIdx].button->setSelected(true); \
    int needInit = 0; \
	if (hasLoadedISO && dynacore != cpuCore) { \
		SysClose(); \
		needInit = 1; \
	} \
	dynacore = cpuCore; \
	if(hasLoadedISO && needInit) { \
		if (dynacore == DYNACORE_DYNAREC) \
		{ \
			VM_Init(1024*1024, 256*1024); \
		} \
		SysInit(); \
		CheckCdrom(); \
		SysReset(); \
		LoadCdrom(); \
		Func_SetPlayGame(); \
		menu::MessageBox::getInstance().setMessage("Game Reset"); \
	}

void Func_CpuInterp()
{
	ChangeCpu(DYNACORE_INTERPRETER, 5);
}

void Func_CpuDynarec()
{
	ChangeCpu(DYNACORE_DYNAREC_OLD, 58);
}

void Func_CpuLightrec()
{
	ChangeCpu(DYNACORE_DYNAREC, 6);
}

void Func_BiosSelectHLE()
{
	for (int i = 7; i <= 10; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[7].button->setSelected(true);

	int needInit = 0;
	if(hasLoadedISO && biosDevice != BIOSDEVICE_HLE){ SysClose(); needInit = 1; }
	biosDevice = BIOSDEVICE_HLE;
	if(hasLoadedISO && needInit) {
		SysInit ();
		CheckCdrom();
		SysReset();
		LoadCdrom();
		Func_SetPlayGame();
		menu::MessageBox::getInstance().setMessage("Game Reset");
	}
}

int checkBiosExists(int testDevice)
{
	fileBrowser_file testFile;
	memset(&testFile, 0, sizeof(fileBrowser_file));

	biosFile_dir = (testDevice == BIOSDEVICE_SD) ? &biosDir_libfat_Default : &biosDir_libfat_USB;
	sprintf(&testFile.name[0], "%s/SCPH1001.BIN", &biosFile_dir->name[0]);
	biosFile_readFile  = fileBrowser_libfat_readFile;
	biosFile_open      = fileBrowser_libfat_open;
	biosFile_init      = fileBrowser_libfat_init;
	biosFile_deinit    = fileBrowser_libfat_deinit;
	biosFile_init(&testFile);  //initialize the bios device (it might not be the same as ISO device)
	return biosFile_open(&testFile);
}

void Func_BiosSelectSD()
{
	for (int i = 7; i <= 10; i++)
		FRAME_BUTTONS[i].button->setSelected(false);

	int needInit = 0;
	if(checkBiosExists(BIOSDEVICE_SD) == FILE_BROWSER_ERROR_NO_FILE) {
		menu::MessageBox::getInstance().setMessage("BIOS not found on SD");
		if(hasLoadedISO && biosDevice != BIOSDEVICE_HLE){ SysClose(); needInit = 1; }
		biosDevice = BIOSDEVICE_HLE;
		FRAME_BUTTONS[7].button->setSelected(true);
	}
	else {
		if(hasLoadedISO && biosDevice != BIOSDEVICE_SD){ SysClose(); needInit = 1; }
		biosDevice = BIOSDEVICE_SD;
		FRAME_BUTTONS[8].button->setSelected(true);
	}
	if(hasLoadedISO && needInit) {
		SysInit ();
		CheckCdrom();
		SysReset();
		LoadCdrom();
		Func_SetPlayGame();
		menu::MessageBox::getInstance().setMessage("Game Reset");
	}
}

void Func_BiosSelectUSB()
{
	for (int i = 7; i <= 10; i++)
		FRAME_BUTTONS[i].button->setSelected(false);

	int needInit = 0;
	if(checkBiosExists(BIOSDEVICE_USB) == FILE_BROWSER_ERROR_NO_FILE) {
		menu::MessageBox::getInstance().setMessage("BIOS not found on USB");
		if(hasLoadedISO && biosDevice != BIOSDEVICE_HLE){ SysClose(); needInit = 1; }
		biosDevice = BIOSDEVICE_HLE;
		FRAME_BUTTONS[7].button->setSelected(true);
	}
	else {
		if(hasLoadedISO && biosDevice != BIOSDEVICE_USB){ SysClose(); needInit = 1; }
		biosDevice = BIOSDEVICE_USB;
		FRAME_BUTTONS[9].button->setSelected(true);
	}
	if(hasLoadedISO && needInit) {
		SysInit ();
		CheckCdrom();
		SysReset();
		LoadCdrom();
		Func_SetPlayGame();
		menu::MessageBox::getInstance().setMessage("Game Reset");
	}
}

void Func_BiosSelectDVD()
{
  	menu::MessageBox::getInstance().setMessage("DVD BIOS not implemented");
}

void Func_BootBiosYes()
{
	/* If HLE bios selected, boot thru bios shouldn't make a difference. TODO: Check this.
	if(biosDevice == BIOSDEVICE_HLE) {
		menu::MessageBox::getInstance().setMessage("You must select a BIOS, not HLE");
		return;
	}*/

	for (int i = 11; i <= 12; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[11].button->setSelected(true);

	LoadCdBios = BOOTTHRUBIOS_YES;
}

void Func_BootBiosNo()
{
	for (int i = 11; i <= 12; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[12].button->setSelected(true);

	LoadCdBios = BOOTTHRUBIOS_NO;
}

bool executedBios = false;
bool executingBios = false;
void biosFileInit();

void Func_ExecuteBios()
{
	if (!hasLoadedISO)
	{
		frameLimit[0] = FRAMELIMIT_AUTO;
	    frameLimit[1] = FRAMELIMIT_AUTO;
		biosFileInit();
	}
	LoadCdBios = BOOTTHRUBIOS_YES;
	if(Config.HLE == BIOS_HLE) {
		menu::MessageBox::getInstance().setMessage("You must select a BIOS, not HLE");
		return;
	}
	if(hasLoadedISO || executedBios) {
		//TODO: Implement yes/no that current game will be reset
		SysClose();
	}

	//TODO: load/save memcards here
	if(SysInit() < 0) {
		menu::MessageBox::getInstance().setMessage("Failed to initialize system.\nTry loading an ISO.");
		return;
	}
	executedBios = true;
	executingBios = true;
	perf_reset();          /* same as go(): fresh counters and a fresh perf.log for this run */
	CheckCdrom();
	SysReset();
	pauseRemovalThread();
	resumeAudio();
	resumeInput();
	menuActive = 0;
	SysStartCPU();
	menuActive = 1;
	pauseInput();
	pauseAudio();
	continueRemovalThread();
}

void Func_SelectLanguage()
{
    FILE* fontFile = NULL;
    do
	{
		lang++;
		if (lang > TURKISH)
		{
			lang = ENGLISH;
		}
		else
		{
			fontFile = IplFont_getFontFile("sd");
			if (fontFile == NULL)
			{
				fontFile = IplFont_getFontFile("usb");
			}
		}
	}
	while (lang != ENGLISH && fontFile == NULL);

    IplFont_loadFontFile(fontFile);
    ChangeLanguage();

    FRAME_BUTTONS[54].button->setSelected(true);
    FRAME_BUTTONS[54].buttonString = LANG_STRINGS[(int)lang];
}

extern void writeConfig(FILE* f);

void Func_SaveSettingsSD()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	configFile_file = &saveDir_libfat_Default;
	struct stat s;
	mkdir("sd:/wiistation", 0777);   /* a new card has no folder yet: make it */
	if (stat("sd:/wiistation/", &s)) {
		menu::MessageBox::getInstance().setMessage("Error opening directory sd:/wiistation");
		return;
	}
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "sd:/wiistation/settings.ini", "wb" );  //attempt to open file
		if(f) {
			writeConfig(f);                                   //write out the config
			fclose(f);
			menu::MessageBox::getInstance().setMessage("Saved settings to SD");
			return;
		}
	}
	menu::MessageBox::getInstance().setMessage("Error saving settings to SD");
}

void Func_SaveSettingsUSB()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	configFile_file = &saveDir_libfat_USB;
	struct stat s;
	mkdir("usb:/wiistation", 0777);   /* a new card has no folder yet: make it */
	if (stat("usb:/wiistation/", &s)) {
		menu::MessageBox::getInstance().setMessage("Error opening directory usb:/wiistation");
		return;
	}
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "usb:/wiistation/settings.ini", "wb" ); //attempt to open file
		if(f) {
			writeConfig(f);                                   //write out the config
			fclose(f);
			menu::MessageBox::getInstance().setMessage("Saved settings to USB");
			return;
		}
	}
	menu::MessageBox::getInstance().setMessage("Error saving settings to USB");
}

/* Save the settings as they are now for the game that is loaded, to
 * <device>:/wiistation/settings/<CdromId>.cfg, which WiiStation reads over the global file
 * the next time that game starts. USB is used when it has the folder, otherwise SD.
 * The folder used to have to exist already, and saying so was all this did when it did
 * not; makeParentDirs creates it, the same as every other save. */
void Func_SaveSettingsSeparately()
{
    struct stat s;
    char settingPathBuf[256];
    fileBrowser_file* configFile_file;
    extern char CdromId[10];
	if (stat("usb:/wiistation/settings/", &s)) {
		{
			sprintf(settingPathBuf, "%s%s%s", "sd:/wiistation/settings/", CdromId, ".cfg");
			configFile_file = &saveDir_libfat_Default;
	        int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	        if(configFile_init(configFile_file)) {                //only if device initialized ok
				makeParentDirs(settingPathBuf);
				FILE* f = fopen( settingPathBuf, "wb" );  //attempt to open file
				if(f) {
					writeConfig(f);                                   //write out the config
					fclose(f);
					menu::MessageBox::getInstance().setMessage("Saved settings to SD");
					return;
				}
			}
			menu::MessageBox::getInstance().setMessage("Error saving settings to SD");
		}
	}
	else
	{
		sprintf(settingPathBuf, "%s%s%s", "usb:/wiistation/settings/", CdromId, ".cfg");
		configFile_file = &saveDir_libfat_USB;
	    int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	    if (configFile_init(configFile_file)) {                //only if device initialized ok
			makeParentDirs(settingPathBuf);
			FILE* f = fopen( settingPathBuf, "wb" ); //attempt to open file
			if(f) {
				writeConfig(f);                                   //write out the config
				fclose(f);
				menu::MessageBox::getInstance().setMessage("Saved settings to USB");
				return;
			}
		}
		menu::MessageBox::getInstance().setMessage("Error saving settings to USB");
	}
}
void Func_ShowFpsOn()
{
	for (int i = 16; i <= 17; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[16].button->setSelected(true);
	showFPSonScreen = FPS_SHOW;
}

void Func_ShowFpsOff()
{
	for (int i = 16; i <= 17; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[17].button->setSelected(true);
	showFPSonScreen = FPS_HIDE;
}

void SetFrameLimit()
{
    if (gpuPtr == &oldSoftGpu)
    {
        gpuPtr->setframelimit(0);
    }
}

void Func_FpsLimitAuto()
{
	for (int i = 18; i <= 19; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[18].button->setSelected(true);
	frameLimit[0] = FRAMELIMIT_AUTO;
	frameLimit[1] = FRAMELIMIT_AUTO;
	SetFrameLimit();
}

void Func_FpsLimitOff()
{
	for (int i = 18; i <= 19; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[19].button->setSelected(true);
	frameLimit[0] = FRAMELIMIT_NONE;
	frameLimit[1] = FRAMELIMIT_NONE;
	SetFrameLimit();
}

void Func_FrameSkipOn()
{
	for (int i = 20; i <= 21; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[20].button->setSelected(true);
	frameSkip = FRAMESKIP_ENABLE;
	SetFrameLimit();
}

void Func_FrameSkipOff()
{
	for (int i = 20; i <= 21; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[21].button->setSelected(true);
	frameSkip = FRAMESKIP_DISABLE;
	SetFrameLimit();
}


void Func_Interlaced()
{
	if(interlacedMode == INTERLACED_ENABLE)
	{
		FRAME_BUTTONS[23].button->setSelected(false);
		interlacedMode = INTERLACED_DISABLE;
	}
	else
	{
		FRAME_BUTTONS[23].button->setSelected(true);
		interlacedMode = INTERLACED_ENABLE;
	}
}


void Func_DeflickerFilter()
{
	if(deflickerFilter == DEFLICKER_ENABLE)
	{
		FRAME_BUTTONS[24].button->setSelected(false);
		deflickerFilter = DEFLICKER_DISABLE;
	}
	else
	{
		FRAME_BUTTONS[24].button->setSelected(true);
		deflickerFilter = DEFLICKER_ENABLE;
	}
}

void Func_ScreenMode()
{
	//SCREENMODE_4x3=0,
	//SCREENMODE_16x9,
	//SCREENMODE_16x9_PILLARBOX
    screenMode++;
    if (screenMode > SCREENMODE_16x9_PILLARBOX)
    {
        screenMode = SCREENMODE_4x3;
    }
    FRAME_BUTTONS[22].button->setSelected(true);
    FRAME_BUTTONS[22].buttonString = FRAME_STRINGS[27 + screenMode];
}

void Func_Screen240p()
{
	if(originalMode == ORIGINALMODE_ENABLE)
	{
		FRAME_BUTTONS[57].button->setSelected(false);
		originalMode = ORIGINALMODE_DISABLE;
	}
	else
	{
		FRAME_BUTTONS[57].button->setSelected(true);
		originalMode = ORIGINALMODE_ENABLE;
	}
	displayModeChanged = 1;
}

void Func_DitheringNone()
{
	for (int i = 25; i <= 27; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[25].button->setSelected(true);
	useDithering = USEDITHER_NONE;
	// Setting variables directly in the GPU is not good......
    iUseDither = useDithering;
}

void Func_DitheringDefault()
{
	for (int i = 25; i <= 27; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[26].button->setSelected(true);
	useDithering = USEDITHER_DEFAULT;
	// Setting variables directly in the GPU is not good......
    iUseDither = useDithering;
}

void Func_DitheringAlways()
{
	for (int i = 25; i <= 27; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[27].button->setSelected(true);
	useDithering = USEDITHER_ALWAYS;
	// Setting variables directly in the GPU is not good......
    iUseDither = useDithering;
}

void Func_BilinearFilter()
{
    bilinearFilter++;
	if(bilinearFilter > BILINEARFILTER_ENABLE)
	{
		bilinearFilter = BILINEARFILTER_DISABLE;
	}

	FRAME_BUTTONS[28].buttonString = TEXTURE_FILTER_STRINGS[(int)bilinearFilter];

	if (gpuPlugin == OPEN_GX)
    {
        extern unsigned int gTexMovieName;
        glChgTextureFilter(gTexMovieName);
    }
}

void Func_TrapFilter()
{
	if(trapFilter == TRAPFILTER_ENABLE)
	{
		FRAME_BUTTONS[29].button->setSelected(false);
		trapFilter = TRAPFILTER_DISABLE;
		VIDEO_SetTrapFilter(0);
	}
	else
	{
		FRAME_BUTTONS[29].button->setSelected(true);
		trapFilter = TRAPFILTER_ENABLE;
		VIDEO_SetTrapFilter(1);
	}
}

void Func_ForceNTSC()
{
	long oldPsxType = Config.PsxType;
	if(forceNTSC == FORCENTSC_ENABLE)
	{
		FRAME_BUTTONS[63].button->setSelected(false);
		forceNTSC = FORCENTSC_DISABLE;
		if (hasLoadedISO)
		{
			CheckPsxType();
		}
	}
	else
	{
		FRAME_BUTTONS[63].button->setSelected(true);
		forceNTSC = FORCENTSC_ENABLE;
		Config.PsxType = PSX_TYPE_NTSC;
	}

	if (hasLoadedISO && oldPsxType != Config.PsxType)
	{
		psxResetRcntRate();
		pl_chg_psxtype(Config.PsxType);
		gpuChangePsxType();

		SetFrameLimit();
	}
}

void Func_ConfigureInput()
{
//	menu::MessageBox::getInstance().setMessage("Input configuration not implemented");
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREINPUT,ConfigureInputFrame::SUBMENU_REINIT);
}

void Func_AdvancedSound()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_SOUND);
}

void Func_PluginsPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_PLUGINS);
}

void Func_CDPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_STORAGE);
}

void Func_AdvGfxPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_ADVGFX);
}

void Func_MemoryPage()
{
	pMenuContext->setActiveFrame(MenuContext::FRAME_OPTIONS, OptionsFrame::PAGE_MEMORY);
}

/* Called once when the Plugins page is left, with the pair the user settled on. Switching
 * either plugin with a game running means tearing the emulator down and building it again,
 * which is why it does not happen on every press of a cycling button. Mirrors the ChangeCpu
 * macro and Func_Use*Gpu, which still serve the (now hidden) General-tab buttons. */
void ApplyPluginSelection(char wantCore, char wantGpu)
{
	int needInit = 0;

	if (hasLoadedISO && (dynacore != wantCore || gpuPlugin != wantGpu)) {
		SysClose();
		needInit = 1;
	}
	dynacore  = wantCore;
	gpuPlugin = wantGpu;

	if      (gpuPlugin == OLD_SOFT) gpuPtr = &oldSoftGpu;
	else if (gpuPlugin == NEW_SOFT) gpuPtr = &newSoftGpu;
	else                            gpuPtr = &glesGpu;

	if (hasLoadedISO && needInit) {
		if (dynacore == DYNACORE_DYNAREC)
			VM_Init(1024*1024, 256*1024);
		SysInit();
		CheckCdrom();
		SysReset();
		LoadCdrom();
		Func_SetPlayGame();
		menu::MessageBox::getInstance().setMessage("Game Reset");
	}
}

void Func_ConfigureButtons()
{
//	menu::MessageBox::getInstance().setMessage("Button Mapping not implemented");
	pMenuContext->setActiveFrame(MenuContext::FRAME_CONFIGUREBUTTONS,ConfigureButtonsFrame::SUBMENU_PSX_PADNONE);
}

/* The Controller Type row: buttons 32 (Standard), 33 (Analog) and 79 (Stick D-pad). */
static void selectPsxType(int button, char type)
{
	FRAME_BUTTONS[32].button->setSelected(false);
	FRAME_BUTTONS[33].button->setSelected(false);
	FRAME_BUTTONS[79].button->setSelected(false);
	FRAME_BUTTONS[button].button->setSelected(true);
	controllerType = type;
}

void Func_PsxTypeStandard()  { selectPsxType(32, CONTROLLERTYPE_STANDARD); }
void Func_PsxTypeAnalog()    { selectPsxType(33, CONTROLLERTYPE_ANALOG); }
void Func_PsxTypeStickDpad() { selectPsxType(79, CONTROLLERTYPE_STICKDPAD); }

void Func_PsxTypeLightgun()
{
    lightGun++;
	FRAME_BUTTONS[59].button->setSelected(true);
    if (lightGun > LIGHTGUN_MOUSE)
    {
        lightGun = LIGHTGUN_DISABLE;
		FRAME_BUTTONS[59].button->setSelected(false);
    }
    FRAME_BUTTONS[59].buttonString = FRAME_STRINGS[70 + lightGun];
}

void Func_EnableRumbleYes()
{
	for (int i = 34; i <= 35; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[34].button->setSelected(true);
	rumbleEnabled = RUMBLE_ENABLE;
}

void Func_EnableRumbleNo()
{
	for (int i = 34; i <= 35; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[35].button->setSelected(true);
	rumbleEnabled = RUMBLE_DISABLE;
}

void Func_Memcard1()
{
	if(memCard[0] == MEMCARD_ENABLE)
	{
		FRAME_BUTTONS[60].button->setSelected(false);
		memCard[0] = MEMCARD_DISABLE;
	}
	else
	{
		FRAME_BUTTONS[60].button->setSelected(true);
		memCard[0] = MEMCARD_ENABLE;
	}
}

void Func_Memcard2()
{
	if(memCard[1] == MEMCARD_ENABLE)
	{
		FRAME_BUTTONS[61].button->setSelected(false);
		memCard[1] = MEMCARD_DISABLE;
	}
	else
	{
		FRAME_BUTTONS[61].button->setSelected(true);
		memCard[1] = MEMCARD_ENABLE;
	}
}

/* What one memory card is, in one button: 0 Off, 1 Shared, 2 Game. Two settings hold it
 * -- whether the card is there at all, and which file it lives in -- because both are
 * written to the settings file under the names they have always had. */
static int cardType(int card)
{
	if (memCard[card] == MEMCARD_DISABLE) return 0;
	return memCardFile[card] == MEMCARDFILE_SHARED ? 1 : 2;
}

/* Off -> Shared -> Game -> Off. The file choice takes effect at the next game load,
 * because the card in memory was read from the old file. */
static void cardTypeCycle(int card)
{
	int next = (cardType(card) + 1) % 3;
	memCard[card] = next ? MEMCARD_ENABLE : MEMCARD_DISABLE;
	if (next) memCardFile[card] = (next == 1) ? MEMCARDFILE_SHARED : MEMCARDFILE_PER_GAME;
	FRAME_BUTTONS[76 + card].buttonString = FRAME_STRINGS[92 + next];
}

void Func_CardType1() { cardTypeCycle(0); }
void Func_CardType2() { cardTypeCycle(1); }



void Func_SaveButtonsSD()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	int num_written = 0;
	configFile_file = &saveDir_libfat_Default;
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "sd:/wiistation/controlG.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_GC);					//write out GC controller mappings
			fclose(f);
			num_written++;
		}
#ifdef HW_RVL
        f = fopen( "sd:/wiistation/controlH.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_HidGC);			//write out HID controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "sd:/wiistation/controlC.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_Classic);			//write out Classic controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "sd:/wiistation/controlN.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_WiimoteNunchuk);	//write out WM+NC controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "sd:/wiistation/controlW.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_Wiimote);			//write out Wiimote controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen("sd:/wiistation/controlP.cfg", "wb");  //attempt to open file
		if (f) {
			save_configurations(f, &controller_WiiUPro);			//write out Wii U Pro controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen("sd:/wiistation/controlD.cfg", "wb");  //attempt to open file
		if (f) {
			save_configurations(f, &controller_WiiUGamepad);		//write out Wii U Gamepad controller mappings
			fclose(f);
			num_written++;
		}
#endif //HW_RVL
	}
	if (num_written == num_controller_t)
		menu::MessageBox::getInstance().setMessage("Saved Button Configs to SD");
	else
		menu::MessageBox::getInstance().setMessage("Error saving Button Configs to SD");
}

void Func_SaveButtonsUSB()
{
	fileBrowser_file* configFile_file;
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	int num_written = 0;
	configFile_file = &saveDir_libfat_USB;
	if(configFile_init(configFile_file)) {                //only if device initialized ok
		FILE* f = fopen( "usb:/wiistation/controlG.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_GC);					//write out GC controller mappings
			fclose(f);
			num_written++;
		}
#ifdef HW_RVL
        f = fopen( "usb:/wiistation/controlH.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_HidGC);			//write out HID controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "usb:/wiistation/controlC.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_Classic);			//write out Classic controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "usb:/wiistation/controlN.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_WiimoteNunchuk);	//write out WM+NC controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen( "usb:/wiistation/controlW.cfg", "wb" );  //attempt to open file
		if(f) {
			save_configurations(f, &controller_Wiimote);			//write out Wiimote controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen("usb:/wiistation/controlP.cfg", "wb");  //attempt to open file
		if (f) {
			save_configurations(f, &controller_WiiUPro);			//write out Wii U Pro controller mappings
			fclose(f);
			num_written++;
		}
		f = fopen("usb:/wiistation/controlD.cfg", "wb");  //attempt to open file
		if (f) {
			save_configurations(f, &controller_WiiUGamepad);		//write out Wii U Gamepad controller mappings
			fclose(f);
			num_written++;
		}
#endif //HW_RVL
	}
	if (num_written == num_controller_t)
		menu::MessageBox::getInstance().setMessage("Saved Button Configs to USB");
	else
		menu::MessageBox::getInstance().setMessage("Error saving Button Configs to USB");
}

void Func_SetButtonLoad()
{
	if (loadButtonSlot == LOADBUTTON_DEFAULT)
		strcpy(FRAME_STRINGS[42], "Default");
	else
		sprintf(FRAME_STRINGS[42], "Slot %d", loadButtonSlot+1);
}

void Func_ToggleButtonLoad()
{
	loadButtonSlot = (loadButtonSlot + 1) % 5;
	Func_SetButtonLoad();
}

/* The Audio tab's Enable row: one toggle per sound source, lit when that source plays.
 * The settings file keeps the historical "Audio" / "DisableXa" / "DisableCdda" keys and
 * Config.Xa/Config.Cdda keep their inverted sense, so each toggle writes both. */
void Func_ToggleAudio()
{
	audioEnabled = (audioEnabled == AUDIO_ENABLE) ? AUDIO_DISABLE : AUDIO_ENABLE;
	FRAME_BUTTONS[39].button->setSelected(audioEnabled == AUDIO_ENABLE);
}

void Func_ToggleXa()
{
	int on = (Config.Xa == XA_DISABLE);          // was muted -> turn it on
	Config.Xa = xaDisabled = on ? XA_ENABLE : XA_DISABLE;
	FRAME_BUTTONS[40].button->setSelected(on);
}

void Func_ToggleCdda()
{
	int on = (Config.Cdda == CDDA_DISABLE);
	Config.Cdda = cddaDisabled = on ? CDDA_ENABLE : CDDA_DISABLE;
	FRAME_BUTTONS[41].button->setSelected(on);
}

void Func_SoundHwAccelYes()
{
	for (int i = 67; i <= 68; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[67].button->setSelected(true);
	soundHwAccel = SOUND_HW_ACCEL_ON;
}

void Func_SoundHwAccelNo()
{
	for (int i = 67; i <= 68; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[68].button->setSelected(true);
	soundHwAccel = SOUND_HW_ACCEL_OFF;
}

/* Sync: how the sound output keeps pace with the mixer. A three-way group like Dithering:
 * Off (neither), Tempo (the legacy mixer-clock pull-back, SoundTempo) or Rate (the output-
 * stage rate control, SoundRateControl). The settings file keeps two keys; this is the one
 * place that makes them exclusive. ratectl.c reads soundRateControl itself. */
static void soundSyncSelect(int which)
{
	for (int i = 69; i <= 71; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[69 + which].button->setSelected(true);
	soundTempo       = (which == 1) ? SOUND_TEMPO_ON : SOUND_TEMPO_OFF;
	soundRateControl = (which == 2) ? SOUND_RATE_CONTROL_ON : SOUND_RATE_CONTROL_OFF;
	setSpuTempo(soundTempo);
}
void Func_SoundSyncOff()   { soundSyncSelect(0); }
void Func_SoundSyncTempo() { soundSyncSelect(1); }
void Func_SoundSyncRate()  { soundSyncSelect(2); }

#ifdef SHOW_DEBUG
extern bool canWriteLog;
#endif // SHOW_DEBUG
void Func_InterpolationToggle()
{
	if (spuInterpolation == SIMPLE_INTERPOLATION)
	{
		spuInterpolation = GAUSSI_INTERPOLATION;
	}
	else
	{
		spuInterpolation = SIMPLE_INTERPOLATION;
	}
	FRAME_BUTTONS[45].buttonString = FRAME_STRINGS[46 + spuInterpolation];
	setSpuInterpolation(spuInterpolation);
}

void Func_MemcardSaveSD()
{
	for (int i = 46; i <= 49; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[46].button->setSelected(true);
	nativeSaveDevice = NATIVESAVEDEVICE_SD;
}

void Func_MemcardSaveUSB()
{
	for (int i = 46; i <= 49; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[47].button->setSelected(true);
	nativeSaveDevice = NATIVESAVEDEVICE_USB;
}

void Func_MemcardSaveCardA()
{
	for (int i = 46; i <= 49; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[48].button->setSelected(true);
	nativeSaveDevice = NATIVESAVEDEVICE_CARDA;
}

void Func_MemcardSaveCardB()
{
	for (int i = 46; i <= 49; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[49].button->setSelected(true);
	nativeSaveDevice = NATIVESAVEDEVICE_CARDB;
}

void Func_AutoSaveYes()
{
	for (int i = 50; i <= 51; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[50].button->setSelected(true);
	autoSave = AUTOSAVE_ENABLE;
}

void Func_AutoSaveNo()
{
	for (int i = 50; i <= 51; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[51].button->setSelected(true);
	autoSave = AUTOSAVE_DISABLE;
}

void Func_SaveStateSD()
{
	for (int i = 52; i <= 53; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[52].button->setSelected(true);
	saveStateDevice = SAVESTATEDEVICE_SD;
}

void Func_SaveStateUSB()
{
	for (int i = 52; i <= 53; i++)
		FRAME_BUTTONS[i].button->setSelected(false);
	FRAME_BUTTONS[53].button->setSelected(true);
	saveStateDevice = SAVESTATEDEVICE_USB;
}

void Func_FastloadYes()
{
	fastLoad = 1;
	FRAME_BUTTONS[55].button->setSelected(true);
	FRAME_BUTTONS[56].button->setSelected(false);
}

void Func_FastloadNo()
{
    fastLoad = 0;
	FRAME_BUTTONS[55].button->setSelected(false);
	FRAME_BUTTONS[56].button->setSelected(true);
}

void Func_UseOldSoftGpu()
{
    int needInit = 0;
    if(hasLoadedISO && gpuPlugin != OLD_SOFT){ SysClose(); needInit = 1; }
    gpuPlugin = OLD_SOFT;
    gpuPtr = &oldSoftGpu;
    FRAME_BUTTONS[64].button->setSelected(true);
    FRAME_BUTTONS[65].button->setSelected(false);
    FRAME_BUTTONS[66].button->setSelected(false);
    if(hasLoadedISO && needInit) {
        SysInit ();
        CheckCdrom();
        SysReset();
        LoadCdrom();
        Func_SetPlayGame();
        menu::MessageBox::getInstance().setMessage("Game Reset");
    }
}

void Func_UseNewSoftGpu()
{
    int needInit = 0;
    if(hasLoadedISO && gpuPlugin != NEW_SOFT){ SysClose(); needInit = 1; }
    gpuPlugin = NEW_SOFT;
    gpuPtr = &newSoftGpu;
    FRAME_BUTTONS[64].button->setSelected(false);
    FRAME_BUTTONS[65].button->setSelected(true);
    FRAME_BUTTONS[66].button->setSelected(false);
    if(hasLoadedISO && needInit) {
        SysInit ();
        CheckCdrom();
        SysReset();
        LoadCdrom();
        Func_SetPlayGame();
        menu::MessageBox::getInstance().setMessage("Game Reset");
    }
}

void Func_UseOpenGxGpu()
{
    int needInit = 0;
    if(hasLoadedISO && gpuPlugin != OPEN_GX){ SysClose(); needInit = 1; }
    gpuPlugin = OPEN_GX;
    gpuPtr = &glesGpu;
    FRAME_BUTTONS[64].button->setSelected(false);
    FRAME_BUTTONS[65].button->setSelected(false);
    FRAME_BUTTONS[66].button->setSelected(true);
    if(hasLoadedISO && needInit) {
        SysInit ();
        CheckCdrom();
        SysReset();
        LoadCdrom();
        Func_SetPlayGame();
        menu::MessageBox::getInstance().setMessage("Game Reset");
    }
}

/* Press one Settings button from a script (autoinput.txt "menuclick").
 * Nothing else in an unattended run can reach a menu button: the menu reads the pads
 * directly, not through the controller drivers that autoinput feeds. */
extern "C" void SettingsFrame_ScriptClick(int button)
{
	if (button < 0 || button >= NUM_FRAME_BUTTONS) return;
	if (FRAME_BUTTONS[button].clickedFunc) FRAME_BUTTONS[button].clickedFunc();
}

void Func_ReturnFromSettingsFrame()
{
	menu::Gui::getInstance().menuLogo->setLocation(580.0, 70.0, -50.0);
	pMenuContext->setActiveFrame(MenuContext::FRAME_MAIN);
}

