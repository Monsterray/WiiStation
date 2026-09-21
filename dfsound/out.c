#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "out.h"
#include "resample.h"

/* Two output drivers for the SPU emulator's signed 16-bit stereo stream:
 *   sdl   -- libSDL hands the stream to the audio interface's DMA. The DSP is idle.
 *   cube  -- the stream goes to an AESND voice and the DSP's microcode does the mixing
 *            and volume.
 * The 44100 -> 48000 conversion sits in front of both (resample.c): the CPU interpolates
 * and the drivers receive 48000 Hz, except for the DSP path in Hold mode, which receives
 * 44100 Hz and lets the microcode hold, as it always did.
 * The SoundHwAccel setting picks which driver is preferred; if its init() fails the other
 * is used, so a machine where one path is unavailable still gets sound. */
#define HAVE_SDL
#define HAVE_CUBE
#define MAX_OUT_DRIVERS 2

static struct out_driver out_drivers[MAX_OUT_DRIVERS];
struct out_driver *out_current;
static int driver_count;

extern char soundHwAccel;   /* Gamecube/wiiSXconfig.h */

#define REGISTER_DRIVER(d) { \
	extern void out_register_##d(struct out_driver *drv); \
	out_register_##d(&out_drivers[driver_count++]); \
}

void SetupSound(void)
{
	int i, preferred = 0;

	if (driver_count == 0) {
#ifdef HAVE_SDL
		REGISTER_DRIVER(sdl);
#endif
#ifdef HAVE_CUBE
		REGISTER_DRIVER(cube);
#endif
	}

	const char *want = soundHwAccel ? "cube" : "sdl";
	for (i = 0; i < driver_count; i++)
		if (strcmp(out_drivers[i].name, want) == 0)
			preferred = i;

	/* Try the preferred driver, then any other, so a failure to open one is not silence. */
	if (out_drivers[preferred].init() == 0) {
		out_current = &out_drivers[preferred];
		resample_reset();
		return;
	}

	for (i = 0; i < driver_count; i++) {
		if (i == preferred) continue;
		if (out_drivers[i].init() == 0) {
			out_current = &out_drivers[i];
			resample_reset();
			return;
		}
	}

	//printf("the impossible happened\n");
	abort();
}
