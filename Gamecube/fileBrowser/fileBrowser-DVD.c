/**
 * WiiStation - fileBrowser-DVD.c
 * Load from DVD. The first version was WiiSX's (C) 2007-2009 emu_kidid.
 *
 * Games on a data DVD (ISO9660, with or without Joliet names), such as a DVD-R burned on a
 * PC. libiso9660 mounts the disc as the device "dvd:" over the drive reader below, and from
 * there it is the code the SD card uses: the browser lists dvd:/ with opendir(), and
 * cdriso.c opens the game with fopen(), so .cue/.bin, CHD, PBP and multi-track images work
 * as they do from the card. (The version before 5.9.0 browsed the disc with its own
 * ISO9660 reader and kept only a file's name, which cdriso.c's fopen() could not open: no
 * game ever started from a disc.)
 *
 * What a Wii can read: data DVDs, on a Wii whose drive reads DVD-R (most made until late
 * 2008; not the Wii Family Edition, the Wii mini or a Wii U). No Wii reads CDs. The reader
 * uses the drive's registers directly, which needs the full hardware access the Homebrew Channel
 * gives an app whose meta.xml asks for it (<ahb_access/>); no cIOS is needed.
 *
 * The disc is mounted afresh each time Load from DVD opens, so a disc changed in between is
 * read -- but not while a game from the disc is loaded: its open file points into the mount,
 * which ISO9660_Unmount() frees (a disc changed meanwhile is read once a game from another
 * device has been loaded). libiso9660 has no lock, so only one thread may read the
 * disc: cdriso.c starts no CD read-ahead thread for a game here.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <iso9660.h>
#ifdef HW_RVL
#include <di/di.h>
#else
#include <ogc/dvd.h>
#endif
#include "fileBrowser.h"
#include "fileBrowser-libfat.h"
#include "fileBrowser-DVD.h"

extern void SysPrintf(const char *fmt, ...);

/* What the disc code does, for the console log (tty.log). Debug builds also append each line
 * to sd:/wiistation/dvd.log at once: tty.log is written only when a game ends, and a drive
 * that hangs the Wii would take the lines with it. */
#include <stdarg.h>
#include <stdio.h>
static void dvd_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dvd_note(const char *fmt, ...)
{
	char line[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	SysPrintf("%s", line);
#ifdef PERF_PROF
	{
		FILE *f = fopen("sd:/wiistation/dvd.log", "a");
		if (f) {
			fputs(line, f);
			fclose(f);
		}
	}
#endif
}

fileBrowser_file topLevel_DVD =
	{ "dvd:/",   // file name
	  0ULL,      // discoffset (u64)
	  0,         // offset
	  0,         // size
	  FILE_BROWSER_ATTR_DIR
	};

extern BOOL hasLoadedISO;    /* GamecubeMain.cpp: a game is loaded, from isoFile */

static int mounted;
static int mount_error;      /* why the last mount failed: a DVD_ERR_*, or 0 */
static u32 drive_error;      /* the drive's own error code after the last failed command */

#ifdef HW_RVL
/* The drive, read by its registers (needs AHBPROT), with every wait bounded.
 *
 * libdi's own reads (__io_wiidvd) spin on the drive's busy bit with no limit, on the
 * calling thread -- the emulator's, priority 64. On the bench Wii (2026-10-08, a DVD+R)
 * the first read never ended: the Wii froze with no crash report, as the HBC agent and the
 * hang watchdog run below that priority and never got the CPU again. Here a command that
 * runs too long is broken off and reported as an error, the wait sleeps between looks so
 * the other threads run, and once a disc has failed both read commands the mount gives up
 * at once instead of retrying sector after sector. libdi still spins the disc up and reads
 * its ID (DI_Mount: a chain of IOS requests), and this side waits for it with a limit.
 *
 * A spin-up that does not finish is the second way to hang. On the bench Wii (2026-10-08)
 * the DVD+R's spin-up was still running after 20 s, and the next IOS request for the drive
 * (the "is a disc inserted" check) queued behind it in IOS and never returned: the Wii froze
 * again. So once a spin-up has timed out, nothing here asks IOS about the drive again until
 * libdi says the chain has ended -- not the insert check, not a new DI_Mount (it starts with
 * one), not DI_Close at exit (fileBrowser_DVD_shutdown). */
static vu32 *const di = (vu32 *)0xCD806000;
enum { DISR, DICVR, DICMD0, DICMD1, DICMD2, DIMAR, DILEN, DICR, DIIMM };
#define DI_CMD_READ      0xA8000000   /* pressed discs */
#define DI_CMD_READ_DVD  0xD0000000   /* DVD-Video / burned discs, on drives that take it */
#define DI_CMD_ERROR     0xE0000000
#define DI_TIMEOUT_MS    10000        /* one read: a drive retrying a bad sector takes seconds */
#define DI_MOUNT_MS      20000        /* spin-up and disc ID */
#define DI_ABORT_MS      15000        /* breaking a spin-up that timed out */
#define DI_SECTORS       16           /* one command: libiso9660 reads 16 sectors at a time */

static u32 read_cmd;          /* the read command this disc answered; 0 = not found yet */
static int spinup_pending;    /* a spin-up timed out: IOS may still be working on it */
static int drive_checked;     /* 1 = this drive reads DVDs, -1 = it cannot, 0 = not asked yet */
static int disc_dead;         /* both read commands failed: no more tries until a remount */
static u8 bounce[DI_SECTORS * 2048] ATTRIBUTE_ALIGN(32);   /* MEM1, for the drive's DMA */

static u32 ms_since(u64 t0)
{
	return ticks_to_millisecs(diff_ticks(t0, gettime()));
}

/* Start a command and wait for it: 0 done, 1 the drive reported an error, -1 too long */
static int di_run(u32 dicr, u32 timeout_ms)
{
	u64 t0 = gettime();

	di[DICR] = dicr;
	while (di[DICR] & 1) {
		if (ms_since(t0) > timeout_ms) {
			di[DISR] |= 1;   /* BRK: ask the drive to stop the command */
			t0 = gettime();
			while ((di[DICR] & 1) && ms_since(t0) < 1000)
				usleep(1000);
			return -1;
		}
		usleep(50);   /* let the other threads run: the HBC agent, the hang watchdog */
	}
	return (di[DISR] & 0x04) ? 1 : 0;   /* DEINT: an error */
}

static u32 di_error(void)
{
	di[DISR] = 0x2E;
	di[DICMD0] = DI_CMD_ERROR;
	di[DIIMM] = 0;
	return di_run(1, 1000) < 0 ? 0xFFFFFFFF : di[DIIMM];
}

/* One read of up to DI_SECTORS sectors into bounce: 0, or the drive's error (timeout: ~0) */
static u32 di_read(u32 cmd, u32 lba, u32 sectors)
{
	int r;

	DCInvalidateRange(bounce, sectors * 2048);
	di[DISR] = 0x2E;   /* clear the last error, as libdi does */
	di[DICMD0] = cmd;
	di[DICMD1] = cmd == DI_CMD_READ_DVD ? lba : lba << 9;           /* sectors, or bytes / 4 */
	di[DICMD2] = cmd == DI_CMD_READ_DVD ? sectors : sectors << 11;
	di[DIMAR] = MEM_VIRTUAL_TO_PHYSICAL(bounce);
	di[DILEN] = sectors << 11;
	r = di_run(3, DI_TIMEOUT_MS);
	if (r == 0)
		return 0;
	return r < 0 ? 0xFFFFFFFF : (di_error() & 0xFFFFFF) | 0x01000000;   /* never 0 */
}

static bool dvd_startup(void)
{
	u64 t0;

	if (DI_Init() < 0)
		return false;
	if (drive_checked < 0)
		return false;
	if (spinup_pending && (DI_GetStatus() & DVD_INIT)) {
		dvd_note("dvd: the last spin-up is still running in IOS: not asking again\n");
		drive_error = 0xFFFFFFFF;
		return false;
	}
	spinup_pending = 0;
	dvd_note("dvd: spinning up\n");
	DI_Mount();
	t0 = gettime();
	while ((DI_GetStatus() & DVD_INIT) && ms_since(t0) < DI_MOUNT_MS)
		usleep(20 * 1000);
	read_cmd = 0;
	disc_dead = 0;
	dvd_note("dvd: drive status %x after %u ms\n", DI_GetStatus(), (unsigned)ms_since(t0));
	if (DI_GetStatus() & DVD_INIT) {
		/* IOS is still working on the spin-up, and while it is, every other IOS request waits
		 * behind it -- the SD card's too: 2-3 minutes for a blank DVD-R on the bench Wii,
		 * until the disc was taken out for a DVD+R. Break the drive's command each time one
		 * is running (DISR BRK): IOS's request then fails, and libdi's chain gives up after
		 * its few retries. */
		u64 t1 = gettime();
		int breaks = 0;
		while ((DI_GetStatus() & DVD_INIT) && ms_since(t1) < DI_ABORT_MS) {
			if (di[DICR] & 1) {
				di[DISR] |= 1;
				breaks++;
			}
			usleep(100 * 1000);
		}
		dvd_note("dvd: spin-up broken off: %d breaks, drive status %x after %u ms more\n", breaks,
		         DI_GetStatus(), (unsigned)ms_since(t1));
		if (DI_GetStatus() & DVD_INIT) {
			/* Still busy: reset the drive itself. Clearing bit 10 of HW_RESETS (0x0D800194)
			 * holds the drive in reset, setting it lets it go (Dolphin's WII_IPC.cpp); the
			 * command IOS waits on then cannot complete normally, and IOS's request fails. */
			vu32 *const resets = (vu32 *)0xCD800194;
			*resets &= ~0x400;
			usleep(10 * 1000);
			*resets |= 0x400;
			t1 = gettime();
			while ((DI_GetStatus() & DVD_INIT) && ms_since(t1) < DI_ABORT_MS) {
				if (di[DICR] & 1)
					di[DISR] |= 1;
				usleep(100 * 1000);
			}
			dvd_note("dvd: drive reset: status %x after %u ms\n", DI_GetStatus(), (unsigned)ms_since(t1));
		}
		spinup_pending = (DI_GetStatus() & DVD_INIT) != 0;
		drive_error = 0xFFFFFFFF;   /* "the drive did not answer in time" */
		return false;
	}
	return (DI_GetStatus() & DVD_READY) != 0;
}

static bool dvd_inserted(void)
{
	uint32_t cover = 0;
	return DI_GetCoverRegister(&cover) == 0 && (cover & DVD_COVER_DISC_INSERTED);
}

static bool dvd_read(sec_t sector, sec_t count, void *buffer)
{
	u8 *out = (u8 *)buffer;

	while (count > 0) {
		u32 n = count > DI_SECTORS ? DI_SECTORS : count, err;

		if (disc_dead)
			return false;
		if (read_cmd)
			err = di_read(read_cmd, sector, n);
		else {
			/* The first read finds the command this disc answers, as libdi does */
			dvd_note("dvd: first read, sector %u\n", (unsigned)sector);
			u32 e0 = di_read(DI_CMD_READ_DVD, sector, n), e1 = 0;
			if (!e0)
				read_cmd = DI_CMD_READ_DVD;
			else if (!(e1 = di_read(DI_CMD_READ, sector, n)))
				read_cmd = DI_CMD_READ;
			err = read_cmd ? 0 : e1;
			dvd_note("dvd: sector %u: read 0xD0 %s %06x, 0xA8 %s %06x\n", (unsigned)sector,
			          e0 ? "failed" : "ok", (unsigned)(e0 & 0xFFFFFF), e0 ? (e1 ? "failed" : "ok") : "-",
			          (unsigned)(e1 & 0xFFFFFF));
			if (!read_cmd)
				disc_dead = 1;
		}
		if (err) {
			drive_error = err;
			dvd_note("dvd: read of sector %u failed: %s %06x\n", (unsigned)sector,
			          err == 0xFFFFFFFF ? "timeout" : "drive error", (unsigned)(err & 0xFFFFFF));
			return false;
		}
		memcpy(out, bounce, n * 2048);
		out += n * 2048;
		sector += n;
		count -= n;
	}
	return true;
}

static bool dvd_true(void) { return true; }
static bool dvd_no_write(sec_t s, sec_t n, const void *b) { (void)s; (void)n; (void)b; return false; }

static DISC_INTERFACE dvd_disc = {
	DEVICE_TYPE_WII_DVD, FEATURE_MEDIUM_CANREAD,
	dvd_startup, dvd_inserted, dvd_read, dvd_no_write, dvd_true, dvd_true
};
#define DVD_DISC (&dvd_disc)
#else
#define DVD_DISC (&__io_gcdvd)
#endif

/* Mount the disc in the drive as "dvd:". Load from DVD calls it each time it opens. */
int fileBrowser_DVD_init(fileBrowser_file* file)
{
	(void)file;
	if (mounted && hasLoadedISO && !strncmp(isoFile.name, "dvd:", 4))
		return 0;   /* the loaded game's file is open in this mount: keep it */
	if (mounted)
		ISO9660_Unmount("dvd");
	mounted = 0;
	drive_error = 0;
	dvd_note("dvd: mounting\n");

#ifdef HW_RVL
	if (*(vu32 *)0xCD800064 != 0xFFFFFFFF) {   /* HW_AHBPROT: no direct access to the drive */
		mount_error = DVD_ERR_NO_ACCESS;
		return mount_error;
	}
	if (DI_Init() < 0) {
		mount_error = DVD_ERR_NO_DRIVE;
		return mount_error;
	}
	/* Only drives made up to mid-2008 read DVDs other than Nintendo's (libdi's
	 * DI_CheckDVDSupport: firmware dated 2008-07-14 or earlier). A later one cannot, and on the
	 * bench Wii a burned disc's spin-up then never ended and stalled IOS (above). So ask the
	 * drive its date first -- one IOS request, no spin-up -- and leave a disc it cannot read
	 * alone. Once per boot, and not while a spin-up may still be running. */
	if (!drive_checked && !spinup_pending) {
		DI_DriveID id;
		memset(&id, 0, sizeof id);
		if (DI_Identify(&id) == 0) {
			drive_checked = id.rel_date <= 0x20080714 ? 1 : -1;
			dvd_note("dvd: drive rev %04x code %04x date %08x: %s\n", id.rev, id.dev_code,
			         (unsigned)id.rel_date, drive_checked > 0 ? "reads DVDs" : "cannot read DVDs");
		} else
			dvd_note("dvd: the drive did not identify itself\n");
	}
	if (drive_checked < 0) {
		mount_error = DVD_ERR_DRIVE_NO_DVD;
		return mount_error;
	}
#else
	DVD_Init();
#endif
	/* Spins the disc up and reads its directories: a few seconds */
	mounted = ISO9660_Mount("dvd", DVD_DISC);
	if (mounted)
		mount_error = 0;
#ifdef HW_RVL
	else if (spinup_pending)
		mount_error = DVD_ERR_UNREADABLE;   /* a disc is there; asking IOS now would block */
#endif
	else
		mount_error = DVD_DISC->isInserted() ? DVD_ERR_UNREADABLE : DVD_ERR_NO_DISC;
	dvd_note("dvd: mount %s (error %d, drive %06x, command %08x)\n", mounted ? "ok" : "failed",
	          mount_error, (unsigned)(drive_error & 0xFFFFFF),
#ifdef HW_RVL
	          (unsigned)read_cmd
#else
	          0u
#endif
	          );
	return mount_error;
}

/* Close libdi's handle at exit -- unless a spin-up may still be running in IOS, as the
 * close would wait behind it (the loader that runs next reloads IOS anyway) */
void fileBrowser_DVD_shutdown(void)
{
#ifdef HW_RVL
	if (!(spinup_pending && (DI_GetStatus() & DVD_INIT)))
		DI_Close();
#endif
}

/* The drive's error code from the last failed read (0 = none; 0xFFFFFF = it timed out) */
unsigned fileBrowser_DVD_drive_error(void)
{
#ifdef HW_RVL
	if (spinup_pending && (DI_GetStatus() & DVD_INIT))
		return 0xFFFFFE;   /* still busy: the disc must come out */
#endif
	return drive_error & 0xFFFFFF;
}

int fileBrowser_DVD_readDir(fileBrowser_file* file, fileBrowser_file** dir)
{
	if (!mounted)
		return mount_error ? mount_error : DVD_ERR_NO_DISC;
	return fileBrowser_libfat_readDir(file, dir);
}

int fileBrowser_DVD_open(fileBrowser_file* file)
{
	return fileBrowser_libfat_open(file);
}

int fileBrowser_DVD_readFile(fileBrowser_file* file, void* buffer, unsigned int length)
{
	return fileBrowser_libfatROM_readFile(file, buffer, length);
}

int fileBrowser_DVD_seekFile(fileBrowser_file* file, unsigned int where, unsigned int type)
{
	return fileBrowser_libfat_seekFile(file, where, type);
}

int fileBrowser_DVD_deinit(fileBrowser_file* file)
{
	/* The mount stays until the next Load from DVD: the game reads through it */
	return fileBrowser_libfatROM_deinit(file);
}
