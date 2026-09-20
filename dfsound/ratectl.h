#ifndef __P_RATECTL_H__
#define __P_RATECTL_H__

/* Dynamic rate control for the sound output stage. See ratectl.c. One controller per
 * output driver; the driver calls ratectl_update() at a steady cadence with how many
 * 44.1 kHz frames it has queued and applies the returned nudge to its playback rate. */

typedef struct {
	int target;     /* frames the driver wants queued */
	int id;         /* RATECTL_SDL / RATECTL_CUBE, for the probes */
	int occ16;      /* smoothed occupancy, 1/16 frame */
	long long acc;  /* integral term accumulator, ppm x frames */
	int ppm;        /* the nudge currently applied */
	int primed;     /* occ16 has been seeded */
	unsigned n;     /* updates so far */
} ratectl_t;

enum { RATECTL_SDL = 0, RATECTL_CUBE = 1 };

void ratectl_init(ratectl_t *c, int target_frames, int id);

/* Returns the playback-rate nudge in parts per million: positive means play faster
 * (consume the queue), negative slower. Bounded to +-RATECTL_MAX_PPM. dt_frames is how
 * much audio (44.1 kHz frames) this update covers -- what the driver just consumed or was
 * just fed -- so the smoothing, the integral and the slew are per second of audio, not per
 * call, and a driver may call at any cadence. */
int ratectl_update(ratectl_t *c, int queued_frames, int dt_frames);

#define RATECTL_MAX_PPM 5000    /* +-0.5 %: the controller's whole authority */

#endif /* __P_RATECTL_H__ */
