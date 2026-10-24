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
extern long ISOgetTN(unsigned char *buffer);
}
extern char CdromId[10];
extern char CdromLabel[33];
extern char biosFileName[16];   /* libgui/gui2/biosSelect.cpp: the BIOS file the PSX BIOS setting uses */

/* A .exe is a program, not a disc: it has no label, serial, tracks or achievements */
static bool loadedIsExe(void)
{
	size_t n = strlen(isoFile.name);
	return n > 4 && !strcasecmp(isoFile.name + n - 4, ".exe");
}

void romInfoFile(char *buf, int len)
{
	const char *slash = strrchr(isoFile.name, '/');
	const char *name = slash ? slash + 1 : isoFile.name;
	size_t n = strlen(name);
	if (!n)
		snprintf(buf, len, "None loaded");
	else if (n >= (size_t)len)
		snprintf(buf, len, "...%s", name + n - (len - 4));
	else
		snprintf(buf, len, "%s", name);
}

void romInfoLabel(char *buf, int len) { snprintf(buf, len, "%s", loadedIsExe() || !CdromLabel[0] ? "None" : CdromLabel); }
void romInfoId(char *buf, int len)    { snprintf(buf, len, "%s", loadedIsExe() || !CdromId[0] ? "None" : CdromId); }

void romInfoSize(char *buf, int len)
{
	if (isoFile.size < 1024 * 1024)   /* under 1 MB in KB (rounded up): never "0 MB" */
		snprintf(buf, len, "%u KB", (isoFile.size + 1023) / 1024);
	else
		snprintf(buf, len, "%u MB", isoFile.size / 1024 / 1024);
}

void romInfoTracks(char *buf, int len)
{
	unsigned char tracks[2] = { 0, 0 };
	if (loadedIsExe() || !isoFile.name[0]) { snprintf(buf, len, "None"); return; }
	ISOgetTN(&tracks[0]);
	snprintf(buf, len, "%u", tracks[1]);
}

void romInfoSystem(char *buf, int len)
{
	const char *region = forceNTSC == FORCENTSC_ENABLE ? "NTSC (forced)" : (Config.PsxType ? "PAL" : "NTSC");
	if (Config.HLE == BIOS_USER_DEFINED)
		snprintf(buf, len, "%s, %s", region, biosFileName);
	else
		snprintf(buf, len, "%s, HLE BIOS", region);
}

void romInfoFixes(char *buf, int len)
{
	int n = 0;
	buf[0] = 0;
#define FIX(on, name) if (on) { menu::glue::appendBounded(buf, len, n++ ? ", " : ""); menu::glue::appendBounded(buf, len, name); }
	FIX(Config.hacks.gpu_slow_list_walking, "GPU list");
	FIX(Config.cycle_multiplier_override, "cycles");
	FIX(Config.hacks.gpu_busy_hack, "GPU busy");
	FIX(Config.hacks.dwActFixes, "game");
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
