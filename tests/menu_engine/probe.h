/* probe.h - helpers the menu host tests share: a control's identity and finding it in a layout. */

#include <math.h>
#include <stdio.h>

struct Key {
	int role, row, slot;
};
static const Key NOKEY = { -1, 0, 0 };

static bool sameKey(Key a, Key b)
{
	return a.role == b.role && a.row == b.row && a.slot == b.slot;
}

static Key keyOf(const Control &c)
{
	Key k = { c.role, c.row, c.slot };
	return k;
}

static int findCtrl(const Layout &lay, Key k)
{
	for (int i = 0; i < lay.n; i++)
		if (sameKey(keyOf(lay.c[i]), k))
			return i;
	return -1;
}

static const char *keyName(Key k, char *buf, size_t len)
{
	if (k.role < 0) { snprintf(buf, len, "none"); return buf; }
	snprintf(buf, len, "role%d row%d slot%d", k.role, k.row, k.slot);
	return buf;
}

static inline bool nearly(float a, float b)
{
	return fabs(a - b) < 1e-3f;
}

/* A choice's button width when its row fits its words: Button::fitToLabel, 2 * ceil(...). */
static float fitWidth(const char *s)
{
	float t = (float)fontWidth(s, 1.0f, NULL);
	return 2.0f * ceilf((t + 16.0f + 16.0f) / 2.0f);
}

/* The two-column grid: a column's label width is its widest label and its button width its widest
 * fitted button, so every line shares the edges. A cell is a label right-aligned 10 px before its
 * button; the columns are 24 apart and the grid is centred once. Nothing in the 15 px margins or
 * past 636. cells[line] = { left row, right row } (-1 for none), ys[line] the line's top. */
static void checkGrid(const Layout &lay, const Row *rows, const int (*cells)[2], const float *ys, int nLines)
{
	float lw[2] = { 0, 0 }, bw[2] = { 0, 0 }, x0, colX[2];
	bool two = false;

	for (int l = 0; l < nLines; l++)
		for (int c = 0; c < 2; c++) {
			if (cells[l][c] < 0) continue;
			const Row &r = rows[cells[l][c]];
			two = two || c == 1;
			if ((float)fontWidth(r.label, 1.0f, NULL) > lw[c]) lw[c] = (float)fontWidth(r.label, 1.0f, NULL);
			for (int k = 0; k < r.count; k++)
				if (fitWidth(r.names[k]) > bw[c]) bw[c] = fitWidth(r.names[k]);
		}
	x0 = (640.0f - (lw[0] + 10.0f + bw[0] + (two ? 24.0f + lw[1] + 10.0f + bw[1] : 0.0f))) / 2;
	colX[0] = x0 + lw[0] + 10.0f;
	colX[1] = x0 + lw[0] + 10.0f + bw[0] + 24.0f + lw[1] + 10.0f;

	for (int l = 0; l < nLines; l++)
		for (int c = 0; c < 2; c++) {
			const int i = cells[l][c];
			if (i < 0) continue;
			const Key kb = { ROLE_BUTTON, i, 0 }, kl = { ROLE_LABEL, i, 0 };
			const int b = findCtrl(lay, kb), t = findCtrl(lay, kl);
			if (b < 0 || t < 0) { fail("2 columns", "a row lost its label or button"); continue; }
			if (lay.c[b].rect.x != colX[c] || lay.c[b].rect.y != ys[l] || lay.c[b].rect.w != bw[c] ||
			    lay.c[b].rect.h != 56.0f)
				fail("2 columns", "a button is not on the grid");
			if (fabsf(lay.c[t].rect.x + lay.c[t].rect.w - (colX[c] - 10.0f)) > 1.0f || lay.c[t].ay != ys[l] + 20.0f)
				fail("2 columns", "a label does not end 10 px before its button");
			if (lay.c[t].rect.x < 15.0f || colX[c] + bw[c] > 636.0f)
				fail("2 columns", "a cell reaches the margin");
		}
}

/* A focus link can be walked back: Down then Up (Right then Left) returns to the row it started on, though
 * not always the same button (a row with another number or width of buttons picks its nearest). Tabs are
 * exempt: only the lit tab links to the first and last rows; so is a link set by hand (Page::overrides). Returns the links that break the rule. */
static inline int checkSymmetry(const char *name, const Page &pg, const Layout &lay)
{
	static const int pairs[2][2] = { { DIR_DOWN, DIR_UP }, { DIR_RIGHT, DIR_LEFT } };
	int bad = 0;
	for (int i = 0; i < lay.n; i++) {
		const Control &a = lay.c[i];
		if (!a.focusable) continue;
		for (int k = 0; k < 2; k++) {
			const int b = a.link[pairs[k][0]];
			if (b < 0 || a.role == ROLE_TAB || lay.c[b].role == ROLE_TAB) continue;
			bool byHand = false;
			for (int o = 0; o < pg.nOverrides; o++)
				byHand = byHand || (pg.overrides[o].row == a.row && pg.overrides[o].slot == a.slot && pg.overrides[o].dir == pairs[k][0]);
			if (byHand) continue;
			const int back = lay.c[b].link[pairs[k][1]];
			if (back >= 0 && lay.c[back].row == a.row) continue;
			char b1[64], b2[64];
			bad++;
			printf("FAIL %s: link %s -> %s cannot be walked back\n", name, keyName(keyOf(a), b1, sizeof b1),
			       keyName(keyOf(lay.c[b]), b2, sizeof b2));
		}
	}
	return bad;
}
