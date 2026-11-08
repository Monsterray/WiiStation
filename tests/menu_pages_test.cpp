/**
 * Host test of the shared page tables (Gamecube/menu/pages/*.h): the tables the menu runs, compiled
 * here with empty hooks. Checks what used to be index coupling with nothing watching it.
 *     sh tests/menu_engine/run.sh        (builds and runs this too)
 */
#include <stdio.h>
#include <string.h>
#include "../Gamecube/menu/pages/OptionsPages.h"
#include "../Gamecube/menu/pages/SettingsPages.h"
#include "../Gamecube/menu/pages/ActionPages.h"

/* the hooks the action menus name */
void Func_LoadROM() {}
void Func_CurrentROM() {}
void Func_Settings() {}
void Func_Players() {}
void Func_Credits() {}
void Func_ExitToLoader() {}
void Func_PlayGame() {}
void Func_ShowRomInfo() {}
void Func_ResetROM() {}
void Func_SwapCD() {}
void Func_LoadSave() {}
void Func_SaveGame() {}
void Func_LoadState() {}
void Func_SaveState() {}
void Func_StateCycle() {}
void Func_Screenshot() {}
void Func_AchievementsList() {}

/* the hooks the Settings tables name */
void Func_PluginsPage(void) {}
void Func_AchievementsPage(void) {}
void Func_MemoryPage(void) {}
void Func_AdvGfxPage(void) {}
void Func_AdvancedSound(void) {}
void Func_BiosSelectHLE(void) {}
void Func_BiosSelectSD(void) {}
void Func_BiosSelectUSB(void) {}
void Func_BiosSelectDVD(void) {}
void Func_ExecuteBios(void) {}
void Func_SaveSettingsSD(void) {}
void Func_SaveSettingsUSB(void) {}
void Func_SaveSettingsSeparately(void) {}
void Func_LoadSettingsSD(void) {}
void Func_LoadSettingsUSB(void) {}
void Func_LoadSettingsGame(void) {}
void Func_SelectLanguage(void) {}
void Func_FrameRateCycle(void) {}
void Func_ForceNTSC(void) {}
void Func_ConfigureInput(void) {}
void Func_ConfigureButtons(void) {}
void Func_SaveButtonsSD(void) {}
void Func_SaveButtonsUSB(void) {}
void Func_ToggleXa(void) {}
void Func_ToggleCdda(void) {}
void Func_SoundSyncOff(void) {}
void Func_SoundSyncTempo(void) {}
void Func_SoundSyncRate(void) {}
void Func_CardType1(void) {}
void Func_CardType2(void) {}
void Func_UsbPadTest(void) {}
bool litXa(void) { return false; }
bool litCdda(void) { return false; }
bool litSyncOff(void) { return false; }
bool litSyncTempo(void) { return false; }
bool litSyncRate(void) { return false; }
const char *textCard0(void) { return "Off"; }
const char *textCard1(void) { return "Off"; }
void applyOriginalMode(void) {}
void applyTrapFilter(void) {}
void applyBilinear(void) {}
void applyInterpolation(void) {}

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
	/* The Settings tabs: tab order, rows and buttons */
	{
		bool seen[SET_TAB_COUNT] = { false };
		for (int p = 0; p < SET_TAB_COUNT; p++)
		{
			check(SET_TAB_AT_POS[p] >= 0 && SET_TAB_AT_POS[p] < SET_TAB_COUNT && !seen[SET_TAB_AT_POS[p]], "each tab once in the tab row", SET_TAB_NAMES[p]);
			seen[SET_TAB_AT_POS[p] < SET_TAB_COUNT && SET_TAB_AT_POS[p] >= 0 ? SET_TAB_AT_POS[p] : 0] = true;
		}
		for (int t = 0; t < SET_TAB_COUNT; t++)
		{
			const SetTab &tab = SET_TABS_DEF[t];
			const char *tn = SET_TAB_NAMES[t];
			check(tab.nRows > 0 && tab.nRows <= eng::MAX_ROWS, "row count in 1..MAX_ROWS", tn);
			for (int r = 0; r < tab.nRows; r++)
			{
				const SetRow &row = tab.rows[r];
				const int n = setRowSegs(row);
				check(n >= 1 && n <= eng::MAX_SLOTS, "a row has 1..MAX_SLOTS buttons", tn);
				check(row.kind == eng::ROW_CHOICE || row.kind == eng::ROW_BUTTONS, "a Settings row is a choice or buttons", tn);
				check(row.plain >= 0 && (row.kind == eng::ROW_CHOICE ? row.plain < n : row.plain == 0), "plain counts only the tail of a choice row", tn);
				check(!row.noLit || row.kind == eng::ROW_CHOICE, "noLit is for a choice row", tn);
				check(!row.prefix || row.kind == eng::ROW_BUTTONS, "prefix is for a buttons row", tn);
				check(row.kind != eng::ROW_CHOICE || row.label || row.seg[0].key, "a choice row has a label or a first setting to name it", tn);
				for (int c = 0; c < n; c++)
				{
					const SetSeg &g = row.seg[c];
					const bool plainSeg = row.kind == eng::ROW_CHOICE && c >= n - row.plain;
					check(g.name && *g.name, "a button has a name", tn);
					if (g.key)
					{
						check(!strcmp(g.name, g.key), "a setting's button is named by its key", g.name);
						check(g.value >= 0 || g.value == SEGV_FLAG || g.value == SEGV_NEXT, "a setting's button is a choice, a flag or a cycle", g.name);
						check(!g.text || g.value >= 0, "only a choice has its own text (Off)", g.name);
					}
					else
					{
						check(g.act && g.text, "an action has a text and a function", g.name);
						check(g.value == 0 || g.value == SEGV_GAME, "an action's value is 0 or SEGV_GAME", g.name);
					}
					check(g.value != SEGV_NEXT || row.kind == eng::ROW_BUTTONS, "a cycle sits in a buttons row", g.name);
					check(!(row.kind == eng::ROW_CHOICE && !plainSeg) || g.value >= 0 || g.lit, "a choice segment is a value, or has a lit function", g.name);
					check(!g.textFn || g.value == SEGV_NEXT, "worked-out text is for a cycle", g.name);
					check(!row.prefix || (g.value == SEGV_NEXT && g.textFn), "a prefix row holds only worked-out cycles", g.name);
					/* a name once per tab, except a choice's values */
					for (int r2 = r; r2 < tab.nRows; r2++)
						for (int c2 = (r2 == r ? c + 1 : 0); c2 < setRowSegs(tab.rows[r2]); c2++)
						{
							const SetSeg &h = tab.rows[r2].seg[c2];
							check(strcmp(g.name, h.name) || (g.key && h.key && g.value >= 0 && h.value >= 0 && g.value != h.value), "a name is once on a tab", g.name);
						}
				}
			}
			if (tab.footer)
				check(true, "footer present", tn);
		}
		check(COUNT(SYS_FOOTER) == COUNT(GEN_ROWS) && COUNT(VID_FOOTER) == COUNT(VID_ROWS) &&
		      COUNT(INP_FOOTER) == COUNT(INP_ROWS) && COUNT(AUD_FOOTER) == COUNT(AUD_ROWS), "one footer entry per row", "Settings");
		/* a name once over all the tabs, so a script finds one button (a choice's values share one) */
		for (int t = 0; t < SET_TAB_COUNT; t++)
			for (int r = 0; r < SET_TABS_DEF[t].nRows; r++)
				for (int c = 0; c < setRowSegs(SET_TABS_DEF[t].rows[r]); c++)
				{
					const SetSeg &g = SET_TABS_DEF[t].rows[r].seg[c];
					for (int t2 = t + 1; t2 < SET_TAB_COUNT; t2++)
						for (int r2 = 0; r2 < SET_TABS_DEF[t2].nRows; r2++)
							for (int c2 = 0; c2 < setRowSegs(SET_TABS_DEF[t2].rows[r2]); c2++)
								check(strcmp(g.name, SET_TABS_DEF[t2].rows[r2].seg[c2].name), "a name is on one tab only", g.name);
				}
	}
	/* The action menus: Main and Current ISO */
	{
		const ActionMenuDef *defs[] = { &MAIN_MENU, &CUR_MENU };
		for (int m = 0; m < 2; m++)
		{
			const ActionMenuDef &d = *defs[m];
			check(d.nActs > 0 && d.nActs <= 12, "1..MAX_ACTIONS buttons", d.name);
			check(d.nRows > 0 && d.nRows <= eng::MAX_ROWS, "row count in 1..MAX_ROWS", d.name);
			for (int k = 0; k < d.nActs; k++)
			{
				const ActionDef &a = d.acts[k];
				check(a.text && *a.text && strlen(a.text) < 24, "a button has a text that fits its label", d.name);
				check(a.click != NULL, "a button has a function", a.text);
				check(a.row >= 0 && a.row < d.nRows && a.slot >= 0 && a.slot < eng::MAX_SLOTS, "a button sits in a row and slot", a.text);
				check(a.width > 0.0f, "a button has a width", a.text);
				for (int j = k + 1; j < d.nActs; j++)
					check(!(d.acts[j].row == a.row && d.acts[j].slot == a.slot), "one button per place", a.text);
			}
			/* a row holds slots 0..n-1 with none missing */
			for (int r = 0; r < d.nRows; r++)
			{
				int n = 0, top = -1;
				for (int k = 0; k < d.nActs; k++)
					if (d.acts[k].row == r) { n++; if (d.acts[k].slot > top) top = d.acts[k].slot; }
				check(n > 0 && top == n - 1, "a row's slots are 0..n-1", d.name);
			}
			for (int l = 0; l < d.nLinks; l++)
			{
				bool from = false, to = false;
				for (int k = 0; k < d.nActs; k++)
				{
					from |= d.acts[k].row == d.links[l].row && d.acts[k].slot == d.links[l].slot;
					to |= d.acts[k].row == d.links[l].toRow && d.acts[k].slot == d.links[l].toSlot;
				}
				check(from && to, "a link joins two buttons", d.name);
			}
		}
		check(!strcmp(MAIN_ACTS[MAIN_PLAY].text, "Play Game"), "MAIN_PLAY is the Play button", "Main");
		check(!strncmp(CUR_ACTS[CUR_SLOT_BUTTON].text, "Slot", 4), "CUR_SLOT_BUTTON is the Slot button", "Current ISO");
	}
	printf("pages: %d checks, %d failed\n", checks, failures);
	return failures;
}
