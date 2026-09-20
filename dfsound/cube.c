//cube.c -- audio output through the Wii's DSP, via libogc's AESND
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

/* The alternative to the SDL driver (dfsound/sdl.c), selected by the SoundHwAccel
 * setting. Both receive the same thing from the SPU emulator: a signed 16-bit stereo
 * stream at the PlayStation's own 44100 Hz. The difference is what happens next.
 *
 *   SDL driver:   the CPU resamples 44100 -> 48000 in the audio callback, then libSDL
 *                 hands the result to the audio interface's DMA.
 *   this driver:  the stream is handed to an AESND voice AT 44100 Hz, and the DSP's
 *                 microcode does the rate conversion, mixing and volume. The CPU only
 *                 copies bytes.
 *
 * The DSP reads these buffers by DMA and does not see the CPU's cache, so every buffer
 * is 32-byte aligned, a multiple of 32 bytes long, and flushed before it is handed over.
 * Getting any of that wrong is inaudible under an emulator that does not model the cache
 * and produces noise on real hardware.
 */

#include "out.h"
#include "../psxcommon.h"

#include <malloc.h>
#include <string.h>
#include <ogc/cache.h>
#include <aesndlib.h>

#include "../Gamecube/DEBUG.h"

char audioEnabled;          /* the "Audio" setting; also read by the menu */
unsigned int iVolume = 3;
int iDisStereo = 0;

/* Four buffers of 1024 stereo frames: about 23 ms each at 44100 Hz, so at most ~93 ms
 * of latency when the ring is full, and the emulator is throttled (see cube_busy) well
 * before that. Bytes per buffer must stay a multiple of 32 for the DMA. */
#define CUBE_BUFFERS      4
#define CUBE_BUF_FRAMES   1024
#define CUBE_BUF_BYTES    (CUBE_BUF_FRAMES * 4)     /* stereo, 16-bit */
#define CUBE_BUSY_BUFFERS 2                         /* keep roughly this much queued */

static AESNDPB *voice = NULL;
static unsigned char *ring[CUBE_BUFFERS];
static int fill_used;                               /* bytes already in ring[fill] */

/* Single producer (the emulator thread) and single consumer (the AESND callback, which
 * runs at interrupt time). Each side only ever writes its own counter, so neither needs
 * a lock: a 32-bit aligned store is atomic on this CPU. queued = filled - played. */
static volatile unsigned int filled = 0, played = 0;

static unsigned int cube_queued(void)
{
    return filled - played;
}

void SetVolume(void)
{
    /* iVolume is a leftover: nothing sets it. The menu's "volume" control actually toggles
     * SPU interpolation (Func_VolumeToggle in SettingsFrame.cpp), and the CPU output path
     * ignores iVolume entirely and always plays at full scale. The old mapping here turned
     * its default of 3 into 127 of 255, which measured as this path being 6.1 dB quieter
     * than the one it replaces. Full scale at the default; only a deliberately lower
     * setting attenuates, should anything ever set one. */
    u16 volume = 255;
    if (iVolume > 3) volume = (u16)(255u >> (iVolume - 3));
    if (voice) AESND_SetVoiceVolume(voice, volume, volume);
}

static void cube_callback(AESNDPB *pb, u32 state)
{
    if (state != VOICE_STATE_STREAM)
        return;

    if (filled != played) {
        AESND_SetVoiceBuffer(pb, ring[played % CUBE_BUFFERS], CUBE_BUF_BYTES);
        played++;
    }
    /* Nothing queued: leave the voice alone. AESND repeats the last buffer rather than
     * clicking, and the emulator's throttle (cube_busy) will catch up. */
}

static int cube_init(void)
{
    int i;

    AESND_Init();               /* idempotent in libogc; also starts the DSP microcode */

    for (i = 0; i < CUBE_BUFFERS; i++) {
        if (ring[i] == NULL) {
            ring[i] = (unsigned char *)memalign(32, CUBE_BUF_BYTES);
            if (ring[i] == NULL)
                return -1;
        }
        memset(ring[i], 0, CUBE_BUF_BYTES);
        DCFlushRange(ring[i], CUBE_BUF_BYTES);
    }
    filled = played = 0;
    fill_used = 0;

    voice = AESND_AllocateVoice(cube_callback);
    if (voice == NULL)
        return -1;

    AESND_SetVoiceFormat(voice, iDisStereo ? VOICE_MONO16 : VOICE_STEREO16);
    AESND_SetVoiceFrequency(voice, PS_SPU_FREQ);   /* the DSP resamples to 48 kHz */
    SetVolume();
    AESND_SetVoiceStream(voice, true);
    AESND_SetVoiceStop(voice, false);
    return 0;
}

static void cube_finish(void)
{
    if (voice) {
        AESND_SetVoiceStop(voice, true);
        AESND_FreeVoice(voice);
        voice = NULL;
    }
    filled = played = 0;
    fill_used = 0;
}

/* Non-zero means "enough audio is queued, stop producing". Returning zero makes the SPU
 * emulator generate more samples to catch up (see DF_SPUasync). */
static int cube_busy(void)
{
    if (voice == NULL) return 1;
    return cube_queued() >= CUBE_BUSY_BUFFERS;
}

static int cube_feed(void *data, int bytes)
{
    const unsigned char *src = (const unsigned char *)data;

    if (!audioEnabled || voice == NULL) return 0;

    while (bytes > 0) {
        unsigned char *dst;
        int space, chunk;

        /* Ring full: drop the excess rather than overwrite a buffer the DSP may be
         * reading. The throttle above normally prevents this. */
        if (cube_queued() >= CUBE_BUFFERS)
            break;

        dst   = ring[filled % CUBE_BUFFERS];
        space = CUBE_BUF_BYTES - fill_used;
        chunk = bytes < space ? bytes : space;

        memcpy(dst + fill_used, src, chunk);
        fill_used += chunk;
        src       += chunk;
        bytes     -= chunk;

        if (fill_used == CUBE_BUF_BYTES) {
            /* The DSP fetches this by DMA and never sees the cache. */
            DCFlushRange(dst, CUBE_BUF_BYTES);
            fill_used = 0;
            filled++;       /* publish only after the flush */
        }
    }

    return 0;
}

void pauseAudio(void)
{
    if (voice) AESND_SetVoiceStop(voice, true);
}

void resumeAudio(void)
{
    if (voice) AESND_SetVoiceStop(voice, false);
}

void out_register_cube(struct out_driver *drv)
{
    drv->name   = "cube";
    drv->init   = cube_init;
    drv->finish = cube_finish;
    drv->busy   = cube_busy;
    drv->feed   = cube_feed;
}
