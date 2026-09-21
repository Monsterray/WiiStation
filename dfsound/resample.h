#ifndef __P_RESAMPLE_H__
#define __P_RESAMPLE_H__

/* The output-stage rate conversion, 44100 -> 48000 Hz. See resample.c. */

/* Forget the stream history; call when an output driver is (re)opened. */
void resample_reset(void);

/* Non-zero when the CPU converts to 48000 Hz before the driver sees the stream. Zero
 * means the driver is fed the mixer's own 44100 Hz stream (only the DSP path in Hold
 * mode, where the microcode does the holding itself). */
int resample_active(void);

/* Hand one block of the mixer's 44100 Hz stereo 16-bit output to the current driver,
 * converting on the way when resample_active(). frames = stereo frames. */
void resample_feed(const short *in, int frames);

/* Dynamic rate control (ratectl.c): nudge the conversion ratio by ppm parts per million.
 * Positive plays faster (fewer output frames per input frame, the driver's queue drains),
 * negative slower. The driver that measures its queue calls this; it applies from the
 * next block converted. Ignored while the driver holds in hardware (resample_active() == 0):
 * that driver nudges its own playback rate instead. */
void resample_set_ppm(int ppm);

#endif /* __P_RESAMPLE_H__ */
