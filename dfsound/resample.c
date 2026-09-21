/* The output-stage rate conversion, 44100 -> 48000 Hz.
 *
 * The SPU emulator mixes at the PlayStation's 44100 Hz and the Wii's audio interface runs
 * at 48000, so every sample is converted once on the way out. Until this file existed that
 * conversion was sample-and-hold in both output paths: the SDL callback repeated the last
 * input sample until its 16.16 accumulator overflowed, and the AESND microcode does exactly
 * the same on the DSP (libogc2 libaesnd/dspcode/dspmixer.s). Hold is the crudest option:
 * for this ratio its images land a few kHz from every partial, audible as harshness on
 * high-frequency content.
 *
 * SoundResampler (Settings -> Audio -> Advanced) picks the interpolation used here:
 *   Hold    the output as it always was, so it is the A of any A/B test. On the DSP path
 *           this mode hands the 44100 Hz stream to the DSP and lets the microcode hold, as
 *           before, so "DSP Sound + Hold" costs the CPU nothing (see resample_active);
 *   Linear  first-order interpolation between neighbouring input samples;
 *   Cubic   4-point Catmull-Rom (Hermite) interpolation.
 * Linear and Cubic run here on the CPU and deliver 48000 Hz to whichever driver is
 * active; the DSP path then sets its voice to 48000 so the microcode passes the stream
 * through 1:1 (cube.c).
 *
 * Why the mixer is not simply run at 48000 instead: the SPU's timing is defined in 44100 Hz
 * ticks (768 CPU cycles each): the ADSR rate tables, the reverb, whose delay lines are
 * addressed by game-written offsets that assume 22050 Hz ticks, the IRQ prediction and the
 * capture buffers all step per tick. Changing the mix rate changes emulated behaviour;
 * converting the finished mix does not.
 *
 * One instance, one stream: the mixer is the only producer, and it calls from one thread.
 * All arithmetic is 32-bit integer and every intermediate is bounded (see the cubic).
 */
#include <string.h>
#include "resample.h"
#include "out.h"
#include "../psxcommon.h"                 /* PS_SPU_FREQ, WII_SPU_FREQ, SINC */
#include "../Gamecube/wiiSXconfig.h"      /* soundResampler and its values */
#include "../Gamecube/perf_prof.h"        /* out_ticks: the cost of this stage, debug builds */

/* The last four input frames, oldest first. An output sample lies between hist[1] and
 * hist[2]; hist[0] and hist[3] are the outer points the cubic needs. phase is the position
 * between hist[1] and hist[2] in 16.16; it advances by SINC (44100/48000) per output. */
static short hist[4][2];
static unsigned int phase;

/* The mixer hands over up to a frame's worth of audio at a time (a few thousand frames);
 * it is converted in chunks through this buffer rather than one the size of the worst
 * case. 512 inputs make at most 558 outputs at this ratio. */
#define CHUNK_IN   512
#define CHUNK_OUT  576
static short outbuf[CHUNK_OUT * 2] __attribute__((aligned(32)));

void resample_reset(void)
{
    memset(hist, 0, sizeof(hist));
    phase = 0;
}

int resample_active(void)
{
    if (soundResampler == SOUND_RESAMPLE_HOLD && out_current && out_current->hold_in_hw)
        return 0;
    return 1;
}

static inline short clamp16(int v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (short)v;
}

/* One output sample from the four surrounding inputs, t = 16.16 position in [0, 1). */
static inline short interp(int mode, int x0, int x1, int x2, int x3, unsigned int t)
{
    switch (mode) {
    default:
    case SOUND_RESAMPLE_HOLD:
        return (short)x1;

    case SOUND_RESAMPLE_LINEAR: {
        /* (x2 - x1) is 17 bits signed; a 14-bit t keeps the product inside 32 bits. */
        int t14 = (int)(t >> 2);
        return (short)(x1 + (((x2 - x1) * t14) >> 14));
    }

    case SOUND_RESAMPLE_CUBIC: {
        /* Catmull-Rom:  y = x1 + t/2 * (c + t * (b + t * a))
         *   a = -x0 + 3x1 - 3x2 + x3   (|a| <= 2^18)
         *   b = 2x0 - 5x1 + 4x2 - x3   (|b| <= 3 * 2^17)
         *   c = x2 - x0                (|c| <= 2^16)
         * Evaluated by Horner with an 11-bit t so that every product stays below 2^31:
         * the largest is (|a| + |b| + |c|) * 2047 < 1.5e9. The final shift is 12, not 11,
         * for the 1/2. An 11-bit fraction places the sample to within 11 ns, far below
         * anything audible. The curve can overshoot by a few percent, hence the clamp. */
        int t11 = (int)(t >> 5);
        int a = -x0 + 3 * x1 - 3 * x2 + x3;
        int b = 2 * x0 - 5 * x1 + 4 * x2 - x3;
        int c = x2 - x0;
        int y = ((a * t11) >> 11) + b;
        y = ((y * t11) >> 11) + c;
        y = (y * t11) >> 12;
        return clamp16(x1 + y);
    }
    }
}

/* Convert in_frames input frames, appending to out; returns output frames written. The
 * history carries across calls so the stream is seamless. */
static int run(const short *in, int in_frames, short *out, int max_out, int mode)
{
    int n = 0;

    for (; in_frames > 0; in_frames--, in += 2) {
        hist[0][0] = hist[1][0]; hist[0][1] = hist[1][1];
        hist[1][0] = hist[2][0]; hist[1][1] = hist[2][1];
        hist[2][0] = hist[3][0]; hist[2][1] = hist[3][1];
        hist[3][0] = in[0];      hist[3][1] = in[1];

        while (phase < 0x10000 && n < max_out) {
            out[2 * n]     = interp(mode, hist[0][0], hist[1][0], hist[2][0], hist[3][0], phase);
            out[2 * n + 1] = interp(mode, hist[0][1], hist[1][1], hist[2][1], hist[3][1], phase);
            n++;
            phase += SINC;
        }
        phase -= 0x10000;
    }
    return n;
}

void resample_feed(const short *in, int frames)
{
    unsigned long long t0;
    int mode;

    if (!resample_active()) {
        out_current->feed((void *)in, frames * 4);
        return;
    }

    t0 = perf_now_ticks();
    mode = soundResampler;
    while (frames > 0) {
        int chunk = frames > CHUNK_IN ? CHUNK_IN : frames;
        int n = run(in, chunk, outbuf, CHUNK_OUT, mode);
        out_current->feed(outbuf, n * 4);
        in     += chunk * 2;
        frames -= chunk;
    }
    PERF_ADD(out_ticks, perf_now_ticks() - t0);
    PERF_INC(out_calls);
}
