#ifndef __P_OUT_H__
#define __P_OUT_H__

struct out_driver {
	const char *name;
	int (*init)(void);
	void (*finish)(void);
	int (*busy)(void);
	int (*feed)(void *data, int bytes);
	/* Non-zero: the hardware behind this driver can do the 44100 -> 48000 hold itself, so in
	 * Hold mode it is fed the mixer's 44100 Hz stream unconverted (resample.c). */
	int hold_in_hw;
};

extern struct out_driver *out_current;
extern int        iDisStereo;

void SetupSound(void);

#endif /* __P_OUT_H__ */
