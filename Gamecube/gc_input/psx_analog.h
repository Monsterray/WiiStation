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

/* Full travel of a GameCube pad's two sticks once libogc has taken the origin off. They
 * differ: Dolphin's pad model, taken from real pads, puts the main stick's gate at 0.794 of
 * the range and the C stick's at 0.722, so a full push reads about +/-101 and +/-92. Each
 * value is about 5% under that, so a worn pad still reaches the edge. With the main stick's
 * 96 used for both, a C stick pushed straight out read 250 or 5, never 255 or 0, and Ape
 * Escape did not take it as a swing (a diagonal did: it reached the corner). */
#define GC_MAIN_FULL   96.0f
#define GC_CSTICK_FULL 87.0f

/* A stick position (x, y, each a fraction of full travel) stretched from a Nintendo stick's
 * gate to a DualShock's range. A GameCube or Classic Controller stick has an octagonal gate
 * with its corners at full cardinal travel, so a full diagonal reads about 0.71 on each axis;
 * a DualShock's full diagonal reads close to the corner of its square (emulators scale modern
 * round sticks by about 1.33 for the same reason). Each direction is stretched so that the
 * octagon's edge lands on the square's edge: straight up, down, left and right are unchanged,
 * a full diagonal becomes (1, 1), and rest stays at rest. psx_analog() clamps what goes
 * past the square, which a stick between gate corners can do by a few percent. */
static inline void psx_square(float *x, float *y)
{
	float ax = *x < 0.0f ? -*x : *x, ay = *y < 0.0f ? -*y : *y;
	float big = ax > ay ? ax : ay, small = ax > ay ? ay : ax, k;
	if (big <= 0.0f) return;
	k = (big + 0.41421356f * small) / big;   /* octagon norm / square norm; tan(22.5) */
	*x *= k;
	*y *= k;
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

/* Controller Type "Stick D-pad": which D-pad directions a stick position (0..255, 128 at
 * rest, 0 left/up) presses. Inside half of full travel it presses nothing. Outside, the
 * stick's angle picks one of eight 45-degree sectors, so a push within 22.5 degrees of
 * straight up presses only Up, and a diagonal presses its two directions together; a
 * per-axis threshold would press two directions for any slightly diagonal push. */
#define STICK_DPAD_UP    1
#define STICK_DPAD_DOWN  2
#define STICK_DPAD_LEFT  4
#define STICK_DPAD_RIGHT 8
#define STICK_DPAD_DEAD  64    /* half of the 128 steps from rest to an edge */

/* Co-Op (Gamecube/coop.c): several players make one PlayStation pad.
 * Buttons are active low (0 = pressed): a button is pressed when any player who may use it
 * presses it; `allowed` has a 1 for each button this player works. */
static inline uint16_t coop_buttons(uint16_t acc, uint16_t in, uint16_t allowed)
{
	return acc & (uint16_t)(in | (uint16_t)~allowed);
}

/* A stick axis (0..255, 128 at rest): the player who pushes further wins. An average would
 * halve one player's push whenever the others leave their sticks alone. */
static inline uint8_t coop_axis(uint8_t acc, uint8_t in)
{
	int a = acc - 128, b = in - 128;
	return (b < 0 ? -b : b) > (a < 0 ? -a : a) ? in : acc;
}

static inline int stick_dpad(int x, int y)
{
	int dx = x - 128, dy = y - 128, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, d = 0;
	if (dx * dx + dy * dy < STICK_DPAD_DEAD * STICK_DPAD_DEAD)
		return 0;
	/* tan(22.5 degrees) = 0.41421, as 53/128: a component this far under the other is off */
	if (ay * 128 > ax * 53)
		d |= dy < 0 ? STICK_DPAD_UP : STICK_DPAD_DOWN;
	if (ax * 128 > ay * 53)
		d |= dx < 0 ? STICK_DPAD_LEFT : STICK_DPAD_RIGHT;
	return d;
}

#endif
