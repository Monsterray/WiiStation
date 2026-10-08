/**
 * WiiSX - fileBrowser-SMB.c
 * Copyright (C) 2010 emu_kidid
 * 
 * fileBrowser module for Samba based shares
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address:  emukidid@gmail.com
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

#ifdef HW_RVL

#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <network.h>
#include <ogcsys.h>
#include "smb2dev.h"   /* "smb:" over libsmb2: SMB2/3 (libtinysmb spoke only SMB1) */
#include "../wiiSXconfig.h"
#include "fileBrowser.h"
#include "fileBrowser-libfat.h"
#include "fileBrowser-SMB.h"
#include "../perf_prof.h"
#include "../hbc_home.h"

/* One thread owns all of the network. It brings the interface up, and it connects to the
 * share. Nothing that blocks is left on the menu thread: init_samba() used to run from
 * fileBrowser_SMB_readDir(), so a wrong address made the menu look dead until TCP gave up.
 *
 * Each flag below is written by one thread and read by the other, so each is volatile. At
 * -O2 the compiler can otherwise hold netInitHalted in a register, and the thread then
 * never stops when the menu asks it to. */
static volatile int net_initialized = 0;   /* the interface has an address */
static volatile int smb_initialized = 0;   /* the share is mounted */
static volatile int smb_dropped     = 0;   /* a read failed: make the session again */
static volatile int netInitHalted   = 0;   /* the menu asks the thread to stop */
static volatile int netInitPending  = 0;   /* a call that blocks is in progress */

static lwp_t initnetthread = LWP_THREAD_NULL;

/* How long the thread waits between attempts, and how long one step of a wait is. The step
 * sets how fast the thread answers a stop request, so it is about one frame. It used to be
 * 100us: libogc programs the decrementer for exactly the time asked (timesupp.c; it does
 * not round up to a scheduler tick), so the old countdown woke this thread 10000 times a
 * second, for ever, while the game ran. */
#define NET_FIRST_WAIT_US   (1 * 1000 * 1000)
#define NET_RETRY_WAIT_US   (5 * 1000 * 1000)
#define NET_STEP_US         (20 * 1000)

/* How long pause_netinit_thread() waits before it gives up. A network stack that does not
 * answer must not stop the user from starting a game. */
#define NET_PAUSE_WAIT_US   (5 * 1000 * 1000)

extern char smbUserName[];
extern char smbPassWord[];
extern char smbShareName[];
extern char smbIpAddr[];

fileBrowser_file topLevel_SMB =
	{ "smb:/", // file name
	  0ULL,      // discoffset (u64)
	  0,         // offset
	  0,         // size
	  FILE_BROWSER_ATTR_DIR
	};

/* The share cannot be found without both of these. */
static int smb_configured(void)
{
	return smbShareName[0] != '\0' && smbIpAddr[0] != '\0';
}

void resume_netinit_thread(void)
{
	if(initnetthread != LWP_THREAD_NULL) {
		netInitHalted = 0;
		/* Safe when the thread already runs: LWP_ResumeThread() then returns
		 * LWP_NOT_SUSPENDED and does not touch the suspend count. */
		LWP_ResumeThread(initnetthread);
	}
}

void pause_netinit_thread(void)
{
	int wait = NET_PAUSE_WAIT_US;

	if(initnetthread == LWP_THREAD_NULL)
		return;

	netInitHalted = 1;

	/* Wait for a call that blocks to finish, as well as for the thread to stop. The old
	 * code returned as soon as it saw one in progress, so the game started with a DHCP
	 * request still running behind it. */
	while((netInitPending || !LWP_ThreadIsSuspended(initnetthread)) && wait > 0) {
		usleep(NET_STEP_US);
		wait -= NET_STEP_US;
	}
}

/* Wait, but stop as soon as the menu asks. */
static void net_wait(int us)
{
	while(us > 0) {
		if(netInitHalted)
			LWP_SuspendThread(initnetthread);
		usleep(NET_STEP_US);
		us -= NET_STEP_US;
	}
}

#if PERF_PROF_NETWAIT
/* Wait one second twice: once the way this thread used to, in steps of 100us, and once
 * the way it does now. What each really takes, less the second it asked for, is what the
 * wakeups cost. Debug builds only, and once per run. */
void net_wait_measure(void)
{
	unsigned long long t0;
	int us, n;

	t0 = perf_now_us();
	for(us = 1000000, n = 0; us > 0; us -= 100, n++)
		usleep(100);
	g_netwait_old_us = perf_now_us() - t0;
	g_netwait_old_wakes = n;

	t0 = perf_now_us();
	for(us = 1000000, n = 0; us > 0; us -= NET_STEP_US, n++)
		usleep(NET_STEP_US);
	g_netwait_new_us = perf_now_us() - t0;
	g_netwait_new_wakes = n;
}
#endif

// Init the GC/Wii net interface (wifi/bba/etc), then connect to the share
static void* init_network(void *args)
{
	char ip[16];

	/* Let the menu come up first. */
	net_wait(NET_FIRST_WAIT_US);

	/* The Homebrew Channel's agent may be starting the network: if_config() below would
	 * call net_init() during its start-up, which can hang (hbc_home.c) */
	hbc_home_net_wait();

	while(1) {
		if(!net_initialized) {
			netInitPending = 1;
			net_initialized = if_config(ip, NULL, NULL, true) >= 0;
			netInitPending = 0;
		}

		/* Only this thread touches the session, so a menu that found a dead share can
		 * ask for a new one without a lock. */
		if(net_initialized && smb_configured()) {
			if(smb_dropped) {
				smb2dev_unmount();
				smb_initialized = 0;
				smb_dropped = 0;
			}
			if(!smb_initialized) {
				netInitPending = 1;
				smb_initialized = smb2dev_mount(&smbIpAddr[0], &smbShareName[0],
				                                &smbUserName[0], &smbPassWord[0]) == 0;
				netInitPending = 0;
			}
		}

		if(net_initialized && (smb_initialized || !smb_configured())) {
			/* Nothing left to do. Wait until somebody wakes the thread: the menu
			 * comes back, or a read finds the share gone. */
			LWP_SuspendThread(initnetthread);
		} else {
			net_wait(NET_RETRY_WAIT_US);
		}
	}
	return NULL;
}

void init_network_thread(void)
{
	LWP_CreateThread (&initnetthread, init_network, NULL, NULL, 0, 40);
}

/* A read failed, so the session is gone. Ask the thread for a new one: it owns the
 * session, and smbClose() must not run while it is inside smbInit(). */
static void smb_drop(void)
{
	smb_dropped = 1;
	smb_initialized = 0;
	if(initnetthread != LWP_THREAD_NULL)
		LWP_ResumeThread(initnetthread);
}

int fileBrowser_SMB_readDir(fileBrowser_file* ffile, fileBrowser_file** dir)
{
	int num;

	// We need at least a share name and ip addr in the settings filled out
	if(!smb_configured())
		return SMB_SMBCFGERR;

	if(!net_initialized)
		return SMB_NETINITERR;

	if(!smb_initialized)
		return netInitPending ? SMB_SMBRETRY : SMB_SMBERR;

	// Call the corresponding FAT function
	num = fileBrowser_libfat_readDir(ffile, dir);

	/* A share that went away leaves the session dead, and every later read then fails
	 * against it. The old code had no way back at all: smb_initialized stayed set for
	 * the rest of the run, and the user had to restart the emulator. */
	if(num < 0) {
		smb_drop();
		return SMB_SMBRETRY;
	}

	return num;
}

int fileBrowser_SMB_open(fileBrowser_file* file) {
  return fileBrowser_libfat_open(file);
}

int fileBrowser_SMB_seekFile(fileBrowser_file* file, unsigned int where, unsigned int type){
	return fileBrowser_libfat_seekFile(file,where,type);
}

int fileBrowser_SMB_readFile(fileBrowser_file* file, void* buffer, unsigned int length){
	return fileBrowser_libfatROM_readFile(file,buffer,length);
}

int fileBrowser_SMB_init(fileBrowser_file* file){
	return 0;
}

int fileBrowser_SMB_deinit(fileBrowser_file* file) {
	/* The session is not closed here. It belongs to the network thread, which closes it
	 * when a read finds it dead (smb_drop), and the browser is opened and left often
	 * enough that a close here would mean a new connection every visit. */
	return fileBrowser_libfatROM_deinit(file);
}

#endif
