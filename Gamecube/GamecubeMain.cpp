/*  Pcsx - Pc Psx Emulator
 *  Copyright (C) 1999-2002  Pcsx Team
 *  Copyright (C) 2009-2010  WiiSX Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#include <time.h>
#include <fat.h>
#include <aesndlib.h>
#include <sys/iosupport.h>

#ifdef DEBUGON
# include <debug.h>
#endif

#include "lab_net.h"
#include <network.h>
extern "C" void __exception_setreload(int t);
#include "../psxcommon.h"
#include "wiiSXconfig.h"
#include "config_parse.h"   /* the two scans in the settings parser */
#include "menu/MenuContext.h"
#include "libgui/IPLFont.h"
#include "libgui/MessageBox.h"

extern char * GetGameBios(char * biosPath, char * fileName, int isoFileNameLen);
extern char* filenameFromAbsPath(char* absPath);
extern u32 __di_check_ahbprot(void);
extern unsigned int cdrIsoMultidiskSelect;
extern bool executedBios;

extern "C" {
#include "DEBUG.h"
#include "perf_prof.h"
#include "lc.h"
#include "fileBrowser/fileBrowser.h"
#include "fileBrowser/fileBrowser-libfat.h"
#include "fileBrowser/fileBrowser-DVD.h"
#include "fileBrowser/fileBrowser-CARD.h"
#include "fileBrowser/fileBrowser-SMB.h"
#include "gc_input/controller.h"
#include "vm/vm.h"
#include "../gpu.h"
#include "../mem2_manager.h"
}

#include "libgui/gui2/gettext.h"

#include "../HidController/KernelHID.h"

#ifdef WII
unsigned int MALLOC_MEM2 = 0;
extern "C" {
#include <di/di.h>
}
#endif //WII

/* function prototypes */
extern "C" {
int SysInit();
void SysReset();
void SysClose();
void *SysLoadLibrary(char *lib);
void *SysLoadSym(void *lib, char *sym);
void SysCloseLibrary(void *lib);
void SysUpdate();
void SysRunGui();
void SysMessage(char *fmt, ...);
void LidInterrupt();
void CheckPsxType();
void psxResetRcntRate();
void plugin_call_rearmed_cbs(unsigned long autoDwActFixes, int cfgUseDithering);
void setSpuInterpolation(int spuInterpolation);
void setSpuTempo(int soundTempo);
void setSpuReverb(int soundReverb);
}

u32* xfb[3] = { NULL, NULL, NULL };	/*** Framebuffers ***/
GXRModeObj *vmode;				/*** Graphics Mode Object ***/
BOOL hasLoadedISO = FALSE;
fileBrowser_file isoFile;  //the ISO file
fileBrowser_file cddaFile; //the CDDA file
fileBrowser_file subFile;  //the SUB file
fileBrowser_file *biosFile = NULL;  //BIOS file

#if defined (CPU_LOG) || defined(DMA_LOG) || defined(CDR_LOG) || defined(HW_LOG) || \
	defined(PSXBIOS_LOG) || defined(GTE_LOG) || defined(PAD_LOG)
FILE *emuLog;
#endif

PcsxConfig Config;
char dynacore;
char biosDevice;
char LoadCdBios=0;
char frameLimit[2];
char frameSkip;
char useDithering;
char mdecChroma;
char fmvColour;
extern char audioEnabled;   // defined in dfsound/cube.c alongside the output drivers
char soundHwAccel;          // read by dfsound/out.c when it picks an output driver
char soundTempo;            // dfsound: legacy pull-back of the mixer clock when the output runs low
char soundReverb;           // dfsound: SPU reverb bus on/off (Advanced Sound page)
char soundMixerPrecision;   // dfsound: round instead of truncate in the voice mixer (Advanced Sound page)
char soundXaResampler;      // dfsound/xa.c: real hardware zigzag FIR vs the legacy nearest/Gaussian step (Advanced Sound page)
char soundRateControl;      // dfsound/ratectl.c: nudge the output rate from the queue's occupancy
char soundResampler;        // read by dfsound/resample.c on every block it converts
char cdBuffer, cdPrefetch, cdChdHunks;   // read by cdriso.c when an image is opened
char xaDisabled;            // mirrors Config.Xa   (see wiiSXconfig.h)
char cddaDisabled;          // mirrors Config.Cdda
char spuInterpolation;
char showFPSonScreen;
char printToScreen;
char menuActive;
char saveEnabled;
char creditsScrolling;
char padNeedScan=1;
char wpadNeedScan=1;
char hidPadNeedScan = 1;
char shutdown = 0;
char nativeSaveDevice;
char saveStateDevice;
char autoSave;
signed char autoSaveLoaded = 0;
char screenMode = 0;
char videoMode = 0;
char fileSortMode = 1;
char padAutoAssign;
char padType[10];
char padAssign[10];
char padLightgun[10];
char rumbleEnabled;
/* OpenGX EFB sync into VRAM (GlesGpu/efbSync.inc): 0 auto, 1 always keep frames, 2 never */
extern "C" { char efbSyncSetting = 0; }
char loadButtonSlot;
char controllerType;
char numMultitaps;
char lang = 0;
char fastLoad = 0;
char originalMode = 0;
char displayModeChanged = 0;
char bilinearFilter = 1;
char trapFilter = 1;
char interlacedMode = 0;
char deflickerFilter = 1;
char lightGun = 0;
char memCard[2];
char memCardFile[2];
char forceNTSC = 0;
char gpuPlugin = 0;

#define CONFIG_STRING_TYPE 0
#define CONFIG_STRING_SIZE 256
char smbUserName[CONFIG_STRING_SIZE];
char smbPassWord[CONFIG_STRING_SIZE];
char smbShareName[CONFIG_STRING_SIZE];
char smbIpAddr[CONFIG_STRING_SIZE];
char menuFont[CONFIG_STRING_SIZE];   /* MenuFont = "Name": sd:/wiisxrx/fonts/Name.dat replaces the built-in menu font */

int stop = 0;
bool needInitCpu = true;

static struct {
	const char* key;
	// For integral options this is a pointer to a char
	// for string values, this is a pointer to a 256-byte string
	// thus, assigning a string value to an integral type will cause overflow
	char* value;
	char  min, max;
} OPTIONS[] =
{ { "Audio", &audioEnabled, AUDIO_DISABLE, AUDIO_ENABLE },
  { "SoundHwAccel", &soundHwAccel, SOUND_HW_ACCEL_OFF, SOUND_HW_ACCEL_ON },
  { "SoundTempo", &soundTempo, SOUND_TEMPO_OFF, SOUND_TEMPO_ON },
  { "SoundReverb", &soundReverb, SOUND_REVERB_OFF, SOUND_REVERB_ON },
  { "SoundMixerPrecision", &soundMixerPrecision, SOUND_MIXER_PRECISION_LEGACY, SOUND_MIXER_PRECISION_HIFI },
  { "SoundXaResampler", &soundXaResampler, SOUND_XA_RESAMPLER_LEGACY, SOUND_XA_RESAMPLER_HIFI },
  { "SoundRateControl", &soundRateControl, SOUND_RATE_CONTROL_OFF, SOUND_RATE_CONTROL_ON },
  { "SoundResampler", &soundResampler, SOUND_RESAMPLE_HOLD, SOUND_RESAMPLE_CUBIC },
  { "CdBuffer", &cdBuffer, CD_BUFFER_16K, CD_BUFFER_256K },
  { "CdPrefetch", &cdPrefetch, CD_PREFETCH_OFF, CD_PREFETCH_ON },
  { "CdChdHunks", &cdChdHunks, CD_CHD_HUNKS_2, CD_CHD_HUNKS_8 },
  { "Interpolation", &spuInterpolation, SIMPLE_INTERPOLATION, GAUSSI_INTERPOLATION },
  { "DisableXa", &xaDisabled, XA_ENABLE, XA_DISABLE },
  { "DisableCdda", &cddaDisabled, CDDA_ENABLE, CDDA_DISABLE },
  { "FPS", &showFPSonScreen, FPS_HIDE, FPS_SHOW },
//  { "Debug", &printToScreen, DEBUG_HIDE, DEBUG_SHOW },
  { "ScreenMode", &screenMode, SCREENMODE_4x3, SCREENMODE_16x9_PILLARBOX },
  { "VideoMode", &videoMode, VIDEOMODE_AUTO, VIDEOMODE_PROGRESSIVE },
  { "FileSortMode", &fileSortMode, FILESORT_DIRS_MIXED, FILESORT_DIRS_FIRST },
  { "Core", &dynacore, DYNACORE_DYNAREC, DYNACORE_DYNAREC_OLD },
  { "NativeDevice", &nativeSaveDevice, NATIVESAVEDEVICE_SD, NATIVESAVEDEVICE_CARDB },
  { "StatesDevice", &saveStateDevice, SAVESTATEDEVICE_SD, SAVESTATEDEVICE_USB },
  { "AutoSave", &autoSave, AUTOSAVE_DISABLE, AUTOSAVE_ENABLE },
  { "BiosDevice", &biosDevice, BIOSDEVICE_HLE, BIOSDEVICE_USB },
  { "BootThruBios", &LoadCdBios, BOOTTHRUBIOS_NO, BOOTTHRUBIOS_YES },
  { "LimitFrames", &frameLimit[1], FRAMELIMIT_NONE, FRAMELIMIT_AUTO },
  { "SkipFrames", &frameSkip, FRAMESKIP_DISABLE, FRAMESKIP_ENABLE },
  { "Dithering", &useDithering, USEDITHER_NONE, USEDITHER_ALWAYS },
  { "MdecChroma", &mdecChroma, MDECCHROMA_SHARP, MDECCHROMA_SMOOTH },
  { "FmvColour", &fmvColour, FMVCOLOUR_15BIT, FMVCOLOUR_24BIT },
  { "PadAutoAssign", &padAutoAssign, PADAUTOASSIGN_MANUAL, PADAUTOASSIGN_AUTOMATIC },
  { "PadType1", &padType[0], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType2", &padType[1], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType3", &padType[2], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType4", &padType[3], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType5", &padType[4], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType6", &padType[5], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType7", &padType[6], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType8", &padType[7], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType9", &padType[8], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadType10", &padType[9], PADTYPE_NONE, PADTYPE_MULTITAP },
  { "PadAssign1", &padAssign[0], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign2", &padAssign[1], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign3", &padAssign[2], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign4", &padAssign[3], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign5", &padAssign[4], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign6", &padAssign[5], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign7", &padAssign[6], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign8", &padAssign[7], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign9", &padAssign[8], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "PadAssign10", &padAssign[9], PADASSIGN_INPUT0, PADASSIGN_INPUT1D },
  { "RumbleEnabled", &rumbleEnabled, RUMBLE_DISABLE, RUMBLE_ENABLE },
  { "EfbSync", &efbSyncSetting, 0, 2 },
  { "LoadButtonSlot", &loadButtonSlot, LOADBUTTON_SLOT0, LOADBUTTON_DEFAULT },
  { "ControllerType", &controllerType, CONTROLLERTYPE_STANDARD, CONTROLLERTYPE_ANALOG },
//  { "NumberMultitaps", &numMultitaps, MULTITAPS_NONE, MULTITAPS_TWO },
  { "smbusername", smbUserName, CONFIG_STRING_TYPE, CONFIG_STRING_TYPE },
  { "smbpassword", smbPassWord, CONFIG_STRING_TYPE, CONFIG_STRING_TYPE },
  { "smbsharename", smbShareName, CONFIG_STRING_TYPE, CONFIG_STRING_TYPE },
  { "smbipaddr", smbIpAddr, CONFIG_STRING_TYPE, CONFIG_STRING_TYPE },
  { "MenuFont", menuFont, CONFIG_STRING_TYPE, CONFIG_STRING_TYPE },
  { "lang", &lang, ENGLISH, TURKISH },
  { "fastLoad", &fastLoad, 0, 1 },
  { "TVMode", &originalMode, ORIGINALMODE_DISABLE, ORIGINALMODE_ENABLE },
  { "BilinearFilter", &bilinearFilter, BILINEARFILTER_DISABLE, BILINEARFILTER_ENABLE },
  { "TrapFilter", &trapFilter, TRAPFILTER_DISABLE, TRAPFILTER_ENABLE },
  { "Interlaced", &interlacedMode, INTERLACED_DISABLE, INTERLACED_ENABLE },
  { "DeflickerFilter", &deflickerFilter, DEFLICKER_DISABLE, DEFLICKER_ENABLE },
  { "LightGun", &lightGun, LIGHTGUN_DISABLE, LIGHTGUN_MOUSE },
  { "Memcard0", &memCard[0], MEMCARD_DISABLE, MEMCARD_ENABLE },
  { "Memcard1", &memCard[1], MEMCARD_DISABLE, MEMCARD_ENABLE },
  { "Memcard0File", &memCardFile[0], MEMCARDFILE_SHARED, MEMCARDFILE_PER_GAME },
  { "Memcard1File", &memCardFile[1], MEMCARDFILE_SHARED, MEMCARDFILE_PER_GAME },
  { "PadLightgun1", &padLightgun[0], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun2", &padLightgun[1], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun3", &padLightgun[2], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun4", &padLightgun[3], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun5", &padLightgun[4], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun6", &padLightgun[5], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun7", &padLightgun[6], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun8", &padLightgun[7], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun9", &padLightgun[8], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "PadLightgun10", &padLightgun[9], PADLIGHTGUN_DISABLE, PADLIGHTGUN_ENABLE },
  { "ForceNTSC", &forceNTSC, FORCENTSC_DISABLE, FORCENTSC_ENABLE },
  { "gpuPlugin", &gpuPlugin, OLD_SOFT, OPEN_GX },
  /* The locked cache: one bit per region in Gamecube/lc.c's table, 0 = all off. Not in the
   * menu -- it is for measuring, and a chained autoboot can set it per game. */
  { "LockedCache", &lockedCache, 0, 127 }
};
void handleConfigPair(char* kv);
void readConfig(FILE* f);
void writeConfig(FILE* f);
int checkBiosExists(int testDevice);
static void loadSeparatelySetting();
static bool loadSeparatelySettingItem(char* s1, char* s2, bool isUsb);
void biosFileInit();

static void setGpuPlugin()
{
    // Set Gpu Plugin
    if (gpuPlugin == OLD_SOFT)
    {
        gpuPtr = &oldSoftGpu;
    }
    if (gpuPlugin == NEW_SOFT)
    {
        gpuPtr = &newSoftGpu;
    }
    if (gpuPlugin == OPEN_GX)
    {
        gpuPtr = &glesGpu;
    }
}

static bool loadControllerMapping(char* usbSd)
{
    char settingPathBuf[256];
    FILE* f;
    bool loadRet = true;

    sprintf(settingPathBuf, "%s:/wiisxrx/controlG.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if (f) {
        load_configurations(f, &controller_GC);					//read in GC controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    #ifdef HW_RVL

    sprintf(settingPathBuf, "%s:/wiisxrx/controlH.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if(f) {
        load_configurations(f, &controller_HidGC);			//read in HID controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    sprintf(settingPathBuf, "%s:/wiisxrx/controlC.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if(f) {
        load_configurations(f, &controller_Classic);			//read in Classic controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    sprintf(settingPathBuf, "%s:/wiisxrx/controlN.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if(f) {
        load_configurations(f, &controller_WiimoteNunchuk);		//read in WM+NC controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    sprintf(settingPathBuf, "%s:/wiisxrx/controlW.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if(f) {
        load_configurations(f, &controller_Wiimote);			//read in Wiimote controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    sprintf(settingPathBuf, "%s:/wiisxrx/controlP.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if (f) {
        load_configurations(f, &controller_WiiUPro);			//read in Wii U Pro controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    sprintf(settingPathBuf, "%s:/wiisxrx/controlD.cfg", usbSd);
    f = fopen(settingPathBuf, "rb" );  //attempt to open file
    if (f) {
        load_configurations(f, &controller_WiiUGamepad);		//read in Wii U Gamepad controller mappings
        fclose(f);
    }
    else
    {
        loadRet = false;
    }

    #endif // HW_RVL

    return loadRet;
}

void loadSettings(int argc, char *argv[])
{
	// Default Settings
	audioEnabled     = 1; // Audio
	soundHwAccel     = SOUND_HW_ACCEL_OFF; // CPU sound path; the DSP one is opt-in
	soundTempo       = SOUND_TEMPO_OFF;    // legacy; the rate control below replaced it (2026-09-20)
	soundReverb      = SOUND_REVERB_ON;    // matches the hardcoded behaviour before this setting existed
	soundMixerPrecision = SOUND_MIXER_PRECISION_HIFI;  // free fidelity: see wiiSXconfig.h
	soundXaResampler    = SOUND_XA_RESAMPLER_HIFI;     // matches real hardware's own XA filter for 37800 Hz streams
	soundRateControl = SOUND_RATE_CONTROL_ON;
	soundResampler   = SOUND_RESAMPLE_CUBIC; // hi-fi default: Hold is what both paths always did, Cubic is materially cleaner
	/* The better of the two voice interpolators. Measured on Crash 3 with PERF_PROF_SPU:
	 * the SPU is about 8% of wall time, of which the ADPCM decode and interpolation is
	 * 5.2%, and choosing Gaussian over Simple is 2.1% of that. An older comment here said
	 * the whole SPU was under 1% of a frame, which the measurement does not support. */
	spuInterpolation = GAUSSI_INTERPOLATION;
	/* The CD defaults, measured 2026-09-22 on eleven games (SETTINGS.md section 2):
	 * 16 KB -- libfat reads the card in 32 KB pages whatever stdio asks, so a larger buffer
	 *   only reads more (256 KB: +20% card commands); the counts are the same on a Wii.
	 * read-ahead off -- it takes 77-90% of the card waits off the emulator thread with
	 *   99.7% hits and no change in behaviour, but it is a disc-path thread only a Wii can
	 *   clear: scripts/chains/cd_ab.txt decides it.
	 * 8 hunks -- a true LRU never misses more with more ways; 8 cost about 160 KB of MEM2. */
	cdBuffer         = CD_BUFFER_16K;
	cdPrefetch       = CD_PREFETCH_OFF;
	cdChdHunks       = CD_CHD_HUNKS_8;
	xaDisabled       = XA_ENABLE;    // XA and CDDA audio both on by default
	cddaDisabled     = CDDA_ENABLE;
#ifdef RELEASE
	showFPSonScreen  = 0; // Don't show FPS on Screen
#else
	showFPSonScreen  = 1; // Show FPS on Screen
#endif
	printToScreen    = 1; // Show DEBUG text on screen
	printToSD        = 0; // Disable SD logging
	frameLimit[1]		 = 1; // Auto limit FPS
	frameSkip		 = 0; // Disable frame skipping
	useDithering		 = 1; // Default dithering (set to 0 (disabled) in PEOPSgpu)
	mdecChroma		 = MDECCHROMA_SHARP;   /* what the console does */
	fmvColour		 = FMVCOLOUR_24BIT;    /* what the console does */
	saveEnabled      = 0; // Don't save game
	nativeSaveDevice = 0; // SD
	saveStateDevice	 = 0; // SD
	autoSave         = 1; // Auto Save Game
	creditsScrolling = 0; // Normal menu for now
	dynacore         = 0; // Dynarec
	screenMode		 = 0; // Stretch FB horizontally
	videoMode		 = VIDEOMODE_AUTO;
	fileSortMode	 = FILESORT_DIRS_FIRST;
	padAutoAssign	 = PADAUTOASSIGN_AUTOMATIC;
	menuFont[0]		 = 0;              // built-in font
	for (int i = 0; i < 10; i++){
		padType[i]		 = PADTYPE_NONE;
		padAssign[i]	 = PADASSIGN_INPUT0;
		padLightgun[i]	 = PADLIGHTGUN_ENABLE;
	}
	padAssign[1]	 = PADASSIGN_INPUT1;
	memCard[0]		 = MEMCARD_ENABLE;
	memCard[1]		 = MEMCARD_ENABLE;
	memCardFile[0]	 = MEMCARDFILE_PER_GAME;   /* what WiiStation always did */
	memCardFile[1]	 = MEMCARDFILE_SHARED;
	rumbleEnabled	 = RUMBLE_ENABLE;
	loadButtonSlot	 = LOADBUTTON_DEFAULT;
	controllerType	 = CONTROLLERTYPE_STANDARD;
	numMultitaps	 = MULTITAPS_NONE;
	menuActive = 1;
	forceNTSC 		 = FORCENTSC_DISABLE;
	gpuPlugin        = OLD_SOFT;
	gpuPtr           = &oldSoftGpu;

	//PCSX-specific defaults
	memset(&Config, 0, sizeof(PcsxConfig));
	Config.Cpu=dynacore;		//Dynarec core
	strcpy(Config.Net,"Disabled");
	Config.PsxOut = 1;
	Config.HLE = 1;
	Config.Xa = xaDisabled;     // from the settings file; see wiiSXconfig.h
	Config.Cdda = cddaDisabled;
	Config.cycle_multiplier = CYCLE_MULT_DEFAULT;
	Config.PsxAuto = 1; //Autodetect
	Config.GpuListWalking = -1;
	Config.FractionalFramerate = -1;
	LoadCdBios = BOOTTHRUBIOS_NO;
	lang = 0;
	fastLoad = 0;
	originalMode = 0;

	//config stuff
	int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
	fileBrowser_file* configFile_file = &saveDir_libfat_USB;
#ifdef HW_RVL
    // Because it is necessary to read the HID INI configuration file of the USB,
    // it is always necessary to initialize the USB
    //int usbInitRet = configFile_init(configFile_file);
	if(argc && argv[0][0] == 'u') {  //assume USB
		if(configFile_init(configFile_file)) {                //only if device initialized ok
            memset(Config.PatchesDir, '\0', sizeof(Config.PatchesDir));
            strcpy(Config.PatchesDir, "usb:/wiisxrx/ppf/");
			FILE* f = fopen( "usb:/wiisxrx/settingsRX2022.cfg", "r" );  //attempt to open file
			if(f) {        //open ok, read it
				readConfig(f);
				fclose(f);
			}
			loadControllerMapping("usb");
		}
	}
	else /*if((argv[0][0]=='s') || (argv[0][0]=='/'))*/
#endif //HW_RVL
	{ //assume SD
		configFile_file = &saveDir_libfat_Default;
		if(configFile_init(configFile_file)) {                //only if device initialized ok
            // add xjsxjs197 start
            memset(Config.PatchesDir, '\0', sizeof(Config.PatchesDir));
            strcpy(Config.PatchesDir, "sd:/wiisxrx/ppf/");
            // add xjsxjs197 end
			FILE* f = fopen( "sd:/wiisxrx/settingsRX2022.cfg", "r" );  //attempt to open file
			if(f) {        //open ok, read it
				readConfig(f);
				fclose(f);
			}

			loadControllerMapping("sd");
		}
	}
#ifdef HW_RVL
	// Handle options passed in through arguments
	int i;
	for(i=1; i<argc; ++i){
		handleConfigPair(argv[i]);
	}
#endif

	//Test for Bios file
	if(biosDevice != BIOSDEVICE_HLE)
		if(checkBiosExists((int)biosDevice) == FILE_BROWSER_ERROR_NO_FILE)
			biosDevice = BIOSDEVICE_HLE;

	//Sync settings with config
	Config.Cpu=dynacore;
	// Seed the live frame-limit value from the just-loaded setting. go() also
	// refreshes it on every gameplay entry, but this boot-time seed is still
	// required: SysInit() -> OpenPlugins() runs BEFORE the first go(), and the
	// old/P.E.Op.S. GPU plugins latch UseFrameLimit from frameLimit[0] at
	// plugin-open time (PEOPS_GPUopen -> GPUsetframelimit). Without this they
	// would open with the zero-initialised FRAMELIMIT_NONE and never correct
	// it, since their per-frame re-check only fires on a *change*.
	frameLimit[0] = frameLimit[1];
	//iUseDither = useDithering;
	setSpuInterpolation(spuInterpolation);
	setSpuTempo(soundTempo);
	setSpuReverb(soundReverb);

	// Set Gpu Plugin
	setGpuPlugin();
}

void ScanPADSandReset(u32 dummy)
{
//	PAD_ScanPads();
	padNeedScan = wpadNeedScan = 1;
	hidPadNeedScan = 1;
	if(!((*(u32*)0xCC003000)>>16))
		stop = 1;
}

#ifdef HW_RVL
void ShutdownWii()
{
    HIDClose(0);
    shutdown = 1;
    stop = 1;
}
#endif

void video_mode_init(GXRModeObj *videomode, u32 *fb1, u32 *fb2, u32 *fb3)
{
	vmode = videomode;
	xfb[0] = fb1;
	xfb[1] = fb2;
	xfb[2] = fb3;
}

// Plugin structure
extern "C" {
#include "GamecubePlugins.h"
PluginTable plugins[] =
	{ PLUGIN_SLOT_0,
	  PLUGIN_SLOT_1,
	  PLUGIN_SLOT_2,
	  PLUGIN_SLOT_3,
	  PLUGIN_SLOT_4,
	  //PLUGIN_SLOT_5,
	  PLUGIN_SLOT_6,
	  PLUGIN_SLOT_7 };
}

/****************************************************************************
 * IOS Check
 ***************************************************************************/
#ifdef HW_RVL
bool SupportedIOS(u32 ios)
{
        if(ios == 58 || ios == 61)
                return true;

        return false;
}

#endif

bool Autoboot;
bool AutobootBios;        /* autoboot.txt first line "BIOS": run the BIOS shell, no disc */
char AutobootROM[1024];
char AutobootPath[1024];

/* A chained autoboot: autoboot.txt can list several games to run one after another in
 * one boot. The first line is CHAIN, then three lines per game:
 *
 *   CHAIN
 *   3600 sd:/wiisxrx/spyro_title.txt
 *   sd:/wiisxrx/isos/Spyro the Dragon
 *   Spyro the Dragon [NTSC-U] [SCUS-94228].cue
 *   3600
 *   sd:/wiisxrx/isos/Medievil
 *   Medievil [U] [SCUS-94227].cue
 *
 * The first line of each game is how many vblanks to run it for, then optionally an
 * autoinput script of its own (without one, the game gets no scripted input) and up to
 * CHAIN_SETS settings for that game alone, as KEY=VALUE with the settings file's names:
 *
 *   3600 sd:/wiisxrx/spyro_title.txt LockedCache=3
 *
 * A setting a game changes goes back to the settings file's value before the next game, so
 * each line says everything that differs for its game. That is what lets one boot compare a
 * setting on and off: the same game twice, once with it. Blank lines and lines that start
 * with '#' are skipped.
 *
 * Games use their memory cards as usual -- a game with no card can stop at a warning (Spyro
 * does: "progress will not be saved") and the input script then runs past a screen it was
 * not written for. sio.c notes every card file a chained game loads or saves, and when the
 * game ends the chain deletes them. So every game starts from freshly created cards -- the
 * same game listed twice (an A/B) boots the same way both times, which it did not when the
 * second run found the card the first had made -- and a chain leaves no cards behind.
 *
 * Each game starts with a vblank count of zero, so its script means the same as it would
 * in a run of its own, and its section of perf.log ends with the line
 *
 *   === chain <n>/<total> end vblanks=<ran> [set=<K=V,...>] rom=<file> ===
 *
 * which scripts/chain_table.py splits the log on. After the last game the console powers
 * off. Dolphin in batch mode then exits, and a Wii is off with its SD card ready to take
 * out: on hardware, moving the card is the slow part, so one boot should collect every
 * game. */
#define CHAIN_MAX 24
#define CHAIN_SETS 8
static struct {
	unsigned vbl; char input[128]; char path[256]; char rom[256]; char state[48];
	char set[CHAIN_SETS][32]; int nset;
} chainList[CHAIN_MAX];

/* The settings a chain entry changed, with the values to put back. */
static struct { char *value; char orig; } chainSaved[CHAIN_SETS * CHAIN_MAX];
static int chainSavedN;
void setOption(char* key, char* valuePointer);
extern "C" void mcd_track_begin(void);   /* sio.c: note the card files games use */
extern "C" void mcd_track_delete(void);  /* sio.c: delete them */
static int chainN, chainI;
extern "C" {
	unsigned chain_stop_vbl;                   /* psxcounters.c stops the game here; 0 = never */
	void autoinput_reset(const char *path);    /* PadWiiSX.c */
	int statetool_service(void);               /* statetool.cpp */
	void statetool_request(int op, const char *name, unsigned vbl, unsigned n);
	extern u32 frame_counter;                  /* psxcounters.c: +1 per vblank */
	extern unsigned short *psxVuw;             /* the PlayStation's VRAM, every GPU plugin */
}

/* The next line that is not blank and not a comment. */
static bool chainLine(FILE *f, char *buf, int n)
{
	while (fgets(buf, n, f)) {
		buf[strcspn(buf, "\r\n")] = 0;
		if (buf[0] && buf[0] != '#')
			return true;
	}
	return false;
}

static int chainLoad(FILE *f)
{
	char line[256];
	while (chainN < CHAIN_MAX && chainLine(f, line, sizeof line)) {
		auto *e = &chainList[chainN];
		char *tok = strtok(line, " \t");
		e->input[0] = 0;
		e->state[0] = 0;
		e->nset = 0;
		e->vbl = tok ? strtoul(tok, NULL, 10) : 0;
		if (!e->vbl)
			continue;
		while ((tok = strtok(NULL, " \t"))) {
			if (!strncasecmp(tok, "State=", 6))   /* start from a save state (statetool.cpp) */
				snprintf(e->state, sizeof e->state, "%s", tok + 6);
			else if (strchr(tok, '=')) {
				if (e->nset < CHAIN_SETS)
					snprintf(e->set[e->nset++], sizeof e->set[0], "%s", tok);
			} else
				snprintf(e->input, sizeof e->input, "%s", tok);
		}
		if (!chainLine(f, e->path, sizeof e->path) || !chainLine(f, e->rom, sizeof e->rom))
			break;
		chainN++;
	}
	/* The log belongs to this chain from here. perf_reset() empties it again at the first
	 * go() in a debug build, before anything is written; a release build never does. */
	if (chainN) { FILE *p = fopen("sd:/wiisxrx/perf.log", "w"); if (p) fclose(p); }
	return chainN;
}

/* Set KEY to VALUE for this game, remembering what it was. Only the settings file's numeric
 * settings: a string setting has no business differing per game. */
static void chainOverride(char *kv, bool remember)
{
	char key[32], *val;
	unsigned i;
	snprintf(key, sizeof key, "%s", kv);
	val = strchr(key, '=');
	if (!val)
		return;
	*val++ = 0;
	for (i = 0; i < sizeof(OPTIONS) / sizeof(OPTIONS[0]); i++) {
		if (strcmp(OPTIONS[i].key, key) || OPTIONS[i].max == CONFIG_STRING_TYPE)
			continue;
		if (remember && chainSavedN < (int)(sizeof chainSaved / sizeof chainSaved[0])) {
			chainSaved[chainSavedN].value = OPTIONS[i].value;
			chainSaved[chainSavedN].orig = *OPTIONS[i].value;
			chainSavedN++;
		}
		setOption(key, val);
		return;
	}
}

/* Loading the game reads the settings file again (loadSeparatelySetting), which put back
 * every setting of this game's line that the file also names -- PadAutoAssign, which the
 * staged settings of a scripted run always set, was silently undone. So they are applied
 * again after it. The originals were saved the first time. */
static void chainReapply(void)
{
	int k;
	if (chainI < chainN)
		for (k = 0; k < chainList[chainI].nset; k++)
			chainOverride(chainList[chainI].set[k], false);
}

static void chainStart(int i)
{
	int k;
	/* Undo the last game's settings, newest first, then apply this one's. */
	while (chainSavedN > 0) {
		chainSavedN--;
		*chainSaved[chainSavedN].value = chainSaved[chainSavedN].orig;
	}
	for (k = 0; k < chainList[i].nset; k++)
		chainOverride(chainList[i].set[k], true);
	mcd_track_begin();
	snprintf(AutobootPath, sizeof AutobootPath, "%s", chainList[i].path);
	snprintf(AutobootROM, sizeof AutobootROM, "%s", chainList[i].rom);
	chain_stop_vbl = chainList[i].vbl;
	frame_counter = 0;
	autoinput_reset(chainList[i].input);
	if (chainList[i].state[0])   /* load it once the game has booted: its first vblank */
		statetool_request(2, chainList[i].state, 1, 0);
	Autoboot = true;
}

/* A chained game has come back from go(). Close its section of the log and start the
 * next one; after the last, power off. Returns true when there is another game to boot. */
static bool chainNext(void)
{
	FILE *f;
	if (!chainN)
		return false;
	/* frame_counter is zero only when the game never ran -- a missing file, most often --
	 * and then g_perf still holds the previous game, so no report. */
	if (frame_counter) {
		/* What the game had in VRAM at the end: scripts/chain_table.py turns it into a
		 * picture, which is how a table row says what the numbers were measured on. */
		char vp[40];
		snprintf(vp, sizeof vp, "sd:/wiisxrx/vram_%02d.bin", chainI + 1);
		if (psxVuw && (f = fopen(vp, "wb"))) {
			fwrite(psxVuw, 2, 1024 * 512, f);
			fclose(f);
		}
		perf_report();
	}
	f = fopen("sd:/wiisxrx/perf.log", "a");
	if (f) {
		char sets[CHAIN_SETS * 32 + 8] = "";
		int k;
		for (k = 0; k < chainList[chainI].nset; k++) {
			strcat(sets, k ? "," : " set=");
			strcat(sets, chainList[chainI].set[k]);
		}
		fprintf(f, "=== chain %d/%d end vblanks=%u%s rom=%s ===\n", chainI + 1, chainN,
			(unsigned)frame_counter, sets, chainList[chainI].rom);
		fclose(f);
	}
	mcd_track_delete();   /* the memory cards this game used */
	if (++chainI < chainN) {
		chainStart(chainI);
		return true;
	}
	chain_stop_vbl = 0;
	autoinput_reset(NULL);   /* closes a recording ("record"), before the card is unmounted */
	/* Unmount first: a Wii that powers off with the FAT cache dirty loses the log. */
	if (lab_active()) {   /* send the results, then back to the Homebrew Channel for the next test */
		lab_report();
		net_deinit();
		fatUnmount("sd");
		exit(0);
	}
	fatUnmount("sd");
	SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	return false;
}

int main(int argc, char *argv[])
{
	/* INITIALIZE */
#ifdef HW_RVL
	if(argc > 2 && argv[1] != NULL && argv[2] != NULL)
	{
		Autoboot = true;
		snprintf(AutobootPath, sizeof(AutobootPath), "%s", argv[1]);
		snprintf(AutobootROM, sizeof(AutobootROM), "%s", argv[2]);
	}
	else
	{
		Autoboot = false;
		memset(AutobootPath, 0, sizeof(AutobootPath));
		memset(AutobootROM, 0, sizeof(AutobootROM));
	}

		L2Enhance();

        u32 ios = IOS_GetVersion();

        if(!SupportedIOS(ios))
        {
            IOS_ReloadIOS(58);
        }

	#endif

	gx_init_mem2();

	control_info_init(); //Perform controller auto assignment at least once at startup.

	loadSettings(argc, argv);

	/* A Wii on the bench (scripts/wii_lab.py): "lab=HOST:PORT" from wiiload fetches this
	 * run's files -- autoboot.txt among them -- before it is read, and a crash goes back
	 * to the Homebrew Channel after 10 s instead of waiting for a button. */
	lab_args(argc, argv);
	if (lab_active()) {
		__exception_setreload(10);
		if (lab_fetch() < 0) {   /* no PC to drive it: back to HBC, not to a menu no one is at */
			net_deinit();
			fatUnmount("sd");
			exit(0);
		}
	}

	/* Test automation: a bare .dol booted by Dolphin gets no loader argv, so
	 * sd:/wiisxrx/autoboot.txt -- two lines, the ISO's directory and its
	 * filename -- stands in for argv[1]/argv[2] and boots straight into the
	 * game with no menu input. Delete the file to boot to the menu as usual.
	 * A first line of just "BIOS" runs the BIOS shell instead (the menu's
	 * "Execute Bios"), for testing the shell's own screens.
	 * Read here rather than in main()'s argv block because the SD card is
	 * only mounted once loadSettings() has run. */
	if (!Autoboot) {
		FILE *ab = fopen("sd:/wiisxrx/autoboot.txt", "r");
		if (ab) {
			if (fgets(AutobootPath, sizeof(AutobootPath), ab)) {
				AutobootPath[strcspn(AutobootPath, "\r\n")] = 0;
				if (strcasecmp(AutobootPath, "BIOS") == 0) {
					AutobootBios = true;
					Autoboot = true;
				} else if (strcasecmp(AutobootPath, "CHAIN") == 0) {
					if (chainLoad(ab))
						chainStart(0);
					else
						AutobootPath[0] = 0;
				} else if (fgets(AutobootROM, sizeof(AutobootROM), ab)) {
					AutobootROM[strcspn(AutobootROM, "\r\n")] = 0;
					Autoboot = AutobootPath[0] && AutobootROM[0];
				}
			}
			fclose(ab);
		}
	}

	#ifdef HW_RVL
	HIDInit(ios);

	VM_Init(1024*1024, 256*1024); // whatever for now, we're not really using this for anything other than mmap on Wii.
	#endif // HW_RVL

	LoadLanguage();
	ChangeLanguage();

	MenuContext *menu = new MenuContext(vmode);
	VIDEO_SetPostRetraceCallback (ScanPADSandReset);

#ifndef WII
	DVD_Init();
#endif

#ifdef DEBUGON
	/* The GDB stub can also run over TCP, on port 2828, which needs the network up
	 * before this point. USB Gecko is used instead because it works before the
	 * network does, and a hang in the network stack is one of the things worth
	 * attaching a debugger to. */
	//DEBUG_Init(GDBSTUB_DEVICE_TCP,GDBSTUB_DEF_TCPPORT);
	DEBUG_Init(GDBSTUB_DEVICE_USB, 1);
	_break();
#endif

	// Start up AESND (inited here because its used in SPU and CD)
	//AESND_Init();

#ifdef HW_RVL
	// Initialize the network if the user has specified something in their SMB settings
	if(strlen(&smbShareName[0]) && strlen(&smbIpAddr[0])) {
	  init_network_thread();
  }
#if PERF_PROF_NETWAIT
	/* Two seconds, on this thread, before the menu: the network thread is suspended for
	 * the whole of a game, so it is not a place a measurement can finish. */
	net_wait_measure();
#endif
#endif

	if(Autoboot)
	{
		do
			menu->Autoboot();
		while (chainNext());
		Autoboot = false;
	}

	while (menu->isRunning()) {}

	// Shut down AESND
	//AESND_Reset();

	#ifdef HW_RVL
	HIDClose(1);
	#endif // HW_RVL

    menu::IplFont obj = menu::IplFont::getInstance();
	delete &obj;

	delete menu;

	ReleaseLanguage();

	return 0;
}

void psxCpuInit()
{
	psxCpuSelect();

    psxCpu->Init();
}

void biosFileInit()
{
	biosFile_dir = &biosDir_libfat_Default;
	if (biosDevice != BIOSDEVICE_HLE) {
		Config.HLE = BIOS_USER_DEFINED;
		biosFile_dir = (biosDevice == BIOSDEVICE_SD) ? &biosDir_libfat_Default : &biosDir_libfat_USB;
	} else {
		Config.HLE = BIOS_HLE;
	}

	// Even in HLE mode, the corresponding BIOS file needs to be loaded for games
	// that have modified the BIOS font library
	biosFile_readFile  = fileBrowser_libfat_readFile;
	biosFile_open      = fileBrowser_libfat_open;
	biosFile_init      = fileBrowser_libfat_init;
	biosFile_deinit    = fileBrowser_libfat_deinit;
	if (biosFile) {
		free(biosFile);
 	}
	biosFile = (fileBrowser_file*)memalign(32,sizeof(fileBrowser_file));
	memcpy(biosFile,biosFile_dir,sizeof(fileBrowser_file));
	{
		size_t used = strlen(biosFile->name);
		size_t avail = (used < sizeof(biosFile->name)) ? sizeof(biosFile->name) - used - 1 : 0;
		const char *tail = GetGameBios(biosFile->name, filenameFromAbsPath(isoFile.name), strlen(isoFile.name));
		if (avail > 0 && tail && tail[0])
			snprintf(biosFile->name + used, avail + 1, "%s", tail);
	}
	biosFile_init(biosFile);  //initialize the bios device (it might not be the same as ISO device)
}

static bool loadSeparatelySettingItem(char* s1, char* s2, bool isUsb)
{
    struct stat s;
    char settingPathBuf[256];
    fileBrowser_file* configFile_file;
    sprintf(settingPathBuf, "%s%s%s", s1, s2, ".cfg");
    if (stat(settingPathBuf, &s))
    {
        return false;
    }

    configFile_file = isUsb ? &saveDir_libfat_USB : &saveDir_libfat_Default;
    int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
    if (configFile_init(configFile_file)) {        //only if device initialized ok
        FILE* f = fopen( settingPathBuf, "r" );   //attempt to open file
        if (f) {
            readConfig(f);
            fclose(f);
            return true;
        }
    }
    return false;
}

static void loadSeparatelySetting()
{
    char oldLoadButtonSlot = loadButtonSlot;

    // First, we load separately game settings.
    // Load separately game settings from USB device
    if (!loadSeparatelySettingItem("usb:/wiisxrx/settings/", CdromId, true))
    {
        // If there is no separate setting for USB
        // Load separately game settings from SD card
        if (!loadSeparatelySettingItem("sd:/wiisxrx/settings/", CdromId, false))
        {
            // If there is no separate setting
            // we load the common (global) settings.
            // Load common (global) settings from USB device
            if (!loadSeparatelySettingItem("usb:/wiisxrx/", "settingsRX2022", true))
            {
                // If there is no common (global) settings for USB
                // Load common (global) settings from SD card
                loadSeparatelySettingItem("sd:/wiisxrx/", "settingsRX2022", false);
            }
        }
    }
    chainReapply();   /* a chained game's own settings win over the file */

    // If the loadButton Slot changes, reload the key mapping
    if (oldLoadButtonSlot != loadButtonSlot)
    {
        // Load button mapping from USB
        if (!loadControllerMapping("usb"))
        {
            // If the key mapping in USB does not exist
            // Load button mapping from SD
            loadControllerMapping("sd");
        }
    }

    Config.pR3000Fix = 0;
    Config.Cpu = dynacore;

    // Init biosFile pointers and stuff
    biosFileInit();

    // FORCE NTSC
    if(forceNTSC == FORCENTSC_ENABLE)
    {
        Config.PsxType = PSX_TYPE_NTSC;
    }
    else
    {
        CheckPsxType();
    }
    psxResetRcntRate();

    extern bool lightrec_mmap_inited;
    if (Config.Cpu == DYNACORE_DYNAREC && !lightrec_mmap_inited) // Lightrec
    {
        psxMemInit();
    }
    psxCpuInit();
    needInitCpu = true;

    // Setting the following values in OpenPlugins is incorrect because the CdromId has not been obtained yet
    // Only after Apply_Hacks_Cdrom was executed , dwActFixes and iUseDither set the correct values
    // Setting variables directly in the GPU is not good......
    extern uint32_t dwActFixes;
    extern int iUseDither;
    dwActFixes = Config.hacks.dwActFixes;
    iUseDither = useDithering;

    // Set Gpu Plugin
    setGpuPlugin();
}

// loadISO loads an ISO file as current media to read from.
int loadISOSwap(fileBrowser_file* file) {

  // Refresh file pointers
	memset(&isoFile, 0, sizeof(fileBrowser_file));
	memset(&cddaFile, 0, sizeof(fileBrowser_file));
	memset(&subFile, 0, sizeof(fileBrowser_file));

	memcpy(&isoFile, file, sizeof(fileBrowser_file) );

    CdromId[0] = '\0';
    CdromLabel[0] = '\0';

	SetIsoFile(&file->name[0]);

	if (ReloadCdromPlugin() < 0) {
		return -1;
	}
	if (CDR_open() < 0) {
		return -1;
	}

	SetCdOpenCaseTime(time(NULL) + 2);
	LidInterrupt();

	return 0;
}


// loadISO loads an ISO, resets the system and loads the save.
int loadISO(fileBrowser_file* file)
{
	// Refresh file pointers
	memset(&isoFile, 0, sizeof(fileBrowser_file));
	memset(&cddaFile, 0, sizeof(fileBrowser_file));
	memset(&subFile, 0, sizeof(fileBrowser_file));

	memcpy(&isoFile, file, sizeof(fileBrowser_file) );

	if(hasLoadedISO || executedBios) {
		SysClose();
		hasLoadedISO = FALSE;
	}
	needInitCpu = false;
	if(SysInit() < 0)
		return -1;
	hasLoadedISO = TRUE;

	char *tempStr = &file->name[0];
	if((strstr(tempStr,".EXE")!=NULL) || (strstr(tempStr,".exe")!=NULL)) {
		SysReset();
		Load(file);
	}
	else {
		CheckCdrom();

        gpu_t *oldGpuPtr = gpuPtr;

		loadSeparatelySetting();

		gpu_t *newGpuPtr = gpuPtr;
		if (newGpuPtr != oldGpuPtr)
        {
            // Gpu Plugin changed
            oldGpuPtr->shutdown();
            oldGpuPtr->close();
            newGpuPtr->init();
            newGpuPtr->open();
        }

		SysReset();
		LoadCdrom();
	}

	if(autoSave==AUTOSAVE_ENABLE) {
		setSaveDevice();   /* fileBrowser.c: point saveFile_* at nativeSaveDevice */
		// Try loading everything
		int result = 0;
		saveFile_init(saveFile_dir);
		result += LoadMcd(1,saveFile_dir);
		result += LoadMcd(2,saveFile_dir);
		saveFile_deinit(saveFile_dir);

		switch (nativeSaveDevice)
		{
		case NATIVESAVEDEVICE_SD:
			if (result) autoSaveLoaded = NATIVESAVEDEVICE_SD;
			break;
		case NATIVESAVEDEVICE_USB:
			if (result) autoSaveLoaded = NATIVESAVEDEVICE_USB;
			break;
		case NATIVESAVEDEVICE_CARDA:
			if (result) autoSaveLoaded = NATIVESAVEDEVICE_CARDA;
			break;
		case NATIVESAVEDEVICE_CARDB:
			if (result) autoSaveLoaded = NATIVESAVEDEVICE_CARDB;
			break;
		}
	}

	return 0;
}

void setOption(char* key, char* valuePointer){
	bool isString = valuePointer[0] == '"';
	char value = 0;

	if(isString)
		valuePointer = config_unquote(valuePointer);
	else
		value = atoi(valuePointer);

	for(unsigned int i=0; i<sizeof(OPTIONS)/sizeof(OPTIONS[0]); i++){
		if(!strcmp(OPTIONS[i].key, key)){
			if(isString) {
				if(OPTIONS[i].max == CONFIG_STRING_TYPE)
					strncpy(OPTIONS[i].value, valuePointer,
					        CONFIG_STRING_SIZE-1);
			} else if(value >= OPTIONS[i].min && value <= OPTIONS[i].max)
				*OPTIONS[i].value = value;
			break;
		}
	}
}

void handleConfigPair(char* kv){
	/* A line with no separator holds no setting. A blank line is the common one, and it
	 * used to walk off the end of the buffer. */
	char* vs = config_split(kv);

	if(vs)
		setOption(kv, vs);
}

void readConfig(FILE* f){
	char line[256];
	while(fgets(line, 256, f)){
		if(line[0] == '#') continue;
		handleConfigPair(line);
	}
}

void writeConfig(FILE* f){
	for(unsigned int i=0; i<sizeof(OPTIONS)/sizeof(OPTIONS[0]); ++i){
		if(OPTIONS[i].max == CONFIG_STRING_TYPE)
			fprintf(f, "%s = \"%s\"\n", OPTIONS[i].key, OPTIONS[i].value);
		else
			fprintf(f, "%s = %d\n", OPTIONS[i].key, *OPTIONS[i].value);
	}
}

extern "C" {
//System Functions
void go(void) {
	Config.PsxOut = 0;
	stop = 0;
	perf_reset();
	lc_configure((unsigned char)lockedCache);   /* the regions this game runs with */

	// Refresh live state from the persisted settings on every entry, the same
	// way frameskip/dithering are refreshed below -- reaching the settings
	// menu means leaving go(), so anything refreshed here is current by the
	// time gameplay resumes. Unconditional (not inside the newSoftGpu guard):
	// every GPU plugin reads frameLimit[0], unlike gc_rearmed_cbs which is
	// newSoftGpu-only.
	frameLimit[0] = frameLimit[1];

#ifdef PERF_PROF
	g_carry.pad[0] = virtualControllers[0].inUse;
	g_carry.pad[1] = (virtualControllers[0].inUse && virtualControllers[0].control)
		? (unsigned char)virtualControllers[0].control->identifier : 0;
	g_carry.pad[2] = gc_connected;
	g_carry.pad[3] = controller_GC.available[0];
#endif

	/* Controllers are assigned once at power-on, often before a pad has answered its first
	 * scan, and after that only while the menu is drawn. An autoboot (a loader's arguments,
	 * autoboot.txt, a chain) never draws the menu, so its game started with no pad. */
	if (padAutoAssign == PADAUTOASSIGN_AUTOMATIC)
		auto_assign_controllers();
	else
		manual_assign_controllers();

	if (gpuPtr == &newSoftGpu)
    {
        plugin_call_rearmed_cbs(Config.hacks.dwActFixes, useDithering);
    }

	/* A scripted save or load (statetool.cpp) stops the CPU at its vblank; it is carried out
	 * here, with the CPU stopped as the menu has it, and the game goes on. */
	do
		psxCpu->Execute();
	while (statetool_service());

	// remove this callback to avoid any issues when returning to the menu.
	GX_SetDrawDoneCallback(NULL);
}

int SysInit() {
#if defined (CPU_LOG) || defined(DMA_LOG) || defined(CDR_LOG) || defined(HW_LOG) || \
	defined(PSXBIOS_LOG) || defined(GTE_LOG) || defined(PAD_LOG)
	emuLog = fopen("sd:/wiisxrx/emu.log", "w");
#endif
	Config.Cpu = dynacore;  //cpu may have changed
	psxInit();
	LoadPlugins();
	if(OpenPlugins() < 0)
		return -1;

	return 0;
}

extern void pl_timing_prepare(int is_pal_);
int g_emu_resetting;

void SysReset() {
    g_emu_resetting = 1;
	// reset can run code, timing must be set
	pl_timing_prepare(Config.PsxType);

	psxReset();

	g_emu_resetting = 0;
}

void SysStartCPU() {
	go();
}

void SysClose()
{
	psxShutdown();
	ClosePlugins();
	ReleasePlugins();
#if defined (CPU_LOG) || defined(DMA_LOG) || defined(CDR_LOG) || defined(HW_LOG) || \
	defined(PSXBIOS_LOG) || defined(GTE_LOG) || defined(PAD_LOG)
	if (emuLog != NULL) fclose(emuLog);
#endif
}

void SysPrintf(const char *fmt, ...)
{
#ifdef PRINTGECKO
	va_list list;
	char msg[512];

	va_start(list, fmt);
	vsprintf(msg, fmt, list);
	va_end(list);

	//if (Config.PsxOut) printf ("%s", msg);
	DEBUG_print(msg, DBG_USBGECKO);
#if defined (CPU_LOG) || defined(DMA_LOG) || defined(CDR_LOG) || defined(HW_LOG) || \
	defined(PSXBIOS_LOG) || defined(GTE_LOG) || defined(PAD_LOG)
	fprintf(emuLog, "%s", msg);
#endif
#endif
}

void *SysLoadLibrary(char *lib)
{
	int i;
	for(i=0; i<NUM_PLUGINS; i++)
		if((plugins[i].lib != NULL) && (!strcmp(lib, plugins[i].lib)))
			return (void*)i;
	return NULL;
}

void *SysLoadSym(void *lib, char *sym)
{
	PluginTable* plugin = plugins + (int)lib;
	int i;
	for(i=0; i<plugin->numSyms; i++)
		if(plugin->syms[i].sym && !strcmp(sym, plugin->syms[i].sym))
			return plugin->syms[i].pntr;
	return NULL;
}

int framesdone = 0;
void SysUpdate()
{
	framesdone++;
	// reamed hack
	if (gpuPtr == &newSoftGpu)
    {
		extern void pl_frame_limit(void);
		// Contains the FrameCap() spin-wait. This runs from the VBlank rcnt
		// callback, i.e. inside a CPU slice, so it is timed separately or it
		// shows up in the profile as scheduler cost.
		unsigned long long limit_t0 = perf_now_ticks();
		pl_frame_limit();
		PERF_ADD(limit_ticks, perf_now_ticks() - limit_t0);
		PERF_INC(limit_calls);
	}
#ifdef PROFILE
	refresh_stat();
#endif
}

void SysRunGui() {}
void SysMessage(char *fmt, ...) {}
void SysCloseLibrary(void *lib) {}
char *SysLibError(void) {	return NULL; }

} //extern "C"
