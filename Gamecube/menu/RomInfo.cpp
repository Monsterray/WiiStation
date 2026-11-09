/* The loaded game's information rows (RomInfo.h): each function writes one value. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "RomInfo.h"
#include "MenuGlue.h"
#include "../wiiSXconfig.h"
#include "../../psxcommon.h"
#include "../../achievements.h"

extern "C" {
#include "../fileBrowser/fileBrowser.h"   /* isoFile */
#include "../fileBrowser/explorer_info.h" /* ei_format */
extern long ISOgetTN(unsigned char *buffer);
extern unsigned int cdrIsoMultidiskCount, cdrIsoMultidiskSelect;
}
extern char CdromId[10];
extern char CdromLabel[33];
extern char biosFileName[16];   /* libgui/gui2/biosSelect.cpp: the BIOS file the PSX BIOS setting uses */
const char *romGameSettingsPath(void);   /* GamecubeMain.cpp: the game's own settings file, or NULL */

static const char *fileName(void)
{
	const char *slash = strrchr(isoFile.name, '/');
	return slash ? slash + 1 : isoFile.name;
}

/* A .exe is a program, not a disc: it has no label, serial, tracks or achievements */
static bool loadedIsExe(void)
{
	size_t n = strlen(isoFile.name);
	return n > 4 && !strcasecmp(isoFile.name + n - 4, ".exe");
}

static bool nothingLoaded(void) { return !isoFile.name[0]; }

/* "SCUS94228" -> "SCUS-94228" (the form printed on the disc); anything else as it is */
static void dashedSerial(char *out, int len)
{
	if (strlen(CdromId) > 4 && CdromId[4] != '-')
		snprintf(out, len, "%.4s-%s", CdromId, CdromId + 4);
	else
		snprintf(out, len, "%s", CdromId);
}

void romInfoGame(char *buf, int len)
{
	const char *name = fileName();
	const char *dot = strrchr(name, '.');
	int n = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
	if (!n)
		snprintf(buf, len, "None loaded");
	else if (n >= len)
		snprintf(buf, len, "%.*s...", len - 4, name);	/* the start names the game; the end is tags */
	else
		snprintf(buf, len, "%.*s", n, name);
}

void romInfoDisc(char *buf, int len)
{
	char serial[16];
	if (nothingLoaded()) { snprintf(buf, len, "None"); return; }
	if (loadedIsExe()) { snprintf(buf, len, "None (a program)"); return; }
	dashedSerial(serial, sizeof serial);
	if (!serial[0])
		snprintf(serial, sizeof serial, "No serial");
	if (cdrIsoMultidiskCount > 1)
		snprintf(buf, len, "%s, disc %u of %u", serial, cdrIsoMultidiskSelect + 1, cdrIsoMultidiskCount);
	else if (CdromLabel[0])
		snprintf(buf, len, "%s, label %s", serial, CdromLabel);
	else
		snprintf(buf, len, "%s", serial);
}

void romInfoImage(char *buf, int len)
{
	char size[16];
	unsigned char tracks[2] = { 0, 0 };
	const char *fmt;
	if (nothingLoaded()) { snprintf(buf, len, "None"); return; }
	fmt = ei_format(isoFile.name);
	if (isoFile.size < 1024 * 1024)   /* under 1 MB in KB (rounded up): never "0 MB" */
		snprintf(size, sizeof size, "%u KB", (isoFile.size + 1023) / 1024);
	else
		snprintf(size, sizeof size, "%u MB", isoFile.size / 1024 / 1024);
	if (loadedIsExe()) { snprintf(buf, len, "EXE, %s", size); return; }
	ISOgetTN(&tracks[0]);
	snprintf(buf, len, "%s, %u track%s, %s", fmt ? fmt : "Disc", tracks[1], tracks[1] == 1 ? "" : "s", size);
}

void romInfoSystem(char *buf, int len)
{
	const char *bios = Config.HLE == BIOS_USER_DEFINED ? biosFileName : "HLE BIOS";
	if (forceNTSC == FORCENTSC_ENABLE && Config.PsxType)
		snprintf(buf, len, "PAL, forced to NTSC, %s", bios);
	else
		snprintf(buf, len, "%s, %s", Config.PsxType ? "PAL, 50 Hz" : "NTSC, 60 Hz", bios);
}

void romInfoCards(char *buf, int len)
{
	char serial[16];
	const bool on1 = memCard[0] == MEMCARD_ENABLE, on2 = memCard[1] == MEMCARD_ENABLE;
	const bool game1 = on1 && memCardFile[0] == MEMCARDFILE_PER_GAME, game2 = on2 && memCardFile[1] == MEMCARDFILE_PER_GAME;
	dashedSerial(serial, sizeof serial);
	if (!on1 && !on2)
		snprintf(buf, len, "Off");
	else if (on1 && on2 && game1 != game2)		/* one card per game, the other shared */
		snprintf(buf, len, "Card %d per game, card %d shared", game1 ? 1 : 2, game1 ? 2 : 1);
	else if ((game1 || game2) && serial[0] && !loadedIsExe())
		snprintf(buf, len, "Per game (%s)", serial);
	else if (game1 || game2)
		snprintf(buf, len, "Per game");
	else
		snprintf(buf, len, "Shared");
}

void romInfoSettings(char *buf, int len)
{
	const char *path = romGameSettingsPath();
	if (!path) { snprintf(buf, len, "Global"); return; }
	const char *slash = strrchr(path, '/');
	snprintf(buf, len, "This Game (%s)", slash ? slash + 1 : path);
}

void romInfoFixes(char *buf, int len)
{
	int n = 0;
	buf[0] = 0;
#define FIX(on, name) if (on) { menu::glue::appendBounded(buf, len, n++ ? ", " : ""); menu::glue::appendBounded(buf, len, name); }
	FIX(Config.hacks.gpu_slow_list_walking, "GPU list");
	FIX(Config.cycle_multiplier_override, "Cycles");
	FIX(Config.hacks.gpu_busy_hack, "GPU busy");
	FIX(Config.hacks.dwActFixes, "Game fix");
	FIX(Config.hacks.lightrec_hacks, "Lightrec");
	FIX(Config.pR3000Fix, "pR3000");
#undef FIX
	if (!n) snprintf(buf, len, "None");
}

void romInfoAchievements(char *buf, int len) { snprintf(buf, len, "%s", achievements_status()); }

void romInfoText(char *text, int size)
{
	static const struct { const char *label; void (*value)(char *, int); } rows[] = {
#define ROM_INFO_LINE(label, fn) { label, fn },
		ROM_INFO_ROWS(ROM_INFO_LINE)
#undef ROM_INFO_LINE
	};
	char value[32], line[64];
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
		rows[i].value(value, sizeof value);
		snprintf(line, sizeof line, "%s: %s\n", rows[i].label, value);
		menu::glue::appendBounded(text, size, line);
	}
}
