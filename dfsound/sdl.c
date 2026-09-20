/* SDL Driver for P.E.Op.S Sound Plugin
 * Copyright (c) 2010, Wei Mingzhi <whistler_wmz@users.sf.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA
 */

#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>
#include "out.h"
#include "../coredebug.h"
#include "../Gamecube/DEBUG.h"
#include "../psxcommon.h"
#include "../Gamecube/perf_prof.h"
#include "ratectl.h"

// Reverted to the original ~250ms: sdl_busy()'s "keep generating more
// samples" heuristic targets BUFFER_SIZE/2 fill as its steady-state
// operating point (not just an emergency-refill threshold), so doubling
// this also doubled the actual audio latency (~125ms -> ~250ms) --
// audible as audio lagging behind video. The frame-limiter fix
// (c19c70f) already removes the speed bursts this was meant to absorb.
#define BUFFER_SIZE        22050
//#define BUFFER_SIZE        12000

// sdl_busy()'s operating point kept as its own literal, deliberately NOT
// derived from BUFFER_SIZE: the capacity (ring-buffer/overflow headroom)
// and the catch-up target (steady-state latency) are two different
// concerns that used to be the same constant (BUFFER_SIZE/2) -- that's
// exactly what caused the latency regression above. If BUFFER_SIZE ever
// needs to grow again for overflow headroom, this stays fixed and the
// operating latency won't silently move with it. Set to match the
// current BUFFER_SIZE/2 (11025) so behavior is unchanged by this split.
#define BUSY_TARGET_SAMPLES 11025

/* Dynamic rate control (ratectl.c): the callback keeps the ring near BUSY_TARGET_SAMPLES
 * by scaling its 44100 -> 48000 step a fraction of a percent up or down, once per callback
 * (libSDL asks for half of spec.samples at a time: 1024 output frames, 21 ms, measured as
 * 47 callbacks a second), from the ring's occupancy. Until the ring has first reached that
 * target after init, the callback plays silence: the loop starts at its operating point
 * instead of on the edge of empty. */
#define SDL_TARGET_FRAMES   (BUSY_TARGET_SAMPLES / 2)
#define SDL_CALLBACK_FRAMES 2048
static ratectl_t rate;
static int primed;                          /* the ring has reached its target since init */
static unsigned int step = SINC;            /* 16.16 input frames per output frame */

/* Ring of 16-bit samples, L/R interleaved, so a frame is two shorts at an even index
 * (BUFFER_SIZE is even). iWritePos is the next index the emulator thread writes,
 * iReadPos the next index the SDL callback reads; equal means empty, and the writer
 * leaves one frame free so full and empty stay distinct. Each side stores only its
 * own index and only after its samples are in place, so no lock is needed.
 *
 * The ring used to be advanced one short at a time and the callback tested
 * `iReadPos != iWritePos` once per output frame before reading two shorts. When the
 * ring ran dry the reader could land one short past the writer, the test then held
 * for a whole lap and the previous 250 ms of audio played again: heard as an echo
 * whenever the core fell behind real time (Spyro speech under Dolphin, SoundTempo=0:
 * the dump's speech jumped by exactly +250 ms every few hundred ms). */
short            *pSndBuffer = NULL;
volatile int    iReadPos = 0, iWritePos = 0;
static int sposTmp = 0x10000L;
static int16_t lastSampleL;
static int16_t lastSampleR;
//extern char audioEnabled;

static void SOUND_FillAudio(void *unused, Uint8 *stream, int len) {
    extern int stop;
    if (stop == 1)
    {
        return;
    }

    int16_t *p = (int16_t *)stream;
    int queued, ppm;

    //len >>= 1;
    len >>= 2;

    queued = iWritePos - iReadPos;          /* shorts waiting to be played */
    if (queued < 0) queued += BUFFER_SIZE;
    queued >>= 1;                           /* frames */
    if (!primed) {
        if (queued < SDL_TARGET_FRAMES) {
            memset(stream, 0, len * 2 * sizeof(int16_t));
            PERF_ADD(rate_prefill, len);
            return;
        }
        primed = 1;
    }
    ppm  = ratectl_update(&rate, queued, (int)(((unsigned)len * SINC) >> 16));   /* dt = input frames this callback consumes */
    step = (unsigned int)(((unsigned long long)SINC * (unsigned)(1000000 + ppm)) / 1000000u);

//    while (iReadPos != iWritePos && len > 0) {
//        *p++ = pSndBuffer[iReadPos++];
//        if (iReadPos >= BUFFER_SIZE) iReadPos = 0;
//        --len;
//    }
    // pitch data from 44100 to 48000
    while (len > 0)
    {
        while (sposTmp >= 0x10000L)
        {
            int rp = iReadPos;
            int queued = iWritePos - rp;
            if (queued < 0) queued += BUFFER_SIZE;
            if (queued < 2)                 /* a whole frame or nothing: never step past the writer */
                goto dry;
            lastSampleL = pSndBuffer[rp];
            lastSampleR = pSndBuffer[rp + 1];
            rp += 2;
            if (rp >= BUFFER_SIZE) rp = 0;
            iReadPos = rp;
            sposTmp -= 0x10000L;
        }

        *p++ = lastSampleL;
        *p++ = lastSampleR;
        sposTmp += step;
        --len;
    }
dry:
    PERF_ADD(out_dry, len);

    // Ring buffer ran dry before satisfying the full request -- SDL does
    // not guarantee `stream` starts zeroed, so without this the tail would
    // play back whatever was previously in that memory (an audible pop).
    // Left un-advanced: sposTmp/lastSample* deliberately aren't touched
    // here, so the resampler picks back up exactly where it left off once
    // real data is available again, instead of skipping ahead over the gap.
    if (len > 0) {
        memset(p, 0, len * 2 * sizeof(int16_t));
    }

    #ifdef SHOW_DEBUG
//    if (len > 0) {
//        sprintf(txtbuffer, "Spu Speed slow %d \n", len * 2);
//        DEBUG_print(txtbuffer, DBG_SPU2);
//    }
    #endif // DISP_DEBUG
}

static void InitSDL() {
    if (SDL_WasInit(SDL_INIT_EVERYTHING)) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    } else {
        SDL_Init(SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE);
    }
}

static void DestroySDL() {
    if (SDL_WasInit(SDL_INIT_EVERYTHING & ~SDL_INIT_AUDIO)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    } else {
        SDL_Quit();
    }
}

static int sdl_init(void) {
    SDL_AudioSpec                spec;

    if (pSndBuffer != NULL) return -1;
    //fill_buffer = play_buffer = 0;

    InitSDL();

    spec.freq = WII_SPU_FREQ;
    spec.format = AUDIO_S16SYS; // AUDIO_S16LSB // //AUDIO_S16MSB; //
    spec.channels = 2;
    spec.samples = SDL_CALLBACK_FRAMES;
    spec.callback = SOUND_FillAudio;

    if (SDL_OpenAudio(&spec, NULL) < 0) {
        DestroySDL();
        return -1;
    }

    pSndBuffer = (short *)malloc(BUFFER_SIZE * sizeof(short));
    if (pSndBuffer == NULL) {
        SDL_CloseAudio();
        return -1;
    }

    iReadPos = 0;
    iWritePos = 0;
    primed = 0;
    step = SINC;
    sposTmp = 0x10000L;
    ratectl_init(&rate, SDL_TARGET_FRAMES, RATECTL_SDL);

    SDL_PauseAudio(0);
    return 0;
}

static void sdl_finish(void) {
    if (pSndBuffer == NULL) return;

    SDL_CloseAudio();
    DestroySDL();

    free(pSndBuffer);
    pSndBuffer = NULL;
}

static int sdl_busy(void) {
    int queued;

    if (pSndBuffer == NULL) return 1;

    queued = iWritePos - iReadPos;          /* shorts waiting to be played */
    if (queued < 0) queued += BUFFER_SIZE;

    return queued > BUSY_TARGET_SAMPLES;    /* enough queued: stop producing */
}

static int sdl_feed(void *pSound, int lBytes) {
    const short *p = (const short *)pSound;
    int wp;

    if (pSndBuffer == NULL) return 0;

    wp = iWritePos;
    while (lBytes >= 2 * (int)sizeof(short)) {
        int next = wp + 2;
        if (next >= BUFFER_SIZE) next = 0;
        if (next == iReadPos) {             /* full: drop the rest; the throttle normally prevents this */
            PERF_INC(out_drop);
            break;
        }
        pSndBuffer[wp]     = p[0];
        pSndBuffer[wp + 1] = p[1];
        p += 2;
        lBytes -= 2 * sizeof(short);
        __asm__ __volatile__("" ::: "memory");   /* the frame lands before the index that publishes it */
        iWritePos = wp = next;
    }

    return 0;
}

void out_register_sdl(struct out_driver *drv)
{
    drv->name = "sdl";
    drv->init = sdl_init;
    drv->finish = sdl_finish;
    drv->busy = sdl_busy;
    drv->feed = sdl_feed;
}
