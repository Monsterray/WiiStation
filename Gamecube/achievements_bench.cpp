/* Test-only achievement evidence for the bench (ACHIEVEMENTS_BENCH). The file is empty in
 * product builds. The bench scripts read the lines below from sd:/wiistation/perf.log. */
#include "achievements_bench.h"

#ifdef ACHIEVEMENTS_BENCH
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../psxcommon.h"
#include "../achievements.h"
#include "fileBrowser/fileBrowser.h"
#include "fileBrowser/fileBrowser-libfat.h"
#include "wiiSXconfig.h"

char achievementBenchRoot[256];

extern u32 frame_counter;          /* psxcounters.c: +1 per vblank */
extern fileBrowser_file isoFile;   /* GamecubeMain.cpp: the ISO file */
extern "C" void SysReset();        /* GamecubeMain.cpp, C linkage there */
int loadISOSwap(fileBrowser_file *file);
extern char CdromId[10];           /* the game's serial, set at load */

/* Real-game benchmarks must never load or let chain cleanup delete a personal card.
 * Fail closed before selecting the private SD directory. */
void achievements_bench_isolate_cards(void)
{
	if (!achievementBenchRoot[0]) return;
	if (strncmp(achievementBenchRoot, "sd:/wiistation/bench-", 21) ||
	    strlen(achievementBenchRoot) > 100 || strstr(achievementBenchRoot, ".."))
		exit(1);
	nativeSaveDevice = NATIVESAVEDEVICE_SD;
	snprintf(saveDir_libfat_Default.name, sizeof saveDir_libfat_Default.name,
		"%s/cards", achievementBenchRoot);
}

/* Called at each chain end, before the chain's log line. The reset and disc-swap checks
 * run only for the non-input chains, and they restore frame_counter afterwards. */
void achievements_bench_chain_end(FILE *f, int has_input)
{
	fprintf(f, "achievement-bench: game=%s frames=%u count=%u earned=%u enabled=%u\n",
		CdromId, (unsigned)frame_counter, achievements_count(), achievements_earned(),
		(unsigned)achievementsEnabled);
	if (has_input && !achievementBenchRoot[0]) {
		unsigned completed_frames = frame_counter;
		unsigned earned = achievements_earned();
		SysReset();
		fprintf(f, "achievement-bench-reset: core=%u earned=%u expected=%u count=%u\n",
			(unsigned)Config.Cpu, achievements_earned(), earned, achievements_count());
		fileBrowser_file swap = isoFile;
		int result = loadISOSwap(&swap);
		fprintf(f, "achievement-bench-swap: core=%u result=%d count=%u earned=%u\n",
			(unsigned)Config.Cpu, result, achievements_count(), achievements_earned());
		frame_counter = completed_frames; /* keep the completed chain's evidence */
	}
}

/* A frozen hardware test returns to HBC for outbound collection, rather than powering off. */
int achievements_bench_hbc_return(void) { return 1; }
#endif
