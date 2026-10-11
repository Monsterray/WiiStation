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
#define NUM_FRAME_TEXTBOXES 30
#define FRAME_TEXTBOXES settingsFrameTextBoxes

/*
General Tab:
Advanced; Memory               (one row of sub-pages, OptionsFrame.cpp. Advanced has tabs
	CPU, GPU, SPU, CD, Input: the CPU core, GPU plugin and CD settings live there, so
	buttons 5/6/58, 64/65/66 and 74 (CD) are still in the table below but never shown)
Select Bios: HLE; SD; USB; DVD
Boot Games Through Bios: Yes; No + Execute Bios
Select language
Fast Load: Yes; No
Save Settings: SD; USB; This Game   (kept on the bottom row)

Video Tab:
Show FPS: Yes; No
Limit FPS: Auto; Off; xxx
Frame Skip: On; Off
Screen Mode: 4:3; 16:9
Scaling: None; 2xSaI
Filters: Bilinear; Trap; Deflicker
Advanced                       (Dithering and MDEC Chroma live on that page)

Input Tab:
Assign Controllers (assign player->pad)
Configure Button Mappings
PSX Controller Type: Standard/Analog/Stick D-pad
Number of Multitaps: 0, 1, 2

Audio Tab:
Disable Audio: Yes; No
Disable XA: Yes; No
Disable CDDA: Yes; No
Interpolation: Simple; Gaussi   (this control was historically labelled "Volume"; it has
	always set spuInterpolation, and there is no volume control)
DSP Sound: Yes; No              (SoundHwAccel: hand the stream to the DSP instead of
	resampling on the CPU)
Sync: Off; Tempo; Rate          (SoundTempo / SoundRateControl: how the output keeps pace
	with the mixer; the two file keys are made exclusive here, see SETTINGS.md)
Advanced                        (opens the Advanced Sound page, AdvancedSoundFrame.cpp:
	options under test, table-driven so rows can be added and removed freely)

Saves Tab:
Memcard Save Device: SD; USB; CardA; CardB
Auto Save Memcards: Yes; No
Save States Device: SD; USB
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

struct ButtonInfo
{
	menu::Button	*button;
	int				buttonStyle;
	char*			buttonString;
	float			x;
	float			y;
	float			width;
	float			height;
	int				focusUp;
	int				focusDown;
	int				focusLeft;
	int				focusRight;
	ButtonFunc		clickedFunc;
	ButtonFunc		returnFunc;
} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =
{ //	button	buttonStyle buttonString		x		y		width	height	Up	Dwn	Lft	Rt	clickFunc				returnFunc
	//Buttons for Tabs (starts at button[0])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[0],	 25.0,	 20.0,	110.0,	56.0,	-1,	-1,	 4,	 1,	Func_TabGeneral,		Func_ReturnFromSettingsFrame }, // General tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[1],	155.0,	 20.0,	100.0,	56.0,	-1,	-1,	 0,	 2,	Func_TabVideo,			Func_ReturnFromSettingsFrame }, // Video tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[2],	275.0,	 20.0,	100.0,	56.0,	-1,	-1,	 1,	 3,	Func_TabInput,			Func_ReturnFromSettingsFrame }, // Input tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[3],	395.0,	 20.0,	100.0,	56.0,	-1,	-1,	 2,	 4,	Func_TabAudio,			Func_ReturnFromSettingsFrame }, // Audio tab
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[4],	515.0,	 20.0,	100.0,	56.0,	-1,	-1,	 3,	 0,	Func_TabSaves,			Func_ReturnFromSettingsFrame }, // Saves tab
	//Buttons for General Tab (starts at button[5])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[10],	215.0,	100.0,	140.0,	56.0,	 0,	 7,	 58, 6,	Func_CpuInterp,			Func_ReturnFromSettingsFrame }, // CPU: Interp
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[69],	365.0,	100.0,	130.0,	56.0,	 0,	 9,	 5,	 58,Func_CpuLightrec,		Func_ReturnFromSettingsFrame }, // CPU: Lightrec
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[12],	295.0,	160.0,	 70.0,	56.0,	73,	11,	10,	 8,	Func_BiosSelectHLE,		Func_ReturnFromSettingsFrame }, // Bios: HLE
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	375.0,	160.0,	 55.0,	56.0,	73,	11,	 7,	 9,	Func_BiosSelectSD,		Func_ReturnFromSettingsFrame }, // Bios: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	440.0,	160.0,	 70.0,	56.0,	75,	12,	 8,	10,	Func_BiosSelectUSB,		Func_ReturnFromSettingsFrame }, // Bios: USB
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[15],	520.0,	160.0,	 70.0,	56.0,	75,	12,	 9,	 7,	Func_BiosSelectDVD,		Func_ReturnFromSettingsFrame }, // Bios: DVD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	295.0,	220.0,	 75.0,	56.0,	 7,	54,	13,	12,	Func_BootBiosYes,		Func_ReturnFromSettingsFrame }, // Boot Thru Bios: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	380.0,	220.0,	 75.0,	56.0,	 8,	54,	11,	13,	Func_BootBiosNo,		Func_ReturnFromSettingsFrame }, // Boot Thru Bios: No
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[8],	465.0,	220.0,	171.0,	56.0,	9,	54,	12,	11,	Func_ExecuteBios,		Func_ReturnFromSettingsFrame }, // Execute Bios

	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[13],	253.0,	400.0,	 55.0,	56.0,	55,	 0,	62,	15,	Func_SaveSettingsSD,	Func_ReturnFromSettingsFrame }, // Save Settings: SD
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[14],	318.0,	400.0,	 70.0,	56.0,	55,	 0,	14,	62,	Func_SaveSettingsUSB,	Func_ReturnFromSettingsFrame }, // Save Settings: USB

	//Buttons for Video Tab (starts at button[16])
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[24],	295.0,	100.0,	 75.0,	56.0,	 1,	18,	17,	17,	Func_ShowFpsOn,			Func_ReturnFromSettingsFrame }, // Show FPS: On
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	380.0,	100.0,	 75.0,	56.0,	 1,	19,	16,	16,	Func_ShowFpsOff,		Func_ReturnFromSettingsFrame }, // Show FPS: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[26],	295.0,	160.0,	 75.0,	56.0,	16,	20,	63,	19,	Func_FpsLimitAuto,		Func_ReturnFromSettingsFrame }, // FPS Limit: Auto
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	380.0,	160.0,	 75.0,	56.0,	17,	21,	18,	63,	Func_FpsLimitOff,		Func_ReturnFromSettingsFrame }, // FPS Limit: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[24],	295.0,	220.0,	 75.0,	56.0,	18,	23,	21,	21,	Func_FrameSkipOn,		Func_ReturnFromSettingsFrame }, // Frame Skip: On
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	380.0,	220.0,	 75.0,	56.0,	19,	23,	20,	20,	Func_FrameSkipOff,		Func_ReturnFromSettingsFrame }, // Frame Skip: Off
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[27],	295.0,	280.0,	 130.0,	56.0,	20,	25,	57,	23,	Func_ScreenMode,		Func_ReturnFromSettingsFrame }, // ScreenMode: 4:3/16:9/Force16:9
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[67],	435.0,	280.0,	 125.0,	56.0,	20,	26,	22,	57,	Func_Interlaced,		Func_ReturnFromSettingsFrame }, // Interlaced Mode
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[68],	428.0,	340.0,	110.0,	56.0,	23,	78,	29,	28,	Func_DeflickerFilter,	Func_ReturnFromSettingsFrame }, // Filters: Deflicker Filter
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[30],	238.0,	340.0,	 70.0,	56.0,	22,	28,	27,	26,	Func_DitheringNone,		Func_ReturnFromSettingsFrame }, // Dithering: None
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[32],	318.0,	340.0,	105.0,	56.0,	23,	29,	25,	27,	Func_DitheringDefault,	Func_ReturnFromSettingsFrame }, // Dithering: Game Dependent
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[33],	433.0,	340.0,	105.0,	56.0,	23,	24,	26,	25,	Func_DitheringAlways,	Func_ReturnFromSettingsFrame }, // Dithering: Always
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[65],	238.0,	340.0,	 95.0,	56.0,	22,	78,	24,	29,	Func_BilinearFilter,	Func_ReturnFromSettingsFrame }, // Filters: Bilinear Filter
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[66],	343.0,	340.0,	 75.0,	56.0,	22,	78,	28,	24,	Func_TrapFilter,		Func_ReturnFromSettingsFrame }, // Filters: Trap Filter
	//Buttons for Input Tab (starts at button[30])
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[34],	 85.0,	100.0,	220.0,	56.0,	 2,	32,	31,	31,	Func_ConfigureInput,	Func_ReturnFromSettingsFrame }, // Configure Input Assignment
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[35],	320.0,	100.0,	235.0,	56.0,	 2,	32,	30,	30,	Func_ConfigureButtons,	Func_ReturnFromSettingsFrame }, // Configure Button Mappings
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[38],	271.0,	160.0,	105.0,	56.0,	30,	59,	79,	33,	Func_PsxTypeStandard,	Func_ReturnFromSettingsFrame }, // PSX Controller Type: Standard
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[39],	386.0,	160.0,	100.0,	56.0,	31,	59,	32,	79,	Func_PsxTypeAnalog,		Func_ReturnFromSettingsFrame }, // PSX Controller Type: Analog
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	295.0,	280.0,	 75.0,	56.0,	59,	36,	35,	35,	Func_EnableRumbleYes,	Func_ReturnFromSettingsFrame }, // Enable Rumble: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	380.0,	280.0,	 75.0,	56.0,	59,	37,	34,	34,	Func_EnableRumbleNo,	Func_ReturnFromSettingsFrame }, // Enable Rumble: No
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[13],	295.0,	340.0,	 55.0,	56.0,	34,	38,	37,	37,	Func_SaveButtonsSD,		Func_ReturnFromSettingsFrame }, // Save Button Mappings: SD
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[14],	360.0,	340.0,	 70.0,	56.0,	35,	38,	36,	36,	Func_SaveButtonsUSB,	Func_ReturnFromSettingsFrame }, // Save Button Mappings: USB
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[42],	295.0,	400.0,	135.0,	56.0,	36,	 2,	-1,	-1,	Func_ToggleButtonLoad,	Func_ReturnFromSettingsFrame }, // Auto Load Button Config Slot: Default,1,2,3,4
	//Buttons for Audio Tab (starts at button[39]) ..was[45]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[88],	295.0,	100.0,	100.0,	56.0,	 3,	45,	41,	40,	Func_ToggleAudio,		Func_ReturnFromSettingsFrame }, // Enable: Audio
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[89],	405.0,	100.0,	 75.0,	56.0,	 3,	45,	39,	41,	Func_ToggleXa,			Func_ReturnFromSettingsFrame }, // Enable: XA
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[90],	490.0,	100.0,	110.0,	56.0,	 3,	45,	40,	39,	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // Enable: CDDA
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	440.0,	160.0,	 75.0,	56.0,	39,	45,	41,	41,	Func_ToggleXa,			Func_ReturnFromSettingsFrame }, // (spare, never shown: the Enable row replaced the three Yes/No pairs; kept so no later index moves)
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	345.0,	220.0,	 75.0,	56.0,	39,	45,	41,	41,	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // (spare, never shown)
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	440.0,	220.0,	 75.0,	56.0,	39,	45,	41,	41,	Func_ToggleCdda,		Func_ReturnFromSettingsFrame }, // (spare, never shown)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[47],	295.0,	170.0,	170.0,	56.0,	39,	67,	-1,	-1,	Func_InterpolationToggle,	Func_ReturnFromSettingsFrame }, // Interpolation: Simple/Gaussian
	//Buttons for Saves Tab (starts at button[46]) ..was[54]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	295.0,	100.0,	 55.0,	56.0,	 4,	76,	49,	47,	Func_MemcardSaveSD,		Func_ReturnFromSettingsFrame }, // Memcard Save: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	360.0,	100.0,	 70.0,	56.0,	 4,	77,	46,	48,	Func_MemcardSaveUSB,	Func_ReturnFromSettingsFrame }, // Memcard Save: USB
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[54],	440.0,	100.0,	 90.0,	56.0,	 4,	77,	47,	49,	Func_MemcardSaveCardA,	Func_ReturnFromSettingsFrame }, // Memcard Save: Card A
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[55],	540.0,	100.0,	 90.0,	56.0,	 4,	77,	48,	46,	Func_MemcardSaveCardB,	Func_ReturnFromSettingsFrame }, // Memcard Save: Card B
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	295.0,	240.0,	 75.0,	56.0,	76,	52,	51,	51,	Func_AutoSaveYes,		Func_ReturnFromSettingsFrame }, // Auto Save Memcards: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	380.0,	240.0,	 75.0,	56.0,	77,	53,	50,	50,	Func_AutoSaveNo,		Func_ReturnFromSettingsFrame }, // Auto Save Memcards: No
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[13],	295.0,	310.0,	 55.0,	56.0,	50,	 4,	53,	53,	Func_SaveStateSD,		Func_ReturnFromSettingsFrame }, // Save State: SD
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[14],	360.0,	310.0,	 70.0,	56.0,	51,	 4,	52,	52,	Func_SaveStateUSB,		Func_ReturnFromSettingsFrame }, // Save State: USB
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[57],	295.0,	280.0,	 90.0,	56.0,	 11,55,	-1,	-1,	Func_SelectLanguage,	Func_ReturnFromSettingsFrame }, // Select Language: En
    //Buttons for Saves Tab (starts at button[55]) ..was[61]
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	295.0,	340.0,	 75.0,	56.0,	54,	14,	56,	56,	Func_FastloadYes,		Func_ReturnFromSettingsFrame }, // Fast load: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	380.0,	340.0,	 75.0,	56.0,	54,	14,	55,	55,	Func_FastloadNo,		Func_ReturnFromSettingsFrame }, // Fast load: No
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[64],	570.0,	280.0,	 66.0,	56.0,	21,	27,	23,	22,	Func_Screen240p,		Func_ReturnFromSettingsFrame },  // ScreenMode: 240p
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[11],	505.0,	100.0,	130.0,	56.0,	 0,	 9,	 6,	 5,	Func_CpuDynarec,		Func_ReturnFromSettingsFrame },  // CPU: Dynarec
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[70],	295.0,	220.0,	135.0,	56.0,	32,	34,	-1,	-1,	Func_PsxTypeLightgun,	Func_ReturnFromSettingsFrame },  // Light Gun: Off/GunCon/Justifier/Mouse
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[74],	295.0,	310.0,	155.0,	56.0,	52,	4,	61,	61,	Func_Memcard1,			Func_ReturnFromSettingsFrame },  // Memcard 1 toggle
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[75],	460.0,	310.0,	155.0,	56.0,	53,	4,	60,	60,	Func_Memcard2,			Func_ReturnFromSettingsFrame },  // Memcard 2 toggle

	//Buttons for ... (starts at button[62]) ..was[65]
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[77],	398.0,	400.0,	 140.0,	56.0,	56,	 0,	15,	14,	Func_SaveSettingsSeparately,	Func_ReturnFromSettingsFrame }, // Save Settings: Separately
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[78],	465.0,	160.0,	160.0,	56.0,	17,	21,	19,	18,	Func_ForceNTSC,			Func_ReturnFromSettingsFrame },  // Force NTSC toggle

	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[1],	215.0,	160.0,	140.0,	56.0,	 5,	 7,	 66, 65,Func_UseOldSoftGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: Soft Fast
	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[2],	365.0,	160.0,	130.0,	56.0,	 6,	 9,	 64, 66,Func_UseNewSoftGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: Soft Timed
	{	NULL,	BTN_A_SEL,	GPU_PLUGIN_STRINGS[3],	505.0,	160.0,	130.0,	56.0,	 6,	 9,	 65, 64,Func_UseOpenGxGpu,		Func_ReturnFromSettingsFrame }, // GpuPlugin: OpenGX

	//Audio tab, appended at the end (starts at button[67]) so no existing index moves
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[16],	295.0,	240.0,	 75.0,	56.0,	45,	69,	68,	68,	Func_SoundHwAccelYes,	Func_ReturnFromSettingsFrame }, // DSP Sound: Yes
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[17],	380.0,	240.0,	 75.0,	56.0,	45,	69,	67,	67,	Func_SoundHwAccelNo,	Func_ReturnFromSettingsFrame }, // DSP Sound: No
	//Audio tab, Sync group (buttons 69..71): like Dithering, one of three is selected. The trio
	//starts left of the other rows' buttons (the "Sync" label is short) and leaves a gap before
	//Advanced, which sits in the row's right corner (right edge 645, the tab's widest row).
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[25],	295.0,	310.0,	 60.0,	56.0,	67,	72,	71,	70,	Func_SoundSyncOff,		Func_ReturnFromSettingsFrame }, // Sync: Off
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[81],	365.0,	310.0,	 90.0,	56.0,	67,	72,	69,	71,	Func_SoundSyncTempo,	Func_ReturnFromSettingsFrame }, // Sync: Tempo
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[82],	465.0,	310.0,	 70.0,	56.0,	68,	72,	70,	69,	Func_SoundSyncRate,		Func_ReturnFromSettingsFrame }, // Sync: Rate
	//Audio tab, Advanced (button 72): opens the Advanced Sound page (AdvancedSoundFrame.cpp)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[83],	258.0,	380.0,	125.0,	56.0,	69,	 3,	-1,	-1,	Func_AdvancedSound,		Func_ReturnFromSettingsFrame }, // Advanced: opens the Advanced Sound page
	//General tab: one row of sub-pages. The CPU core and GPU plugin moved to Plugins and the
	//CD settings to the CD page; buttons 5/6/58 and 64/65/66 stay in this table but are not shown.
	//Up goes to the General tab, as the first row of every other tab goes to its tab.
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[84],	175.0,	100.0,	130.0,	56.0,	 0,	 7,	75,	75,	Func_PluginsPage,		Func_ReturnFromSettingsFrame }, // Advanced pages
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[85],	255.0,	100.0,	130.0,	56.0,	 0,	 8,	73,	75,	Func_CDPage,			Func_ReturnFromSettingsFrame }, // CD page
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[86],	335.0,	100.0,	130.0,	56.0,	 0,	 9,	73,	73,	Func_MemoryPage,		Func_ReturnFromSettingsFrame }, // Memory page
	//Saves tab: which file each memory card lives in (buttons 76 and 77)
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[94],	295.0,	170.0,	105.0,	56.0,	46,	50,	77,	77,	Func_CardType1,			Func_ReturnFromSettingsFrame }, // Memcard 1 type: Off/Shared/Game
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[93],	430.0,	170.0,	105.0,	56.0,	47,	51,	76,	76,	Func_CardType2,			Func_ReturnFromSettingsFrame }, // Memcard 2 type: Off/Shared/Game
	{	NULL,	BTN_A_NRM,	FRAME_STRINGS[96],	245.0,	400.0,	150.0,	56.0,	28,	 1,	78,	78,	Func_AdvGfxPage,		Func_ReturnFromSettingsFrame }, // Video tab: Advanced Graphics page
	//Input tab: the third controller type (button 79), appended so no index moves
	{	NULL,	BTN_A_SEL,	FRAME_STRINGS[98],	496.0,	160.0,	140.0,	56.0,	31,	59,	33,	32,	Func_PsxTypeStickDpad,	Func_ReturnFromSettingsFrame }, // PSX Controller Type: Stick D-pad
};

struct TextBoxInfo
{
	menu::TextBox	*textBox;
	char*			textBoxString;
	float			x;
	float			y;
	float			scale;
	bool			centered;
} FRAME_TEXTBOXES[NUM_FRAME_TEXTBOXES] =
{ //	textBox	textBoxString		x		y		scale	centered
	//TextBoxes for General Tab (starts at textBox[0])
	{	NULL,	FRAME_STRINGS[5],	105.0,	128.0,	 1.0,	true }, // CPU Core: Pure Interp/Dynarec
	{	NULL,	FRAME_STRINGS[6],	150.0,	188.0,	 1.0,	true }, // Bios: HLE/SD/USB/DVD
	{	NULL,	FRAME_STRINGS[7],	150.0,	248.0,	 1.0,	true }, // Boot Thru Bios: Yes/No
	{	NULL,	FRAME_STRINGS[9],	150.0,	428.0,	 1.0,	true }, // Save settings: SD/USB
	//TextBoxes for Video Tab (starts at textBox[4])
	{	NULL,	FRAME_STRINGS[18],	150.0,	128.0,	 1.0,	true }, // Show FPS: On/Off
	{	NULL,	FRAME_STRINGS[19],	150.0,	188.0,	 1.0,	true }, // Limit FPS: Auto/Off
	{	NULL,	FRAME_STRINGS[20],	150.0,	248.0,	 1.0,	true }, // Frame Skip: On/Off
	{	NULL,	FRAME_STRINGS[21],	150.0,	308.0,	 1.0,	true }, // ScreenMode: 4x3/16x9/Force16x9/Interlaced/240p
	{	NULL,	FRAME_STRINGS[22],	150.0,	368.0,	 1.0,	true }, // Dithering: None/Game Dependent/Always
	{	NULL,	FRAME_STRINGS[23],	150.0,	368.0,	 1.0,	true }, // Filters
	//TextBoxes for Input Tab (starts at textBox[10])
	{	NULL,	FRAME_STRINGS[36],	150.0,	188.0,	 1.0,	true }, // PSX Controller Type: Standard/Analog
	{	NULL,	FRAME_STRINGS[37],	150.0,	308.0,	 1.0,	true }, // Enable Rumble: Yes/No
	{	NULL,	FRAME_STRINGS[40],	150.0,	368.0,	 1.0,	true }, // Save Button Configs: SD/USB
	{	NULL,	FRAME_STRINGS[41],	150.0,	428.0,	 1.0,	true }, // Auto Load Slot: Default/1/2/3/4
	//TextBoxes for Audio Tab (starts at textBox[14]) ..was[17]
	{	NULL,	FRAME_STRINGS[87],	150.0,	128.0,	 1.0,	true }, // Enable: Audio/XA/CDDA
	{	NULL,	FRAME_STRINGS[44],	210.0,	188.0,	 1.0,	true }, // Disable XA Audio: Yes/No
	{	NULL,	FRAME_STRINGS[45],	210.0,	248.0,	 1.0,	true }, // Disable CDDA Audio: Yes/No
	{	NULL,	FRAME_STRINGS[46],	150.0,	198.0,	 1.0,	true }, // Interpolation: Simple/Gaussian
	//TextBoxes for Saves Tab (starts at textBox[18]) ..was[23]
	{	NULL,	FRAME_STRINGS[51],	150.0,	128.0,	 1.0,	true }, // Memcard Save Device: SD/USB/CardA/CardB
	{	NULL,	FRAME_STRINGS[52],	150.0,	268.0,	 1.0,	true }, // Auto Save Memcards: Yes/No
	{	NULL,	FRAME_STRINGS[53],	150.0,	338.0,	 1.0,	true }, // Save State Device: SD/USB
	{	NULL,	FRAME_STRINGS[56],	150.0,	308.0,	 1.0,	true }, // Select language: En, Chs, ......
	{	NULL,	FRAME_STRINGS[63],	150.0,	368.0,	 1.0,	true }, // Fast load
	{	NULL,	FRAME_STRINGS[76],	150.0,	198.0,	 1.0,	true }, // Memcard Type: Off/Shared/Game
    //TextBoxes for Saves Tab (starts at textBox[24]) ..was[24]
	{	NULL,	GPU_PLUGIN_STRINGS[0],	110.0,	188.0,	 1.0,	true }, // GPU Plugin: Soft Fast/Soft Timed/OpenGX
	//TextBox for the Audio tab, appended (textBox[25])
	{	NULL,	FRAME_STRINGS[79],	150.0,	268.0,	 1.0,	true }, // DSP Sound: Yes/No
	//TextBox for the Sync group (textBox[26])
	{	NULL,	FRAME_STRINGS[80],	150.0,	338.0,	 1.0,	true }, // Sync: Off/Tempo/Rate
	//Which card each Memcard Type button belongs to (textBox[27] and [28])
	{	NULL,	FRAME_STRINGS[91],	285.0,	198.0,	 1.0,	true }, // "1", left of the first button
	{	NULL,	FRAME_STRINGS[95],	420.0,	198.0,	 1.0,	true }, // "2", left of the second
	//Input tab: the light gun row's label (textBox[29])
	{	NULL,	FRAME_STRINGS[97],	150.0,	248.0,	 1.0,	true }, // Light Gun: Off/GunCon/Justifier/Mouse
};

/*  The General tab, on the menu engine (Docs/MENU_FRAMEWORK_PLAN.md, step 2)  */

/* The tab's rows, top to bottom, as the engine lays them out. Each slot is the FRAME_BUTTONS
 * entry it is drawn with (the table keeps the boxes, the labels and the focus of the other tabs'
 * buttons too), and each label is a FRAME_TEXTBOXES entry. The numbers are the table's: a row's
 * y is 100 + 60 n, its labels are centred at x 150 on the row's middle (page labelCx/labelDy). */
namespace eng = menu::engine;

#define GEN_ROWS_N 6

static const int GEN_BUTTON[GEN_ROWS_N][4] = {
	{ 73, 75, -1, -1 },		/* Advanced, Memory */
	{  7,  8,  9, 10 },		/* Select BIOS: HLE, SD, USB, DVD */
	{ 11, 12, 13, -1 },		/* Boot Through BIOS: Yes, No, and Execute BIOS (an action) */
	{ 54, -1, -1, -1 },		/* Select Language */
	{ 55, 56, -1, -1 },		/* Fast Load: Yes, No */
	{ 14, 15, 62, -1 },		/* Save Settings: SD, USB, This Game */
};
static const int GEN_COUNT[GEN_ROWS_N]    = { 2, 4, 3, 1, 2, 3 };
static const int GEN_LABEL[GEN_ROWS_N]    = { -1, 1, 2, 21, 22, 3 };	/* FRAME_TEXTBOXES, -1: none */
static const eng::RowKind GEN_KIND[GEN_ROWS_N] = {
	eng::ROW_BUTTONS, eng::ROW_CHOICE, eng::ROW_CHOICE, eng::ROW_BUTTONS, eng::ROW_CHOICE, eng::ROW_BUTTONS
};
static const float GEN_WIDTH[GEN_ROWS_N][4] = {
	{ 130, 130,   0,   0 }, {  70,  55,  70,  70 }, { 75, 75, 171, 0 },
	{  90,   0,   0,   0 }, {  75,  75,   0,   0 }, { 55, 70, 140, 0 },
};
static const float GEN_X0[GEN_ROWS_N]    = { 0.0f, 295.0f, 295.0f, 295.0f, 295.0f, 253.0f };	/* 0: centred */
static const float GEN_GAP[GEN_ROWS_N]   = { 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };		/* 0: TAB_GAP */
static const int   GEN_PLAIN[GEN_ROWS_N] = { 0, 0, 1, 0, 0, 0 };

/* A row's text: set from the buttons when the tab is shown (Select Language changes with the
 * setting). The rows, actions and tabs are the engine's view of them. */
static const char *GEN_TEXT[GEN_ROWS_N][4];
static eng::ActionButton GEN_ACTIONS[GEN_ROWS_N][4];
static eng::Row GEN_ROWS[GEN_ROWS_N];
static eng::Tab GEN_TABS[NUM_TAB_BUTTONS];
static eng::Page GEN_PAGE;
static eng::Layout GEN_LAYOUT;

extern "C" void SysPrintf(const char *fmt, ...);
extern char fastLoad;

/* The engine's text widths: IplFont, the font every label is drawn with (OptionsFrame.cpp). */
static int genWidth(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringWidth((char*)s, scale);
}
static int genHeight(const char *s, float scale, void *)
{
	return menu::IplFont::getInstance().getStringHeight((char*)s, scale);
}
static const eng::Metrics GEN_METRICS = { genWidth, genHeight, NULL };

/* The FRAME_BUTTONS entry a control of the General tab is drawn with. */
static menu::Button *genButton(const eng::Control &c)
{
	return FRAME_BUTTONS[c.role == eng::ROLE_TAB ? c.slot : GEN_BUTTON[c.row][c.slot]].button;
}

/* A button at the engine's rectangle, shown. Its lit state and whether it is active are set by
 * the caller. The pointer's box follows it, as OptionsFrame's placeButton does. */
static void genPlace(SettingsFrame *frame, menu::Button *b, const eng::Control &c)
{
	b->setBounds(c.rect.x, c.rect.y, c.rect.w, c.rect.h);
	b->setVisible(true);
	menu::Cursor::getInstance().moveComponent(frame, b,
		c.rect.x, c.rect.x + c.rect.w, c.rect.y, c.rect.y + c.rect.h);
}

/* Lays the General tab out on the engine and places its buttons and labels; links the focus.
 * Everything here is done when the tab is shown, never per frame. The other tabs are unchanged. */
static void layoutGeneral(SettingsFrame *frame)
{
	FRAME_BUTTONS[54].buttonString = LANG_STRINGS[(int)lang];

	for (int r = 0; r < GEN_ROWS_N; r++)
		for (int c = 0; c < GEN_COUNT[r]; c++) {
			GEN_TEXT[r][c] = FRAME_BUTTONS[GEN_BUTTON[r][c]].buttonString;
			GEN_ACTIONS[r][c].text = GEN_TEXT[r][c];
			GEN_ACTIONS[r][c].width = GEN_WIDTH[r][c];
		}
	for (int r = 0; r < GEN_ROWS_N; r++) {
		eng::Row &row = GEN_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = GEN_KIND[r];
		row.label   = GEN_LABEL[r] >= 0 ? FRAME_TEXTBOXES[GEN_LABEL[r]].textBoxString : NULL;
		row.names   = GEN_TEXT[r];
		row.count   = GEN_COUNT[r];
		row.actions = GEN_ACTIONS[r];
		row.x0      = GEN_X0[r];
		row.gap     = GEN_GAP[r];
		row.plain   = GEN_PLAIN[r];
		row.widths  = row.kind == eng::ROW_CHOICE ? GEN_WIDTH[r] : NULL;
	}
	/* The values the rows show: the BIOS device, Boot Through BIOS, Fast Load. */
	GEN_ROWS[1].value = biosDevice;
	GEN_ROWS[2].value = LoadCdBios == BOOTTHRUBIOS_YES ? 0 : 1;
	GEN_ROWS[4].value = fastLoad == 1 ? 0 : 1;

	for (int t = 0; t < NUM_TAB_BUTTONS; t++) {
		GEN_TABS[t].text  = FRAME_BUTTONS[t].buttonString;
		GEN_TABS[t].width = FRAME_BUTTONS[t].width;
	}
	GEN_PAGE.title   = NULL;
	GEN_PAGE.tabs    = GEN_TABS;
	GEN_PAGE.nTabs   = NUM_TAB_BUTTONS;
	GEN_PAGE.curTab  = SettingsFrame::SUBMENU_GENERAL;
	GEN_PAGE.rows    = GEN_ROWS;
	GEN_PAGE.nRows   = GEN_ROWS_N;
	GEN_PAGE.help    = NULL;
	GEN_PAGE.nHelp   = 0;
	GEN_PAGE.logo    = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_SETTINGS is in LOGO_BOTTOM_FRAMES */
	GEN_PAGE.focus   = eng::FOCUS_ROWS;
	GEN_PAGE.labelCx = 150.0f;	/* the Settings labels: TextBox centred at x 150 */
	GEN_PAGE.labelDy = 28.0f;	/* on the middle of a 56-high row */

	if (eng::layoutPage(GEN_PAGE, GEN_METRICS, GEN_LAYOUT) < 0) {
		/* The tables are fixed, so this is a programming error: the tab is left as it was. */
		SysPrintf("SettingsFrame: the General tab does not fit the menu engine\n");
		return;
	}
	eng::linkFocus(GEN_PAGE, GEN_LAYOUT);

	for (int i = 0; i < GEN_LAYOUT.n; i++) {
		const eng::Control &c = GEN_LAYOUT.c[i];
		switch (c.role) {
		case eng::ROLE_TAB:
			genPlace(frame, genButton(c), c);
			genButton(c)->setActive(true);
			genButton(c)->setSelected(c.selected);
			break;
		case eng::ROLE_LABEL:
		case eng::ROLE_RLABEL: {
			menu::TextBox *box = FRAME_TEXTBOXES[GEN_LABEL[c.row]].textBox;
			box->setPosition(c.ax, c.ay);
			box->setVisible(true);
			break;
		}
		case eng::ROLE_BUTTON:
		case eng::ROLE_CHOICE:
			genPlace(frame, genButton(c), c);
			/* This Game is shown always, and active only with a game loaded (as before). */
			genButton(c)->setActive(GEN_BUTTON[c.row][c.slot] == 62 ? (hasLoadedISO ? true : false) : true);
			genButton(c)->setSelected(c.selected);
			break;
		default:
			break;
		}
	}

	/* The focus links, on the buttons. A link of -1 is no link. */
	{
		static const int DIRS[eng::DIR_COUNT] = {
			menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
			menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
		};
		for (int i = 0; i < GEN_LAYOUT.n; i++) {
			const eng::Control &c = GEN_LAYOUT.c[i];
			if (!c.focusable)
				continue;
			for (int d = 0; d < eng::DIR_COUNT; d++) {
				const int to = c.link[d];
				genButton(c)->setNextFocus(DIRS[d], to >= 0 ? genButton(GEN_LAYOUT.c[to]) : NULL);
			}
		}
	}
}

/*  The Video tab, on the menu engine (Docs/MENU_FRAMEWORK_PLAN.md, step 2, as the General tab)  */

/* The tab's rows, top to bottom, as the engine lays them out. Each slot is the FRAME_BUTTONS
 * entry it is drawn with, and each label a FRAME_TEXTBOXES entry. A row's y is 100 + 60 n and its
 * labels are centred at x 150 on the row's middle (page labelCx/labelDy). Three rows hold a
 * toggle beside a choice or a cycle, which a choice row cannot show (it lights one button), so
 * their buttons are action buttons with a lit flag: FPS Limit with Force NTSC, Screen Mode with
 * Interlaced and 240p, Filters with Trap and Deflicker. The table's Dithering buttons (25..27) and
 * label (8) are not on this tab any more and are not shown. */
#define VID_ROWS_N 6

static const int VID_BUTTON[VID_ROWS_N][4] = {
	{ 16, 17, -1, -1 },		/* Show FPS: On, Off */
	{ 18, 19, 63, -1 },		/* FPS Limit: Auto, Off; Force NTSC (a toggle) */
	{ 20, 21, -1, -1 },		/* Frame Skip: On, Off */
	{ 22, 23, 57, -1 },		/* Screen Mode (a cycle); Interlaced, 240p (toggles) */
	{ 28, 29, 24, -1 },		/* Bilinear (a cycle); Trap, Deflicker (toggles) */
	{ 78, -1, -1, -1 },		/* Advanced Graphics (an action) */
};
static const int VID_COUNT[VID_ROWS_N]    = { 2, 3, 2, 3, 3, 1 };
static const int VID_LABEL[VID_ROWS_N]    = { 4, 5, 6, 7, 9, -1 };	/* FRAME_TEXTBOXES, -1: none */
static const eng::RowKind VID_KIND[VID_ROWS_N] = {
	eng::ROW_CHOICE, eng::ROW_BUTTONS, eng::ROW_CHOICE, eng::ROW_BUTTONS, eng::ROW_BUTTONS, eng::ROW_BUTTONS
};
static const float VID_WIDTH[VID_ROWS_N][4] = {
	{  75,  75,   0,   0 }, {  75,  75, 160,   0 }, {  75,  75,   0,   0 },
	{ 130, 125,  66,   0 }, {  95,  75, 110,   0 }, { 150,   0,   0,   0 },
};
static const float VID_X0[VID_ROWS_N] = { 295.0f, 295.0f, 295.0f, 295.0f, 238.0f, 245.0f };	/* 0: centred */

/* A row's text and actions are set from the buttons when the tab is shown, as the General tab's are. */
static const char *VID_TEXT[VID_ROWS_N][4];
static eng::ActionButton VID_ACTIONS[VID_ROWS_N][4];
static eng::Row VID_ROWS[VID_ROWS_N];
static eng::Tab VID_TABS[NUM_TAB_BUTTONS];
static eng::Page VID_PAGE;
static eng::Layout VID_LAYOUT;

/* The FRAME_BUTTONS entry a control of the Video tab is drawn with. */
static menu::Button *vidButton(const eng::Control &c)
{
	return FRAME_BUTTONS[c.role == eng::ROLE_TAB ? c.slot : VID_BUTTON[c.row][c.slot]].button;
}

/* Lays the Video tab out on the engine and places its buttons and labels; links the focus.
 * Everything here is done when the tab is shown, never per frame. The other tabs are unchanged. */
static void layoutVideo(SettingsFrame *frame)
{
	/* The two buttons whose text depends on a setting. */
	FRAME_BUTTONS[22].buttonString = FRAME_STRINGS[27 + screenMode];
	FRAME_BUTTONS[28].buttonString = TEXTURE_FILTER_STRINGS[(int)bilinearFilter];

	for (int r = 0; r < VID_ROWS_N; r++)
		for (int c = 0; c < VID_COUNT[r]; c++) {
			VID_TEXT[r][c] = FRAME_BUTTONS[VID_BUTTON[r][c]].buttonString;
			VID_ACTIONS[r][c].text = VID_TEXT[r][c];
			VID_ACTIONS[r][c].width = VID_WIDTH[r][c];
		}
	for (int r = 0; r < VID_ROWS_N; r++) {
		eng::Row &row = VID_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = VID_KIND[r];
		row.label   = VID_LABEL[r] >= 0 ? FRAME_TEXTBOXES[VID_LABEL[r]].textBoxString : NULL;
		row.names   = VID_TEXT[r];
		row.count   = VID_COUNT[r];
		row.actions = VID_ACTIONS[r];
		row.x0      = VID_X0[r];
		row.widths  = row.kind == eng::ROW_CHOICE ? VID_WIDTH[r] : NULL;
	}
	/* The lit buttons: a choice's value, or each toggle's own setting. */
	VID_ROWS[0].value = showFPSonScreen == FPS_SHOW ? 0 : 1;
	VID_ROWS[2].value = frameSkip == FRAMESKIP_ENABLE ? 0 : 1;
	VID_ACTIONS[1][0].lit = frameLimit[1] == FRAMELIMIT_AUTO;
	VID_ACTIONS[1][1].lit = frameLimit[1] != FRAMELIMIT_AUTO;
	VID_ACTIONS[1][2].lit = forceNTSC == FORCENTSC_ENABLE;
	VID_ACTIONS[3][0].lit = false;
	VID_ACTIONS[3][1].lit = interlacedMode == INTERLACED_ENABLE;
	VID_ACTIONS[3][2].lit = originalMode == ORIGINALMODE_ENABLE;
	VID_ACTIONS[4][0].lit = false;
	VID_ACTIONS[4][1].lit = trapFilter == TRAPFILTER_ENABLE;
	VID_ACTIONS[4][2].lit = deflickerFilter == DEFLICKER_ENABLE;
	VID_ACTIONS[5][0].lit = false;

	for (int t = 0; t < NUM_TAB_BUTTONS; t++) {
		VID_TABS[t].text  = FRAME_BUTTONS[t].buttonString;
		VID_TABS[t].width = FRAME_BUTTONS[t].width;
	}
	VID_PAGE.title   = NULL;
	VID_PAGE.tabs    = VID_TABS;
	VID_PAGE.nTabs   = NUM_TAB_BUTTONS;
	VID_PAGE.curTab  = SettingsFrame::SUBMENU_VIDEO;
	VID_PAGE.rows    = VID_ROWS;
	VID_PAGE.nRows   = VID_ROWS_N;
	VID_PAGE.help    = NULL;
	VID_PAGE.nHelp   = 0;
	VID_PAGE.logo    = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_SETTINGS is in LOGO_BOTTOM_FRAMES */
	VID_PAGE.focus   = eng::FOCUS_ROWS;
	VID_PAGE.labelCx = 150.0f;	/* the Settings labels: TextBox centred at x 150 */
	VID_PAGE.labelDy = 28.0f;	/* on the middle of a 56-high row */

	if (eng::layoutPage(VID_PAGE, GEN_METRICS, VID_LAYOUT) < 0) {
		/* The tables are fixed, so this is a programming error: the tab is left as it was. */
		SysPrintf("SettingsFrame: the Video tab does not fit the menu engine\n");
		return;
	}
	eng::linkFocus(VID_PAGE, VID_LAYOUT);

	for (int i = 0; i < VID_LAYOUT.n; i++) {
		const eng::Control &c = VID_LAYOUT.c[i];
		switch (c.role) {
		case eng::ROLE_TAB:
		case eng::ROLE_BUTTON:
		case eng::ROLE_CHOICE:
			genPlace(frame, vidButton(c), c);
			vidButton(c)->setActive(true);
			vidButton(c)->setSelected(c.selected);
			break;
		case eng::ROLE_LABEL:
		case eng::ROLE_RLABEL: {
			menu::TextBox *box = FRAME_TEXTBOXES[VID_LABEL[c.row]].textBox;
			box->setPosition(c.ax, c.ay);
			box->setVisible(true);
			break;
		}
		default:
			break;
		}
	}

	/* The focus links, on the buttons. A link of -1 is no link. */
	{
		static const int DIRS[eng::DIR_COUNT] = {
			menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
			menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
		};
		for (int i = 0; i < VID_LAYOUT.n; i++) {
			const eng::Control &c = VID_LAYOUT.c[i];
			if (!c.focusable)
				continue;
			for (int d = 0; d < eng::DIR_COUNT; d++) {
				const int to = c.link[d];
				vidButton(c)->setNextFocus(DIRS[d], to >= 0 ? vidButton(VID_LAYOUT.c[to]) : NULL);
			}
		}
	}
}

/*  The Input tab, on the menu engine (Docs/MENU_FRAMEWORK_PLAN.md, step 2, as the General and Video tabs)  */

/* The tab's rows, top to bottom, as the engine lays them out on the Settings grid (a row's y is
 * 100 + 60 n). Each slot is the FRAME_BUTTONS entry it is drawn with and each label a FRAME_TEXTBOXES
 * entry (labelCx 150, labelDy 28). Three rows hold a choice or a single toggle, drawn as the engine
 * draws a row of buttons with a lit flag: the Light Gun cycle is an action with a lit flag. */
#define INP_ROWS_N 6

static const int INP_BUTTON[INP_ROWS_N][4] = {
	{ 30, 31, -1, -1 },		/* Configure Input Assignment, Configure Button Mappings */
	{ 32, 33, 79, -1 },		/* PSX Controller Type: Standard, Analog, Stick D-pad */
	{ 59, -1, -1, -1 },		/* Light Gun (a cycle, lit when a gun is on) */
	{ 34, 35, -1, -1 },		/* Enable Rumble: Yes, No */
	{ 36, 37, -1, -1 },		/* Save Button Configs: SD, USB */
	{ 38, -1, -1, -1 },		/* Auto Load Slot (an action) */
};
static const int INP_COUNT[INP_ROWS_N] = { 2, 3, 1, 2, 2, 1 };
static const int INP_LABEL[INP_ROWS_N] = { -1, 10, 29, 11, 12, 13 };	/* FRAME_TEXTBOXES, -1: none */
static const eng::RowKind INP_KIND[INP_ROWS_N] = {
	eng::ROW_BUTTONS, eng::ROW_CHOICE, eng::ROW_BUTTONS, eng::ROW_CHOICE, eng::ROW_BUTTONS, eng::ROW_BUTTONS
};
static const float INP_WIDTH[INP_ROWS_N][4] = {
	{ 220, 235,   0,   0 }, { 105, 100, 140,   0 }, { 135,   0,   0,   0 },
	{  75,  75,   0,   0 }, {  55,  70,   0,   0 }, { 135,   0,   0,   0 },
};
static const float INP_X0[INP_ROWS_N]  = { 85.0f, 271.0f, 295.0f, 295.0f, 295.0f, 295.0f };	/* 0: centred */
static const float INP_GAP[INP_ROWS_N] = { 15.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };		/* 0: TAB_GAP */

/* A row's text and actions are set from the buttons when the tab is shown, as the other tabs' are. */
static const char *INP_TEXT[INP_ROWS_N][4];
static eng::ActionButton INP_ACTIONS[INP_ROWS_N][4];
static eng::Row INP_ROWS[INP_ROWS_N];
static eng::Tab INP_TABS[NUM_TAB_BUTTONS];
static eng::Page INP_PAGE;
static eng::Layout INP_LAYOUT;

/* The FRAME_BUTTONS entry a control of the Input tab is drawn with. */
static menu::Button *inpButton(const eng::Control &c)
{
	return FRAME_BUTTONS[c.role == eng::ROLE_TAB ? c.slot : INP_BUTTON[c.row][c.slot]].button;
}

/* Lays the Input tab out on the engine and places its buttons and labels; links the focus.
 * Everything here is done when the tab is shown, never per frame. */
static void layoutInput(SettingsFrame *frame)
{
	/* The Light Gun button's text follows its setting. The Auto Load Slot's text is FRAME_STRINGS[42],
	 * which the caller sets (Func_SetButtonLoad) before this runs. */
	FRAME_BUTTONS[59].buttonString = FRAME_STRINGS[70 + lightGun];

	for (int r = 0; r < INP_ROWS_N; r++)
		for (int c = 0; c < INP_COUNT[r]; c++) {
			INP_TEXT[r][c] = FRAME_BUTTONS[INP_BUTTON[r][c]].buttonString;
			INP_ACTIONS[r][c].text = INP_TEXT[r][c];
			INP_ACTIONS[r][c].width = INP_WIDTH[r][c];
		}
	for (int r = 0; r < INP_ROWS_N; r++) {
		eng::Row &row = INP_ROWS[r];
		memset(&row, 0, sizeof row);
		row.kind    = INP_KIND[r];
		row.label   = INP_LABEL[r] >= 0 ? FRAME_TEXTBOXES[INP_LABEL[r]].textBoxString : NULL;
		row.names   = INP_TEXT[r];
		row.count   = INP_COUNT[r];
		row.actions = INP_ACTIONS[r];
		row.x0      = INP_X0[r];
		row.gap     = INP_GAP[r];
		row.widths  = row.kind == eng::ROW_CHOICE ? INP_WIDTH[r] : NULL;
	}
	/* The lit buttons: the controller type, the rumble setting, and the Light Gun when a gun is on. */
	INP_ROWS[1].value = controllerType;
	INP_ROWS[3].value = rumbleEnabled == RUMBLE_ENABLE ? 0 : 1;
	INP_ACTIONS[2][0].lit = lightGun != LIGHTGUN_DISABLE;

	for (int t = 0; t < NUM_TAB_BUTTONS; t++) {
		INP_TABS[t].text  = FRAME_BUTTONS[t].buttonString;
		INP_TABS[t].width = FRAME_BUTTONS[t].width;
	}
	INP_PAGE.title   = NULL;
	INP_PAGE.tabs    = INP_TABS;
	INP_PAGE.nTabs   = NUM_TAB_BUTTONS;
	INP_PAGE.curTab  = SettingsFrame::SUBMENU_INPUT;
	INP_PAGE.rows    = INP_ROWS;
	INP_PAGE.nRows   = INP_ROWS_N;
	INP_PAGE.help    = NULL;
	INP_PAGE.nHelp   = 0;
	INP_PAGE.logo    = eng::LOGO_SPOT_BOTTOM;	/* MenuContext: FRAME_SETTINGS is in LOGO_BOTTOM_FRAMES */
	INP_PAGE.focus   = eng::FOCUS_ROWS;
	INP_PAGE.labelCx = 150.0f;	/* the Settings labels: TextBox centred at x 150 */
	INP_PAGE.labelDy = 28.0f;	/* on the middle of a 56-high row */

	if (eng::layoutPage(INP_PAGE, GEN_METRICS, INP_LAYOUT) < 0) {
		/* The tables are fixed, so this is a programming error: the tab is left as it was. */
		SysPrintf("SettingsFrame: the Input tab does not fit the menu engine\n");
		return;
	}
	eng::linkFocus(INP_PAGE, INP_LAYOUT);

	for (int i = 0; i < INP_LAYOUT.n; i++) {
		const eng::Control &c = INP_LAYOUT.c[i];
		switch (c.role) {
		case eng::ROLE_TAB:
		case eng::ROLE_BUTTON:
		case eng::ROLE_CHOICE:
			genPlace(frame, inpButton(c), c);
			inpButton(c)->setActive(true);
			inpButton(c)->setSelected(c.selected);
			break;
		case eng::ROLE_LABEL:
		case eng::ROLE_RLABEL: {
			menu::TextBox *box = FRAME_TEXTBOXES[INP_LABEL[c.row]].textBox;
			box->setPosition(c.ax, c.ay);
			box->setVisible(true);
			break;
		}
		default:
			break;
		}
	}

	/* The focus links, on the buttons. A link of -1 is no link. */
	{
		static const int DIRS[eng::DIR_COUNT] = {
			menu::Focus::DIRECTION_UP, menu::Focus::DIRECTION_DOWN,
			menu::Focus::DIRECTION_LEFT, menu::Focus::DIRECTION_RIGHT
		};
		for (int i = 0; i < INP_LAYOUT.n; i++) {
			const eng::Control &c = INP_LAYOUT.c[i];
			if (!c.focusable)
				continue;
			for (int d = 0; d < eng::DIR_COUNT; d++) {
				const int to = c.link[d];
				inpButton(c)->setNextFocus(DIRS[d], to >= 0 ? inpButton(INP_LAYOUT.c[to]) : NULL);
			}
		}
	}
}

SettingsFrame::SettingsFrame()
		: activeSubmenu(SUBMENU_GENERAL)
{
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
		FRAME_BUTTONS[i].button = new menu::Button(FRAME_BUTTONS[i].buttonStyle, &FRAME_BUTTONS[i].buttonString,
										FRAME_BUTTONS[i].x, FRAME_BUTTONS[i].y,
										FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].height);

	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		if (FRAME_BUTTONS[i].focusUp != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, FRAME_BUTTONS[FRAME_BUTTONS[i].focusUp].button);
		if (FRAME_BUTTONS[i].focusDown != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_DOWN, FRAME_BUTTONS[FRAME_BUTTONS[i].focusDown].button);
		if (FRAME_BUTTONS[i].focusLeft != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_LEFT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusLeft].button);
		if (FRAME_BUTTONS[i].focusRight != -1) FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_RIGHT, FRAME_BUTTONS[FRAME_BUTTONS[i].focusRight].button);
		FRAME_BUTTONS[i].button->setActive(true);
		if (FRAME_BUTTONS[i].clickedFunc) FRAME_BUTTONS[i].button->setClicked(FRAME_BUTTONS[i].clickedFunc);
		if (FRAME_BUTTONS[i].returnFunc) FRAME_BUTTONS[i].button->setReturn(FRAME_BUTTONS[i].returnFunc);
		add(FRAME_BUTTONS[i].button);
		menu::Cursor::getInstance().addComponent(this, FRAME_BUTTONS[i].button, FRAME_BUTTONS[i].x,
												FRAME_BUTTONS[i].x+FRAME_BUTTONS[i].width, FRAME_BUTTONS[i].y,
												FRAME_BUTTONS[i].y+FRAME_BUTTONS[i].height);
	}

	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
	{
		FRAME_TEXTBOXES[i].textBox = new menu::TextBox(&FRAME_TEXTBOXES[i].textBoxString,
										FRAME_TEXTBOXES[i].x, FRAME_TEXTBOXES[i].y,
										FRAME_TEXTBOXES[i].scale, FRAME_TEXTBOXES[i].centered);
		add(FRAME_TEXTBOXES[i].textBox);
	}

	setDefaultFocus(FRAME_BUTTONS[0].button);
	setBackFunc(Func_ReturnFromSettingsFrame);
	setEnabled(true);
	activateSubmenu(SUBMENU_GENERAL);
}

SettingsFrame::~SettingsFrame()
{
	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
		delete FRAME_TEXTBOXES[i].textBox;
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		menu::Cursor::getInstance().removeComponent(this, FRAME_BUTTONS[i].button);
		delete FRAME_BUTTONS[i].button;
	}
}

extern char fastLoad; // variable for see if game has reduce load time

void SettingsFrame::activateSubmenu(int submenu)
{
	activeSubmenu = submenu;

	//All buttons: hide; unselect
	for (int i = 0; i < NUM_FRAME_BUTTONS; i++)
	{
		FRAME_BUTTONS[i].button->setVisible(false);
		FRAME_BUTTONS[i].button->setSelected(false);
		FRAME_BUTTONS[i].button->setActive(false);
	}
	//All textBoxes: hide
	for (int i = 0; i < NUM_FRAME_TEXTBOXES; i++)
	{
		FRAME_TEXTBOXES[i].textBox->setVisible(false);
	}
	switch (activeSubmenu)	//Tab buttons: set visible; set focus up/down; set selected
	{						//Config buttons: set visible; set selected
		case SUBMENU_GENERAL:
			/* The General tab is laid out on the menu engine (layoutGeneral, above): its rows,
			 * labels and focus. The tab row is the same five buttons on every tab. */
			setDefaultFocus(FRAME_BUTTONS[0].button);
			layoutGeneral(this);
			break;
		case SUBMENU_VIDEO:
			/* The Video tab is laid out on the menu engine (layoutVideo, above): its rows,
			 * labels and focus. The tab row is the same five buttons on every tab. */
			setDefaultFocus(FRAME_BUTTONS[1].button);
			layoutVideo(this);
			break;
		case SUBMENU_INPUT:
			/* The Input tab is laid out on the menu engine (layoutInput, above): its rows, labels and
			 * focus. The tab row is the same five buttons on every tab. */
			Func_SetButtonLoad();
			setDefaultFocus(FRAME_BUTTONS[2].button);
			layoutInput(this);
			break;
		case SUBMENU_AUDIO:
			setDefaultFocus(FRAME_BUTTONS[3].button);
			for (int i = 0; i < NUM_TAB_BUTTONS; i++)
			{
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_DOWN, FRAME_BUTTONS[39].button);
				FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, FRAME_BUTTONS[72].button);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			/* textBox[15] and [16] were the XA and CDDA labels; the Enable row carries its
			 * own button labels now, so only [14] "Enable" and [17] "Interpolation" show. */
			FRAME_TEXTBOXES[14].textBox->setVisible(true);
			FRAME_TEXTBOXES[17].textBox->setVisible(true);
			FRAME_BUTTONS[3].button->setSelected(true);
			/* Each toggle is lit when that source is ON, so the row reads as what you get.
			 * The settings file still keeps the historical "Disable" keys; see SETTINGS.md. */
			FRAME_BUTTONS[39].button->setSelected(audioEnabled == AUDIO_ENABLE);
			FRAME_BUTTONS[40].button->setSelected(Config.Xa != XA_DISABLE);
			FRAME_BUTTONS[41].button->setSelected(Config.Cdda != CDDA_DISABLE);
			FRAME_BUTTONS[45].buttonString = FRAME_STRINGS[46 + spuInterpolation];
			if (soundHwAccel == SOUND_HW_ACCEL_ON)	FRAME_BUTTONS[67].button->setSelected(true);
			else									FRAME_BUTTONS[68].button->setSelected(true);
			/* 39..41 are the Enable toggles, 45 is Interpolation; 42..44 are spare slots. */
			for (int i = 39; i <= 41; i++)
			{
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			FRAME_BUTTONS[45].button->setVisible(true);
			FRAME_BUTTONS[45].button->setActive(true);
			/* DSP Sound and Advanced live at the end of the array (see the button table) */
			for (int i = 67; i <= 68; i++)
			{
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			FRAME_TEXTBOXES[25].textBox->setVisible(true);
			/* Sync group (buttons 69..71, textBox 26): exactly one selected, from the two file keys;
			 * button 72 is Advanced, next to it */
			for (int i = 69; i <= 72; i++)
			{
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			FRAME_BUTTONS[69 + (soundRateControl ? 2 : (soundTempo ? 1 : 0))].button->setSelected(true);
			FRAME_TEXTBOXES[26].textBox->setVisible(true);
			break;
		case SUBMENU_SAVES:
			setDefaultFocus(FRAME_BUTTONS[4].button);
			for (int i = 0; i < NUM_TAB_BUTTONS; i++)
			{
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_DOWN, FRAME_BUTTONS[46].button);
				FRAME_BUTTONS[i].button->setNextFocus(menu::Focus::DIRECTION_UP, FRAME_BUTTONS[52].button);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			for (int i = 18; i < 21; i++)
				FRAME_TEXTBOXES[i].textBox->setVisible(true);
			FRAME_TEXTBOXES[23].textBox->setVisible(true);   /* "Memcard Type" */
			FRAME_BUTTONS[4].button->setSelected(true);
			FRAME_BUTTONS[46+nativeSaveDevice].button->setSelected(true);
			if (autoSave == AUTOSAVE_ENABLE)	FRAME_BUTTONS[50].button->setSelected(true);
			else								FRAME_BUTTONS[51].button->setSelected(true);
			if (saveStateDevice == SAVESTATEDEVICE_SD)	FRAME_BUTTONS[52].button->setSelected(true);
			else										FRAME_BUTTONS[53].button->setSelected(true);
			/* One button per card: Off, or the file it lives in. Buttons 60 and 61 were
			 * the old Enable Memcard pair and are no longer shown. */
			FRAME_TEXTBOXES[27].textBox->setVisible(true);
			FRAME_TEXTBOXES[28].textBox->setVisible(true);
			for (int i = 76; i <= 77; i++)
			{
				FRAME_BUTTONS[i].buttonString = FRAME_STRINGS[92 + cardType(i - 76)];
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			for (int i = 46; i < NUM_FRAME_BUTTONS; i++)
			{
			    if (i >= 54) {
                    // language info
                    continue;
			    }
				FRAME_BUTTONS[i].button->setVisible(true);
				FRAME_BUTTONS[i].button->setActive(true);
			}
			break;
	}
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

