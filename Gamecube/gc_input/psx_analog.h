/**
 * WiiStation - psx_analog.h
 *
 * The whole analog-stick path, as two pure functions.
 *
 * A controller driver turns its own hardware's reading into a fraction of full travel
 * (-1.0 hard left or up, +1.0 hard right or down) and calls psx_analog() to put it in the
 * range a PlayStation pad reports: 0 .. 255 with 128 at rest. The pad plugin then applies
 * the player's sensitivity setting, once, with apply_sensitivity().
 *
 * Both live here rather than in controller.h so that tests/psx_analog_test.c can include
 * them on a host compiler -- they are the part of the input path worth testing exhaustively,
 * and they are the part that was wrong: drivers used to disagree about where the centre was
 * and a fixed extra gain in the pad plugin threw away the outer half of some sticks' travel.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#ifndef PSX_ANALOG_H
#define PSX_ANALOG_H

#include <stdint.h>

/* A stick position, as a fraction of its own full travel, in what the PlayStation reports:
 * 0 is hard left or up, 255 hard right or down, 128 is rest. Every driver goes through
 * this so that all of them agree on the centre -- one of them used to centre on 127, and a
 * stick that reads one off centre at rest walks the player slowly across the screen. */
static inline uint8_t psx_analog(float unit)
{
	int v;
	if (unit < -1.0f) unit = -1.0f;
	else if (unit > 1.0f) unit = 1.0f;
	/* 128 steps below rest and 127 above, so that both 0 and 255 are reachable: the
	 * PlayStation's range is not symmetric about its centre and neither is this. */
	v = unit < 0.0f ? 128 + (int)(unit * 128.0f - 0.5f)
	                : 128 + (int)(unit * 127.0f + 0.5f);
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* The one gain applied to a stick after its driver has mapped it. At 1.0 the stick is
 * exactly what the hardware reported. Above 1.0 the travel is stretched and its outer part
 * saturates, which is the trade the sensitivity setting exists to make: a worn stick that
 * no longer reaches the corners gets there sooner, at the cost of the top of its range. */
static inline uint8_t apply_sensitivity(uint8_t val, float sensitivity)
{
	int a = (int)(((int)val - 128) * sensitivity);
	if (a > 127) a = 127;
	else if (a < -128) a = -128;
	return (uint8_t)(a + 128);
}

#endif
