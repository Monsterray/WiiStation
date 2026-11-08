/**
 * Host test of the shared page tables (Gamecube/menu/pages/*.h): the tables the menu runs, compiled
 * here with empty hooks. Checks what used to be index coupling with nothing watching it.
 *     sh tests/menu_engine/run.sh        (builds and runs this too)
 */
#include <stdio.h>
#include <string.h>
#include "../Gamecube/menu/pages/OptionsPages.h"

/* the hooks the tables name */
void applySpuReverb(void) {}
void applyFrameSkip(void) {}
void applyVideoGamma(void) {}
extern "C" void applyCopyFilter(void) {}
void Func_UsbPadTestAdvanced(void) {}
void pluginsEnter(void) {}
void pluginsLeave(void) {}
void infoMem1(char *, int) {}
void infoMem2(char *, int) {}
void infoTexPool(char *, int) {}
void infoLockedCache(char *, int) {}
void infoFontCache(char *, int) {}
void infoJitBuffer(char *, int) {}
void infoFixedBuffers(char *, int) {}
void infoPsxBuffers(char *, int) {}
void infoSpuPlugin(char *, int) {}
void infoAchFile(char *, int) {}
#define ROM_STUB(label, fn) void fn(char *, int) {}
ROM_INFO_ROWS(ROM_STUB)

static int failures, checks;
static void check(bool ok, const char *what, const char *page)
{
	checks++;
	if (!ok) { failures++; printf("FAIL %s: %s\n", page, what); }
}

int main()
{
	/* title and tab by id: the index coupling of OptPageId and PAGES[] */
	static const struct { int id; const char *title, *tab; } WANT[] = {
		{ OPT_PAGE_SOUND, "Advanced Sound", NULL }, { OPT_PAGE_ADVGFX, "Advanced Graphics", NULL },
		{ OPT_PAGE_PLUGINS, "Advanced", "CPU" }, { OPT_PAGE_STORAGE, "Advanced", "CD" },
		{ OPT_PAGE_MEMORY, "Memory", NULL }, { OPT_PAGE_ADV_GPU, "Advanced", "GPU" },
		{ OPT_PAGE_ADV_INPUT, "Advanced", "Input" }, { OPT_PAGE_ADV_SPU, "Advanced", "SPU" },
		{ OPT_PAGE_ACHIEVEMENTS, "Achievements", NULL }, { OPT_PAGE_ISOINFO, "ISO Info", NULL },
	};
	for (unsigned k = 0; k < sizeof WANT / sizeof WANT[0]; k++)
	{
		const OptPage &p = PAGES[WANT[k].id];
		check(!strcmp(p.title, WANT[k].title), "title at its id", WANT[k].title);
		check((p.tab == NULL) == (WANT[k].tab == NULL) && (!p.tab || !strcmp(p.tab, WANT[k].tab)), "tab at its id", WANT[k].title);
	}
	/* the Advanced tabs run in ADV_TABS order and each is a tabbed page */
	for (int t = 0; t < NUM_TABS; t++)
		check(PAGES[ADV_TABS[t]].tab != NULL, "ADV_TABS entry has a tab", PAGES[ADV_TABS[t]].title);

	for (int i = 0; i < OPT_PAGE_COUNT; i++)
	{
		const OptPage &p = PAGES[i];
		check(p.nrows > 0 && p.nrows <= OPT_MAX_ROWS, "row count in 1..OPT_MAX_ROWS", p.title);
		check(p.returnSubmenu == RETURN_CURRENT_ISO || p.returnSubmenu == OPT_RET_GENERAL ||
		      p.returnSubmenu == OPT_RET_VIDEO || p.returnSubmenu == OPT_RET_AUDIO, "return target is a Settings tab or Current ISO", p.title);
		check(p.standard == 0 || p.footer != NULL || i == OPT_PAGE_ISOINFO, "a standard page has its footer (ISO Info has none yet)", p.title);
		check(p.tab == NULL || p.standard != 0, "a tabbed Advanced page is a standard page", p.title);
		check(p.help == NULL || p.nhelp <= OPT_MAX_HELP, "help lines fit", p.title);
		for (int r = 0; r < p.nrows; r++)
		{
			const OptRow &o = p.rows[r];
			if (o.kind == OPT_CYCLE || o.kind == OPT_RADIO)
			{
				check(o.key && *o.key, "a setting row has a key", p.title);
				for (int q = r + 1; q < p.nrows; q++)
					check(!(p.rows[q].key && o.key && p.rows[q].kind != OPT_ACTION && !strcmp(p.rows[q].key, o.key)), "a key once per page", p.title);
			}
			else if (o.kind == OPT_INFO)
				check(o.info && o.label, "a readout has a label and a function", p.title);
			else if (o.kind == OPT_ACTION)
				check(o.apply && o.label && o.key, "an action has a label, a text and a function", p.title);
			else
				check(false, "a known row kind", p.title);
		}
	}
	printf("pages: %d checks, %d failed\n", checks, failures);
	return failures;
}
