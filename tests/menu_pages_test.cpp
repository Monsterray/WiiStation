/**
 * Host test of the shared page tables (Gamecube/menu/pages/*.h): the tables the menu runs, compiled
 * here with empty hooks. Checks what used to be index coupling with nothing watching it.
 *     sh tests/menu_engine/run.sh        (builds and runs this too)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Gamecube/menu/pages/OptionsPages.h"
#include "../Gamecube/menu/pages/SettingsPages.h"
#include "../Gamecube/menu/pages/ActionPages.h"

#include "menu_engine/font_metrics.h"

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
void Func_AchievementsMode(void) {}
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

/* ---- The shipped pages through the real engine ----
 * Each page is built by the same function the Wii runs (buildActionPage, buildSettingsPage,
 * buildOptionsPage) and put through layoutPage, linkFocus, checkLayout and checkFooters. The texts
 * the program works out at run time come from the real tables: the settings' labels and choice
 * texts are read from Gamecube/GamecubeMain.cpp (OPTIONS and the OPT_* arrays). */

using namespace menu::engine;

static void fail(const char *where, const char *what) { check(false, what, where); }
#include "menu_engine/probe.h"

/* No STL: the same file builds with the MSVC library under clang++ and with devkitPro's g++. */
enum { MAX_OPTS = 400, MAX_ARRAYS = 64, MAX_TEXTS = 16 };
struct HostArray { char name[40]; const char *t[MAX_TEXTS]; int n; };
struct HostOpt { char key[40]; SettingOption so; const char *choices[MAX_TEXTS]; };
static HostArray hostArrays[MAX_ARRAYS];
static int nHostArrays;
static HostOpt hostOpts[MAX_OPTS];
static int nHostOpts;
static char hostPool[96 * 1024];
static size_t hostPoolUsed;
static char hostValue;

static const char *keep(const char *s)
{
	char *p = hostPool + hostPoolUsed;
	hostPoolUsed += strlen(s) + 1;
	if (hostPoolUsed > sizeof hostPool) { printf("host pool full\n"); exit(2); }
	return strcpy(p, s);
}

/* The top-level comma-separated fields of s, in place: trimmed, a quoted one unquoted. */
static int splitFields(char *s, char *out[], int max)
{
	int n = 0, depth = 0;
	bool q = false;
	char *start = s, *w = s;
	for (char *r = s;; r++) {
		const char c = *r;
		if (q) {
			if (c == '\\' && r[1]) { *w++ = *++r; continue; }
			if (c == '"') { q = false; continue; }
			if (!c) break;
			*w++ = c;
			continue;
		}
		if (c == '"') { q = true; continue; }
		if (c == '(' || c == '{') depth++;
		if (c == ')' || c == '}') depth--;
		if ((c == ',' && depth == 0) || !c) {
			*w = 0;
			while (*start == ' ' || *start == '\t') start++;
			for (char *e = w; e > start && (e[-1] == ' ' || e[-1] == '\t'); e--) e[-1] = 0;
			if (n < max) out[n++] = start;
			start = w = r + 1;
			if (!c) break;
			continue;
		}
		*w++ = c;
	}
	return n;
}

static HostArray *findArray(const char *name)
{
	for (int i = 0; i < nHostArrays; i++)
		if (!strcmp(hostArrays[i].name, name)) return &hostArrays[i];
	return NULL;
}

static bool loadSettingsTable(const char *root)
{
	char path[512], line[2048], *fl[16];
	snprintf(path, sizeof path, "%s/Gamecube/GamecubeMain.cpp", root);
	FILE *f = fopen(path, "r");
	bool inTable = false;
	if (!f)
		return false;
	while (fgets(line, sizeof line, f)) {
		char *o = strchr(line, '{'), *e = strrchr(line, '}');
		if (!strncmp(line, "static const char* const OPT_", 29) && o && e && nHostArrays < MAX_ARRAYS) {
			HostArray &a = hostArrays[nHostArrays++];
			char *a0 = strstr(line, "OPT_"), *b = strstr(line, "[]");
			*e = 0;
			snprintf(a.name, sizeof a.name, "%.*s", (int)(b - a0), a0);
			a.n = splitFields(o + 1, (char **)a.t, MAX_TEXTS);
			for (int k = 0; k < a.n; k++) a.t[k] = keep(a.t[k]);
		} else if (strstr(line, "OPTIONS[] =")) {
			inTable = true;
		} else if (inTable && !strncmp(line, "};", 2)) {
			break;
		} else if (inTable && o && e && strstr(line, "{ \"") && nHostOpts < MAX_OPTS) {
			*e = 0;
			const int n = splitFields(o + 1, fl, 16);
			if (n < 7 || !strcmp(fl[1], "NULL") || !strcmp(fl[5], "NULL"))
				continue;		/* a section, or a setting with no menu row */
			HostOpt &h = hostOpts[nHostOpts++];
			snprintf(h.key, sizeof h.key, "%s", fl[0]);
			h.so.key = h.key;
			h.so.value = &hostValue;
			h.so.min = 0;
			h.so.label = keep(fl[5]);
			h.so.help = n > 7 && strcmp(fl[7], "NULL") ? keep(fl[7]) : NULL;
			if (strcmp(fl[6], "NULL")) {
				const HostArray *a = findArray(fl[6]);
				if (!a) { printf("no choice array %s\n", fl[6]); exit(2); }
				for (int k = 0; k < a->n; k++) h.choices[k] = a->t[k];
				h.so.choices = h.choices;
				h.so.max = (char)(a->n - 1);
			}
		}
	}
	fclose(f);
	return nHostOpts > 0;
}

static const SettingOption *lookup(const char *key)
{
	for (int i = 0; i < nHostOpts; i++)
		if (!strcmp(hostOpts[i].key, key)) return &hostOpts[i].so;
	return NULL;
}

/* A problem the test knows and the owner has not decided on: it is listed, never silently passed. */
struct KnownProblem { const char *page; int kind; const char *why; };
static const KnownProblem KNOWN[] = {
	{ NULL, PROB_PAST_EDGE, NULL },	/* no entry yet: the list ends at a NULL page after the last entry */
};

static bool known(const char *page, int kind)
{
	for (unsigned k = 0; k < sizeof KNOWN / sizeof KNOWN[0]; k++)
		if (KNOWN[k].page && !strcmp(KNOWN[k].page, page) && KNOWN[k].kind == kind)
			return true;
	return false;
}

/* layoutPage + linkFocus + checkLayout + checkFooters on one built page. */
static void checkPage(const Page &p, const char *name)
{
	static Layout lay;
	static Problem probs[MAX_PROBLEMS];
	check(layoutPage(p, kFont, lay) >= 0, "layoutPage accepts the page", name);
	if (layoutPage(p, kFont, lay) < 0)
		return;
	linkFocus(p, lay);
	const int n = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	for (int k = 0; k < n && k < MAX_PROBLEMS; k++) {
		const Problem &q = probs[k];
		char what[160];
		snprintf(what, sizeof what, "layout: %s '%s'%s%s", problemName(q.kind), lay.c[q.a].text,
		         q.b >= 0 ? " and " : "", q.b >= 0 ? lay.c[q.b].text : "");
		if (known(name, q.kind)) { checks++; printf("known %s: %s\n", name, what); }
		else check(false, what, name);
	}
	check(checkFooters(p, kFont) == 0, "every help line fits (checkFooters)", name);
	check(checkSymmetry(name, p, lay) == 0, "focus links are two-way", name);
	check(n < MAX_PROBLEMS, "checkLayout did not run out of room", name);
	if (p.tabs && p.standard) {
		int tws[8];
		const int total = stdTabWidths(kFont, p.tabs, p.nTabs, tws);
		check(((int)MENU_W - total) / 2 >= (int)MENU_EDGE, "the tab strip stays inside the screen margin", name);
	}
}

/* Every value of every Choice and Cycle row, laid out with the real font: a name that does not fit shows up here.
 * (A built page shows one value per cycle; this is the rest.) */
static void checkValues(SetPage &ob, const char *name)
{
	static Layout lay;
	static Problem probs[MAX_PROBLEMS];
	for (int r = 0; r < ob.page.nRows; r++) {
		Row &row = ob.rows[r];
		if (row.kind != ROW_CHOICE && row.kind != ROW_CYCLE)
			continue;
		const int saved = row.value;
		for (int v = row.lo; v < row.lo + row.count; v++) {
			row.value = v;
			check(layoutPage(ob.page, kFont, lay) >= 0, "layoutPage accepts every value", name);
			linkFocus(ob.page, lay);
			check(checkLayout(ob.page, kFont, lay, probs, MAX_PROBLEMS) == 0, "every value of every row lays out clean", name);
		}
		row.value = saved;
	}
}

/* The shipped Advanced Graphics page: two columns on one shared grid (rows 1..8 in cells), its choice row on top. */
static void checkAdvGfxGrid(const Page &p, const Row *rows)
{
	static Layout lay;
	const char *name = "Advanced Graphics grid";
	check(layoutPage(p, kFont, lay) >= 0 && p.nRows == 9 && p.columns == 2, "the page is nine rows in two columns", name);
	linkFocus(p, lay);
	static const int cells[4][2] = { { 1, 2 }, { 3, 4 }, { 5, 6 }, { 7, 8 } };
	static const float ys[4] = { 146.0f, 202.0f, 258.0f, 314.0f };	/* Dithering is at 90 */
	const int before = failures;
	checkGrid(lay, rows, cells, ys, 4);
	check(failures == before, "the cycle pairs are on the grid", name);
	const Key kc = { ROLE_CHOICE, 0, 0 };
	const int c = findCtrl(lay, kc);
	check(c >= 0 && lay.c[c].rect.y == 90.0f, "the Dithering row did not move", name);
	/* Down and Up keep the column, Left and Right change it and wrap, the bottom line wraps to the top by column. */
	static const struct { int row, dir, to; } F[] = {
		{ 1, DIR_RIGHT, 2 }, { 2, DIR_LEFT, 1 }, { 2, DIR_RIGHT, 1 }, { 1, DIR_LEFT, 2 },
		{ 1, DIR_DOWN, 3 },  { 2, DIR_DOWN, 4 }, { 3, DIR_UP, 1 },    { 4, DIR_UP, 2 },
		{ 3, DIR_DOWN, 5 },  { 4, DIR_DOWN, 6 }, { 6, DIR_RIGHT, 5 },
		{ 5, DIR_DOWN, 7 },  { 6, DIR_DOWN, 8 }, { 7, DIR_UP, 5 },    { 8, DIR_UP, 6 },
		{ 7, DIR_DOWN, 0 },  { 8, DIR_DOWN, 0 },
	};
	for (unsigned k = 0; k < sizeof F / sizeof F[0]; k++) {
		const Key kf = { ROLE_BUTTON, F[k].row, 0 };
		const int a = findCtrl(lay, kf);
		const int t = a < 0 ? -1 : lay.c[a].link[F[k].dir];
		check(t >= 0 && lay.c[t].row == F[k].to, "a focus link goes to the wrong cell", name);
	}
}

/* A readout's text is whatever the emulator reports (OptionsFrame.cpp's info functions, not host code):
 * the longest each can be. A readout with no entry here is the length of a MEM1 line. */
static const struct { const char *label, *text; } WORST[] = {
	{ "Texture pool",   "999 textures, 9999 + 9999 KB" },
	{ "Locked cache",   "16 / 16 KB (spu-gauss, tex-tile)" },		/* lc_usage(): the region names */
	{ "Font glyph cache", "1024 / 1024 KB" },
	{ "Recompiler buffer", "1536 of 1536 KB used" },
	{ "PS1 RAM + VRAM", "2048 + 2048 KB" },
	{ "SPU + BIOS buffers", "64 + 512 KB" },
};
static const OptPage *readoutPage;
static const char *readoutHost(int row)
{
	for (unsigned k = 0; k < sizeof WORST / sizeof WORST[0]; k++)
		if (readoutPage->rows[row].label && !strcmp(readoutPage->rows[row].label, WORST[k].label))
			return WORST[k].text;
	return "24576 used, 24576 free KB";
}

/* The text a Settings button shows: the longest the setting can show, for a cycle. */
static const char *segTextHost(const SetSeg &g, int)
{
	if (g.textFn) return g.textFn();
	if (g.text) return g.text;
	const SettingOption *s = g.key ? lookup(g.key) : NULL;
	if (!s) return g.name;
	if (g.value == SEGV_FLAG) return s->label;
	if (g.value != SEGV_NEXT) return s->choices[g.value];
	const char *best = s->choices[0];
	for (int v = 1; v <= s->max; v++)
		if (strlen(s->choices[v]) > strlen(best)) best = s->choices[v];
	return best;
}
static bool segLitHost(const SetSeg &, int) { return false; }
static const SettingOption *segSettingHost(const SetSeg &g)
{
	return g.key ? lookup(g.key) : NULL;
}

static void checkShippedPages(const char *root)
{
	static ActionPage ap;
	static SetPage sp;
	static SetPage ob;
	const ActionMenuDef *defs[] = { &MAIN_MENU, &CUR_MENU };
	const int before = checks;

	if (!loadSettingsTable(root)) {
		check(false, "GamecubeMain.cpp: the settings table is readable", "shipped pages");
		return;
	}
	for (int m = 0; m < 2; m++) {
		const char *texts[ACTION_MAX];
		for (int k = 0; k < defs[m]->nActs; k++)
			texts[k] = defs[m]->acts[k].text;
		check(buildActionPage(*defs[m], texts, ap), "the action table fits the engine", defs[m]->name);
		checkPage(ap.page, defs[m]->name);
		if (m == 1) {	/* the hand-set links no rule can check: Swap CD <-> Slot, Save State -> Slot */
			static Layout lay;
			layoutPage(ap.page, kFont, lay);
			linkFocus(ap.page, lay);
			const int swap = lay.slot[0][1], slot = lay.slot[3][1], save = lay.slot[4][0];
			check(lay.c[swap].link[DIR_UP] == slot && lay.c[slot].link[DIR_DOWN] == swap, "Swap CD Up is Slot, Slot Down is Swap CD", "Current ISO");
			check(lay.c[save].link[DIR_LEFT] == slot && lay.c[save].link[DIR_RIGHT] == slot, "Save State Left and Right are Slot", "Current ISO");
		}
	}
	/* Settings: every tab. Its seg texts name settings that must exist. */
	for (int t = 0; t < SET_TAB_COUNT; t++) {
		const SetBind bind = { segTextHost, segLitHost, segSettingHost, NULL, true, false };
		bool keys = true;
		for (int r = 0; r < SET_TABS_DEF[t].nRows; r++)
			for (int c = 0; c < setRowSegs(SET_TABS_DEF[t].rows[r]); c++) {
				const SetSeg &g = SET_TABS_DEF[t].rows[r].seg[c];
				const bool needs = g.key && !g.text && !g.textFn && g.value != SEGV_FLAG;
				if (g.key && !lookup(g.key)) keys = false;
				else if (needs && g.value >= 0 && g.value > lookup(g.key)->max) keys = false;
			}
		check(keys, "every setting key of the tab is in the table with that value", SET_TAB_NAMES[t]);
		if (!keys) continue;
		buildSettingsPage(t, bind, sp);
		checkPage(sp.page, SET_TAB_NAMES[t]);
		if (!strcmp(SET_TAB_NAMES[t], "Saves")) {	/* Down from Save Memcards To lands on the leftmost Memcard Type button */
			static Layout lay;
			layoutPage(sp.page, kFont, lay);
			linkFocus(sp.page, lay);
			for (int k = 0; k < sp.page.rows[1].count; k++)
				check(lay.c[lay.slot[1][k]].link[DIR_DOWN] == lay.slot[2][0],
				      "Down from Save Memcards To lands on the leftmost Memcard Type button", "Saves");
		}
	}
	/* Options: every page; a tabbed one once per tab, so every tab is lit once. */
	for (int i = 0; i < OPT_PAGE_COUNT; i++) {
		const OptPage &p = PAGES[i];
		const SetBind bind = { segTextHost, segLitHost, segSettingHost, readoutHost, true, true };
		bool keys = true;
		int tab = -1;
		for (int r = 0; r < p.nrows; r++)
			if (p.rows[r].kind != ROW_READOUT && p.rows[r].seg[0].key)
					keys = keys && lookup(p.rows[r].seg[0].key);
		check(keys, "every setting key of the page is in the table", p.title);
		if (!keys) continue;
		for (int t = 0; t < NUM_TABS; t++)
			if (ADV_TABS[t] == i) tab = t;
		char name[48];
		snprintf(name, sizeof name, "%s%s%s", p.title, p.tab ? " / " : "", p.tab ? p.tab : "");
		readoutPage = &p;
		buildOptionsPage(i, tab, bind, ob);
		checkPage(ob.page, name);
		checkValues(ob, name);
		if (i == OPT_PAGE_ADVGFX)
			checkAdvGfxGrid(ob.page, ob.rows);
	}
	printf("shipped pages through the engine: %d checks\n", checks - before);
}

int main(int argc, char **argv)
{
	if (argc < 3 || !loadFont(argv[2])) {
		printf("usage: menu_pages_test REPO_ROOT FONT.dat\n");
		return 2;
	}
	checkShippedPages(argv[1]);

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
		check(p.id == i, "the page's id is its index in PAGES[]", p.title);
		check(p.nrows > 0 && p.nrows <= eng::MAX_ROWS, "row count in 1..eng::MAX_ROWS", p.title);
		check(p.returnSubmenu == RETURN_CURRENT_ISO || p.returnSubmenu == OPT_RET_GENERAL ||
		      p.returnSubmenu == OPT_RET_VIDEO || p.returnSubmenu == OPT_RET_AUDIO, "return target is a Settings tab or Current ISO", p.title);
		check(p.tab == NULL || p.standard != eng::DENSITY_OFF, "a tabbed Advanced page is a standard page", p.title);
		check(p.help == NULL || p.nhelp <= eng::MAX_HELP, "help lines fit", p.title);
		for (int r = 0; r < p.nrows; r++)
		{
			const SetRow &o = p.rows[r];
			if (o.kind == ROW_CYCLE || (o.kind == ROW_CHOICE && o.seg[0].value == SEGV_EACH))
			{
				check(o.seg[0].key && *o.seg[0].key, "a setting row has a key", p.title);
				for (int q = r + 1; q < p.nrows; q++)
					check(!(p.rows[q].seg[0].key && o.seg[0].key && p.rows[q].kind != ROW_BUTTONS && !strcmp(p.rows[q].seg[0].key, o.seg[0].key)), "a key once per page", p.title);
			}
			else if (o.kind == ROW_READOUT)
				check(o.info && o.label, "a readout has a label and a function", p.title);
			else if (o.kind == ROW_BUTTONS)
				check(o.seg[0].act && o.label && o.seg[0].text, "an action has a label, a text and a function", p.title);
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
		}
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
	return failures != 0;
}
