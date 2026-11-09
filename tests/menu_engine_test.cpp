/* menu_engine_test.cpp - the menu framework's layout and focus, on the host.
 *
 *     tests/menu_engine/run.sh            (builds with clang++ or g++, runs against fonts/En.dat)
 *
 * Every OptionsFrame page is described here (its row and help tables are copied from
 * Gamecube/menu/OptionsFrame.cpp) and laid out by MenuEngine. The expected rectangles,
 * text positions, focus links and help block are re-derived from OptionsFrame's own code --
 * its constructor, activateSubmenu() and the help loops -- and compared with the engine's.
 * The text widths are the real font's: fonts/En.dat, read the way IplFont reads it
 * (1156-byte records, big-endian code, width in byte 3), so a label that is too wide for its
 * button is found here, not on a television.
 *
 * Also checks that checkLayout() catches what it should (synthetic bad pages), and reports
 * how FOCUS_GEOMETRY would link the same pages.
 *
 * Exit status: the number of failed checks, capped at 255.
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Gamecube/menu/MenuEngine.h"

using namespace menu::engine;

static int failures = 0;

static void fail(const char *where, const char *what)
{
	printf("  FAIL  %-12s %s\n", where, what);
	failures++;
}

#include "menu_engine/font_metrics.h"
#include "menu_engine/probe.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const DITHER_NAMES[]       = { "None", "Default", "Always" };
static const char *const CHROMA_NAMES[]       = { "Sharp", "Smooth" };
static const char *const FMV_COLOUR_NAMES[]   = { "15-bit", "24-bit" };
#define CYC(label, names, value)   { ROW_CYCLE,   label, names, 0, (int)(sizeof(names) / sizeof((names)[0])), value, NULL, NULL }
#define RADIOROW(label, names, value) { ROW_CHOICE, label, names, 0, (int)(sizeof(names) / sizeof((names)[0])), value, NULL, NULL }

/* Findings the OptionsFrame pages have, on the real font. They are printed and do not fail
 * the test: fixing one changes a page's picture, and the plan keeps pictures until someone has
 * looked at the change on a Wii (Docs/MENU_PLAN.md, "Decided").
 * Matched by page, problem, and the text of the control (and of the second one, if any).
 * Any finding not listed here is a failure. Every Advanced page lays out with zero problems.
 * The list pages' findings are listed below, not changed. The sentinel
 * (page NULL) matches nothing; it stays, since a zero-length array is not C. */
struct Known {
	const char *page;
	int         kind;
	const char *a;
	const char *b;		/* NULL: a finding on one control */
};

static const Known KNOWN[] = {
	/* The list pages (the file browser): "Prev" and "Next" are 56 and 59 px in the font, on 70 px
	 * buttons with 16 px of padding a side. The browser has always drawn them so; the look is kept. */
	{ "List", PROB_TEXT_WIDE, "Prev", NULL },
	{ "List", PROB_TEXT_WIDE, "Next", NULL },
	{ NULL, 0, NULL, NULL },
};

static bool isKnown(const char *page, int kind, const Layout &lay, int a, int b)
{
	for (unsigned k = 0; k < COUNT(KNOWN); k++) {
		const Known &x = KNOWN[k];
		if (!x.page || strcmp(x.page, page) != 0 || x.kind != kind || strcmp(x.a, lay.c[a].text) != 0)
			continue;
		if (b < 0 ? x.b != NULL : (x.b == NULL || strcmp(x.b, lay.c[b].text) != 0))
			continue;
		return true;
	}
	return false;
}

static void describe(const Layout &lay, int i, char *buf, size_t len)
{
	const Rect &r = lay.c[i].rect;
	snprintf(buf, len, "'%s' at %g,%g %gx%g", lay.c[i].text, r.x, r.y, r.w, r.h);
}

/* Runs checkLayout and returns how many problems are not known findings. onlyRow >= 0 counts
 * only the problems on that row. With report, each problem is printed. */
static int auditLayout(const Page &p, const Layout &lay, const char *page, int onlyRow, bool report)
{
	Problem probs[MAX_PROBLEMS];
	int n = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	int unknown = 0;

	if (n > MAX_PROBLEMS)
		n = MAX_PROBLEMS;
	for (int i = 0; i < n; i++) {
		const Problem &q = probs[i];
		bool known;
		char d1[160], d2[160];
		if (onlyRow >= 0 && lay.c[q.a].row != onlyRow && (q.b < 0 || lay.c[q.b].row != onlyRow))
			continue;
		known = isKnown(page, q.kind, lay, q.a, q.b);
		if (!known)
			unknown++;
		if (report) {
			describe(lay, q.a, d1, sizeof d1);
			if (q.b >= 0)
				describe(lay, q.b, d2, sizeof d2);
			printf("        %-12s %-30s %s%s%s  [%s]\n", page, problemName(q.kind), d1,
			       q.b >= 0 ? " / " : "", q.b >= 0 ? d2 : "", known ? "known" : "NEW");
		}
	}
	return unknown;
}

/*  Main  */

/* checkLayout must catch each thing it claims to. Each case builds one bad page. */
static void testCheckCatches(void)
{
	static const char *const WIDE[] = { "Interpreter with a much longer name than fits" };
	static const char *const NARROW[] = { "On", "Off" };
	static const ActionButton FOUR[] = { {"A", 180, false}, {"B", 180, false}, {"C", 180, false}, {"D", 180, false} };
	Row rows[5];
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int np;
	bool seen[32];
	Page page;
	const char *tag = "checks";

	printf("checkLayout on deliberately bad pages\n");

	/* A value wider than its button. */
	page = Page();
	page.logo = LOGO_SPOT_BOTTOM;
	page.standard = DENSITY_REGULAR;
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "Short";
	rows[0].names = WIDE; rows[0].count = 1; rows[0].value = 0;
	page.rows = rows; page.nRows = 1;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_TEXT_WIDE]) fail(tag, "a label wider than its button not reported");

	/* A row of buttons in the logo's corner. A row of buttons is centred on the screen, so
	 * nothing moves it clear. */
	static const ActionButton WIDE3[] = { {"A", 200, false}, {"B", 200, false}, {"C", 200, false} };
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 4; i++) { rows[i].kind = ROW_BUTTONS; rows[i].count = 1; rows[i].actions = WIDE3; }
	page.standard = DENSITY_OFF;	/* the old path stays for the action menus */
	page.rowY0 = 160.0f;
	/* Three buttons reach x = 610, into the logo's box at the bottom right. */
	rows[4].kind = ROW_BUTTONS; rows[4].count = 3; rows[4].actions = WIDE3;
	page.rows = rows; page.nRows = 5;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_IN_LOGO]) fail(tag, "a row in the logo's box not reported");

	/* An action row wider than the screen. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].count = 4; rows[0].actions = FOUR;
	page.rows = rows; page.nRows = 1;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_PAST_EDGE]) fail(tag, "a button row off the screen not reported");

	/* Two free buttons on top of each other (an Editor page: the old path that stays). */
	static FreeButton twin[2] = { { "One", 100, 100, 120, 40, false, false }, { "Two", 160, 110, 120, 40, false, false } };
	Page fp = Page();
	fp.logo = LOGO_SPOT_NONE;
	fp.focus = FOCUS_GEOMETRY;
	fp.freeButtons = twin; fp.nFreeButtons = 2;
	layoutPage(fp, kFont, lay);
	linkFocus(fp, lay);
	np = checkLayout(fp, kFont, lay, probs, MAX_PROBLEMS);
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
	if (!seen[PROB_OVERLAP]) fail(tag, "two overlapping free buttons not reported");

	page.standard = DENSITY_REGULAR;
	/* Two cycle rows. Each one's Up and Down link to the other (the wrap makes both), so cutting
	 * both of the first row's vertical links leaves the second unreachable. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "A"; rows[0].names = NARROW; rows[0].count = 2;
	rows[1].kind = ROW_CYCLE; rows[1].label = "B"; rows[1].names = NARROW; rows[1].count = 2;
	page.rows = rows; page.nRows = 2;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	{
		int first = lay.slot[0][0];
		lay.c[first].link[DIR_DOWN] = -1;
		lay.c[first].link[DIR_UP] = -1;
		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		memset(seen, 0, sizeof seen);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
		if (!seen[PROB_UNREACHABLE]) fail(tag, "a control cut off from focus not reported");
	}

	/* A link to a label, which is not focusable. */
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	{
		int first = lay.slot[0][0];
		int label = -1;
		for (int i = 0; i < lay.n; i++) if (lay.c[i].role == ROLE_LABEL) { label = i; break; }
		lay.c[first].link[DIR_RIGHT] = label;
		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		memset(seen, 0, sizeof seen);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++) seen[probs[i].kind] = true;
		if (!seen[PROB_BAD_LINK]) fail(tag, "a focus link to a label not reported");
	}

	/* Things layoutPage refuses rather than laying out past its arrays. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CHOICE; rows[0].names = WIDE; rows[0].count = 5;
	page.rows = rows; page.nRows = 1;
	if (layoutPage(page, kFont, lay) != -1) fail(tag, "a five-button choice row was accepted");
	page.rows = rows; page.nRows = MAX_ROWS + 1;
	if (layoutPage(page, kFont, lay) != -1) fail(tag, "a seventh row was accepted");
}

/*  Inactive buttons are never focus targets  */

/* Counts the links that point at a control which is not focusable. */
static int linksToInactive(const Layout &lay)
{
	int n = 0;
	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++) {
			const int t = lay.c[i].link[d];
			if (t >= 0 && !lay.c[t].focusable)
				n++;
		}
	return n;
}

/* The control of a row's slot, or -1. */
static int findButton(const Layout &lay, int row, int slot)
{
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].row == row && lay.c[i].slot == slot &&
		    (lay.c[i].role == ROLE_BUTTON || lay.c[i].role == ROLE_CHOICE))
			return i;
	return -1;
}

static void testInactiveNeverFocus(void)
{
	static const char *const THREE[] = { "On", "Off", "Auto" };
	static const char *const TWO[]   = { "Off", "On" };
	static const ActionButton ACT[]  = { {"Save", 100, false}, {"Load", 100, false}, {"This Game", 140, false} };
	static Row rows[4];
	static Layout lay;
	static Page page;
	static const FocusMode modes[] = { FOCUS_ROWS, FOCUS_GEOMETRY };
	static const char *const modeName[] = { "rows", "geometry" };

	for (int m = 0; m < 2; m++) {
		char where[64];
		Problem probs[MAX_PROBLEMS];
		int np;

		memset(rows, 0, sizeof rows);
		/* Row 0: Save (slot 0) inactive. Row 1: the middle choice inactive. Row 2: its only
		 * button inactive, so the whole row is out of the links. Row 3: all active. */
		rows[0].kind = ROW_BUTTONS;  rows[0].actions = ACT; rows[0].count = 3; rows[0].inactive = 1u << 0;
		rows[1].kind = ROW_CHOICE;   rows[1].names = THREE; rows[1].count = 3; rows[1].value = 1;
		rows[1].inactive = 1u << 1;
		rows[2].kind = ROW_CYCLE;    rows[2].names = TWO;   rows[2].count = 2; rows[2].inactive = 1u << 0;
		rows[3].kind = ROW_TOGGLE;   rows[3].names = TWO;   rows[3].count = 2;

		memset(&page, 0, sizeof page);
		page.nRows = 4;
		page.rows = rows;
		page.logo = LOGO_SPOT_NONE;
		page.focus = modes[m];
		page.standard = DENSITY_REGULAR;
		snprintf(where, sizeof where, "inactive/%s", modeName[m]);

		if (layoutPage(page, kFont, lay) < 0) {
			fail(where, "the page does not lay out");
			continue;
		}
		linkFocus(page, lay);

		{
			const int save = findButton(lay, 0, 0), load = findButton(lay, 0, 1), game = findButton(lay, 0, 2);
			const int mid = findButton(lay, 1, 1), cycle = findButton(lay, 2, 0), toggle = findButton(lay, 3, 0);

			if (save < 0 || mid < 0 || cycle < 0 || load < 0 || game < 0 || toggle < 0) {
				fail(where, "a button is missing from the layout");
				continue;
			}
			if (lay.c[save].focusable || lay.c[mid].focusable || lay.c[cycle].focusable)
				fail(where, "an inactive button is focusable");
			if (!lay.c[load].focusable || !lay.c[game].focusable || !lay.c[toggle].focusable)
				fail(where, "an active button is not focusable");
			if (lay.defaultFocus != load)
				fail(where, "focus does not start on the first active button");
		}
		if (linksToInactive(lay) != 0)
			fail(where, "a focus link points at an inactive button");

		np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
		for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
			if (probs[i].kind == PROB_BAD_LINK || probs[i].kind == PROB_UNREACHABLE) {
				fail(where, problemName(probs[i].kind));
				break;
			}
	}
}

/*  Link overrides (Page::overrides), and the D-pad reach of Current ROM  */

/* The controls a D-pad walk from the default focus reaches, by following the links. Returns the
 * count; reached[i] is set for each control reached. */
static int reachFromDefault(const Layout &lay, bool *reached)
{
	int queue[MAX_CONTROLS], qh = 0, qt = 0;

	memset(reached, 0, sizeof(bool) * MAX_CONTROLS);
	if (lay.defaultFocus < 0 || lay.defaultFocus >= lay.n || !lay.c[lay.defaultFocus].focusable)
		return 0;
	reached[lay.defaultFocus] = true;
	queue[qt++] = lay.defaultFocus;
	while (qh < qt) {
		const int u = queue[qh++];
		for (int d = 0; d < DIR_COUNT; d++) {
			const int v = lay.c[u].link[d];
			if (v >= 0 && v < lay.n && lay.c[v].focusable && !reached[v]) {
				reached[v] = true;
				queue[qt++] = v;
			}
		}
	}
	return qt;
}

/* Lays out a page and links it, with the page's overrides. Returns false if it does not lay out. */
static bool layOutLinked(const Page &page, Layout &lay, const char *where)
{
	if (layoutPage(page, kFont, lay) < 0) {
		fail(where, "the page does not lay out");
		return false;
	}
	linkFocus(page, lay);
	return true;
}

/* Three rows of buttons and a readout, one row with an inactive button. The rule links first, then
 * the overrides are checked against it: one applies, and the others each name something that is
 * not a focus target (an inactive button, a readout, a row past the page, a slot past the row's
 * buttons, an inactive source) and must leave the rule's link alone. */
static void testLinkOverrides(void)
{
	static const ActionButton TOP[] = { { "A", 100.0f, false }, { "B", 100.0f, false } };
	static const ActionButton MID[] = { { "C", 100.0f, false }, { "D", 100.0f, false } };
	static const ActionButton LOW[] = { { "E", 100.0f, false } };
	static const LinkOverride OV[] = {
		{ 0, 1, DIR_DOWN,  3, 0 },		/* B down to E: applied */
		{ 0, 0, DIR_DOWN,  1, 1 },		/* A down to D, which is inactive: the rule's C stands */
		{ 0, 0, DIR_RIGHT, 2, 0 },		/* A right to the readout's row (no button): B stands */
		{ 0, 0, DIR_LEFT,  7, 0 },		/* A left to a row past the page: B stands */
		{ 3, 0, DIR_UP,    0, MAX_SLOTS },	/* E up to a slot past the row's buttons: C stands */
		{ 1, 1, DIR_UP,    3, 0 },		/* D up, from an inactive button: nothing to link */
	};
	static Row rows[4];
	static Layout lay;
	static bool reached[MAX_CONTROLS];
	Page page;
	Problem probs[MAX_PROBLEMS];
	const char *where = "overrides";
	int A, B, C, D, E, np;

	printf("Link overrides (Page::overrides)\n");
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].actions = TOP; rows[0].count = 2;
	rows[1].kind = ROW_BUTTONS; rows[1].actions = MID; rows[1].count = 2; rows[1].inactive = 1u << 1;
	rows[2].kind = ROW_READOUT; rows[2].label = "Readout"; rows[2].text = "0";
	rows[3].kind = ROW_BUTTONS; rows[3].actions = LOW; rows[3].count = 1;

	memset(&page, 0, sizeof page);
	page.rows = rows; page.nRows = 4; page.logo = LOGO_SPOT_NONE; page.focus = FOCUS_ROWS; page.standard = DENSITY_REGULAR;

	/* The rule alone: A and B go down to C (D is inactive), C down to E, and E up to C. */
	if (!layOutLinked(page, lay, where))
		return;
	A = lay.slot[0][0]; B = lay.slot[0][1]; C = lay.slot[1][0]; D = lay.slot[1][1]; E = lay.slot[3][0];
	if (A < 0 || B < 0 || C < 0 || D < 0 || E < 0) {
		fail(where, "a button is missing from the layout");
		return;
	}
	if (lay.c[B].link[DIR_DOWN] != C || lay.c[A].link[DIR_DOWN] != C || lay.c[E].link[DIR_UP] != C)
		fail(where, "the rule's links are not the ones this test expects");

	page.overrides = OV;
	page.nOverrides = (int)COUNT(OV);
	if (!layOutLinked(page, lay, where))
		return;

	if (lay.c[B].link[DIR_DOWN] != E)
		fail(where, "an override that names two focus targets was not applied");
	if (lay.c[A].link[DIR_DOWN] != C)
		fail(where, "an override to an inactive button replaced the rule's link");
	if (lay.c[A].link[DIR_RIGHT] != B || lay.c[A].link[DIR_LEFT] != B)
		fail(where, "an override to a readout or a row past the page replaced the rule's link");
	if (lay.c[E].link[DIR_UP] != C)
		fail(where, "an override to a slot past the row's buttons replaced the rule's link");
	if (lay.c[D].link[DIR_UP] != -1 || lay.c[D].link[DIR_DOWN] != -1 ||
	    lay.c[D].link[DIR_LEFT] != -1 || lay.c[D].link[DIR_RIGHT] != -1)
		fail(where, "an override from an inactive button set a link");
	if (linksToInactive(lay) != 0)
		fail(where, "a focus link points at an inactive button");
	np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
		if (probs[i].kind == PROB_BAD_LINK || probs[i].kind == PROB_UNREACHABLE) {
			fail(where, problemName(probs[i].kind));
			break;
		}
	if (reachFromDefault(lay, reached) != 4 || !reached[A] || !reached[B] || !reached[C] || !reached[E])
		fail(where, "the overridden page is not reachable from the default focus");

	/* A NULL array is ignored, whatever the count says: the page keeps the rule's links. */
	page.overrides = NULL;
	page.nOverrides = 3;
	if (!layOutLinked(page, lay, where))
		return;
	if (lay.c[B].link[DIR_DOWN] != C)
		fail(where, "a NULL override array was not ignored");
	printf("  overrides: one applied; inactive, readout, out-of-range and inactive-source ones fall back\n");
}

/*  Lists (ROW_LIST, step 4): a window of 10 over 5,000 items  */

/* The item names, made once: the callback hands out pointers into this, as the file browser
 * hands out its directory entries. */
static char listNames[5000][16];
static int  listCalls;

static void listItemFn(void *, int index, ListItem &out)
{
	listCalls++;
	out.text = listNames[index];
	out.folder = (index % 7) == 0;		/* every seventh item is a folder */
}

static const ListButton LIST_PREV = { "Prev" };
static const ListButton LIST_NEXT = { "Next" };

/* The file browser's geometry: items at x 120, 400 wide, 35 high, 40 apart from y 40. */
static ListSpec listSpec(ListState *st, ListMode mode)
{
	ListSpec sp;
	memset(&sp, 0, sizeof sp);
	sp.state = st;
	sp.mode = mode;
	sp.item = listItemFn;
	sp.ctx = NULL;
	sp.prev = LIST_PREV;
	sp.next = LIST_NEXT;
	return sp;
}

/* A page of one list row, and nothing else. */
static void listPageOf(Page &page, Row &row, const ListSpec &spec)
{
	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_TOP;
	page.focus = FOCUS_ROWS;
}

/* Lays a list page out and links it, after the state has changed. Returns false if it does not lay out. */
static bool layOutList(const Page &page, Layout &lay, const char *where)
{
	listCalls = 0;
	if (layoutPage(page, kFont, lay) < 0) {
		fail(where, "the list page does not lay out");
		return false;
	}
	linkFocus(page, lay);
	if (listCalls > page.rows[0].list->state->slots) {
		fail(where, "the layout asked for more items than the window shows");
		return false;
	}
	return true;
}

static void testListPaging(void)
{
	/* The browser's own turn rule, written out: Prev and Next clamp, L and R wrap, and a page is
	 * ten items. Checked against the arithmetic the browser has always used (current_page,
	 * max_page). */
	static const int COUNTS[] = { 1, 10, 11, 25, 5000, 5005 };
	int bad = 0;

	printf("Lists, page turns against the browser's own arithmetic\n");
	for (unsigned c = 0; c < sizeof COUNTS / sizeof COUNTS[0]; c++) {
		const int maxPage = (COUNTS[c] + 9) / 10;
		for (int start = 0; start < maxPage; start++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				for (int wrap = 0; wrap <= 1; wrap++) {
					ListState s = { COUNTS[c], 10, start * 10, -1 };
					int want, got;
					if (wrap)
						want = ((start + dir) % maxPage + maxPage) % maxPage;
					else
						want = start + dir < 0 ? 0 : (start + dir >= maxPage ? maxPage - 1 : start + dir);
					listTurn(s, dir, wrap != 0);
					got = s.top / 10;
					if (got != want || s.top % 10 != 0 || s.focus != s.top) {
						bad++;
						if (bad <= 5)
							printf("  FAIL  list      count %d page %d dir %d wrap %d: went to %d, want %d\n",
							       COUNTS[c], start, dir, wrap, got, want);
					}
				}
			}
		}
	}
	if (bad)
		fail("list", "page turns differ from the browser's arithmetic (see above)");
	printf("  page turns: %s over %u counts, every page, both directions, wrap and clamp\n",
	       bad ? "MISMATCHES" : "all as the browser", (unsigned)(sizeof COUNTS / sizeof COUNTS[0]));
}

/*  Configure Buttons (step 5): the 30 buttons and 5 labels at ConfigureButtonsFrame.cpp's places  */

/* The table's rectangles, by FRAME_BUTTONS index (x, y, w, h), and the labels (x, y, scale, centred). */
static const float CB_RECT[30][4] = {
	{ 20, 20, 80, 40 }, { 110, 20, 100, 40 }, { 220, 20, 80, 40 }, { 310, 20, 70, 40 }, { 390, 20, 70, 40 },
	{ 15, 160, 110, 40 }, { 140, 130, 80, 40 }, { 140, 180, 80, 40 }, { 235, 140, 80, 40 }, { 325, 140, 80, 40 },
	{ 420, 130, 80, 40 }, { 420, 180, 80, 40 }, { 75, 230, 80, 40 }, { 30, 280, 80, 40 }, { 120, 280, 80, 40 },
	{ 75, 330, 80, 40 }, { 485, 230, 80, 40 }, { 440, 280, 80, 40 }, { 530, 280, 80, 40 }, { 485, 330, 80, 40 },
	{ 160, 345, 100, 40 }, { 380, 345, 100, 40 }, { 95, 395, 130, 40 }, { 415, 395, 130, 40 }, { 235, 395, 80, 40 },
	{ 325, 395, 80, 40 }, { 325, 340, 50, 40 }, { 265, 340, 50, 40 }, { 515, 160, 110, 40 }, { 480, 20, 140, 40 } };
/*  Edge wrap (Page::wrap)  */

/* Offset of control j from control i along direction d: positive when j lies on d's side. */
static float alongOf(const Layout &lay, int i, int j, int d)
{
	const Rect &A = lay.c[i].rect, &B = lay.c[j].rect;
	const float dx = (B.x + B.w / 2) - (A.x + A.w / 2), dy = (B.y + B.h / 2) - (A.y + A.h / 2);
	switch (d) {
	case DIR_UP:   return -dy;
	case DIR_DOWN: return dy;
	case DIR_LEFT: return -dx;
	default:       return dx;
	}
}

/* j shares a row (for Left and Right) or a column (for Up and Down) with i. */
static bool sharesLine(const Layout &lay, int i, int j, int d)
{
	const Rect &A = lay.c[i].rect, &B = lay.c[j].rect;
	if (d == DIR_LEFT || d == DIR_RIGHT)
		return B.y < A.y + A.h && A.y < B.y + B.h;
	return B.x < A.x + A.w && A.x < B.x + B.w;
}

/* The first wrap stage (the row or column) has a target: a focusable control on i's row or
 * column, beyond i on the side opposite d. */
static bool hasLineWrap(const Layout &lay, int i, int d)
{
	for (int j = 0; j < lay.n; j++)
		if (j != i && lay.c[j].focusable && sharesLine(lay, i, j, d) && alongOf(lay, i, j, d ^ 1) > 1.0f)
			return true;
	return false;
}

static void expectLink(int &bad, const char *where, bool ok, const char *what)
{
	if (!ok) {
		fail(where, what);
		bad++;
	}
}

/* Lays a page out with the wrap flag off and on. With it on, each link the rule made stays the same.
 * Each link the rule left out may now name a control on the same row or column. Returns the wrap count. */
static int wrapLinks(const char *where, Page p, Layout &off, Layout &on, int &bad)
{
	int n = 0;

	p.wrap = false;
	if (layoutPage(p, kFont, off) < 0) {
		fail(where, "the page does not lay out");
		bad++;
		return 0;
	}
	linkFocus(p, off);
	p.wrap = true;
	layoutPage(p, kFont, on);
	linkFocus(p, on);

	for (int i = 0; i < on.n; i++) {
		if (!on.c[i].focusable)
			continue;
		for (int d = 0; d < DIR_COUNT; d++) {
			const int was = off.c[i].link[d], now = on.c[i].link[d];
			const Rect &A = on.c[i].rect;
			if (was >= 0)
				expectLink(bad, where, now == was, "a link the rule made changed with wrap on");
			if (was < 0 && now >= 0) {
				const Rect &B = on.c[now].rect;
				const bool horizontal = d == DIR_LEFT || d == DIR_RIGHT;
				const bool sameLine = horizontal ? (B.y < A.y + A.h && A.y < B.y + B.h)
				                                 : (B.x < A.x + A.w && A.x < B.x + B.w);
				n++;
				/* The row or column first; with none on that side, the farthest control on that side. */
				expectLink(bad, where, on.c[now].focusable && alongOf(on, i, now, d ^ 1) > 1.0f &&
				           (sameLine || !hasLineWrap(on, i, d)),
				           "a wrap link is not a focusable control beyond the origin, on its row or column first");
			}
		}
	}
	return n;
}

static void testEdgeWrap(void)
{
	/* Row 0: A B C D. Row 1: E F, under A and B. In round 1, D is inactive. */
	static FreeButton fb[6];
	static const float at[6][2] = { {20, 20}, {150, 20}, {280, 20}, {410, 20}, {20, 200}, {150, 200} };
	static Layout off, on;
	Page p;
	int bad = 0;
	const char *where = "wrap";

	printf("Edge wrap (Page::wrap): a 6-button page with and without an inactive button, and Configure Buttons\n");
	for (int round = 0; round < 2; round++) {
		for (int i = 0; i < 6; i++) {
			fb[i].text = "Button";
			fb[i].x = at[i][0];
			fb[i].y = at[i][1];
			fb[i].w = 100;
			fb[i].h = 40;
			fb[i].lit = false;
			fb[i].inactive = round == 1 && i == 3;
		}
		memset(&p, 0, sizeof p);
		p.logo = LOGO_SPOT_NONE;
		p.focus = FOCUS_GEOMETRY;
		p.freeButtons = fb;
		p.nFreeButtons = 6;
		wrapLinks(where, p, off, on, bad);

		{
			const int A = on.freeCtl[0], B = on.freeCtl[1], C = on.freeCtl[2];
			const int D = on.freeCtl[3], E = on.freeCtl[4], F = on.freeCtl[5];
			const int far = round == 0 ? D : C;	/* the far end of row 0 from A: D, or C with D inactive */

			expectLink(bad, where, off.c[A].link[DIR_LEFT] == -1 && off.c[A].link[DIR_UP] == -1,
			           "wrap off: an edge has a link");
			expectLink(bad, where, on.c[A].link[DIR_LEFT] == far, "Left from A does not wrap to the far end of row 0");
			expectLink(bad, where, on.c[far].link[DIR_RIGHT] == A, "Right from the far end does not wrap to A");
			expectLink(bad, where, on.c[A].link[DIR_UP] == E, "Up from A does not wrap to the far end of column 0");
			expectLink(bad, where, on.c[E].link[DIR_DOWN] == A, "Down from E does not wrap to A");
			expectLink(bad, where, on.c[E].link[DIR_LEFT] == F, "Left from E does not wrap to the far end of row 1");
			expectLink(bad, where, on.c[A].link[DIR_RIGHT] == B, "Right from A changed");
			if (round == 1) {
				bool toInactive = false;
				for (int i = 0; i < on.n; i++)
					for (int d = 0; d < DIR_COUNT; d++)
						if (on.c[i].link[d] == D)
							toInactive = true;
				expectLink(bad, where, !toInactive, "a link points at an inactive button");
			}
		}
	}

	/* The real table: Next Pad is the leftmost button of the top row, Gun/Mouse the rightmost. */
	{
		static FreeButton cb[30];
		Page q;
		for (int i = 0; i < 30; i++) {
			cb[i].text = "Button";
			cb[i].x = CB_RECT[i][0];
			cb[i].y = CB_RECT[i][1];
			cb[i].w = CB_RECT[i][2];
			cb[i].h = CB_RECT[i][3];
			cb[i].lit = false;
			cb[i].inactive = false;
		}
		memset(&q, 0, sizeof q);
		q.logo = LOGO_SPOT_NONE;
		q.focus = FOCUS_GEOMETRY;
		q.freeButtons = cb;
		q.nFreeButtons = 30;
		const int n = wrapLinks("ConfigureButtons", q, off, on, bad);
		expectLink(bad, "ConfigureButtons", on.c[on.freeCtl[0]].link[DIR_LEFT] == on.freeCtl[29],
		           "Left from Next Pad does not wrap to Gun/Mouse");
		expectLink(bad, "ConfigureButtons", off.c[off.freeCtl[0]].link[DIR_LEFT] == -1,
		           "wrap off: Left from Next Pad has a link");
		printf("        ConfigureButtons: %d links wrap with the flag on\n", n);
	}
	printf("  Edge wrap  far ends, inactive skipped, wrap off unchanged  %s\n", bad ? "FAILED" : "ok");
}

/*  Configure Buttons in every controller state (Page::wrap)  */

/* The states ConfigureButtonsFrame::activateSubmenu sets up: no pad, a plain pad (not a lightgun),
 * a lightgun pad with the gun off, on with no gun mode, and on with Mouse, Just or GunCon. */
enum CbState { CB_NO_PAD, CB_PLAIN_PAD, CB_GUN_OFF, CB_GUN_IDLE, CB_GUN_MOUSE, CB_GUN_JUST, CB_GUN_GUNCON, CB_STATES };
static const char *const CB_STATE_NAME[CB_STATES] = {
	"no controller", "plain pad", "lightgun pad, gun off", "lightgun pad, gun on, no mode",
	"gun on, Mouse", "gun on, Just", "gun on, GunCon" };

/* The buttons a state turns on (visible and active). Button 0, Next Pad, is always on. */
static void cbActive(int state, bool on[30])
{
	int lo = 0, hi = -1;		/* buttons lo..hi are on, besides the extras set below */

	for (int i = 0; i < 30; i++)
		on[i] = false;
	on[0] = true;
	switch (state) {
	case CB_NO_PAD:    break;
	case CB_PLAIN_PAD: lo = 1; hi = 28; break;	/* button 29 (Gun/Mouse) is off: not a lightgun */
	case CB_GUN_OFF:
	case CB_GUN_IDLE:  lo = 1; hi = 29; break;
	case CB_GUN_MOUSE:
		lo = 1; hi = 5;
		on[10] = on[11] = on[19] = on[26] = on[27] = on[28] = on[29] = true;
		break;
	case CB_GUN_JUST:
		lo = 1; hi = 5;
		on[9] = on[17] = on[19] = on[28] = on[29] = true;
		break;
	case CB_GUN_GUNCON:
		lo = 1; hi = 5;
		on[9] = on[18] = on[19] = on[28] = on[29] = true;
		break;
	}
	for (int i = lo; i <= hi; i++)
		on[i] = true;
}

static const char *const DIR_NAME[DIR_COUNT] = { "Up", "Down", "Left", "Right" };

static void testConfigureButtonStates(void)
{
	static FreeButton cb[30];
	static Layout off, on;
	int bad = 0, addedAll = 0, deadBefore = 0, deadAfter = 0;

	printf("Configure Buttons in every controller state (Page::wrap: no dead ends, no link to a hidden or inactive button)\n");
	for (int s = 0; s < CB_STATES; s++) {
		bool act[30];
		Page q;
		int nFocus = 0, added = 0, before = 0, after = 0, deadI = -1, deadD = -1;
		const char *where = CB_STATE_NAME[s];

		cbActive(s, act);
		for (int i = 0; i < 30; i++) {
			cb[i].text = "Button";
			cb[i].x = CB_RECT[i][0];
			cb[i].y = CB_RECT[i][1];
			cb[i].w = CB_RECT[i][2];
			cb[i].h = CB_RECT[i][3];
			cb[i].lit = false;
			cb[i].inactive = !act[i];
		}
		memset(&q, 0, sizeof q);
		q.logo = LOGO_SPOT_NONE;
		q.focus = FOCUS_GEOMETRY;
		q.freeButtons = cb;
		q.nFreeButtons = 30;
		q.wrap = false;
		if (layoutPage(q, kFont, off) < 0) {
			fail(where, "the page does not lay out");
			continue;
		}
		linkFocus(q, off);
		q.wrap = true;
		if (layoutPage(q, kFont, on) < 0) {
			fail(where, "the page does not lay out with wrap");
			continue;
		}
		linkFocus(q, on);

		for (int i = 0; i < on.n; i++) {
			if (!on.c[i].focusable)
				continue;
			nFocus++;
			for (int d = 0; d < DIR_COUNT; d++) {
				const int was = off.c[i].link[d], now = on.c[i].link[d];

				/* the current code: the geometric rule, then the row or column wrap */
				if (was < 0 && !hasLineWrap(on, i, d))
					before++;
				if (was >= 0)
					expectLink(bad, where, now == was, "a link the rule made changed with wrap on");
				if (was < 0 && now >= 0 && hasLineWrap(on, i, d))
					expectLink(bad, where, sharesLine(on, i, now, d) && on.c[now].focusable,
					           "a row or column wrap changed");
				if (was < 0 && now >= 0 && !hasLineWrap(on, i, d))
					added++;
				if (now < 0) {
					if (deadI < 0) {
						deadI = i;
						deadD = d;
					}
					after++;
					continue;
				}
				expectLink(bad, where, on.c[now].focusable, "a link points at a hidden or inactive button");
				if (was < 0)
					expectLink(bad, where, alongOf(on, i, now, d ^ 1) > 1.0f,
					           "a wrap link does not reach beyond the origin on that side");
			}
		}
		if (nFocus >= 2 && deadI >= 0) {
			char msg[96];
			snprintf(msg, sizeof msg, "button %d has no %s target", on.c[deadI].slot, DIR_NAME[deadD]);
			expectLink(bad, where, false, msg);
		} else if (nFocus < 2)	/* a lone focus target has nothing to link to */
			expectLink(bad, where, after == 4 * nFocus, "a lone focus target has a link");
		printf("        %-34s %2d focusable, dead ends %d -> %d, links added %d%s\n", where, nFocus,
		       before, after, added, nFocus < 2 ? " (lone focus target)" : "");
		addedAll += added;
		deadBefore += before;
		deadAfter += after;
	}
	printf("        total: dead ends %d -> %d, links added vs the current code %d\n",
	       deadBefore, deadAfter, addedAll);
	printf("  Configure Buttons states  every focusable button has Left, Right, Up and Down  %s\n",
	       bad ? "FAILED" : "ok");
}

/* The tab buttons (L/R): tabStep goes round with wrap (TabWrap = 1), stops at the ends without. */
static void testTabStep(void)
{
	const int before = failures;
	if (tabStep(4, 5, 1, true) != 0 || tabStep(0, 5, -1, true) != 4 || tabStep(2, 5, 1, true) != 3)
		fail("tabs", "wrap: past the last tab is the first, before the first the last");
	if (tabStep(4, 5, 1, false) != 4 || tabStep(0, 5, -1, false) != 0 || tabStep(2, 5, -1, false) != 1)
		fail("tabs", "no wrap: the ends stop");
	if (tabStep(3, 5, 0, true) != 3 || tabStep(7, 5, 1, true) != 7 || tabStep(0, 0, 1, true) != 0)
		fail("tabs", "no press, or no such tab: the tab stays");
	printf("tab buttons: wrap and stop ... %s\n", failures == before ? "ok" : "FAILED");
}

/* A cycle wraps and its text always comes from its names (the Configure Buttons Slot button
 * once stopped at "Game": it wrote one digit into the label, past the end of "Game"). */
static void testCycle(void)
{
	static const char *const names[] = { "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Game" };
	Cycle c = { names, 5, 0 };
	int before = failures;
	for (unsigned step = 1; step <= 11; step++) {
		const char *got = cycleNext(c);
		if (strcmp(got, names[step % 5]) != 0) {
			printf("cycle step %u: got \"%s\", expected \"%s\"\n", step, got, names[step % 5]);
			failures++;
		}
	}
	printf("cycle: wraps through all names ... %s\n", failures == before ? "ok" : "FAILED");
}

/* Page::columns == 2 on a standard page: a full-width choice, then Cycle rows in pairs. Each cell is its own
 * row (its own focus, footer); Left/Right run along a line, Up/Down keep the column; a Toggle takes a whole line. */
static void testStdColumns(void)
{
	static const char *const GAMMA_NAMES[] = { "1.0", "1.1", "1.2", "1.3", "1.4", "1.5" };
	Page p;
	Layout lay;
	Problem probs[MAX_PROBLEMS];
	int before = failures;

	memset(&p, 0, sizeof p);
	p.title = "Std columns";
	p.logo = LOGO_SPOT_BOTTOM;
	p.focus = FOCUS_ROWS;
	p.standard = DENSITY_REGULAR;
	p.columns = 2;
	{
		Row rows[] = { RADIOROW("Dithering", DITHER_NAMES, 1), CYC("Gamma", GAMMA_NAMES, 0), CYC("Video Color", FMV_COLOUR_NAMES, 0),
		               CYC("MDEC Chroma", CHROMA_NAMES, 0), CYC("Gamma", GAMMA_NAMES, 0), CYC("Gamma", GAMMA_NAMES, 0) };
		Page q = p;
		q.rows = rows;
		q.nRows = COUNT(rows);
		if (layoutPage(q, kFont, lay) < 0) { fail("std columns", "layoutPage refused"); return; }
		linkFocus(q, lay);
		const int a = lay.slot[1][0], b = lay.slot[2][0], c = lay.slot[3][0], d = lay.slot[4][0], e = lay.slot[5][0];
		if (lay.c[a].rect.y != lay.c[b].rect.y || lay.c[a].rect.x >= lay.c[b].rect.x)
			fail("std columns", "a pair is not side by side on one line");
		if (lay.c[c].rect.y != lay.c[a].rect.y + 56.0f || lay.c[e].rect.y != lay.c[c].rect.y + 56.0f)
			fail("std columns", "lines are not one pitch apart");
		if (lay.c[a].link[DIR_RIGHT] != b || lay.c[b].link[DIR_LEFT] != a || lay.c[a].link[DIR_DOWN] != c ||
		    lay.c[b].link[DIR_DOWN] != d || lay.c[d].link[DIR_UP] != b || lay.c[e].link[DIR_UP] != c)
			fail("std columns", "focus does not move along a line and keep the column");
		if (checkLayout(q, kFont, lay, probs, MAX_PROBLEMS) != 0)
			fail("std columns", "checkLayout finds a problem on a good two-column standard page");
			/* every button of a column is one width; shrunk to text + 16 the arrows are touched (PROB_ARROW_TIGHT) */
			if (lay.c[a].rect.w != lay.c[c].rect.w || lay.c[a].rect.w != lay.c[e].rect.w || lay.c[b].rect.w != lay.c[d].rect.w)
				fail("std columns", "the buttons of one column are not one width");
			lay.c[a].rect.w = 20.0f;
			{
				bool tight = false;
				for (int i = 0, n = checkLayout(q, kFont, lay, probs, MAX_PROBLEMS); i < n && i < MAX_PROBLEMS; i++)
					tight = tight || probs[i].kind == PROB_ARROW_TIGHT;
				if (!tight)
					fail("std columns", "a cycle button too narrow for its widest value and arrows was not reported");
			}
			
	}
	/* the bad page: names so long that the grid runs past x 616 and off the left edge */
	{
		static const char *const LONG[] = { "A name far too long for half the screen", "Another one just as long as it" };
		Row rows[] = { CYC("Left", LONG, 0), CYC("Right", LONG, 0) };
		Page q = p;
		bool wide = false;
		q.rows = rows;
		q.nRows = COUNT(rows);
		if (layoutPage(q, kFont, lay) < 0) { fail("std columns", "layoutPage refused the bad page"); return; }
		linkFocus(q, lay);
		for (int i = 0, n = checkLayout(q, kFont, lay, probs, MAX_PROBLEMS); i < n && i < MAX_PROBLEMS; i++)
			if (probs[i].kind == PROB_PAST_RIGHT || probs[i].kind == PROB_PAST_EDGE)
				wide = true;
		if (!wide)
			fail("std columns", "checkLayout missed a two-column standard page wider than the screen");
	}
	printf("standard two columns: cells, focus, bad page ... %s\n", failures == before ? "ok" : "FAILED");
}

/*  The standard (Docs/MENU_PLAN.md): its widths, footer and four checks. Each check has a failing and a passing page.  */

static bool stdHas(const Page &p, const Layout &lay, int kind)
{
	Problem probs[MAX_PROBLEMS];
	const int np = checkLayout(p, kFont, lay, probs, MAX_PROBLEMS);
	for (int i = 0; i < np && i < MAX_PROBLEMS; i++)
		if (probs[i].kind == kind)
			return true;
	return false;
}

static Page stdPage(Row *rows, int n, Density d, LogoSpot logo)
{
	Page p;
	memset(&p, 0, sizeof p);
	p.rows = rows;
	p.nRows = n;
	p.standard = d;
	p.logo = logo;
	return p;
}

static void testStandard(void)
{
	static const char *const OFF_ON[]  = { "Off", "On" };
	static const char *const LONG3[]   = { "Interpreter", "Lightrec", "Dynarec with a long name" };
	static const char *const VIDEO[]   = { "Off", "Near", "Bilinear", "Trap" };
	static const ActionButton BTN2[]   = { { "Configure Input", 0, false }, { "Configure Buttons", 0, false } };
	static const ActionButton BTN3[]   = { { "Configure Input", 0, false }, { "Configure Buttons", 0, false }, { "Advanced settings", 0, false } };
	const char *tag = "standard";
	const int before = failures;
	Row rows[8];
	Layout lay;
	Page p;

	/* widths: step 8, equal when they fit, else own text + 16 */
	{
		int w[4], sum;
		bool eq = false;
		const int a = stdStepWidth(kFont, "Off", 32), b = stdButtonWidth(kFont, "Off");
		if (a != b || a % STD_STEP != 0 || a < kFont.width("Off", 1.0f, NULL) + 32)
			fail(tag, "stdButtonWidth is not text + 32 in steps of 8");
		if (stdStepWidth(kFont, "", 32) != 32 || stdStepWidth(kFont, NULL, 16) != 16)
			fail(tag, "an empty text must measure 0 (width = the padding)");
		printf("  text widths: Off %d, Near %d, Bilinear %d, Trap %d\n", kFont.width("Off", 1.0f, NULL),
		       kFont.width("Near", 1.0f, NULL), kFont.width("Bilinear", 1.0f, NULL), kFont.width("Trap", 1.0f, NULL));
		sum = stdSegmentWidths(kFont, OFF_ON, 2, 352, w, &eq);
		if (!eq || w[0] != w[1] || sum != w[0] * 2)
			fail(tag, "two segments that fit in 352 must get equal widths");
		sum = stdSegmentWidths(kFont, VIDEO, 3, 400, w, &eq);
		if (!eq || w[0] != w[1] || w[1] != w[2] || sum != w[0] * 3)
			fail(tag, "three segments that fit in 400 must get equal widths");
		printf("  segments: Off|Near|Bilinear equal %d each (room 400)", w[0]);
		sum = stdSegmentWidths(kFont, VIDEO, 3, 260, w, &eq);
		if (eq || sum != w[0] + w[1] + w[2] || w[0] != stdStepWidth(kFont, "Off", 16) ||
		    w[2] != stdStepWidth(kFont, "Bilinear", 16) || w[0] == w[2])
			fail(tag, "segments that do not fit must get their own text + 16");
		printf(", own %d+%d+%d (room 260)\n", w[0], w[1], w[2]);
	}

	/* the approved Video row: Filters Off | Near | Bilinear, then the separate button Trap, in 352 */
	{
		memset(rows, 0, sizeof rows);
		rows[0].kind = ROW_CHOICE; rows[0].label = "Filters"; rows[0].names = VIDEO; rows[0].count = 4; rows[0].plain = 1;
		p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
		if (layoutPage(p, kFont, lay) < 0) {
			fail(tag, "the Video filters row does not lay out");
		} else {
			const Control &o = lay.c[lay.slot[0][0]], &nr = lay.c[lay.slot[0][1]], &bl = lay.c[lay.slot[0][2]], &tr = lay.c[lay.slot[0][3]];
			printf("  Video row: widths %d+%d+%d, Trap at x %d w %d (ends %d)\n", (int)o.rect.w, (int)nr.rect.w, (int)bl.rect.w,
			       (int)tr.rect.x, (int)tr.rect.w, (int)(tr.rect.x + tr.rect.w));
				/* the equal rule would not leave room for the button: the segments keep their own widths */
				if (o.rect.w != stdStepWidth(kFont, "Off", 16) || nr.rect.w != stdStepWidth(kFont, "Near", 16) ||
				    bl.rect.w != stdStepWidth(kFont, "Bilinear", 16))
					fail(tag, "Video row: the segments must keep their own text + 16 widths (equal widths leave no room for Trap)");
				if (tr.role != ROLE_BUTTON || tr.rect.w != stdStepWidth(kFont, "Trap", 16) ||
				    tr.rect.x != bl.rect.x + bl.rect.w + STD_BTN_GAP)
					fail(tag, "Video row: Trap must be a separate button, its own text + 16 (the row has no room for equal widths), 12 px after the segments");
				if (stdHas(p, lay, PROB_BTN_TOUCH))
					fail(tag, "Video row: the button touches the segments");
				/* Monty's width rule (2026-11-04): a row that does not fit with equal widths gives every part
				 * its own width, separate buttons included: 64+80+112, gap 12, Trap 72 = x 604. */
				if (stdHas(p, lay, PROB_PAST_RIGHT) || (int)(tr.rect.x + tr.rect.w) != 604)
					fail(tag, "Video row: it must end at x 604, inside 616");
			}
		}

		/* The same structure in a row that does fit: Off | Near | Bilinear, then a short button. Equal widths
		 * (3 x 128) would not leave room for it; own widths do. */
		{
			static const char *const NAMES[] = { "Off", "Near", "Bilinear", "Go" };
			memset(rows, 0, sizeof rows);
			rows[0].kind = ROW_CHOICE; rows[0].label = "Filters"; rows[0].names = NAMES; rows[0].count = 4; rows[0].plain = 1;
			p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
			if (layoutPage(p, kFont, lay) < 0) {
				fail(tag, "a choice followed by a button does not lay out");
			} else {
				const Control &bl = lay.c[lay.slot[0][2]], &go = lay.c[lay.slot[0][3]];
				const int room = (int)STD_CTRL_W - stdButtonWidth(kFont, "Go") - (int)STD_BTN_GAP;
				int w[3];
				bool eq = true;
				stdSegmentWidths(kFont, NAMES, 3, room, w, &eq);
				if (eq || bl.rect.w != (float)stdStepWidth(kFont, "Bilinear", 16))
					fail(tag, "choice + button: the equal rule must give way to own widths");
				if (go.rect.x != bl.rect.x + bl.rect.w + STD_BTN_GAP || go.rect.x + go.rect.w > STD_CTRL_X1)
					fail(tag, "choice + button: the button must sit 12 px after the segments, inside 616");
				if (stdHas(p, lay, PROB_PAST_RIGHT) || stdHas(p, lay, PROB_BTN_TOUCH) || stdHas(p, lay, PROB_TEXT_WIDE))
					fail(tag, "choice + button: a fitting row was reported");
			}
		}

	/* PROB_LABEL_WIDE */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_CYCLE; rows[0].label = "A label that is much too long for the left column"; rows[0].names = OFF_ON; rows[0].count = 2;
	p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_LABEL_WIDE)) fail(tag, "a label wider than 224 px was not reported");
	rows[0].label = "Short";
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_LABEL_WIDE)) fail(tag, "a short label was reported as too wide");

	/* PROB_PAST_RIGHT: a row of separate buttons from 264 reaching past 616; and one that stays inside */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].label = "Input"; rows[0].actions = BTN3; rows[0].count = 3;
	p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_PAST_RIGHT)) fail(tag, "a row reaching past x 616 was not reported");
	rows[0].count = 1;
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_PAST_RIGHT)) fail(tag, "a row inside x 616 was reported as past the right edge");

	/* PROB_IN_STD_LOGO: row 5 of a Regular page is y 368-418; the logo box is 572,384 64x88 */
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 5; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
	rows[5].kind = ROW_CHOICE; rows[5].label = "Wide"; rows[5].names = LONG3; rows[5].count = 3;
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
	layoutPage(p, kFont, lay);
	if (!stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "a control in the logo box of the standard was not reported");
	p.logo = LOGO_SPOT_NONE;
	if (stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "the logo box was checked on a page with no logo");
	p.logo = LOGO_SPOT_BOTTOM;
	rows[5].names = OFF_ON; rows[5].count = 2;
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_IN_STD_LOGO)) fail(tag, "a short choice left of the logo box was reported in it");
	if (stdHas(p, lay, PROB_IN_LOGO)) fail(tag, "the legacy logo check ran on a standard page");

	/* PROB_BTN_TOUCH: layoutPage always leaves 12; move one button by hand to touch. */
	memset(rows, 0, sizeof rows);
	rows[0].kind = ROW_BUTTONS; rows[0].label = "Input"; rows[0].actions = BTN2; rows[0].count = 2;
	rows[1].kind = ROW_CHOICE; rows[1].label = "Mode"; rows[1].names = VIDEO; rows[1].count = 4; rows[1].plain = 1;
	p = stdPage(rows, 2, DENSITY_REGULAR, LOGO_SPOT_NONE);
	layoutPage(p, kFont, lay);
	if (stdHas(p, lay, PROB_BTN_TOUCH)) fail(tag, "buttons 12 px apart, and touching segments of one choice, were reported as touching");
	{
		Layout t = lay;
		Control &b = t.c[t.slot[0][1]];
		const Control &a = t.c[t.slot[0][0]];
		b.rect.x = a.rect.x + a.rect.w + 4;
		if (!stdHas(p, t, PROB_BTN_TOUCH)) fail(tag, "two action buttons 4 px apart were not reported");
		t = lay;
		t.c[t.slot[1][3]].rect.x = t.c[t.slot[1][2]].rect.x + t.c[t.slot[1][2]].rect.w;
		if (!stdHas(p, t, PROB_BTN_TOUCH)) fail(tag, "an action button touching a choice segment was not reported");
	}

	/* the density limits: 6 Regular rows, 7 Compact rows; one more is -1 */
	memset(rows, 0, sizeof rows);
	for (int i = 0; i < 8; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
	p = stdPage(rows, 6, DENSITY_REGULAR, LOGO_SPOT_NONE);
	if (layoutPage(p, kFont, lay) < 0) fail(tag, "6 Regular rows must fit");
	p.nRows = 7;
	if (layoutPage(p, kFont, lay) != -1) fail(tag, "a 7th Regular row must be refused (-1)");
	p.standard = DENSITY_COMPACT;
	if (layoutPage(p, kFont, lay) < 0) fail(tag, "7 Compact rows must fit");
	p.nRows = 8;
	if (layoutPage(p, kFont, lay) != -1) fail(tag, "an 8th Compact row must be refused (-1)");

	/* the footer */
	{
		static const FooterHelp OK[2] = { { { "One line of help.", NULL } },
		                                  { { "First line of the help for this row.", "Second line of the help." } } };
		static const FooterHelp BAD[2] = { { { "Short.", NULL } },
		                                   { { "A help line that is much too long to fit in the footer, which is 532 pixels wide at 13 px, so it has to be cut or reworded", NULL } } };
		Control c[STD_HELP_LINES];
		memset(rows, 0, sizeof rows);
		for (int i = 0; i < 2; i++) { rows[i].kind = ROW_CYCLE; rows[i].label = "Row"; rows[i].names = OFF_ON; rows[i].count = 2; }
		p = stdPage(rows, 2, DENSITY_REGULAR, LOGO_SPOT_NONE);
		if (layoutFooter(p, kFont, 0, c) != 0) fail(tag, "a page with no footer must give 0 lines");
		p.footer = OK;
		if (layoutFooter(p, kFont, 0, c) != 1 || strcmp(c[0].text, "One line of help.") != 0) fail(tag, "a 1-line footer");
		if (layoutFooter(p, kFont, 1, c) != 2) {
			fail(tag, "a 2-line footer must give 2 lines");
		} else if (c[0].role != ROLE_FOOTER || c[0].ax != STD_HELP_X0 || c[0].ay != STD_HELP_Y0 ||
		           c[1].ay != STD_HELP_Y0 + STD_HELP_DY || c[0].align != ALIGN_LEFT_TOP) {
			fail(tag, "footer lines are not at x 24, y 442 and 459, left-aligned");
		}
		if (layoutFooter(p, kFont, 2, c) != 0 || layoutFooter(p, kFont, -1, c) != 0) fail(tag, "a row outside the page must give 0 lines");
		if (checkFooters(p, kFont) != 0) fail(tag, "checkFooters reported a fitting footer");
		p.footer = BAD;
		if (layoutFooter(p, kFont, 1, c) != -1) fail(tag, "a footer line wider than 532 px must give -1");
		if (layoutFooter(p, kFont, 0, c) != 1) fail(tag, "a short footer line beside a bad one must still lay out");
		if (checkFooters(p, kFont) != 1) fail(tag, "checkFooters must count the one bad row");
		{
			/* a line that fits the footer (556) but reaches under the page logo (box left 542) */
			static char NEAR_LOGO[96];
			static const FooterHelp LG[1] = { { { NEAR_LOGO, NULL } } };
			int n = 0;
			for (; n < 90; n++) {
				NEAR_LOGO[n] = 'm';
				NEAR_LOGO[n + 1] = 0;
				if (kFont.width(NEAR_LOGO, (float)STD_HELP_PX / 24.0f, kFont.ctx) > 520.0f)
					break;
			}
			p.nRows = 1;
			p.footer = LG;
			p.logo = LOGO_SPOT_NONE;
			if (layoutFooter(p, kFont, 0, c) != 1) fail(tag, "a 520 px help line must fit a page with no logo");
			p.logo = LOGO_SPOT_BOTTOM;
			if (layoutFooter(p, kFont, 0, c) != -1) fail(tag, "a help line reaching under the page logo was not reported");
		}
	}

	printf("standard pages: widths, Video row, four checks, density limits, footer ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/* The shipped Video, Controls, Saves, Audio, System and Advanced tabs are checked in menu_pages_test.cpp
 * (the builders' output). This one is synthetic: the standard's x 616 check must catch an overflow. */
static void testStdOverflowCaught(void)
{
	const int before = failures;
	Row rows[1];
	Page p;
	memset(rows, 0, sizeof rows);
	/* the overflow must be caught: the old "Force NTSC" after a 200 px cycle ends at x 644 */
	memset(rows, 0, sizeof rows);
	{
		static const ActionButton OLD[] = { { "Auto", 200, false }, { "Force NTSC", 0, false } };
		Layout lay;
		rows[0].kind = ROW_BUTTONS; rows[0].label = "Frame Rate"; rows[0].actions = OLD; rows[0].count = 2;
		p = stdPage(rows, 1, DENSITY_REGULAR, LOGO_SPOT_BOTTOM);
		if (layoutPage(p, kFont, lay) < 0 || !stdHas(p, lay, PROB_PAST_RIGHT))
			fail("Video tab", "the check does not catch Frame Rate + Force NTSC passing x 616");
	}

	printf("standard pages: the x 616 check catches an overflow ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/*  The explorer (DENSITY_EXPLORER, menu standard step 3p slice 1): the file browser's page  */

struct ExpItem {
	const char *text;
	bool        folder;
	const char *tag;
	const char *note;
};

static void expItemFn(void *ctx, int index, ListItem &out)
{
	const ExpItem &e = ((const ExpItem *)ctx)[index];
	out.text = e.text;
	out.folder = e.folder;
	out.tag = e.tag;
	out.note = e.note;
}

/* The control of a given text role on window slot k, or -1. */
static int expText(const Layout &lay, CtrlRole role, int k)
{
	for (int i = 0; i < lay.n; i++)
		if (lay.c[i].kind == CTRL_TEXT && lay.c[i].role == role && lay.c[i].slot == k)
			return i;
	return -1;
}

static void expCheckPage(const char *where, const Page &page, const Layout &lay, const ListState &st,
                         const ExpItem *items, int nTabs)
{
	Problem probs[MAX_PROBLEMS];
	const int np = checkLayout(page, kFont, lay, probs, MAX_PROBLEMS);
	const int top = st.top;

	if (np != 0) {
		char buf[96];
		sprintf(buf, "checkLayout found %d problem(s); first: %s", np, problemName(probs[0].kind));
		fail(where, buf);
	}

	for (int i = 0; i < lay.n; i++) {
		const Rect &r = lay.c[i].rect;
		if (r.x + r.w > 616.0f + 0.01f)
			fail(where, "a control reaches past x 616");
	}
	/* The explorer carries no logo: its last row and the Next arrow (x to 616, y to 398) and its tab strip
	 * would meet the standard's logo box (x 572-636, y 384-472) or the top-right one (x 534-606, y 34-106).
	 * With LOGO_SPOT_NONE no logo is drawn and checkLayout's logo checks have nothing to test. */
	if (page.logo != LOGO_SPOT_NONE)
		fail(where, "the explorer page has a logo");

	for (int k = 0; k < st.slots; k++) {
		const int c = lay.listItem[k];
		const int index = top + k;
		if (c < 0) { fail(where, "a window slot has no control"); return; }
		if (!nearly(lay.c[c].rect.x, (float)STD_LIST_X0) || !nearly(lay.c[c].rect.w, (float)STD_LIST_W) ||
		    !nearly(lay.c[c].rect.y, (float)STD_LIST_Y0 + (float)STD_LIST_PITCH * (float)k) ||
		    !nearly(lay.c[c].rect.h, (float)STD_LIST_H))
			fail(where, "a row's box is not 576 x STD_LIST_H at STD_LIST_PITCH from y 126");
		if (k + 1 < st.slots && lay.listItem[k + 1] >= 0) {
			const float gap = lay.c[lay.listItem[k + 1]].rect.y - (lay.c[c].rect.y + lay.c[c].rect.h);
			if (gap < -0.01f)
				fail(where, "two list rows overlap");
			if (gap > 0.01f)
				fail(where, "two list rows have a gap (the explorer rows touch)");
		}
		if (index >= st.count) {
			if (lay.c[c].focusable)
				fail(where, "a row past the end of the list is focusable");
			continue;
		}
		{
			const ExpItem &e = items[index];
			const Control &it = lay.c[c];
			const int tg = expText(lay, ROLE_LIST_TAG, k);
			const int nt = expText(lay, ROLE_LIST_NOTE, k);
			const float wantX = e.folder ? (float)STD_LIST_FOLDER_X : (float)STD_LIST_NAME_X;
			const int nameW = fontWidth(it.text, grid::TEXT_SCALE, NULL);

			if (!it.focusable || it.folder != e.folder)
				fail(where, "a row is not focusable, or its folder flag is wrong");
			if (!it.full || strcmp(it.full, e.text) != 0)
				fail(where, "Control::full is not the whole name");
			if (!nearly(it.textX, wantX))
				fail(where, "a name does not start at the folder or game x");
			if ((e.tag != NULL) != (tg >= 0) || (e.note != NULL) != (nt >= 0))
				fail(where, "a tag or note control is missing, or there is one for nothing");
			if (nameW > (int)(it.textR - it.textX))
				fail(where, "a cut name is wider than its room");
			if (tg >= 0) {
				const Rect &t = lay.c[tg].rect;
				if (!nearly(t.x + t.w, (float)STD_LIST_TAG_R))
					fail(where, "a tag does not end at the tag's right edge");
				if (it.textR > t.x - (float)STD_LIST_NAME_GAP + 0.01f)
					fail(where, "a name's room is not 16 px short of its tag");
			}
			if (nt >= 0) {
				const Rect &n = lay.c[nt].rect;
				const float wantNoteR = tg >= 0 ? lay.c[tg].rect.x - (float)STD_LIST_NOTE_GAP : (float)STD_LIST_NOTE_R;
				if (!nearly(n.x + n.w, wantNoteR))
					fail(where, "a note does not end 10 px left of its tag (or at the note edge without a tag)");
				if (it.textR > n.x - (float)STD_LIST_NAME_GAP + 0.01f)
					fail(where, "a name's room is not 16 px short of its note");
				if (tg >= 0 && n.x + n.w > lay.c[tg].rect.x)
					fail(where, "a note is not left of its tag");
			}
			if (strcmp(it.text, e.text) != 0) {
				const size_t n = strlen(it.text);
				if (n < 3 || strcmp(it.text + n - 3, "...") != 0 || strncmp(it.text, e.text, n - 3) != 0)
					fail(where, "a cut name is not a prefix of the name followed by \"...\"");
			}
		}
	}
	(void)nTabs;
}

static void expCheckLinks(const char *where, const Page &page, const Layout &lay, const ListState &st)
{
	int first = -1, last = -1, nAct = 0;
	const int prev = lay.slot[0][0], next = lay.slot[0][1];
	const int def = lay.defaultFocus;

	for (int k = 0; k < st.slots; k++) {
		const int c = lay.listItem[k];
		if (c >= 0 && lay.c[c].focusable) {
			if (first < 0) first = c;
			last = c;
			nAct++;
		}
	}
	if (first < 0) { fail(where, "no focusable row"); return; }

	if (def < 0 || lay.c[def].role != ROLE_LIST_ITEM)
		fail(where, "the default focus is not a row");
	for (int t = 0; t < page.nTabs; t++) {
		const int c = lay.tab[t];
		if (c < 0 || !lay.c[c].focusable) { fail(where, "a tab is not focusable"); continue; }
		if (lay.c[c].link[DIR_DOWN] != def)
			fail(where, "Down from a tab does not go to the default row");
		if (lay.c[c].link[DIR_UP] != -1)
			fail(where, "a tab has an Up link");
		if (lay.c[c].link[DIR_RIGHT] != lay.tab[(t + 1) % page.nTabs] ||
		    lay.c[c].link[DIR_LEFT] != lay.tab[(t + page.nTabs - 1) % page.nTabs])
			fail(where, "Left or Right on a tab does not go to the next or previous tab");
	}
	if (lay.c[first].link[DIR_UP] != lay.tab[page.curTab])
		fail(where, "Up from the first row does not go to the lit tab");
	if (lay.c[last].link[DIR_DOWN] != first)
		fail(where, "Down from the last row does not wrap to the first");
	for (int k = 0, prevC = -1; k < st.slots; k++) {
		const int c = lay.listItem[k];
		if (c < 0 || !lay.c[c].focusable)
			continue;
		if (prevC >= 0 && (lay.c[c].link[DIR_UP] != prevC || lay.c[prevC].link[DIR_DOWN] != c))
			fail(where, "two neighbouring rows are not linked Up and Down");
		prevC = c;
		if (prev >= 0 && lay.c[prev].focusable && lay.c[c].link[DIR_LEFT] != prev)
			fail(where, "Left from a row does not go to the Prev arrow");
		if (next >= 0 && lay.c[next].focusable && lay.c[c].link[DIR_RIGHT] != next)
			fail(where, "Right from a row does not go to the Next arrow");
	}
	/* the arrows keep their links while inactive (focus stays on one after A turns the page) */
	if (prev < 0 || next < 0) { fail(where, "an arrow is missing"); return; }
	if (lay.c[prev].link[DIR_RIGHT] != first || lay.c[prev].link[DIR_LEFT] != first ||
	    lay.c[prev].link[DIR_DOWN] != first)
		fail(where, "the Prev arrow does not go back to the first row (Down, Left, Right)");
	if (lay.c[prev].link[DIR_UP] != lay.tab[page.curTab])
		fail(where, "Up from the Prev arrow does not go to the lit tab");
	if (lay.c[next].link[DIR_UP] != last || lay.c[next].link[DIR_LEFT] != last)
		fail(where, "Up and Left from the Next arrow do not go to the last row");
	if (lay.c[next].link[DIR_DOWN] != -1 || lay.c[next].link[DIR_RIGHT] != -1)
		fail(where, "the Next arrow has a Down or Right link");
	(void)nAct;
}

static void testStdExplorer(void)
{
	static const char *const TABS[] = { "SD", "USB", "DVD", "Network", "Recent" };
	static const char *const LONG_NAME =
	    "Final Fantasy VII International (Disc 1) (Rev 2) (Special Limited Edition With A Name Far Too Long For One Row).chd";
	static const char *const LONG_FOLDER =
	    "A folder with an absurdly long name that goes on and on and on past every limit the row has, "
	    "so that the buffer of the cut name is the thing that ends it, long before the text itself does ......";
	static const char *const UTF8_NAME =
	    "Caf\xC3\xA9 \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
	    "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
	    "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 end";
	static const ExpItem ITEMS[] = {
		{ "..",                      true,  NULL,  NULL },
		{ "Games",                   true,  NULL,  NULL },
		{ "Crash Bandicoot 3",       false, "BIN", NULL },
		{ "Final Fantasy VII",       false, "CHD", "3 discs" },
		{ LONG_NAME,                 false, "CHD", "3 discs" },
		{ LONG_FOLDER,               true,  NULL,  NULL },
		{ UTF8_NAME,                 false, "PBP", "12 Mar" },
		{ "Spyro 2",                 false, "ISO", NULL },
		{ "Tekken 3",                false, NULL,  "2 discs" },
		{ "Gex",                     false, "CUE", NULL },
		{ "Vagrant Story",           false, "ECM", NULL },
	};
	const int before = failures;
	Tab tabs[5];
	Row row;
	Page page;
	Layout lay;
	ListState st;
	ListSpec spec;

	for (int i = 0; i < 5; i++) { tabs[i].text = TABS[i]; tabs[i].width = 0; }

	memset(&spec, 0, sizeof spec);
	st.count = (int)COUNT(ITEMS);
	st.slots = STD_LIST_ROWS;
	st.top = 0;
	st.focus = -1;
	spec.state = &st;
	spec.mode = LIST_PAGED;
	spec.item = expItemFn;
	spec.ctx = (void *)ITEMS;
	spec.prev.text = "^";
	spec.next.text = "v";		/* the geometry fields stay 0: the explorer ignores them */

	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.tabs = tabs;
	page.nTabs = 5;
	page.curTab = 1;
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_NONE;
	page.focus = FOCUS_ROWS;
	page.standard = DENSITY_EXPLORER;

	/* page 1 of 2: every item kind, the long names cut, the tag rightmost */
	if (layoutPage(page, kFont, lay) < 0) {
		fail("Explorer", "the explorer page does not lay out");
		return;
	}
	linkFocus(page, lay);
	expCheckPage("Explorer p1", page, lay, st, ITEMS, 5);
	expCheckLinks("Explorer p1", page, lay, st);
	{
		const Control &dd = lay.c[lay.listItem[0]];
		const Control &gm = lay.c[lay.listItem[2]];
		const Control &lg = lay.c[lay.listItem[4]];
		const Control &lf = lay.c[lay.listItem[5]];
		const Control &u8 = lay.c[lay.listItem[6]];
		if (!dd.folder || strcmp(dd.text, "..") != 0 || !nearly(dd.textX, (float)STD_LIST_FOLDER_X))
			fail("Explorer p1", "the \"..\" row is not a folder row with its text whole");
		if (gm.folder || strcmp(gm.text, "Crash Bandicoot 3") != 0)
			fail("Explorer p1", "a short game name was changed");
		if (strcmp(lg.text, LONG_NAME) == 0 || strlen(lg.text) < 4 || strcmp(lg.text + strlen(lg.text) - 3, "...") != 0)
			fail("Explorer p1", "a long game name was not cut with \"...\"");
		if (strcmp(lf.text, LONG_FOLDER) == 0 || strcmp(lf.text + strlen(lf.text) - 3, "...") != 0 ||
		    strlen(lf.text) > (size_t)STD_CUT_LEN - 1)
			fail("Explorer p1", "a long folder name was not cut with \"...\" inside the buffer");
		/* the cut never splits a UTF-8 sequence: every 0xC3 is followed by 0xA9 */
		for (const unsigned char *s = (const unsigned char *)u8.text; *s; s++) {
			if (*s == 0xC3 && s[1] != 0xA9)
				fail("Explorer p1", "a cut name splits a UTF-8 character");
			if (*s >= 0x80 && *s != 0xC3 && *s != 0xA9)
				fail("Explorer p1", "a cut name has a stray byte");
		}
		if (strcmp(u8.text, UTF8_NAME) == 0)
			fail("Explorer p1", "the UTF-8 name was not cut");
		/* a row with a tag leaves less room than one without */
		if (!(lay.c[lay.listItem[3]].textR < lay.c[lay.listItem[1]].textR))
			fail("Explorer p1", "a row with a tag and a note has no less room than a bare one");
		/* a row's tag and note sit on its own row */
		{
			const int tg = expText(lay, ROLE_LIST_TAG, 3), nt = expText(lay, ROLE_LIST_NOTE, 3);
			if (tg < 0 || nt < 0 || lay.c[tg].rect.y < lay.c[lay.listItem[3]].rect.y ||
			    lay.c[tg].rect.y + lay.c[tg].rect.h > lay.c[lay.listItem[3]].rect.y + (float)STD_LIST_H)
				fail("Explorer p1", "a tag is not inside its row's height");
		}
	}
	/* Page arrows: Prev off at the top, Next on */
	if (lay.slot[0][0] < 0 || lay.c[lay.slot[0][0]].focusable || lay.slot[0][1] < 0 || !lay.c[lay.slot[0][1]].focusable)
		fail("Explorer p1", "Prev should be inactive and Next active on the first page");
	if (!nearly(lay.c[lay.slot[0][0]].rect.x, (float)STD_SCROLL_X) || !nearly(lay.c[lay.slot[0][0]].rect.w, (float)STD_SCROLL_W) ||
	    !nearly(lay.c[lay.slot[0][0]].rect.y, (float)STD_LIST_Y0) ||
	    !nearly(lay.c[lay.slot[0][1]].rect.y + lay.c[lay.slot[0][1]].rect.h,
	            (float)STD_LIST_Y0 + (float)(STD_LIST_ROWS - 1) * (float)STD_LIST_PITCH + (float)STD_LIST_H))
		fail("Explorer p1", "the arrows are not at the two ends of the scroll column");

	/* the tab strip: five equal tabs do not fit, so each tab has its own width (text + 16), gap 12, centred,
	 * inside x 24..616 (Monty 2026-11-05) */
	for (int t = 1; t < 5; t++) {
		const float gap = lay.c[lay.tab[t]].rect.x - (lay.c[lay.tab[t - 1]].rect.x + lay.c[lay.tab[t - 1]].rect.w);
		if (!nearly(gap, (float)STD_TAB_GAP))
			fail("Explorer tabs", "the gap between two tabs is not 12");
	}
	if (lay.c[lay.tab[0]].rect.x < 24.0f - 0.01f || lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w > 616.0f + 0.01f)
		fail("Explorer tabs", "the tab strip is not inside x 24..616");
	if (fabsf(lay.c[lay.tab[0]].rect.x - ((float)MENU_W - (lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w))) > 1.01f)
		fail("Explorer tabs", "the tab strip is not centred");
	printf("  explorer tab widths: %d %d %d %d %d (x %d..%d)\n", (int)lay.c[lay.tab[0]].rect.w, (int)lay.c[lay.tab[1]].rect.w,
	       (int)lay.c[lay.tab[2]].rect.w, (int)lay.c[lay.tab[3]].rect.w, (int)lay.c[lay.tab[4]].rect.w,
	       (int)lay.c[lay.tab[0]].rect.x, (int)(lay.c[lay.tab[4]].rect.x + lay.c[lay.tab[4]].rect.w));

	/* the state's own focus: Down from a tab goes to that row */
	st.focus = 3;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	if (lay.defaultFocus != lay.listItem[3] || lay.c[lay.tab[0]].link[DIR_DOWN] != lay.listItem[3])
		fail("Explorer focus", "Down from a tab does not go to the row the state names");
	if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.tab[1])
		fail("Explorer focus", "Up from the first row does not go to the lit tab");
	st.focus = -1;

	/* page 2 of 2: four rows, Prev on, Next off, the rest of the window inactive */
	st.top = 7;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	expCheckPage("Explorer p2", page, lay, st, ITEMS, 5);
	expCheckLinks("Explorer p2", page, lay, st);
	if (!lay.c[lay.slot[0][0]].focusable || lay.c[lay.slot[0][1]].focusable)
		fail("Explorer p2", "Prev should be active and Next inactive on the last page");
	for (int k = 4; k < 7; k++)
		if (lay.c[lay.listItem[k]].focusable || lay.c[lay.listItem[k]].text[0] != '\0')
			fail("Explorer p2", "a row past the end is not empty and inactive");
	st.top = 0;

	/* the last page with fewer items (page 2 above: 4 rows): nothing past them is focusable, no link
	 * reaches it, and the arrows' links keep going to the shown rows */
	st.top = 7;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++) {
			const int to = lay.c[i].link[d];
			for (int k = 4; k < 7; k++)
				if (to == lay.listItem[k])
					fail("Explorer p2", "a link goes to a row past the end of the list");
		}
	if (lay.c[lay.slot[0][1]].link[DIR_UP] != lay.listItem[3] || lay.c[lay.slot[0][1]].link[DIR_LEFT] != lay.listItem[3])
		fail("Explorer p2", "the Next arrow does not go up to the last shown row");
	st.top = 0;

	/* a list shorter than the window (3 items): both arrows off, the rest of the rows off, links to the 3 */
	{
		ListState ss = st;
		ListSpec ssp = spec;
		Row srow = row;
		Page sp = page;
		ss.count = 3;
		ssp.state = &ss;
		srow.list = &ssp;
		sp.rows = &srow;
		layoutPage(sp, kFont, lay);
		linkFocus(sp, lay);
		expCheckLinks("Explorer short", sp, lay, ss);
		if (lay.c[lay.slot[0][0]].focusable || lay.c[lay.slot[0][1]].focusable)
			fail("Explorer short", "an arrow is active on a list that fits the window");
		for (int k = 0; k < 7; k++)
			if (lay.c[lay.listItem[k]].focusable != (k < 3))
				fail("Explorer short", "the rows past the 3 items are not off, or a shown one is");
	}

	/* the other tab lit: Up from the first row follows it */
	page.curTab = 4;
	layoutPage(page, kFont, lay);
	linkFocus(page, lay);
	if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.tab[4])
		fail("Explorer tab 4", "Up from the first row does not follow the lit tab");
	page.curTab = 1;

	/* an empty list (the Recent tab with nothing in it): still lays out, tabs reachable, no problem */
	{
		ListState es = st;
		ListSpec esp = spec;
		Row erow = row;
		Page ep = page;
		Problem probs[MAX_PROBLEMS];
		es.count = 0;
		esp.state = &es;
		erow.list = &esp;
		ep.rows = &erow;
		if (layoutPage(ep, kFont, lay) < 0) {
			fail("Explorer empty", "an empty list does not lay out");
		} else {
			linkFocus(ep, lay);
			if (checkLayout(ep, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Explorer empty", "an empty list has layout problems");
			if (lay.defaultFocus < 0 || lay.c[lay.defaultFocus].role != ROLE_TAB)
				fail("Explorer empty", "an empty list's default focus is not a tab");
		}
	}

	/* no tabs (Accounts mode): a title instead, the rows' first Up has nowhere to go */
	{
		Page ap = page;
		ap.tabs = NULL;
		ap.nTabs = 0;
		ap.title = "Accounts";
		if (layoutPage(ap, kFont, lay) < 0) {
			fail("Explorer title", "the explorer with a title does not lay out");
		} else {
			Problem probs[MAX_PROBLEMS];
			linkFocus(ap, lay);
			if (checkLayout(ap, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Explorer title", "the explorer with a title has layout problems");
			if (lay.c[lay.listItem[0]].link[DIR_UP] != lay.listItem[STD_LIST_ROWS - 1])
				fail("Explorer title", "Up from the first row does not wrap to the last");
		}
	}

	/* the Players account page (AccountFrame): "Account for <Mii>", a few plain rows, no tags, no tabs */
	{
		static const ExpItem ACC[] = {
			{ "None: earn Local", false, NULL, NULL },
			{ "Ann", false, NULL, NULL },
			{ "Bob", false, NULL, NULL },
			{ "This Wii's account", false, NULL, NULL },
		};
		ListState as;
		ListSpec asp;
		Row arow;
		Page ap = page;
		Problem probs[MAX_PROBLEMS];
		memset(&asp, 0, sizeof asp);
		as.count = (int)COUNT(ACC);
		as.slots = STD_LIST_ROWS;
		as.top = 0;
		as.focus = -1;
		asp.state = &as;
		asp.mode = LIST_PAGED;
		asp.item = expItemFn;
		asp.ctx = (void *)ACC;
		asp.prev.text = "^";
		asp.next.text = "v";
		memset(&arow, 0, sizeof arow);
		arow.kind = ROW_LIST;
		arow.list = &asp;
		ap.rows = &arow;
		ap.tabs = NULL;
		ap.nTabs = 0;
		ap.title = "Account for Player One";
		if (layoutPage(ap, kFont, lay) < 0) {
			fail("Account page", "the account page does not lay out");
		} else {
			linkFocus(ap, lay);
			if (checkLayout(ap, kFont, lay, probs, MAX_PROBLEMS) != 0)
				fail("Account page", "the account page has layout problems");
			if (lay.defaultFocus != lay.listItem[0])
				fail("Account page", "the default focus is not the first row");
			for (int k = 0; k < STD_LIST_ROWS; k++)
				if (lay.c[lay.listItem[k]].focusable != (k < 4))
					fail("Account page", "the rows past the 4 accounts are not off, or a shown one is");
		}
	}

	/* what the explorer refuses */
	{
		Row two[2];
		Page bad = page;
		two[0] = row; two[1] = row;
		bad.rows = two; bad.nRows = 2;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "two list rows were accepted");
		bad = page;
		bad.standard = DENSITY_REGULAR;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "a list on a non-explorer density was accepted");
		bad = page;
		st.slots = STD_LIST_ROWS + 1;
		if (layoutPage(bad, kFont, lay) >= 0)
			fail("Explorer refuse", "STD_LIST_ROWS + 1 rows were accepted");
		st.slots = STD_LIST_ROWS;
	}

	printf("standard explorer: every item kind, cut names, tag and note, links, arrows, nothing past x 616 ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

/* The explorer's letter strip (stripLetter, stripMask, stripFirst, stripStep, stripCell, stripHit) and
 * the scroll thumb (scrollThumb, scrollDragPage). */
static const char *const kStripNames[] = {
	"..", "Formats", "Tests", "Alpha (USA).cue", "alpine.chd", "Castlevania (USA).chd", "Crash 3.cue",
	"Zelda.iso", "1942.cue", "[demo].bin", "\xC3\x89t\xC3\xA9.chd", "Final Fantasy VIII (USA).pbp",
	"Frogger.pbp", "Spyro (USA).chd"
};

static const char *stripNameAt(void *, int i)
{
	return kStripNames[i];
}

/* The facts strip: the panel under the list, the title line, six fact cells in two columns. */
/* A list item with dim set (Recent: its device is not there) lays out dim, and no other row does. */
static const bool DIM_ROWS[4] = { false, true, false, true };
static void dimItemFn(void *, int index, ListItem &out)
{
	static const char *const names[4] = { "Crash 3", "FF8 (usb)", "Gex", "Spyro (smb)" };
	out.text = names[index];
	out.folder = false;
	out.dim = DIM_ROWS[index];
	out.tag = "BIN";
	out.note = index == 1 ? "USB, 2026-09-28" : "today";
}

/* The guards: a bad page is refused (layoutPage -1, the old layout kept) and linkFocus never indexes with it. */
static void testGuards(void)
{
	static ListState st;
	static ListSpec spec;
	static Row row;
	static Page page;
	static Layout lay;
	static const Tab tabs[2] = { { "A", 0.0f }, { "B", 0.0f } };
	const char *where = "guards";

	printf("Guards\n");
	st.count = 100; st.slots = 5; st.top = 0; st.focus = -1;	/* a window of 5, less than the 8 buttons a view makes */
	spec = listSpec(&st, LIST_PAGED);
	listPageOf(page, row, spec);
	page.standard = DENSITY_EXPLORER;
	page.tabs = tabs; page.nTabs = 2; page.curTab = 0;
	if (!layOutList(page, lay, where))
		return;
	for (int k = 0; k < 5; k++)
		if (lay.listItem[k] < 0)
			fail(where, "a window of 5 has an item without a control");
	for (int k = 5; k < STD_LIST_ROWS; k++)
		if (lay.listItem[k] >= 0)
			fail(where, "a window of 5 has a control past its slots (the view reads -1 there)");

	const int n = lay.n;
	page.curTab = 7;
	if (layoutPage(page, kFont, lay) >= 0)
		fail(where, "layoutPage accepted a curTab out of range");
	if (lay.n != n)
		fail(where, "a refused layoutPage changed the layout (the caller keeps the old one)");
	linkFocus(page, lay);		/* must not index lay.tab[7] */
	if (lay.defaultFocus != -1)
		fail(where, "linkFocus with a curTab out of range left a default focus");
	for (int i = 0; i < lay.n; i++)
		for (int d = 0; d < DIR_COUNT; d++)
			if (lay.c[i].link[d] != -1)
				fail(where, "linkFocus with a curTab out of range left a link");
	page.curTab = -1;
	linkFocus(page, lay);
	if (lay.defaultFocus != -1)
		fail(where, "linkFocus with curTab -1 left a default focus");

	page.curTab = 0;
	st.slots = STD_LIST_ROWS + 1;
	if (layoutPage(page, kFont, lay) >= 0)
		fail(where, "layoutPage accepted a window larger than the explorer's rows");
}

static void testCountText(void)
{
	const int before = failures;
	char out[64];
	const float s = (float)STD_PATH_PX / 24.0f;
	const int room = 440;		/* what FileBrowserFrame gives the count: the path keeps the rest of x 24..616 */
	struct Case { int folders, games, hint, cell; const char *want; };
	static const Case cases[] = {
		{ 0, 42, COUNT_HINT_AZ,      -1, "42 games, A-Z" },
		{ 0, 1,  COUNT_HINT_AZ,      -1, "1 game, A-Z" },
		{ 0, 42, COUNT_HINT_FOLDERS, -1, "42 games, folders first" },
		{ 1, 7,  COUNT_HINT_FOLDERS, -1, "1 folder, 7 games, folders first" },
		{ 3, 0,  COUNT_HINT_AZ,      -1, "3 folders, A-Z" },
		{ 0, 9,  COUNT_HINT_NEWEST,  -1, "9 games, newest first" },
		{ 0, 3,  COUNT_HINT_NEWEST,   3, "3 games, C" },
		{ 0, 2,  COUNT_HINT_NEWEST,   0, "2 games, #" },
		{ 0, 5,  COUNT_HINT_NONE,    -1, "5 games" },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		const Case &c = cases[i];
		countText(kFont, s, room, c.folders, c.games, c.hint, c.cell, out, sizeof out);
		if (strcmp(out, c.want)) {
			char msg[160];
			snprintf(msg, sizeof msg, "case %u: got \"%s\", want \"%s\"", i, out, c.want);
			fail("countText", msg);
		}
	}
	/* the worst case fits the room, and a tiny room drops the hint, then cuts with ... */
	countText(kFont, s, room, 20, 100, COUNT_HINT_FOLDERS, -1, out, sizeof out);
	if (fontWidth(out, s, NULL) > room || strcmp(out, "20 folders, 100 games, folders first"))
		fail("countText", "the longest count does not fit x 24..616 with the path's share");
	countText(kFont, s, fontWidth("42 games", s, NULL) + 2, 0, 42, COUNT_HINT_AZ, -1, out, sizeof out);
	if (strcmp(out, "42 games"))
		fail("countText", "a hint that does not fit is not dropped");
	countText(kFont, s, 40, 0, 42, COUNT_HINT_AZ, -1, out, sizeof out);
	if (fontWidth(out, s, NULL) > 40)
		fail("countText", "a tiny room is not cut");
	printf("explorer count text with sort hint, Recent hint, letter filter, fits the room %s\n",
	       failures == before ? "ok" : "FAILED");
}

/* The explorer's path cut: the one binary-search cut gives what the old per-character loop gave. */
static void oldCutLeft(char *out, size_t size, const char *s, float scale, int room)
{
	snprintf(out, size, "%s", s);
	if (fontWidth(out, scale, NULL) <= room)
		return;
	for (const char *p = s; *p; ) {
		p++;
		while ((*p & 0xC0) == 0x80)
			p++;
		snprintf(out, size, "...%s", p);
		if (fontWidth(out, scale, NULL) <= room)
			return;
	}
}

static void testCutTextLeft(void)
{
	const int before = failures;
	const float s = (float)STD_PATH_PX / 24.0f;
	static const char *paths[] = {
		"sd:/wiistation/isos/Formats/Frogger (USA) [CBN]/Frogger (USA).cbn",
		"usb:/games/psx/Final Fantasy VII (USA) (Disc 1)/Final Fantasy VII (USA) (Disc 1).chd",
		"smb:/General/Roms/PlayStation/Spyro the Dragon (Europe) (En,Fr,De,Es,It)",
		"sd:/\xC3\xA9\xC3\xA9\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E/\xC3\xA9tude \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.iso",
		"sd:/a", "sd:/", "x", "",
	};
	for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; i++)
		for (int room = -5; room < 500; room += 7) {
			char want[256], got[256];
			oldCutLeft(want, sizeof want, paths[i], s, room);
			cutTextLeft(kFont, paths[i], s, room, got, sizeof got);
			if (strcmp(want, got)) {
				char msg[600];
				snprintf(msg, sizeof msg, "path %u room %d: got \"%s\", want \"%s\"", i, room, got, want);
				fail("cutTextLeft", msg);
				return;
			}
		}
	printf("explorer path cut (binary search = per-character loop) %s\n", failures == before ? "ok" : "FAILED");
}

static void testStdDim(void)
{
	const int before = failures;
	static const char *const TABS[] = { "SD", "USB", "DVD", "Network", "Recent" };
	Tab tabs[5];
	Row row;
	Page page;
	Layout lay;
	ListState st;
	ListSpec spec;

	for (int i = 0; i < 5; i++) { tabs[i].text = TABS[i]; tabs[i].width = 0; }
	memset(&spec, 0, sizeof spec);
	st.count = 4;
	st.slots = STD_LIST_ROWS;
	st.top = 0;
	st.focus = -1;
	spec.state = &st;
	spec.mode = LIST_PAGED;
	spec.item = dimItemFn;
	spec.prev.text = "^";
	spec.next.text = "v";
	memset(&row, 0, sizeof row);
	row.kind = ROW_LIST;
	row.list = &spec;
	memset(&page, 0, sizeof page);
	page.tabs = tabs;
	page.nTabs = 5;
	page.curTab = 4;
	page.rows = &row;
	page.nRows = 1;
	page.logo = LOGO_SPOT_NONE;
	page.focus = FOCUS_ROWS;
	page.standard = DENSITY_EXPLORER;

	if (layoutPage(page, kFont, lay) < 0) {
		fail("Dim", "the page does not lay out");
		return;
	}
	for (int k = 0; k < st.slots; k++) {
		const bool want = k < 4 && DIM_ROWS[k];
		if (lay.c[lay.listItem[k]].dim != want)
			fail("Dim", "a row's dim flag is not the item's (or an empty slot is dim)");
	}
	/* the other lists never dim: the older list layout, with an item that leaves dim unset */
	if (failures == before)
		printf("Dim: ok\n");
}

static void testStdFacts(void)
{
	const int before = failures;
	FactsBox b;
	factsLayout(b);

	/* the path and count line clears the letter strip (the 2 px move), and the panel clears the list */
	if ((int)(STD_PATH_Y + STD_PATH_PX) > (int)STD_STRIP_Y)
		fail("Facts", "the path line (y + size) reaches the letter strip");
	if (b.y0 < (int)(STD_LIST_Y0 + (STD_LIST_ROWS - 1) * STD_LIST_PITCH + STD_LIST_H) + 10)
		fail("Facts", "the panel is less than 10 px below the last list row");
	if (b.y1 > 472 || b.x0 < 24 || b.x1 > 616 || b.y0 >= b.y1 || b.x0 >= b.x1)
		fail("Facts", "the panel leaves the 24..616 x 472 safe area or is empty");

	/* the title: inside the panel, above the first fact line */
	if (b.titleY < b.y0 || b.titleY + (int)STD_FACTS_TITLE_PX >= b.cell[0].y)
		fail("Facts", "the title is outside the panel or touches the first fact line");
	if (b.titleX < b.x0 || b.titleX + b.titleRoom > b.x1)
		fail("Facts", "the title's room leaves the panel");

	/* the cells: inside the panel, lines do not overlap, columns do not meet */
	for (int k = 0; k < STD_FACTS_N; k++) {
		const FactsCell &c = b.cell[k];
		if (c.y < b.y0 || c.y + (int)STD_FACTS_PX > b.y1)
			fail("Facts", "a fact line is outside the panel");
		if (c.valX + c.valRoom > b.x1)
			fail("Facts", "a value's room leaves the panel");
		if (c.keyR >= c.valX || c.keyR - 100 < b.x0)
			fail("Facts", "a key and its value are not in order or the key leaves the panel");
		if (k % 3 > 0 && c.y - b.cell[k - 1].y < (int)STD_FACTS_PX)
			fail("Facts", "two fact lines overlap (pitch below the text size)");
		if (k % 3 == 0 && c.y != b.cell[0].y)
			fail("Facts", "the columns' first lines are not level");
	}
	{
		const int keyW = fontWidth("Achievements", (float)STD_FACTS_PX / 24.0f, NULL);
		const int col1ValueEnd = b.cell[0].valX + b.cell[0].valRoom;
		const int col2KeyLeft = b.cell[3].keyR - keyW;
		if (col1ValueEnd >= col2KeyLeft)
			fail("Facts", "the first column's values reach the second column's keys");
	}

	/* fitText: a short name as it is, a long one cut with "..." inside the room */
	{
		char buf[128];
		const float s = (float)STD_FACTS_TITLE_PX / 24.0f;
		const char *shortName = "Spyro 2.chd";
		const char *longName = "Final Fantasy VII - International Edition (Japan) (Disc 1) (Rev 1) (Alt) (Beta 3) - Final Fantasy VII - International Edition (Japan) (Disc 2) (Rev 2) (Alt 2) (Beta 4).chd";
		const char *r = fitText(kFont, shortName, s, b.titleRoom, buf, sizeof buf);
		if (strcmp(r, shortName))
			fail("Facts fitText", "a name that fits was changed");
		r = fitText(kFont, longName, s, b.titleRoom, buf, sizeof buf);
		const int n = (int)strlen(r);
		if (!strcmp(r, longName) || n < 4 || strcmp(r + n - 3, "...") || fontWidth(r, s, NULL) > b.titleRoom)
			fail("Facts fitText", "a long name is not cut with ... inside the room");
	}
	printf("standard explorer facts strip: panel, title, 2 columns, path clears the strip, fitText ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

static void testStdStrip(void)
{
	const int before = failures;
	const int n = (int)COUNT(kStripNames);

	/* the letter of a name */
	if (stripLetter("Alpha") != 1 || stripLetter("alpha") != 1 || stripLetter("zelda") != 26 ||
	    stripLetter("Z") != 26)
		fail("Strip letter", "A, a or Z is on the wrong cell");
	if (stripLetter("1942") != 0 || stripLetter("[demo]") != 0 || stripLetter("\xC3\x89t\xC3\xA9") != 0 ||
	    stripLetter(".hidden") != 0)
		fail("Strip letter", "a digit, a bracket, a UTF-8 name or a dot file is not '#'");
	if (stripLetter("..") != -1 || stripLetter(".") != -1 || stripLetter("") != -1 || stripLetter(NULL) != -1)
		fail("Strip letter", "the up row, an empty name or NULL has a cell");
	if (stripChar(0) != '#' || stripChar(1) != 'A' || stripChar(26) != 'Z' || stripChar(3) != 'C')
		fail("Strip letter", "stripChar is wrong");

	/* dimmed letters: the mask holds exactly the cells with a name */
	const unsigned mask = stripMask(n, stripNameAt, NULL);
	const char *have = "#ACFSTZ";
	for (int k = 0; k < STRIP_CELLS; k++) {
		const bool want = strchr(have, stripChar(k)) != NULL;
		if (((mask >> k) & 1u) != (want ? 1u : 0u)) {
			char msg[64];
			snprintf(msg, sizeof msg, "cell %c is %s", stripChar(k), want ? "dimmed" : "lit");
			fail("Strip mask", msg);
		}
	}
	if (stripMask(0, stripNameAt, NULL) != 0)
		fail("Strip mask", "an empty list has letters");

	/* jump targets: the first item of the letter, in list order */
	if (stripFirst(n, stripNameAt, NULL, 1) != 3)
		fail("Strip jump", "A is not the first A row (Alpha)");
	if (stripFirst(n, stripNameAt, NULL, 3) != 5)
		fail("Strip jump", "C is not Castlevania");
	if (stripFirst(n, stripNameAt, NULL, 6) != 1)
		fail("Strip jump", "F is not the first F row (the Formats folder)");
	if (stripFirst(n, stripNameAt, NULL, 0) != 8)
		fail("Strip jump", "# is not 1942 (the up row is no '#')");
	if (stripFirst(n, stripNameAt, NULL, 2) != -1 || stripFirst(n, stripNameAt, NULL, 25) != -1)
		fail("Strip jump", "a letter with no entry has a target");
	if (stripFirst(n, stripNameAt, NULL, 26) != 7 || stripFirst(n, stripNameAt, NULL, 19) != 13)
		fail("Strip jump", "Z or S is not at its row");

	/* links: the C stick walks the letters that have entries, both ways, and stops at the ends */
	int chain[STRIP_CELLS], len = 0, cur = -1;
	while ((cur = stripStep(mask, cur, 1)) >= 0 && len < STRIP_CELLS)
		chain[len++] = cur;
	if (len != 7 || chain[0] != 0 || chain[1] != 1 || chain[2] != 3 || chain[3] != 6 || chain[4] != 19 ||
	    chain[5] != 20 || chain[6] != 26)
		fail("Strip links", "the forward chain is not #, A, C, F, S, T, Z");
	cur = -1; len = 0;
	while ((cur = stripStep(mask, cur, -1)) >= 0 && len < STRIP_CELLS)
		chain[len++] = cur;
	if (len != 7 || chain[0] != 26 || chain[6] != 0)
		fail("Strip links", "the backward chain is not Z .. #");
	if (stripStep(mask, 26, 1) != -1 || stripStep(mask, 0, -1) != -1)
		fail("Strip links", "a step past an end did not stop");
	if (stripStep(mask, 3, 1) != 6 || stripStep(mask, 6, -1) != 3 || stripStep(mask, 2, 1) != 3 ||
	    stripStep(mask, 4, -1) != 3)
		fail("Strip links", "a step from a letter or from a dimmed one skips wrongly");
	if (stripStep(0, -1, 1) != -1 || stripStep(0, 5, -1) != -1)
		fail("Strip links", "an empty strip has a step");
	if (stripStep(mask, 3, 0) != -1)
		fail("Strip links", "direction 0 moves");

	/* geometry: 27 cells inside x 24..616, y 94-118, in order, never touching */
	int px = (int)STD_STRIP_X0 - 1;
	for (int k = 0; k < STRIP_CELLS; k++) {
		int x, w;
		stripCell(k, &x, &w);
		if (w < 16 || x <= px || x + w > (int)STD_STRIP_X1 || x < (int)STD_STRIP_X0) {
			char msg[64];
			snprintf(msg, sizeof msg, "cell %c at x %d w %d", stripChar(k), x, w);
			fail("Strip fit", msg);
		}
		if (k > 0 && x - px < STD_STRIP_GAP)
			fail("Strip fit", "two cells touch");
		px = x + w;
		/* the centre and the first pixel hit their own cell; the gap belongs to the cell before it */
		if (stripHit(x, (int)STD_STRIP_Y) != k || stripHit(x + w / 2, (int)STD_STRIP_Y + 10) != k ||
		    stripHit(x + w + STD_STRIP_GAP - 1, (int)STD_STRIP_Y) != k)
			fail("Strip fit", "a point on a cell hits another cell");
	}
	if ((int)STD_STRIP_Y != 94 || (int)(STD_STRIP_Y + STD_STRIP_H) != 118)
		fail("Strip fit", "the strip is not at y 94-118");
	{
		int x, w;
		stripCell(STRIP_CELLS - 1, &x, &w);
		if (x + w + STD_STRIP_GAP != (int)STD_STRIP_X1)
			fail("Strip fit", "the cells do not fill x 24..616");
	}
	/* outside the strip: above, below, left of it, right of it */
	if (stripHit(100, 93) != -1 || stripHit(100, 118) != -1 || stripHit(23, 100) != -1 || stripHit(616, 100) != -1)
		fail("Strip hit", "a point outside the strip hits a cell");
	/* it is below the tab strip (y 18-70) and above the first row (y 126) */
	if ((int)STD_STRIP_Y < (int)(STD_TAB_Y + STD_TAB_H) || (int)(STD_STRIP_Y + STD_STRIP_H) > (int)STD_LIST_Y0)
		fail("Strip fit", "the strip meets the tabs or the first row");

	/* the scroll thumb: by page, and a drag maps back to the page it was drawn for */
	{
		const int y0 = 152, y1 = 376;
		ListState s = { 20, 7, 0, 0 };		/* 3 pages */
		Thumb t = scrollThumb(s, y0, y1);
		if (!t.live || t.y != y0 || t.h != (y1 - y0) * 7 / 20)
			fail("Scroll thumb", "page 1 of 3: not live, not at the top, or the wrong height");
		for (int page = 0; page < 3; page++) {
			s.top = page * 7;
			t = scrollThumb(s, y0, y1);
			if (t.y < y0 || t.y + t.h > y1)
				fail("Scroll thumb", "the thumb leaves the track");
			if (scrollDragPage(s, y0, y1, t.y) != page)
				fail("Scroll thumb", "a drag to where the thumb is changes the page");
		}
		s.top = 14;
		t = scrollThumb(s, y0, y1);
		if (t.y + t.h != y1)
			fail("Scroll thumb", "the last page's thumb is not at the bottom");
		if (scrollDragPage(s, y0, y1, -50) != 0 || scrollDragPage(s, y0, y1, 9999) != 2)
			fail("Scroll thumb", "a drag past the track ends does not clamp to the first and last page");
		s.top = 0;
		const int mid = y0 + ((y1 - y0) - t.h) / 2;
		if (scrollDragPage(s, y0, y1, mid) != 1)
			fail("Scroll thumb", "a drag to the middle is not the middle page");
		ListState all = { 5, 7, 0, 0 };		/* fits: no thumb to drag */
		t = scrollThumb(all, y0, y1);
		if (t.live || t.y != y0 || t.h != y1 - y0 || scrollDragPage(all, y0, y1, 200) != 0)
			fail("Scroll thumb", "a list that fits has a live thumb");
		ListState none = { 0, 7, 0, -1 };
		if (scrollThumb(none, y0, y1).live || scrollDragPage(none, y0, y1, 200) != 0)
			fail("Scroll thumb", "an empty list has a live thumb");
		ListState big = { 450, 7, 63 * 7, 0 };	/* 450 stubs: the 16 px floor */
		t = scrollThumb(big, y0, y1);
		if (t.h != 16 || t.y < y0 || t.y + t.h > y1)
			fail("Scroll thumb", "a long list's thumb is not 16 px inside the track");
		if (scrollDragPage(big, y0, y1, t.y) != 63)
			fail("Scroll thumb", "a long list: a drag to the thumb is another page");
	}

	printf("standard explorer letter strip: letters, dim, jump targets, links, fit in 24..616, scroll thumb ... %s\n",
	       failures == before ? "ok" : "FAILED");
}

int main(int argc, char **argv)
{
	const char *font = argc > 1 ? argv[1] : "fonts/En.dat";

	if (!loadFont(font)) {
		printf("cannot read the font file '%s' (pass its path as the first argument)\n", font);
		return 2;
	}

	testCycle();
	testTabStep();
	testStdColumns();
	testEdgeWrap();
	testConfigureButtonStates();
	testLinkOverrides();
	testCheckCatches();
	testInactiveNeverFocus();
	testListPaging();
	testStandard();
	testStdOverflowCaught();
	testStdExplorer();
	testStdStrip();
	testStdFacts();
	testStdDim();
	testCountText();
	testCutTextLeft();
	testGuards();

	printf("\n%s: %d check%s failed\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures > 255 ? 255 : failures;
}
