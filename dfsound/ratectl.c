/* ratectl.c -- dynamic rate control for the sound output stage
 *
 * The SPU emulator mixes exactly as much audio as emulated time has passed, and the
 * frame limiter (SoftGPU/oldGpuFps.c FrameCap) keeps emulated time close to wall time.
 * Close is not equal: the limiter runs on a 100 us tick and targets a frame rate a little
 * different from the emulated vblank's, the Wii's audio clock is its own crystal, and the
 * output drivers play at a fixed 48000 Hz. Left alone, the drivers' queues fill or drain
 * at the difference and eventually overflow or starve.
 *
 * Until 2026-09-20 dfspu.c bridged that with a "tempo pull-back": whenever the driver ran
 * low, the mixer's clock was set back half a frame so it produced extra audio. That kept
 * the driver fed, but it also pushed the SPU's state ahead of the emulated clock, and the
 * CD-XA stream, decoded on the emulated clock, was consumed faster than the disc delivered
 * it: one-sector (53 ms) silent gaps in streamed speech, 61 of them in 110 s in a slow
 * scene (the diagnostics skill's case study 7 has the measurements).
 *
 * This is the replacement, the way RetroArch's dynamic rate control and DuckStation's
 * host stream do it: leave the mixer strictly on emulated time and absorb the drift at the
 * output stage by nudging the playback rate by a fraction of a percent, from the queue's
 * occupancy. The CPU driver (sdl.c) scales its 44100 -> 48000 step, the DSP driver
 * (cube.c) sets the AESND voice frequency. A drift of a few thousand ppm is inaudible as
 * pitch; what would be audible is the nudge *changing* quickly, so the applied value is
 * slew-limited.
 *
 * Controller: PI on the occupancy error, normalised so that an empty queue is -MAX and a
 * queue at twice the target is +MAX. The proportional term does the work (RetroArch is
 * P-only); the slow integral term, active only near the operating point, takes out the
 * systematic drift so the queue sits at the target instead of a little off it. Both are
 * clamped, the sum is clamped, then slewed. Integer arithmetic throughout: sdl.c calls
 * this from the SDL audio thread. Every time constant is in audio frames, so a driver's
 * call cadence does not matter (the SDL callback comes ~47 times a second, the DSP feed
 * ~16 times per emulated frame; a first version counted updates and got the DSP path's
 * integral 16x too fast).
 *
 * What this cannot do: recover from a stall. If the core stops for 200 ms (a disc read
 * from the SD card), the queue drains by 200 ms and a +-0.5 % rate cannot rebuild it in
 * any useful time. That is the frame limiter's job: FrameCap now carries a bounded debt,
 * so after a stall the core runs unthrottled until emulated time has caught up, and the
 * queue refills with audio that is consistent with the emulated clock (XA included).
 * Two things a driver needs on its side: a start threshold, so playback begins with the
 * queue at the target rather than empty, and a target near half its capacity, so there is
 * room in both directions.
 */

#include <string.h>
#include "ratectl.h"
#include "../Gamecube/perf_prof.h"

extern char soundRateControl;   /* the SoundRateControl setting (Gamecube/GamecubeMain.cpp) */

#define RC_HZ          44100    /* the frames dt_frames counts */
#define RC_I_MAX_PPM   2500     /* the integral term alone may hold this much */
#define RC_I_TAU_S     30       /* integral time constant, seconds */
#define RC_I_WINDOW    (RATECTL_MAX_PPM / 2)  /* integrate only while |error| is below this: a
                                                 stall is not drift, and must not wind it up */
#define RC_SLEW_PPM_S  3000     /* the applied nudge moves at most this fast */
#define RC_EMA_FRAMES  (RC_HZ / 8)  /* occupancy smoothing time constant: 125 ms of audio */

void ratectl_init(ratectl_t *c, int target_frames, int id)
{
	memset(c, 0, sizeof(*c));
	c->target = target_frames > 0 ? target_frames : 1;
	c->id     = id;
}

int ratectl_update(ratectl_t *c, int queued_frames, int dt_frames)
{
	int e, i, u, slew, sat = 0;
	long long lim;

	if (!soundRateControl) {
		c->ppm = 0; c->acc = 0; c->primed = 0;
		return 0;
	}
	if (queued_frames < 0) queued_frames = 0;
	if (dt_frames < 1) dt_frames = 1;
	if (dt_frames > RC_HZ) dt_frames = RC_HZ;

	/* first-order smoothing with a fixed time constant: alpha = dt / (dt + tau) */
	if (!c->primed) { c->occ16 = queued_frames << 4; c->primed = 1; }
	else            c->occ16 += (int)(((long long)((queued_frames << 4) - c->occ16) * dt_frames) / (dt_frames + RC_EMA_FRAMES));

	/* error, already scaled to ppm of authority */
	e = (int)(((long long)(c->occ16 - (c->target << 4)) * RATECTL_MAX_PPM) / (c->target << 4));
	if (e >  RATECTL_MAX_PPM) e =  RATECTL_MAX_PPM;
	if (e < -RATECTL_MAX_PPM) e = -RATECTL_MAX_PPM;

	/* integral: i = acc / (tau in frames), acc in ppm x frames */
	lim = (long long)RC_I_MAX_PPM * RC_HZ * RC_I_TAU_S;
	if (e > -RC_I_WINDOW && e < RC_I_WINDOW) c->acc += (long long)e * dt_frames;
	if (c->acc >  lim) c->acc =  lim;
	if (c->acc < -lim) c->acc = -lim;
	i = (int)(c->acc / ((long long)RC_HZ * RC_I_TAU_S));

	u = e + i;
	if (u >  RATECTL_MAX_PPM) { u =  RATECTL_MAX_PPM; sat = 1; }
	if (u < -RATECTL_MAX_PPM) { u = -RATECTL_MAX_PPM; sat = 1; }

	slew = (int)(((long long)RC_SLEW_PPM_S * dt_frames) / RC_HZ);
	if (slew < 1) slew = 1;
	if      (u > c->ppm + slew) u = c->ppm + slew;
	else if (u < c->ppm - slew) u = c->ppm - slew;
	c->ppm = u;
	c->n++;

#ifdef PERF_PROF
	if (g_perf.rate_updates == 0 || u < g_perf.rate_ppm_min) g_perf.rate_ppm_min = u;
	if (g_perf.rate_updates == 0 || u > g_perf.rate_ppm_max) g_perf.rate_ppm_max = u;
	if (g_perf.rate_updates == 0 || (unsigned)queued_frames < g_perf.rate_occ_min) g_perf.rate_occ_min = queued_frames;
	if ((unsigned)queued_frames > g_perf.rate_occ_max) g_perf.rate_occ_max = queued_frames;
	g_perf.rate_occ_sum += (unsigned)queued_frames;
	g_perf.rate_updates++;
	g_perf.rate_sat += sat;
	g_perf.rate_ppm_now = u;
	g_perf.rate_i_now = i;
	/* timeline: a = queued frames, b = ppm, c = driver id | saturated << 1 | integral term << 2 */
	if ((c->n & 15) == 0)
		perf_audio_event('D', 0, queued_frames, u, (unsigned)c->id | ((unsigned)sat << 1) | ((unsigned)(i + 4096) << 2));
#endif
	return u;
}
