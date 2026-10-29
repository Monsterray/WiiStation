/* The rule for a progress pop-up: it shows only when measured progress crosses a new 10 % step
 * (10 %, 20 % ... of the target), never for the first reading after a game load, a disc swap, a
 * reset or a state load. That reading is the baseline: at boot the game's memory is not set up
 * yet, and a number that jumps there says nothing about the player. Plain C, host-tested
 * (tests/ach_progress_test.c); the Local engine keeps one step per achievement, Official mode a
 * small table of them (ach_steps_t). */
#ifndef ACH_PROGRESS_H
#define ACH_PROGRESS_H

#include <stddef.h>

#define ACH_STEP_NONE 0xFFu   /* no reading yet since the load, reset or state load */
#define ACH_SETTLE_FRAMES 180 /* after a load, reset, disc swap or state load: 3 s of readings that only set the baseline (the game is setting up its memory) */

/* The step of a percent: 0 for 0-9 %, 1 for 10-19 % ... 10 for 100 % */
static inline unsigned char ach_step_of(unsigned percent) { return percent >= 100 ? 10 : (unsigned char)(percent / 10); }

/* A new reading: 1 when its pop-up is due (the step is above the last one seen). The step
 * kept is always the latest, so progress that falls back and climbs again shows again, and
 * the first reading only sets the baseline. */
static inline int ach_step_update(unsigned char *last, unsigned percent)
{
	unsigned char step = ach_step_of(percent);
	int due = *last != ACH_STEP_NONE && step > *last;
	*last = step;
	return due;
}

/* Official mode: the step of each achievement that has reported progress. The table is a ring:
 * past ACH_STEPS entries the oldest is forgotten, and an achievement forgotten starts at a new
 * baseline (a pop-up is missed once; never one shown by mistake). */
#define ACH_STEPS 128
typedef struct {
	unsigned id[ACH_STEPS];
	unsigned char step[ACH_STEPS];
	unsigned n, next;
} ach_steps_t;

static inline void ach_steps_reset(ach_steps_t *t) { t->n = t->next = 0; }

static inline int ach_steps_update(ach_steps_t *t, unsigned id, unsigned percent)
{
	unsigned i;
	for (i = 0; i < t->n; i++)
		if (t->id[i] == id)
			return ach_step_update(&t->step[i], percent);
	i = t->next;
	t->next = (t->next + 1) % ACH_STEPS;
	if (t->n < ACH_STEPS) t->n++;
	t->id[i] = id;
	t->step[i] = ACH_STEP_NONE;
	return ach_step_update(&t->step[i], percent);
}

#endif
