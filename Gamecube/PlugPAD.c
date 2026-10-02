/**
 * WiiSX - PlugPAD.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 *
 * Basic Analog PAD plugin for WiiSX
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                sepp256@gmail.com
 *                emukidid@gmail.com
 *
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

#include <gccore.h>
#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <ogc/pad.h>
#include "../plugins.h"
#include "../psxcommon.h"
#include "../psemu_plugin_defs.h"
#include "gc_input/controller.h"
#include "wiiSXconfig.h"
#include "PadSSSPSX.h"

PadDataS lastport1;
PadDataS lastport2;

extern void SysPrintf(char *fmt, ...);
extern int stop;

/* Controller type, later do this by a Variable in the GUI */
//extern char controllerType = 0; // 0 = standard, 1 = analog (analog fails on old games)
long  PadFlags = 0;

virtualControllers_t virtualControllers[10];

controller_t* controller_ts[num_controller_t] =
#if defined(WII) && !defined(NO_BT)
	{ &controller_GC, &controller_Classic,
	  &controller_WiimoteNunchuk,
	  &controller_Wiimote,
	  &controller_WiiUPro,
	  &controller_WiiUGamepad,
	  &controller_HidGC,
	 };
#else
	{ &controller_GC,
	 };
#endif

// Use to invoke func on the mapped controller with args
#define DO_CONTROL(Control,func,args...) \
	virtualControllers[Control].control->func( \
		virtualControllers[Control].number, ## args)

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#include <wupc/wupc.h>
#include <wiidrc/wiidrc.h>

void wpad_scan_if_needed(void){
	if(!wpadNeedScan) return;
	WUPC_UpdateButtonStats();
	WiiDRC_ScanPads();
	WPAD_ScanPads();
	wpadNeedScan = 0;
}
#endif

/* The manual assignment (PadAutoAssign off): port i takes controller padAssign[i] of the
 * family padType[i] names, if that controller answers. Returns the driver it assigned, or
 * NULL, when the port keeps what it had (a manual port is never unassigned here).
 *
 * The menu's status bar calls this on every frame it draws, and that used to be the only
 * place a manual port was filled. An autoboot draws the menu once, just after power-on, and
 * a GameCube pad that had not answered its first scan yet left the first game with nothing
 * in port 1 -- while a second game in the same boot found the pad. go() calls it too. */
controller_t *manual_assign_port(int i)
{
	int w = padAssign[i];
	controller_t *type = NULL;

	switch (padType[i]) {
	case PADTYPE_GAMECUBE:
		controller_GC.refreshAvailable();   /* PAD_ScanPads when one is due */
		controller_GC.available[w] = (gc_connected & (1 << w)) ? 1 : 0;
		if (controller_GC.available[w])
			type = &controller_GC;
		break;
#if defined(HW_RVL) && defined(WII) && !defined(NO_BT)
	case PADTYPE_HID: {
		extern s32 hidControllerConnected;   /* HidController/KernelHID.c */
		if (hidControllerConnected)
			type = &controller_HidGC;
		break;
	}
	case PADTYPE_WII: {
		u32 exp;
		s32 err = WPAD_Probe(w, &exp);
		controller_Classic.available[w] = (err == WPAD_ERR_NONE && exp == WPAD_EXP_CLASSIC) ? 1 : 0;
		controller_WiimoteNunchuk.available[w] = (err == WPAD_ERR_NONE && exp == WPAD_EXP_NUNCHUK) ? 1 : 0;
		controller_Wiimote.available[w] = (err == WPAD_ERR_NONE && exp == WPAD_EXP_NONE) ? 1 : 0;
		controller_WiiUPro.available[w] = (WUPC_Data(w) != NULL) ? 1 : 0;
		controller_WiiUGamepad.available[w] = (w == 0 && WiiDRC_Inited() && WiiDRC_Connected()) ? 1 : 0;
		if (controller_Classic.available[w])             type = &controller_Classic;
		else if (controller_WiiUPro.available[w])        type = &controller_WiiUPro;
		else if (controller_WiiUGamepad.available[w])    type = &controller_WiiUGamepad;
		else if (controller_WiimoteNunchuk.available[w]) type = &controller_WiimoteNunchuk;
		else if (controller_Wiimote.available[w])        type = &controller_Wiimote;
		break;
	}
#endif
	default:
		break;
	}
	if (type)
		assign_controller(i, type, w);
	return type;
}

/* Every port the manual assignment uses: 1 and 2, or a multitap's four slots in their place. */
void manual_assign_controllers(void)
{
	int i;
	for (i = 0; i < 10; i++) {
		if (i >= 2 && i < 6 && padType[0] != PADTYPE_MULTITAP) continue;
		if (i >= 6 && padType[1] != PADTYPE_MULTITAP) continue;
		manual_assign_port(i);
	}
}

/* A PlayStation port (0 or 1) becomes `type` (PADTYPE_*), as the Configure Input menu sets
 * it, also while a game runs. A multitap whose slots are all None gets GameCube pads 1..4
 * in slots A..D; leaving Multitap frees the four slots, which used to stay assigned. */
/* Vblanks left during which a port answers as empty (sio.c): a new device on a port, while
 * a game runs, comes after a moment with nothing plugged in, as a real swap does. Crash Bash
 * read a multitap that became a GameCube pad as a multitap still, and took no more input;
 * games look for their controllers again when one goes away. */
unsigned char pad_unplug[2];
#define PAD_UNPLUG_VBLANKS 30

void set_port_type(int port, int type)
{
	int first = port ? 6 : 2, j;

	if (type != padType[port])
		pad_unplug[port] = PAD_UNPLUG_VBLANKS;

	if (padType[port] == PADTYPE_MULTITAP && type != PADTYPE_MULTITAP)
		for (j = first; j < first + 4; j++)
			unassign_controller(j);
	padType[port] = type;
	if (type == PADTYPE_MULTITAP) {
		unassign_controller(port);
		for (j = first; j < first + 4 && padType[j] == PADTYPE_NONE; j++)
			;
		if (j == first + 4)
			for (j = 0; j < 4; j++) {
				padType[first + j] = PADTYPE_GAMECUBE;
				padAssign[first + j] = (char)j;
			}
		for (j = first; j < first + 4; j++)
			if (!(padType[j] && manual_assign_port(j)))
				unassign_controller(j);
	}
	else if (!(type && manual_assign_port(port)))
		unassign_controller(port);
}

void control_info_init(void){
	//Call once during emulator start to auto assign controllers
	init_controller_ts();
	auto_assign_controllers();
}

void pauseInput(void){
	int i;
	for(i=0; i<2; ++i)
		if(virtualControllers[i].inUse) DO_CONTROL(i, pause);
}

void resumeInput(void){
	int i;
	for(i=0; i<2; ++i)
		if(virtualControllers[i].inUse) DO_CONTROL(i, resume);
}

void init_controller_ts(void){
	int i, j;
	for(i=0; i<num_controller_t; ++i){
		controller_ts[i]->refreshAvailable();

		for(j=0; j<4; ++j){
			memcpy(&controller_ts[i]->config[j],
			       &controller_ts[i]->config_default,
			       sizeof(controller_config_t));
			memcpy(&controller_ts[i]->config_slot[j],
			       &controller_ts[i]->config_default,
			       sizeof(controller_config_t));
		}
	}
}

void assign_controller(int wv, controller_t* type, int wp){
	virtualControllers[wv].control = type;
	virtualControllers[wv].inUse   = 1;
	virtualControllers[wv].number  = wp;
	virtualControllers[wv].config  = &type->config[wp];

	type->assign(wp,wv);
}

void unassign_controller(int wv){
	controller_t* type = virtualControllers[wv].control;

	/* Tell the pad it is no longer a player, so it can put its LEDs back. */
	if(type && virtualControllers[wv].number >= 0)
		type->assign(virtualControllers[wv].number, -1);

	virtualControllers[wv].control = NULL;
	virtualControllers[wv].inUse   = 0;
	virtualControllers[wv].number  = -1;
}

/* Fill the PlayStation's ports from whatever is plugged into the Wii.
 *
 * A port the user set to Multitap has four slots rather than one, and keeps that type:
 * this used to overwrite padType[] with the driver's own type, so a multitap worked only
 * with the assignment set to Manual. The ten virtual controllers are port 1, port 2, then
 * multitap 1 slots A to D, then multitap 2 slots A to D (SETTINGS.md section 6.1). */
void auto_assign_controllers(void)
{
	int i,t,w;
	int slots[10], nslots = 0;
	char used[num_controller_t][4], done[10];
	int gct;   /* controller_GC's place in controller_ts */

//	init_controller_ts();

	memset(used, 0, sizeof(used));
	memset(done, 0, sizeof(done));

	for(i=0; i<2; ++i){
		if(padType[i] == PADTYPE_MULTITAP){
			int s;
			for(s=0; s<4; ++s)
				slots[nslots++] = 2 + i * 4 + s;
		} else
			slots[nslots++] = i;
	}

	/* A multitap's slot A..D is GameCube pad 1..4 when that pad is there, so a player keeps
	 * the same slot whatever answered first. Slots were filled in order with the first
	 * controllers found, and a pad that missed the first scan moved every player along and
	 * could take port 2's pad (2026-10-02, Crash Bash: a "number of players" prompt that
	 * came and went from one boot to the next). */
	for(gct=0; gct<num_controller_t && controller_ts[gct] != &controller_GC; ++gct);
	controller_GC.refreshAvailable();
	for(i=0; i<nslots && gct<num_controller_t; ++i){
		int v = slots[i], s = v >= 6 ? v - 6 : v - 2;
		if(v < 2 || !controller_GC.available[s] || used[gct][s])
			continue;
		assign_controller(v, &controller_GC, s);
		padType[v] = PADTYPE_GAMECUBE;
		padAssign[v] = s;
		used[gct][s] = 1;
		done[v] = 1;
	}

	// Map controllers in the priority given
	// Outer loop: virtual controllers
	for(i=0; i<nslots; ++i){
		int v = slots[i];
		if(done[v])
			continue;
		// Middle loop: controller type
		for(t=0; t<num_controller_t; ++t){
			controller_t* type = controller_ts[t];
			type->refreshAvailable();

			// Inner loop: which controller, the lowest one of this type still free
			for(w=0; w<4 && (!type->available[w] || used[t][w]); ++w);
			// If we've exhausted this type, move on
			if(w == 4) continue;

			assign_controller(v, type, w);
			if (type == &controller_GC)
			{
				padType[v] = PADTYPE_GAMECUBE;
			}
			else if (type == &controller_HidGC)
			{
				padType[v] = PADTYPE_HID;
			}
			else
			{
				padType[v] = PADTYPE_WII;
			}
			padAssign[v] = w;

			// Don't assign the same controller twice
			used[t][w] = 1;
			done[v] = 1;
			break;
		}
		/* No controller for this one: it stays empty, and the next one still gets a turn
		 * (an empty multitap slot used to leave port 2 empty too) */
		if(t == num_controller_t){
			unassign_controller(v);
			padType[v] = PADTYPE_NONE;
		}
	}

	/* A port an input script plays (PadWiiSX.c autoinput_active) is a digital pad whatever is
	 * plugged into the Wii: a recording is made with a GameCube pad on each port it uses, and a
	 * replay that left port 2 empty, or gave it to Dolphin's emulated Wiimote, made the game
	 * talk to a different set of pads -- different pad traffic every frame, and ten-minute
	 * recordings drifted out of step (2026-09-30). A host GameCube pad on the port may stay. */
	for(i=0; i<2; ++i){
		extern int autoinput_active(int port);
		if(padType[i] == PADTYPE_MULTITAP || !autoinput_active(i))
			continue;
		if(virtualControllers[i].inUse && virtualControllers[i].control != &controller_GC)
			unassign_controller(i);
		padType[i] = PADTYPE_GAMECUBE;
	}
}

int load_configurations(FILE* f, controller_t* controller){
	int i;
	char magic[4] = { 
		'W', 'X', controller->identifier, CONTROLLER_CONFIG_VERSION
	};
	char actual[4];
	if(fread(actual, 1, 4, f) != 4 || memcmp(magic, actual, 4))
		return 0;

	inline button_t* getPointer(button_t* list, int size){
		// A truncated/corrupt config previously left `index` as uninitialized
		// stack garbage (fread's return value was ignored) and then used
		// signed `%`, which can go negative on a negative dividend -- turning
		// list + (index % size) into an out-of-bounds pointer that callers
		// later dereference unconditionally (e.g. .SQU->index on save, with
		// no NULL check), so falling back to NULL here isn't safe either.
		// Clamp to a known-valid index instead.
		int index;
		if (fread(&index, 4, 1, f) != 1 || index < 0 || index >= size)
			index = 0;
		return list + index;
	}
	inline button_t* getButton(void){
		return getPointer(controller->buttons, controller->num_buttons);
	}
	
	for(i=0; i<4; ++i){
		controller->config_slot[i].SQU = getButton();
		controller->config_slot[i].CRO = getButton();
		controller->config_slot[i].CIR = getButton();
		controller->config_slot[i].TRI = getButton();
		
		controller->config_slot[i].R1 = getButton();
		controller->config_slot[i].L1 = getButton();
		controller->config_slot[i].R2 = getButton();
		controller->config_slot[i].L2 = getButton();
		controller->config_slot[i].R3 = getButton();
		controller->config_slot[i].L3 = getButton();

		controller->config_slot[i].DL = getButton();
		controller->config_slot[i].DR = getButton();
		controller->config_slot[i].DU = getButton();
		controller->config_slot[i].DD = getButton();

		controller->config_slot[i].START  = getButton();
		controller->config_slot[i].SELECT = getButton();
				
		controller->config_slot[i].analogL = 
			getPointer(controller->analog_sources, controller->num_analog_sources);
		controller->config_slot[i].analogR = 
			getPointer(controller->analog_sources, controller->num_analog_sources);
		controller->config_slot[i].exit =
			getPointer(controller->menu_combos, controller->num_menu_combos);
		fread(&controller->config_slot[i].invertedYL, 4, 1, f);
		fread(&controller->config_slot[i].invertedYR, 4, 1, f);
		fread(&controller->config_slot[i].sensitivity, 4, 1, f);
		controller->config_slot[i].fastf =
			getPointer(controller->menu_combos, controller->num_menu_combos);
	}

	if (loadButtonSlot != LOADBUTTON_DEFAULT) {
		int j;
		for(j=0; j<4; ++j)
			memcpy(&controller->config[j],
			       &controller->config_slot[(int)loadButtonSlot],
			       sizeof(controller_config_t));
	}
	
	return 1;
}

void save_configurations(FILE* f, controller_t* controller){
	int i;
	char magic[4] = { 
		'W', 'X', controller->identifier, CONTROLLER_CONFIG_VERSION
	};
	fwrite(magic, 1, 4, f);
	
	for(i=0; i<4; ++i){
		fwrite(&controller->config_slot[i].SQU->index, 4, 1, f);
		fwrite(&controller->config_slot[i].CRO->index, 4, 1, f);
		fwrite(&controller->config_slot[i].CIR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].TRI->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].R1->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L1->index, 4, 1, f);
		fwrite(&controller->config_slot[i].R2->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L2->index, 4, 1, f);
		fwrite(&controller->config_slot[i].R3->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L3->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].DL->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DU->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DD->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].START->index, 4, 1, f);
		fwrite(&controller->config_slot[i].SELECT->index, 4, 1, f);
				
		fwrite(&controller->config_slot[i].analogL->index, 4, 1, f);
		fwrite(&controller->config_slot[i].analogR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].exit->index, 4, 1, f);
		fwrite(&controller->config_slot[i].invertedYL, 4, 1, f);
		fwrite(&controller->config_slot[i].invertedYR, 4, 1, f);
		fwrite(&controller->config_slot[i].sensitivity, 4, 1, f);
		fwrite(&controller->config_slot[i].fastf->index, 4, 1, f);
	}
}

long PAD__init(long flags) {
	PadFlags |= flags;

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__shutdown(void) {
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}
