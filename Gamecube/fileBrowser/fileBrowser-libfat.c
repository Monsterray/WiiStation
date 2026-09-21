/**
 * WiiSX - fileBrowser-libfat.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 *
 * fileBrowser for any devices using libfat
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
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


#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "fileBrowser.h"
#include <sdcard/gcsd.h>

extern BOOL hasLoadedROM;
extern int stop;

#ifdef HW_RVL
#define DEVICE_REMOVAL_THREAD
#endif

#ifdef HW_RVL
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
const DISC_INTERFACE* frontsd = &__io_wiisd;
const DISC_INTERFACE* usb = &__io_usbstorage;
#endif
const DISC_INTERFACE* carda = &__io_gcsda;
const DISC_INTERFACE* cardb = &__io_gcsdb;

// Threaded insertion/removal detection
#define THREAD_SLEEP 100
#define FRONTSD 1
#define CARD_A  2
#define CARD_B  3
static lwp_t removalThread = LWP_THREAD_NULL;
static int rThreadRun = 0;
static int rThreadCreated = 0;
static char sdMounted  = 0;
static char sdNeedsUnmount  = 0;
static char usbMounted = 0;
static char usbNeedsUnmount = 0;

fileBrowser_file topLevel_libfat_Default =
	{ "sd:/wiisxrx/isos", // file name
	  0, // sector
	  0, // offset
	  0, // size
	  FILE_BROWSER_ATTR_DIR
	 };

fileBrowser_file topLevel_libfat_USB =
	{ "usb:/wiisxrx/isos", // file name
	  0, // sector
	  0, // offset
	  0, // size
	  FILE_BROWSER_ATTR_DIR
	 };

fileBrowser_file saveDir_libfat_Default =
	{ "sd:/wiisxrx/saves",
	  0,
	  0,
	  0,
	  FILE_BROWSER_ATTR_DIR
	 };

fileBrowser_file saveDir_libfat_USB =
	{ "usb:/wiisxrx/saves",
	  0,
	  0,
	  0,
	  FILE_BROWSER_ATTR_DIR
	 };

fileBrowser_file biosDir_libfat_Default =
	{ "sd:/wiisxrx/bios",
	  0,
	  0,
	  0,
	  FILE_BROWSER_ATTR_DIR
	 };

fileBrowser_file biosDir_libfat_USB =
	{ "usb:/wiisxrx/bios",
	  0,
	  0,
	  0,
	  FILE_BROWSER_ATTR_DIR
	 };

void continueRemovalThread()
{
#ifdef DEVICE_REMOVAL_THREAD
  if(rThreadRun)
	return;
  rThreadRun = 1;
  LWP_ResumeThread(removalThread);
#endif
}

void pauseRemovalThread()
{
#ifdef DEVICE_REMOVAL_THREAD
  if(!rThreadRun)
	return;
  rThreadRun = 0;

  // wait for thread to finish
  while(!LWP_ThreadIsSuspended(removalThread)) usleep(THREAD_SLEEP);
#endif
}

static int devsleep = 1*1000*1000;

static void *removalCallback (void *arg)
{
  while(devsleep > 0)
  {
	if(!rThreadRun)
	  LWP_SuspendThread(removalThread);
	usleep(THREAD_SLEEP);
	devsleep -= THREAD_SLEEP;
  }

  while (1)
  {
	switch(sdMounted) //some kind of SD is mounted
	{
#ifdef HW_RVL
	  case FRONTSD:   //check which one, if removed, set as unmounted
		if(!frontsd->isInserted()) {
		  sdNeedsUnmount=sdMounted;
		  sdMounted=0;
		}
		break;
#endif
/*    //Polling EXI is bad with locks, so lets not do it.
	  case CARD_A:   //check which one, if removed, set as unmounted
		if(!carda->isInserted()) {
		  sdNeedsUnmount=sdMounted;
		  sdMounted=0;
		}
		break;
	  case CARD_B:   //check which one, if removed, set as unmounted
		if(!cardb->isInserted()) {
		  sdNeedsUnmount=sdMounted;
		  sdMounted=0;
		}
		break;
*/
	}
#ifdef HW_RVL
	if(usbMounted) // check if the device was removed
	  if(!usb->isInserted()) {
		usbMounted = 0;
		usbNeedsUnmount=1;
	  }
#endif

	devsleep = 1000*1000; // 1 sec
	while(devsleep > 0)
	{
	  if(!rThreadRun)
		LWP_SuspendThread(removalThread);
	  usleep(THREAD_SLEEP);
	  devsleep -= THREAD_SLEEP;
	}
  }
  return NULL;
}

void InitRemovalThread()
{
#ifdef DEVICE_REMOVAL_THREAD
  LWP_CreateThread (&removalThread, removalCallback, NULL, NULL, 0, 40);
  rThreadCreated = 1;
#endif
}

static bool isCueCcdFileExist(const char *filePath, const char *fileName, const char *fileType) {
    static char cuename[FILE_BROWSER_MAX_PATH_LEN];
    memset(cuename, 0, FILE_BROWSER_MAX_PATH_LEN);
    snprintf(cuename, sizeof(cuename), "%s/%s", filePath, fileName);

    if (strlen(cuename) >= 4) {
        char *trackPos = strstr(cuename, " (Track");
        if (trackPos)
        {
            strcpy(trackPos, fileType);
        }
        else
        {
            strcpy(cuename + strlen(cuename) - 4, fileType);
        }

        if (access(cuename, F_OK) == 0) {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

// Case-insensitive suffix check -- Wii SD/USB storage is FAT via libfat,
// which is case-preserving but not case-sensitive for lookups, so this
// matches ".CUE"/".cue"/etc equally without needing a separate check per case.
static bool hasExt(const char *name, const char *ext) {
    size_t nameLen = strlen(name);
    size_t extLen = strlen(ext);
    if (extLen > nameLen) return false;
    return strcasecmp(name + (nameLen - extLen), ext) == 0;
}

static bool isFileOk(const char *filePath, const char *fileName) {
    // Suffix match, not substring: previously used strstr(), so a file
    // named e.g. "readme.cue.txt" was incorrectly treated as a playable
    // .cue image just for containing that substring anywhere in its name.
    (void)filePath;
    if (hasExt(fileName, ".sub"))
        return false;
    return true;
}

// A .bin/.img is hidden when its .cue/.ccd is in the same listing, so the
// user sees one entry per game. This used to be decided per entry with
// access() on the card, which is a directory lookup through libfat for every
// .bin in the folder: a directory of a few hundred images cost a few hundred
// extra FAT scans, each of them linear in the directory size, before the
// list appeared. Now the whole directory is read once into memory and the
// pairing is resolved there by name. FAT is case-insensitive, so the match is.
static bool hasPairedSheet(const fileBrowser_file *entries, int count, int idx, const char *sheetExt)
{
    const char *bin = strrchr(entries[idx].name, '/');
    size_t baseLen;
    int j;
    bin = bin ? bin + 1 : entries[idx].name;
    baseLen = strlen(bin) - 4;                       /* strip .bin/.img */
    for (j = 0; j < count; j++) {
        const char *other = strrchr(entries[j].name, '/');
        other = other ? other + 1 : entries[j].name;
        if (j == idx || (entries[j].attr & FILE_BROWSER_ATTR_DIR))
            continue;
        if (strlen(other) == baseLen + 4 && strncasecmp(other, bin, baseLen) == 0 && hasExt(other, sheetExt))
            return true;
    }
    return false;
}

int fileBrowser_libfat_readDir(fileBrowser_file* file, fileBrowser_file** dir){

  pauseRemovalThread();

  DIR* dp = opendir(file->name );
	if(!dp) {
		continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}
	struct dirent * temp = NULL;

	// Set everything up to read
	//char filename[MAXPATHLEN];
	int capacity = 32, i = 0;
	fileBrowser_file *entries = malloc( capacity * sizeof(fileBrowser_file) );
	if (!entries) {
		closedir(dp);
		continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}
	// Read each entry of the directory
	while( (temp = readdir(dp)) && (temp != NULL) ){
        if (!isFileOk(file->name, temp->d_name)) {
            continue;
		}
		// Make sure we have room for this one -- double capacity instead of
		// growing by one entry at a time, which was O(n) reallocations
		// (each copying more data than the last) for an n-entry directory
		if(i == capacity){
			capacity *= 2;
			fileBrowser_file *tmp = realloc( entries, capacity * sizeof(fileBrowser_file) );
			if (!tmp) {
				free(entries);
				closedir(dp);
				continueRemovalThread();
				return FILE_BROWSER_ERROR;
			}
			entries = tmp;
		}

		snprintf(entries[i].name, sizeof(entries[i].name), "%s/%s", file->name, temp->d_name);
		entries[i].offset = 0;
		entries[i].size	 = 0; //TODO
		entries[i].attr	 = (temp->d_type & DT_DIR) ?
							FILE_BROWSER_ATTR_DIR : 0;
		++i;
	}

	closedir(dp);

	// Second pass over the listing in memory: drop each .bin/.img whose
	// .cue/.ccd is present (see hasPairedSheet).
	{
		int k, kept = 0;
		for (k = 0; k < i; k++) {
			const char *nm = strrchr(entries[k].name, '/');
			nm = nm ? nm + 1 : entries[k].name;
			if (!(entries[k].attr & FILE_BROWSER_ATTR_DIR) &&
			    ((hasExt(nm, ".bin") && hasPairedSheet(entries, i, k, ".cue")) ||
			     (hasExt(nm, ".img") && hasPairedSheet(entries, i, k, ".ccd"))))
				continue;
			if (kept != k)
				entries[kept] = entries[k];
			kept++;
		}
		i = kept;
	}

	*dir = entries;
	continueRemovalThread();

	return i;
}

int fileBrowser_libfat_open(fileBrowser_file* file) {
  struct stat fileInfo;
  if(!stat(&file->name[0], &fileInfo)){
	file->offset = 0;
	file->discoffset = 0;
	file->size = fileInfo.st_size;
	return 0;
  }
  return FILE_BROWSER_ERROR_NO_FILE;
}

int fileBrowser_libfat_seekFile(fileBrowser_file* file, unsigned int where, unsigned int type){
	if(type == FILE_BROWSER_SEEK_SET) file->offset = where;
	else if(type == FILE_BROWSER_SEEK_CUR) file->offset += where;
	else file->offset = file->size + where;

	return 0;
}

static int fileBrowser_libfat_exists(const char* name);

int fileBrowser_libfat_readFile(fileBrowser_file* file, void* buffer, unsigned int length){
	/* A swap that was interrupted between the remove and the rename leaves the whole new
	 * file under its temporary name and nothing under the real one. Take it. */
	if(!fileBrowser_libfat_exists(file->name)) {
		char tmpName[FILE_BROWSER_MAX_PATH_LEN];
		snprintf(tmpName, sizeof(tmpName), "%s%s", file->name, FILE_BROWSER_TMP_SUFFIX);
		if(fileBrowser_libfat_exists(tmpName)) rename(tmpName, file->name);
	}
  pauseRemovalThread();
	FILE* f = fopen( file->name, "rb" );
	if(!f) { continueRemovalThread(); return FILE_BROWSER_ERROR; }

	fseek(f, file->offset, SEEK_SET);
	int bytes_read = fread(buffer, 1, length, f);
	if(bytes_read > 0) file->offset += bytes_read;

	fclose(f);
	continueRemovalThread();
	return bytes_read;
}

/* fopen(..., "wb") fails outright if the parent directory does not exist, and
 * nothing else in the tree ever creates sd:/wiisxrx/saves. On a card that only
 * has wiisxrx/isos (the usual hand-made layout), every memory-card write
 * therefore failed and the only symptom was "Failed to save game" when leaving
 * a game -- a whole session's progress lost to a missing folder. Create the
 * chain on demand so saving works on a fresh card with no setup step.
 *
 * Only the directories are created, never the file itself: the loop acts on
 * each '/' in turn and the last path component (the filename) has no trailing
 * slash to trigger on. An existing directory just returns EEXIST, which is the
 * normal case and is deliberately ignored. */
/* Is there a file of this name? Used to find a write that was interrupted. */
static int fileBrowser_libfat_exists(const char* name) {
	struct stat st;
	return stat(name, &st) == 0;
}

void makeParentDirs(const char* path) {
	char dir[FILE_BROWSER_MAX_PATH_LEN];
	char* p;

	strncpy(dir, path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';

	/* Skip the "sd:/" / "usb:/" mount prefix -- mkdir on a mount point is
	 * meaningless and would only fail. */
	p = strchr(dir, '/');
	if(!p) return;

	for(p = p + 1; *p; ++p) {
		if(*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0777);
		*p = '/';
	}
}

/* Write to a second file, then put it in the place of the first.
 *
 * fopen(name, "wb") empties the file before it writes a byte. A memory card is 128 KB and
 * a state is megabytes, so the card holds an empty or half-written file for as long as the
 * write takes. Pull the SD card, or lose power, in that time and the save is gone: the old
 * one has already been thrown away and the new one is not there yet.
 *
 * Writing beside it and then swapping means the old file stays whole until the new one is
 * complete. Only the last two steps can be interrupted, and both leave something to
 * recover: the old file, or the new one under its temporary name, which readFile looks for.
 *
 * A write at an offset (the +64 and +3904 memory-card containers) still has to keep what
 * is already there, so those go straight to the file as before. */
int fileBrowser_libfat_writeFile(fileBrowser_file* file, void* buffer, unsigned int length){
	char tmpName[FILE_BROWSER_MAX_PATH_LEN];
	FILE* f;
	int bytes_written;

	pauseRemovalThread();
	makeParentDirs(file->name);

	if(file->offset != 0) {
		f = fopen( file->name, "r+" );
		if(!f) f = fopen( file->name, "wb" );
		if(!f) { continueRemovalThread(); return FILE_BROWSER_ERROR; }
		fseek(f, file->offset, SEEK_SET);
		bytes_written = fwrite(buffer, 1, length, f);
		if(bytes_written > 0) file->offset += bytes_written;
		fclose(f);
		continueRemovalThread();
		return bytes_written;
	}

	snprintf(tmpName, sizeof(tmpName), "%s%s", file->name, FILE_BROWSER_TMP_SUFFIX);
	f = fopen( tmpName, "wb" );
	if(!f) { continueRemovalThread(); return FILE_BROWSER_ERROR; }

	bytes_written = fwrite(buffer, 1, length, f);
	fclose(f);

	if(bytes_written != (int)length) {
		remove(tmpName);                  /* leave the old file alone */
		continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}

	remove(file->name);
	if(rename(tmpName, file->name) != 0) {
		/* The new file is whole but still under its own name; readFile finds it there. */
		continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}

	file->offset += bytes_written;
	continueRemovalThread();
	return bytes_written;
}

/* call fileBrowser_libfat_init as much as you like for all devices
	- returns 0 on device not present/error
	- returns 1 on ok
*/
int fileBrowser_libfat_init(fileBrowser_file* f){

	if(!rThreadCreated) InitRemovalThread();
#ifdef HW_RVL
  int res = 0;
  if(f->name[0] == 's') { //SD
	if(!sdMounted) { //if there's nothing currently mounted
	  pauseRemovalThread();
	  if(sdNeedsUnmount) {
		fatUnmount("sd");
		if(sdNeedsUnmount==FRONTSD)
		  frontsd->shutdown();
		else if(sdNeedsUnmount==CARD_A)
		  carda->shutdown();
		else if(sdNeedsUnmount==CARD_B)
		  cardb->shutdown();
		sdNeedsUnmount = 0;
      }
		if(fatMountSimple ("sd", frontsd)) {
		  sdMounted = FRONTSD;
		  res = 1;
		}
		else if(fatMountSimple ("sd", carda)) {
		  sdMounted = CARD_A;
		  res = 1;
	  }
	  else if(fatMountSimple ("sd", cardb)) {
		  sdMounted = CARD_B;
		  res = 1;
	  }
	  continueRemovalThread();
	  return res;
	  }
	  else
		return 1;
	}
	else if(f->name[0] == 'u') {
	if(!usbMounted) {
		pauseRemovalThread();
		if(usbNeedsUnmount) {
		  fatUnmount("usb");
		  usb->shutdown();
		  usbNeedsUnmount=0;
	  }
		if(fatMountSimple ("usb", usb))
		usbMounted = 1;
	  continueRemovalThread();
	  return usbMounted;
	}
	else
	  return 1;
  }
  return res;
#else
  if(!sdMounted) { //GC has only SD
	int res = 0;

	if(sdNeedsUnmount) fatUnmount("sd");
	switch(sdNeedsUnmount){  //unmount previous devices
	  case CARD_A:
		carda->shutdown();
		break;
	  case CARD_B:
		cardb->shutdown();
		break;
	}
	if(carda->startup()) {
		res |= fatMountSimple ("sd", carda);
		if(res)
		sdMounted = CARD_A;
	}
	else if(cardb->startup() && !res) {
		res |= fatMountSimple ("sd", cardb);
		if(res)
		sdMounted = CARD_B;
	}

	return res;
  }
  return 1; //we're always ok
#endif
}

int fileBrowser_libfat_deinit(fileBrowser_file* f){
  //we can't support multiple device re-insertion
  //because there's no device removed callbacks
	return 0;
}


/* Special for ISO,CDDA,SUB file reading only
 * - Holds the same fat file handle to avoid fopen/fclose
 * - Modified to keep 3 file handles open at once,
 *   type is determined via attr value.
 */
#define FILE_BROWSER_MAX_FILE_PTRS 3
static FILE* fd[FILE_BROWSER_MAX_FILE_PTRS];

int fileBrowser_libfatROM_deinit(fileBrowser_file* f){
  pauseRemovalThread();

	if(fd[f->attr]) {
		fclose(fd[f->attr]);
	}

	fd[f->attr] = NULL;
	continueRemovalThread();
	return 0;
}

int fileBrowser_libfatROM_readFile(fileBrowser_file* file, void* buffer, unsigned int length){
  if(stop)     //do this only in the menu
	pauseRemovalThread();
	if(file->attr >= FILE_BROWSER_MAX_FILE_PTRS) {
		if(stop) continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}
	if(!fd[file->attr]) fd[file->attr] = fopen( file->name, "rb");
	if(!fd[file->attr]) {
		if(stop) continueRemovalThread();
		return FILE_BROWSER_ERROR;
	}

	fseek(fd[file->attr], file->offset, SEEK_SET);
	int bytes_read = fread(buffer, 1, length, fd[file->attr]);
	if(bytes_read > 0) {
	file->offset += bytes_read;
	}

	if(stop)
	  continueRemovalThread();
	return bytes_read;
}
