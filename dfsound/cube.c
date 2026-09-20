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
#include "ratectl.h"
#include "../psxcommon.h"

#include <malloc.h>
#include <string.h>
#include <ogc/cache.h>
#include <aesndlib.h>

#include "../Gamecube/DEBUG.h"
#include "../Gamecube/perf_prof.h"

char audioEnabled;          /* the "Audio" setting; also read by the menu */
unsigned int aesndAttenuation = 3;   /* see aesnd_set_volume() */
int iDisStereo = 0;

/* Each buffer MUST be a whole number of DSP_STREAMBUFFER_SIZE (1152) bytes. AESND's
 * stream mode copies an application buffer into the DSP in 1152-byte chunks and zero-fills
 * whatever a short final chunk lacks (libogc2 aesndlib.c, __aesndfillbuffer), and the DSP
 * plays that padding. The first version of this driver used 4096-byte buffers: 4096 mod
 * 1152 = 640, so every buffer ended in 512 bytes of silence, 128 frames, about 2.7 ms,
 * one hole per buffer, ~1800 of them in 100 s of Spyro, heard as crackle. Halving the
 * buffers doubled the holes, which is what gave it away. Measured 2026-09-20 in Dolphin
 * (DSP LLE, audio dump; scripts/wav_compare.py counts the holes).
 *
 * Four chunks per buffer (1152 frames, 24 ms at 48000 Hz, 26 at 44100), eight buffers,
 * the emulator throttled (cube_busy) at five queued: about 120 ms in hand, the same depth
 * as the SDL path's ring (125 ms), and headroom for the mixer's one-burst-per-frame
 * delivery. 1152 is also a multiple of 32, as the cache flush needs. */
#define CUBE_BUF_BYTES    (DSP_STREAMBUFFER_SIZE * 4)
#define CUBE_BUF_FRAMES   (CUBE_BUF_BYTES / 4)      /* stereo, 16-bit */
#define CUBE_BUFFERS      8
#define CUBE_BUSY_BUFFERS 5                         /* keep roughly this much queued */
typedef char cube_buf_is_whole_chunks[(CUBE_BUF_BYTES % DSP_STREAMBUFFER_SIZE) == 0 && (CUBE_BUF_BYTES % 32) == 0 ? 1 : -1];

/* One AESND request is a 2 ms DSP frame: 96 output frames at 48000 Hz. */
#define CUBE_DSP_FRAME_OUT 96

/* Dynamic rate control (ratectl.c): cube_feed keeps the ring near CUBE_BUSY_BUFFERS by
 * moving the voice frequency a fraction of a percent either side of 44100 Hz, once per
 * feed (the SPU feeds about 16 times per emulated frame, ~46 frames each), from the ring's
 * occupancy. Until the ring has first reached that depth after init the callback hands the
 * DSP nothing, which it plays as silence: the loop starts at its operating point instead
 * of on the edge of empty. */
#define CUBE_TARGET_FRAMES (CUBE_BUSY_BUFFERS * CUBE_BUF_FRAMES)
static ratectl_t rate;
static int primed;                                  /* the ring has reached its target since init */
static u32 voice_rate;                              /* what the voice is set to play at */

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

static void aesnd_set_volume(void)
{
    /* A leftover knob: nothing sets aesndAttenuation, there is no menu control for it (the
     * one that used to be called "volume" actually toggles SPU interpolation), and the CPU
     * output path has no volume control at all and always plays at full scale. The old
     * mapping turned the default of 3 into 127 of 255, which measured as this path being
     * 6.1 dB quieter than the one it replaces. Full scale at the default; larger values
     * attenuate, should anything ever set one. */
    u16 volume = 255;
    if (aesndAttenuation > 3) volume = (u16)(255u >> (aesndAttenuation - 3));
    if (voice) AESND_SetVoiceVolume(voice, volume, volume);
}

static void cube_callback(AESNDPB *pb, u32 state)
{
    if (state != VOICE_STATE_STREAM)
        return;

    if (!primed) {
        if (filled - played < CUBE_BUSY_BUFFERS) {
            PERF_ADD(rate_prefill, CUBE_DSP_FRAME_OUT);
            return;
        }
        primed = 1;
    }

    if (filled != played) {
        AESND_SetVoiceBuffer(pb, ring[played % CUBE_BUFFERS], CUBE_BUF_BYTES);
        played++;
    }
    else
        PERF_ADD(out_dry, CUBE_DSP_FRAME_OUT);
    /* Nothing queued: hand the DSP nothing. AESND then asks again every 2 ms frame and plays
     * silence until a buffer arrives (so this counts output frames per request, like sdl.c;
     * before 2026-09-20 it added a whole buffer per request and read 12x too high). */
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
    primed = 0;
    ratectl_init(&rate, CUBE_TARGET_FRAMES, RATECTL_CUBE);

    voice = AESND_AllocateVoice(cube_callback);
    if (voice == NULL)
        return -1;

    AESND_SetVoiceFormat(voice, iDisStereo ? VOICE_MONO16 : VOICE_STEREO16);
    voice_rate = PS_SPU_FREQ;
    AESND_SetVoiceFrequency(voice, voice_rate);    /* the DSP resamples to 48 kHz; cube_feed nudges this */
    aesnd_set_volume();
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
    const int fed = bytes / 4;     /* frames this feed covers, for the rate control */

    if (voice == NULL) return 0;   /* the audioEnabled gate lives in DF_SPUasync */

    while (bytes > 0) {
        unsigned char *dst;
        int space, chunk;

        /* Ring full: drop the excess rather than overwrite a buffer the DSP may be
         * reading. The throttle above normally prevents this. */
        if (cube_queued() >= CUBE_BUFFERS) {
            PERF_INC(out_drop);
            break;
        }

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

    /* Rate control, once per feed. `played` is written at interrupt time; a 32-bit read of
     * it is atomic and a value one request stale is fine for a loop this slow. The
     * frequency write masks interrupts and takes effect at the next 2 ms DSP frame; the
     * microcode's 16.16 accumulator just gets a new increment, nothing in the output jumps. */
    {
        int queued = (int)cube_queued() * CUBE_BUF_FRAMES + fill_used / 4;
        int ppm    = ratectl_update(&rate, queued, fed);
        u32 hz     = (u32)(((u64)PS_SPU_FREQ * (u32)(1000000 + ppm)) / 1000000u);
        if (hz != voice_rate) {
            AESND_SetVoiceFrequency(voice, hz);
            voice_rate = hz;
            PERF_INC(rate_changes);
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
