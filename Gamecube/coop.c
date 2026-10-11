/**
 * WiiStation - coop.c
 *
 * Co-Op: several controllers play one PlayStation pad, so that more people than the game
 * has players can play it together (one steers, one shoots). A port set to PADTYPE_COOP
 * has coopPlayers[port] players; player k has a controller type and number (coopType,
 * coopAssign, as a port has) and a layout (coopLayout: which PlayStation buttons and
 * sticks that player works). Player k is virtual controller COOP_VC(port, k).
 *
 * Every poll reads each player's controller and mixes them (psx_analog.h): a button is
 * pressed when any player who has it presses it; on each stick axis the larger push wins.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include "gc_input/controller.h"
#include "wiiSXconfig.h"

const char *coop_layout_names[COOP_LAYOUT_COUNT] = {
	"Full pad", "Left half", "Right half", "D-pad+sticks", "Action", "Shoulders", "Custom"
};

/* The names in a CoopPButtonsK list, in the Customize screen's order (CustomizeCoopFrame.cpp) */
const char *coop_button_names[COOP_NUM_BUTTONS] = {
	"L1", "L2", "Select", "Start", "R1", "R2", "Up", "Left", "Right", "Down",
	"Triangle", "Square", "Circle", "Cross", "LStick", "RStick", "L3", "R3"
};

/* Button i's mask: the _BUTTONS bit in the low 16, a stick (COOP_STICK_*) << 16 */
u32 coop_button_mask(int i)
{
	_BUTTONS b;
	b.All = 0;
	switch (i) {
	case 0:  b.L1_BUTTON = 1; break;
	case 1:  b.L2_BUTTON = 1; break;
	case 2:  b.SELECT_BUTTON = 1; break;
	case 3:  b.START_BUTTON = 1; break;
	case 4:  b.R1_BUTTON = 1; break;
	case 5:  b.R2_BUTTON = 1; break;
	case 6:  b.U_DPAD = 1; break;
	case 7:  b.L_DPAD = 1; break;
	case 8:  b.R_DPAD = 1; break;
	case 9:  b.D_DPAD = 1; break;
	case 10: b.TRIANGLE_BUTTON = 1; break;
	case 11: b.SQUARE_BUTTON = 1; break;
	case 12: b.CIRCLE_BUTTON = 1; break;
	case 13: b.CROSS_BUTTON = 1; break;
	case 14: return COOP_STICK_L << 16;
	case 15: return COOP_STICK_R << 16;
	case 16: b.L3_BUTTON = 1; break;
	case 17: b.R3_BUTTON = 1; break;
	}
	return b.All;
}

/* "L1, Up,Cross" -> its mask. Names are matched without case; unknown ones are skipped. */
u32 coop_buttons_parse(const char *list)
{
	u32 mask = 0;
	while (*list) {
		size_t n;
		int i;
		while (*list == ',' || *list == ' ')
			list++;
		n = strcspn(list, ", ");
		for (i = 0; n && i < COOP_NUM_BUTTONS; i++)
			if (strlen(coop_button_names[i]) == n && !strncasecmp(list, coop_button_names[i], n))
				mask |= coop_button_mask(i);
		list += n;
	}
	return mask;
}

void coop_buttons_format(u32 mask, char *out, int size)
{
	int i, len = 0;
	out[0] = 0;
	for (i = 0; i < COOP_NUM_BUTTONS; i++)
		if ((mask & coop_button_mask(i)) && len < size)
			len += snprintf(out + len, size - len, "%s%s", len ? "," : "", coop_button_names[i]);
}

u16 coop_player_buttons(int port, int k, int *sticks)
{
	if (coopLayout[port][k] == COOP_LAYOUT_CUSTOM) {
		u32 m = coop_buttons_parse(coopButtons[port][k]);
		*sticks = m >> 16;
		return (u16)m;
	}
	return coop_layout_buttons(coopLayout[port][k], sticks);
}


/* The buttons (a 1 for each, in _BUTTONS' own bit order) and sticks (COOP_STICK_L/R) a
 * layout has. The Customize screen shows the same table. */
u16 coop_layout_buttons(int layout, int *sticks)
{
	_BUTTONS b;
	b.All = 0;
	*sticks = 0;
	switch (layout) {
	case COOP_LAYOUT_LEFT:
		b.L1_BUTTON = b.L2_BUTTON = b.L3_BUTTON = b.SELECT_BUTTON = 1;
		b.U_DPAD = b.D_DPAD = b.L_DPAD = b.R_DPAD = 1;
		*sticks = COOP_STICK_L;
		break;
	case COOP_LAYOUT_RIGHT:
		b.R1_BUTTON = b.R2_BUTTON = b.R3_BUTTON = b.START_BUTTON = 1;
		b.TRIANGLE_BUTTON = b.CIRCLE_BUTTON = b.CROSS_BUTTON = b.SQUARE_BUTTON = 1;
		*sticks = COOP_STICK_R;
		break;
	case COOP_LAYOUT_DPAD_STICKS:
		b.U_DPAD = b.D_DPAD = b.L_DPAD = b.R_DPAD = 1;
		b.L3_BUTTON = b.R3_BUTTON = 1;
		*sticks = COOP_STICK_L | COOP_STICK_R;
		break;
	case COOP_LAYOUT_ACTION:
		b.TRIANGLE_BUTTON = b.CIRCLE_BUTTON = b.CROSS_BUTTON = b.SQUARE_BUTTON = 1;
		break;
	case COOP_LAYOUT_SHOULDERS:
		b.L1_BUTTON = b.L2_BUTTON = b.R1_BUTTON = b.R2_BUTTON = 1;
		break;
	default:   /* COOP_LAYOUT_FULL */
		b.All = 0xFFFF;
		*sticks = COOP_STICK_L | COOP_STICK_R;
		break;
	}
	return b.All;
}

void coop_unassign_port(int port)
{
	int k;
	for (k = 0; k < COOP_MAX; k++)
		if (virtualControllers[COOP_VC(port, k)].inUse)
			unassign_controller(COOP_VC(port, k));
}

/* Each player takes its controller if that one answers; a player beyond coopPlayers, or
 * whose type is None, has none. Called wherever ports are assigned (go(), the menu). */
void coop_assign_port(int port)
{
	int k;
	for (k = 0; k < COOP_MAX; k++) {
		int vc = COOP_VC(port, k);
		if (k >= coopPlayers[port] || coopType[port][k] == PADTYPE_NONE ||
		    !assign_port_as(vc, coopType[port][k], coopAssign[port][k]))
			if (virtualControllers[vc].inUse)
				unassign_controller(vc);
	}
}

/* The port's pad as its players hold it together, in GetKeys' form (out: active-low
 * buttons, sticks 0..255). Returns 1 when a player pressed the menu combination, 2 for
 * fast forward, else 0; *connected tells whether any player has a controller. */
int coop_get_keys(int port, BUTTONS *out, int *connected)
{
	int k, misc = 0;

	out->btns.All = 0xFFFF;
	out->leftStickX = out->leftStickY = out->rightStickX = out->rightStickY = 128;
	*connected = 0;
	for (k = 0; k < coopPlayers[port] && k < COOP_MAX; k++) {
		virtualControllers_t *vc = &virtualControllers[COOP_VC(port, k)];
		BUTTONS in;
		int sticks, m;
		u16 allowed;
		float sens;

		if (!vc->inUse || !vc->control)
			continue;
		*connected = 1;
		m = vc->control->GetKeys(vc->number, &in, vc->config);
		if (m == 1) misc = 1;
		else if (m == 2 && !misc) misc = 2;

		sens = vc->config ? vc->config->sensitivity : 1.0f;
		if (sens >= 0.1f && sens != 1.0f) {
			in.leftStickX  = apply_sensitivity(in.leftStickX,  sens);
			in.leftStickY  = apply_sensitivity(in.leftStickY,  sens);
			in.rightStickX = apply_sensitivity(in.rightStickX, sens);
			in.rightStickY = apply_sensitivity(in.rightStickY, sens);
		}
		allowed = coop_player_buttons(port, k, &sticks);
		out->btns.All = coop_buttons(out->btns.All, in.btns.All, allowed);
		if (sticks & COOP_STICK_L) {
			out->leftStickX = coop_axis(out->leftStickX, in.leftStickX);
			out->leftStickY = coop_axis(out->leftStickY, in.leftStickY);
		}
		if (sticks & COOP_STICK_R) {
			out->rightStickX = coop_axis(out->rightStickX, in.rightStickX);
			out->rightStickY = coop_axis(out->rightStickY, in.rightStickY);
		}
	}
	return misc;
}

/* The pad's motor runs in every player's controller */
void coop_rumble(int port, int on)
{
	int k;
	for (k = 0; k < coopPlayers[port] && k < COOP_MAX; k++) {
		virtualControllers_t *vc = &virtualControllers[COOP_VC(port, k)];
		if (vc->inUse && vc->control && vc->control->rumble)
			vc->control->rumble(vc->number, on);
	}
}
