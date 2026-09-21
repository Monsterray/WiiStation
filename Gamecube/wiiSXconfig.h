/**
 * WiiSX - wiiSXconfig.h
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 *
 * External declaration and enumeration of config variables
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


#ifndef WIISXCONFIG_H
#define WIISXCONFIG_H


extern char audioEnabled;
enum audioEnabled
{
	AUDIO_DISABLE=0,
	AUDIO_ENABLE
};

/* Which sound output path the SPU emulator feeds (dfsound/out.c): the CPU one, which
 * resamples 44100 -> 48000 itself and goes out through SDL, or the DSP one, which hands
 * the stream to an AESND voice at 44100 and lets the DSP microcode convert it. */
extern char soundHwAccel;
enum soundHwAccel
{
	SOUND_HW_ACCEL_OFF=0,
	SOUND_HW_ACCEL_ON
};

/* dfsound's legacy tempo pull-back (DF_SPUasync): when the output driver runs low, generate
 * extra audio so it never starves. Keeps sound continuous when the core is slower than real
 * time, at the price of the SPU consuming CD-XA faster than the drive delivers it. Off by
 * default since the rate control below replaced it. */
extern char soundTempo;
enum soundTempo
{
	SOUND_TEMPO_OFF=0,
	SOUND_TEMPO_ON
};

/* dfsound/dfspu.c do_samples: whether the SPU's stereo reverb bus (reverb.c) is mixed in.
 * Was hardcoded on; a setting mainly so it can be turned off as an A/B check on a game's
 * reverb effect (echo, hall, room) without also touching anything else. */
extern char soundReverb;
enum soundReverb
{
	SOUND_REVERB_OFF=0,
	SOUND_REVERB_ON
};

/* dfsound/dfspu.c mix_chan/mix_chan_rvb/do_samples_finish: round each per-voice and final
 * volume scale-down to the nearest value instead of truncating (a plain C `>>14` on a
 * signed value always rounds toward -infinity). Halves the average quantisation error at
 * each of those steps; the value's scale is unchanged, so nothing downstream (MixCD,
 * REVERBDo) needs to know or care. Default on: this is pure software bookkeeping with no
 * real-hardware behaviour to be faithful to, so more precision here is free fidelity. */
extern char soundMixerPrecision;
enum soundMixerPrecision
{
	SOUND_MIXER_PRECISION_LEGACY=0,
	SOUND_MIXER_PRECISION_HIFI
};

/* dfsound/xa.c FeedXA: how 37800 Hz stereo/mono CD-XA is stepped up to 44100 Hz. Legacy is
 * nearest-sample or the 4-tap Gaussian FeedXA always had (shared with Interpolation).
 * HiFi replaces it, for 37800 Hz streams only, with the real hardware's own 7-phase,
 * 29-tap "zigzag" FIR (coefficients from DuckStation's ResampleXAADPCM, itself derived from
 * the real chip's behaviour) -- an exact 6-in/7-out ratio (37800*7/6 = 44100). 18900 Hz
 * streams (rare) always use the legacy path regardless of this setting; the real hardware's
 * filter for that rate uses a different, separately-sourced table not included here. */
extern char soundXaResampler;
enum soundXaResampler
{
	SOUND_XA_RESAMPLER_LEGACY=0,
	SOUND_XA_RESAMPLER_HIFI
};

/* dfsound/ratectl.c: keep the output driver's queue at its target by nudging the playback
 * rate by at most +-0.5 %, from the queue's occupancy. The mixer stays on emulated time. */
extern char soundRateControl;
enum soundRateControl
{
	SOUND_RATE_CONTROL_OFF=0,
	SOUND_RATE_CONTROL_ON
};

/* How the mixer's 44100 Hz output is converted to the audio interface's 48000 Hz
 * (dfsound/resample.c). Hold is what both paths always did; the other two interpolate on
 * the CPU. Settings -> Audio -> Advanced. */
extern char soundResampler;
enum soundResampler
{
	SOUND_RESAMPLE_HOLD=0,
	SOUND_RESAMPLE_LINEAR,
	SOUND_RESAMPLE_CUBIC
};

/* Settings-file mirrors of Config.Xa/Config.Cdda. Those are longs inside PCSX's config
 * struct and the settings table writes a char, so they are stored here and copied across
 * at startup, the same way dynacore mirrors Config.Cpu. Without this the two menu toggles
 * worked for the session and were forgotten on the next boot. */
extern char xaDisabled;
extern char cddaDisabled;

enum ConfigXa //Config.Xa
{
	XA_ENABLE=0,
	XA_DISABLE
};

enum ConfigCdda //Config.Cdda
{
	CDDA_ENABLE=0,
	CDDA_DISABLE
};

/* Storage (cdriso.c). All three take effect when the next game is loaded. */
extern char cdBuffer;            /* stdio buffer per image handle */
enum cdBuffer
{
	CD_BUFFER_16K=0,
	CD_BUFFER_64K,
	CD_BUFFER_256K
};
extern char cdPrefetch;          /* read-ahead thread for raw images */
enum cdPrefetch
{
	CD_PREFETCH_OFF=0,
	CD_PREFETCH_ON
};
extern char cdChdHunks;          /* CHD decoded-hunk cache depth */
enum cdChdHunks
{
	CD_CHD_HUNKS_2=0,
	CD_CHD_HUNKS_4,
	CD_CHD_HUNKS_8
};

extern char spuInterpolation;
enum spuInterpolationEnum
{
	SIMPLE_INTERPOLATION=1,
	GAUSSI_INTERPOLATION
};

extern char showFPSonScreen;
enum showFPSonScreen
{
	FPS_HIDE=0,
	FPS_SHOW
};

extern char printToScreen;
enum printToScreen
{
	DEBUG_HIDE=0,
	DEBUG_SHOW
};

extern char printToSD;
enum printToSD
{
	SDLOG_DISABLE=0,
	SDLOG_ENABLE
};

extern char frameLimit[2];
enum frameLimit
{
	FRAMELIMIT_NONE=0,
	FRAMELIMIT_AUTO
};

extern char frameSkip;
enum frameSkip
{
	FRAMESKIP_DISABLE=0,
	FRAMESKIP_ENABLE
};

extern int iUseDither;
extern char useDithering;
enum iUseDither
{
	USEDITHER_NONE=0,
	USEDITHER_DEFAULT,
	USEDITHER_ALWAYS
};

extern char saveEnabled;	//???

extern char nativeSaveDevice;
enum nativeSaveDevice
{
	NATIVESAVEDEVICE_NONE=-1,
	NATIVESAVEDEVICE_SD,
	NATIVESAVEDEVICE_USB,
	NATIVESAVEDEVICE_CARDA,
	NATIVESAVEDEVICE_CARDB
};

extern char saveStateDevice;
enum saveStateDevice
{
	SAVESTATEDEVICE_SD=0,
	SAVESTATEDEVICE_USB
};

extern char autoSave;
enum autoSave
{
	AUTOSAVE_DISABLE=0,
	AUTOSAVE_ENABLE
};

extern char creditsScrolling;	//deprecated?

extern char dynacore;
enum dynacore
{

	DYNACORE_DYNAREC=0, // Lightrec
	DYNACORE_INTERPRETER, // Interpreter
	DYNACORE_DYNAREC_OLD // 'new' PPC dynarec
};

extern char biosDevice;
enum biosDevice
{
	BIOSDEVICE_HLE=0,
	BIOSDEVICE_SD,
	BIOSDEVICE_USB
};

extern char LoadCdBios;
enum loadCdBios
{
	BOOTTHRUBIOS_NO=0,
	BOOTTHRUBIOS_YES
};

extern char screenMode;
enum screenMode
{
	SCREENMODE_4x3=0,
	SCREENMODE_16x9,
	SCREENMODE_16x9_PILLARBOX
};

extern char videoMode;
enum videoMode
{
	VIDEOMODE_AUTO=0,
	VIDEOMODE_NTSC,
	VIDEOMODE_PAL50,
	VIDEOMODE_PAL60,
	VIDEOMODE_PROGRESSIVE
};

extern char fileSortMode;
enum fileSortMode
{
	FILESORT_DIRS_MIXED=0,
	FILESORT_DIRS_FIRST
};

extern char padAutoAssign;
enum padAutoAssign
{
	PADAUTOASSIGN_MANUAL=0,
	PADAUTOASSIGN_AUTOMATIC
};

extern char padType[10];
extern char menuFont[];          // menu font file name without .dat (empty = built-in)
enum padType
{
	PADTYPE_NONE=0,
	PADTYPE_GAMECUBE,
	PADTYPE_WII,
	PADTYPE_HID,
	PADTYPE_MULTITAP

};

extern char padAssign[10];
enum padAssign
{
	PADASSIGN_INPUT0=0,
	PADASSIGN_INPUT1,
	PADASSIGN_INPUT0A,
	PADASSIGN_INPUT0B,
	PADASSIGN_INPUT0C,
	PADASSIGN_INPUT0D,
	PADASSIGN_INPUT1A,
	PADASSIGN_INPUT1B,
	PADASSIGN_INPUT1C,
	PADASSIGN_INPUT1D
};

extern char padLightgun[10];
enum padLightgun
{
	PADLIGHTGUN_DISABLE=0,
	PADLIGHTGUN_ENABLE

};

extern char rumbleEnabled;
enum rumbleEnabled
{
	RUMBLE_DISABLE=0,
	RUMBLE_ENABLE
};

extern char loadButtonSlot;
enum loadButtonSlot
{
	LOADBUTTON_SLOT0=0,
	LOADBUTTON_SLOT1,
	LOADBUTTON_SLOT2,
	LOADBUTTON_SLOT3,
	LOADBUTTON_DEFAULT
};

extern char controllerType;
enum controllerType
{
	CONTROLLERTYPE_STANDARD=0,
	CONTROLLERTYPE_ANALOG,
	CONTROLLERTYPE_LIGHTGUN
};

extern char numMultitaps;
enum numMultitaps
{
	MULTITAPS_NONE=0,
	MULTITAPS_ONE,
	MULTITAPS_TWO
};

extern char lang;
enum lang
{
	ENGLISH=0,
	SIMP_CHINESE,
	KOREAN,
	SPANISH,
	PORTUGUESE,
	ITALIAN,
	GERMAN,
	TRAD_CHINESE,
	JAPANESE,
	FRENCH,
	BRAZILIAN_PORTUGUESE,
	CATALAN,
	TURKISH
};

extern char originalMode;
extern char displayModeChanged;
enum originalMode
{
	ORIGINALMODE_DISABLE=0,
	ORIGINALMODE_ENABLE
};

extern char bilinearFilter;
enum bilinearFilter
{
	BILINEARFILTER_DISABLE=0,
	BILINEARFILTER_NEAR,
	BILINEARFILTER_ENABLE
};

extern char trapFilter;
enum trapFilter
{
	TRAPFILTER_DISABLE=0,
	TRAPFILTER_ENABLE
};

extern char interlacedMode;
enum interlacedMode
{
	INTERLACED_DISABLE=0,
	INTERLACED_ENABLE
};

extern char deflickerFilter;
enum deflickerFilter
{
	DEFLICKER_DISABLE=0,
	DEFLICKER_ENABLE
};

extern char lightGun;
enum lightGun
{
	LIGHTGUN_DISABLE=0,
	LIGHTGUN_GUNCON,
	LIGHTGUN_JUST,
	LIGHTGUN_MOUSE
};

extern char memCard[2];
enum memCard
{
	MEMCARD_DISABLE=0,
	MEMCARD_ENABLE
};

/* Which file each memory card lives in. PER_GAME gives every game its own card, which is
 * what a real console owner does with one card per game. SHARED gives one card that every
 * game sees, which is how a real card works when it is moved between games -- and it is
 * the only way a game can read another game's save, which a few of them do.
 * The defaults keep what WiiStation always did: card 1 per game, card 2 shared. */
extern char memCardFile[2];
enum memCardFile
{
	MEMCARDFILE_SHARED=0,
	MEMCARDFILE_PER_GAME
};

extern char forceNTSC;
enum forceNTSC
{
	FORCENTSC_DISABLE=0,
	FORCENTSC_ENABLE
};

extern char gpuPlugin;
enum gpuPlugin
{
	OLD_SOFT=0,
	NEW_SOFT,
	OPEN_GX
};

extern const unsigned char En_dat[];
extern const unsigned int  En_dat_size;

#endif //WIISXCONFIG_H
