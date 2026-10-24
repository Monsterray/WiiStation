/* The achievement toast (Docs/design/PS1_STYLES.md): the Save Block card that slides in at
 * the top right when an achievement unlocks.
 *
 * Its material is the tier. A single unlock is Blue, Bronze, Silver, Gold or Crystal by
 * rarity (a Local set has no rarity, so its points decide). The unlock that beats the game
 * flashes into Finish Line; the unlock that completes the set bursts into Platinum.
 *
 * It is drawn with raw GX in the present's overlay (showFpsAndDebugInfo, both GPU plugins),
 * on the picture sent to the TV and never in the game's frame. Times are emulated vblanks,
 * as for the ANALOG note: a toast waits while the menu is open. The coordinates are the
 * 640 x 480 picture that IPLFont also uses. */
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../psxcommon.h"
#include "wiiSXconfig.h"
#include "libgui/IPLFontC.h"
#include "ach_toast.h"
#include "ach_icons.h"

extern u32 frame_counter;          /* psxcounters.c: +1 per vblank */
extern char menuActive;
extern GXRModeObj *vmode, *g_tv_mode;
int IplFont_getStringWidth(char *string, float scale);

/* ---- the queue: a second unlock waits until the first has gone ---- */

enum { KIND_UNLOCK, KIND_BEATEN, KIND_MASTERED };
enum { T_BLUE, T_BRONZE, T_SILVER, T_GOLD, T_CRYSTAL, T_FINISH, T_PLAT };
enum { ICON_TROPHY, ICON_KEY, ICON_HOURGLASS, ICON_FLAG, ICON_CROWN };

typedef struct {
	char title[80], desc[96], pts[16], left[24], right[24];   /* fitted to the card at push time */
	unsigned char tier, icon, chip;                           /* chip: an ACH_TYPE_*, or STANDARD for none */
	float fill;                                               /* the set's progress, 0..1 */
	unsigned blocks;                                          /* 0: a plain bar */
} Face;

typedef struct {
	Face f1, f2;          /* f2: the Finish Line or Platinum card, for BEATEN / MASTERED */
	unsigned char kind;
	int started, fitted;
	u32 last;             /* the vblank it was last drawn at */
	u32 shown;            /* vblanks it has been on screen */
} Toast;

#define QUEUE 4
static Toast queue[QUEUE];
static int q_head, q_len;

static unsigned tier_of(const achievement_notice_t *n)
{
	if (n->rarity_x100 >= 0) {
		if (n->rarity_x100 < 100) return T_CRYSTAL;
		if (n->rarity_x100 < 200) return T_GOLD;
		if (n->rarity_x100 < 500) return T_SILVER;
		if (n->rarity_x100 < 1000) return T_BRONZE;
		return T_BLUE;
	}
	if (n->points >= 100) return T_CRYSTAL;
	if (n->points >= 50) return T_GOLD;
	if (n->points >= 25) return T_SILVER;
	if (n->points >= 10) return T_BRONZE;
	return T_BLUE;
}

static const unsigned char ICON_OF_TYPE[4] = { ICON_TROPHY, ICON_HOURGLASS, ICON_KEY, ICON_FLAG };

void ach_toast_push(const achievement_notice_t *n)
{
	Toast *t;
	Face *f;
	if (q_len == QUEUE) return;   /* more unlocks at once than fit: the journal still has them */
	t = &queue[(q_head + q_len++) % QUEUE];
	memset(t, 0, sizeof *t);
	t->kind = n->mastered ? KIND_MASTERED : n->beaten ? KIND_BEATEN : KIND_UNLOCK;

	f = &t->f1;
	snprintf(f->title, sizeof f->title, "%s", n->title ? n->title : "");
	snprintf(f->desc, sizeof f->desc, "%s", n->desc ? n->desc : "");
	if (n->points) snprintf(f->pts, sizeof f->pts, "+%u", n->points);
	snprintf(f->left, sizeof f->left, "BLOCK %u/%u", n->earned, n->count);
	if (n->rarity_x100 >= 0)
		snprintf(f->right, sizeof f->right, "%d.%d%% have it", n->rarity_x100 / 100, n->rarity_x100 % 100 / 10);
	f->tier = tier_of(n);
	f->icon = ICON_OF_TYPE[n->type & 3];
	f->chip = n->type & 3;
	f->fill = n->count ? (float)n->earned / n->count : 0;
	f->blocks = n->count <= 60 ? n->count : 0;

	if (t->kind != KIND_UNLOCK) {
		const char *game = n->game && n->game[0] ? n->game : "Game";
		t->f2 = *f;
		f = &t->f2;
		f->chip = ACH_TYPE_STANDARD;
		f->right[0] = 0;
		if (t->kind == KIND_BEATEN) {
			f->tier = T_FINISH;
			f->icon = ICON_FLAG;
			snprintf(f->title, sizeof f->title, "%s beaten", game);
			snprintf(f->desc, sizeof f->desc, "Every story achievement and the win");
		} else {
			f->tier = T_PLAT;
			f->icon = ICON_CROWN;
			snprintf(f->title, sizeof f->title, "%s mastered", game);
			snprintf(f->desc, sizeof f->desc, "Every achievement earned");
			f->pts[0] = 0;
			if (n->total_points) snprintf(f->pts, sizeof f->pts, "%u pts", n->total_points);
		}
	}
}

void ach_toast_clear(void) { q_head = q_len = 0; }

int ach_toast_wanted(void) { return q_len > 0 && !menuActive; }

/* ---- tiers: the card's colours, as in the mockup (0xRRGGBBAA) ---- */

typedef struct {
	u32 band[5], border, ink, ink2, shadow, ibox, iboxb, mtx, trk, fill, gap, glow, ray, pts;
} Tier;

static const Tier TIERS[] = {
	[T_BLUE]    = { { 0x2b50a8ff, 0x27499eff, 0x234394ff, 0x1f3c89ff, 0x1a347cff }, 0xa7c2ffff, 0xffffffff, 0xffffffdb,
	                0x0a1638ff, 0x0a1638ff, 0x6f8fd8ff, 0xc9d1e2ff, 0xffffff29, 0xe8b23aff, 0x152b66ff, 0x00000000, 0xfff8d6f2, 0xe8b23aff },
	[T_BRONZE]  = { { 0xc98a55ff, 0xb97b49ff, 0xa86c3eff, 0x965d34ff, 0x834f2aff }, 0xf3c9a2ff, 0xffffffff, 0xffe6d1ff,
	                0x3a1d08ff, 0x3a1d08ff, 0xe7a874ff, 0xffe0c4ff, 0x3a1d0859, 0xffd9b0ff, 0x6e3f1cff, 0xf0a06499, 0xfff8d6f2, 0xffffffff },
	[T_SILVER]  = { { 0xdfe4eaff, 0xc6ccd5ff, 0xaeb6c2ff, 0xcdd3dbff, 0x9ea6b3ff }, 0xffffffff, 0x141a24ff, 0x2c3544ff,
	                0xffffffff, 0x2a3342ff, 0xf4f7fbff, 0x2c3544ff, 0x141a2433, 0xffffffff, 0x8a93a3ff, 0xe6f0ffbf, 0xfffffff2, 0x141a24ff },
	[T_GOLD]    = { { 0xffe7a0ff, 0xf6d27aff, 0xeec064ff, 0xe3ad4cff, 0xd39936ff }, 0xfff3c9ff, 0x2a1a00ff, 0x4a3205ff,
	                0xfff1b8ff, 0x3a2500ff, 0xfff1b8ff, 0x4a3205ff, 0x5a3a0040, 0xfff6d0ff, 0xc98a1cff, 0xffd66bbf, 0xfff0bef2, 0x2a1a00ff },
	[T_CRYSTAL] = { { 0xbef2ff9e, 0xa5e6fd94, 0x8ed7f98c, 0x7cc8f485, 0x6cb9ee80 }, 0xe8fbffff, 0x062238ff, 0x0f3a57ff,
	                0xffffffd9, 0x0b3550ff, 0xe8fbffff, 0x0f3a57ff, 0x06223830, 0xffffffff, 0x5fa9d6ff, 0x96e6ffd9, 0xd2f8fff2, 0x062238ff },
	[T_FINISH]  = { { 0x3c414dff, 0x30353fff, 0x262a33ff, 0x1e2128ff, 0x16181eff }, 0xe8b23aff, 0xffffffff, 0xc9cfdbff,
	                0x000000ff, 0x0b0c10ff, 0xe8b23aff, 0xc9cfdbff, 0xffffff24, 0xffffffff, 0x16181eff, 0xffd66b8c, 0xfff0bef2, 0xe8b23aff },
	[T_PLAT]    = { { 0xffffffff, 0xe3e7eeff, 0xbfc6d1ff, 0xeef1f5ff, 0xaab2bfff }, 0xffffffff, 0x151a26ff, 0x323a4cff,
	                0xffffffff, 0x232836ff, 0xf2f4f8ff, 0x323a4cff, 0x151a2633, 0xffffffff, 0x8d95a5ff, 0xe1e8f5e6, 0xfafbfff2, 0x151a26ff },
};

/* ---- card geometry, in picture pixels ---- */

#define CARD_W 330
#define CARD_H 98
#define CARD_X 270            /* 40 px from the right edge */
#define CARD_Y 34             /* inside the title-safe area */
#define TEXT_X 78             /* after the 56 px icon box */
#define ICON_CX 38            /* the icon's centre: the rays and the light core start there */
#define ICON_CY 40
#define TITLE_SCALE 0.8f
#define DESC_SCALE 0.56f
#define FOOT_SCALE 0.5f
#define CHIP_SCALE 0.42f
#define PTS_SCALE 0.6f

/* ---- raw GX: flat shapes in vertex format 7 (as the light gun reticle) ---- */

static float ortho_l, ortho_r;

static void shapes_begin(void)
{
	Mtx44 proj;
	Mtx mv;
	guOrtho(proj, 0, 479, ortho_l, ortho_r, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	guMtxIdentity(mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_InvVtxCache();
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevDirect(GX_TEVSTAGE0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	/* RGB as written; gc_vout_render / gx_vout_render put the plugins' BGR table back */
	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
	GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetCullMode(GX_CULL_NONE);
}

static inline void vtx(float x, float y, u32 c)
{
	GX_Position2f32(x, y);
	GX_Color4u8(c >> 24, c >> 16, c >> 8, c);
}

static u32 alpha(u32 c, float a)
{
	if (a <= 0) return c & ~0xffu;
	if (a > 1) a = 1;
	return (c & ~0xffu) | (u32)((c & 0xff) * a);
}

static void rect(float x0, float y0, float x1, float y1, u32 c)
{
	if (!(c & 0xff)) return;
	GX_Begin(GX_QUADS, GX_VTXFMT7, 4);
	vtx(x0, y0, c); vtx(x1, y0, c); vtx(x1, y1, c); vtx(x0, y1, c);
	GX_End();
}

/* a band leaning right by `lean` per pixel of height, solid colour c0 on its left edge
 * fading to c1 on its right */
static void slant(float x0, float x1, float y0, float y1, float lean, u32 c0, u32 c1)
{
	float d = (y1 - y0) * lean;
	GX_Begin(GX_QUADS, GX_VTXFMT7, 4);
	vtx(x0 + d, y0, c0); vtx(x1 + d, y0, c1); vtx(x1, y1, c1); vtx(x0, y1, c0);
	GX_End();
}

/* the GX scissor around a rectangle of the picture: the light core and the sheen stay on the card */
static void clip_to(float x0, float y0, float x1, float y1)
{
	GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
	float sx = m->fbWidth / (ortho_r - ortho_l), sy = m->efbHeight / 479.0f;
	int l = (int)((x0 - ortho_l) * sx), t = (int)(y0 * sy);
	int r = (int)((x1 - ortho_l) * sx + 0.5f), b = (int)(y1 * sy + 0.5f);
	if (l < 0) l = 0;
	if (t < 0) t = 0;
	GX_SetScissor(l, t, r > l ? r - l : 1, b > t ? b - t : 1);
}

static void clip_off(void)
{
	GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
	GX_SetScissor(0, 0, m->fbWidth, m->efbHeight);
}

/* ---- text: the menu font, with a hard shadow, bold by drawing twice ---- */

static void text(int x, int y, const char *s, float scale, u32 c, u32 shadow, int bold)
{
	char buf[128];
	if (!s[0]) return;
	snprintf(buf, sizeof buf, "%s", s);
	if (shadow & 0xff) {
		IplFont_drawInit((GXColor){ shadow >> 24, shadow >> 16, shadow >> 8, shadow });
		IplFont_drawString(x + 1, y + 1, buf, scale, false);
		if (bold) IplFont_drawString(x + 2, y + 1, buf, scale, false);
	}
	IplFont_drawInit((GXColor){ c >> 24, c >> 16, c >> 8, c });
	IplFont_drawString(x, y, buf, scale, false);
	if (bold) IplFont_drawString(x + 1, y, buf, scale, false);
}

static int text_w(const char *s, float scale)
{
	char buf[128];
	if (!s[0]) return 0;
	snprintf(buf, sizeof buf, "%s", s);
	return IplFont_getStringWidth(buf, scale);
}

/* cut s to `width` pixels, with "..." when it is cut */
static void fit(char *s, size_t size, float scale, int width)
{
	char cut[128];
	size_t len = strlen(s);
	if (text_w(s, scale) <= width) return;
	snprintf(cut, sizeof cut, "%s", s);
	while (len > 0) {
		cut[--len] = 0;
		while (len > 0 && cut[len - 1] == ' ') cut[--len] = 0;
		snprintf(s, size, "%s...", cut);
		if (text_w(s, scale) <= width) return;
	}
}

static const char *const CHIP_TEXT[4] = { "", "MISSABLE", "PROGRESS", "WIN" };
static const u32 CHIP_BG[4] = { 0, 0xd9363eff, 0x1f8f80ff, 0xffffffff };
static const u32 CHIP_INK[4] = { 0, 0xffffffff, 0xffffffff, 0x111111ff };

static int chip_w(const Face *f) { return f->chip ? text_w(CHIP_TEXT[f->chip], CHIP_SCALE) + 4 : 0; }

/* the text that does not fit is cut once, when the toast first shows (the font is loaded by then) */
static void fit_face(Face *f)
{
	int right = text_w(f->pts, PTS_SCALE) + (f->chip ? chip_w(f) + 6 : 0);
	fit(f->title, sizeof f->title, TITLE_SCALE, CARD_W - TEXT_X - 12 - right - 6);
	fit(f->desc, sizeof f->desc, DESC_SCALE, CARD_W - TEXT_X - 12);
}

/* ---- the card ---- */

static void icon_pixels(const char map[16][17], float x, float y, float px)
{
	int r, c, k;
	for (r = 0; r < 16; r++)
		for (c = 0; c < 16; c++) {
			char ch = map[r][c];
			if (ch == '.') continue;
			for (k = 0; k < (int)(sizeof ACH_ICON_PALETTE / sizeof ACH_ICON_PALETTE[0]); k++)
				if (ACH_ICON_PALETTE[k].c == ch) {
					const u32 rgb = (ACH_ICON_PALETTE[k].r << 24) | (ACH_ICON_PALETTE[k].g << 16) | (ACH_ICON_PALETTE[k].b << 8) | 0xff;
					rect(x + c * px, y + r * px, x + (c + 1) * px, y + (r + 1) * px, rgb);
					break;
				}
		}
}

/* the icon's frame at time t: the trophy turns (front, turned, side, turned), the sand runs,
 * the flag waves; the key and the crown sparkle */
static void draw_icon(int icon, float x, float y, float t)
{
	static const char (*const TROPHY[4])[17] = { ACH_ICON_TROPHY_A, ACH_ICON_TROPHY_B, ACH_ICON_TROPHY_C, ACH_ICON_TROPHY_B };
	static const char (*const SAND[3])[17] = { ACH_ICON_HOURGLASS_A, ACH_ICON_HOURGLASS_B, ACH_ICON_HOURGLASS_C };
	const float px = 3;
	switch (icon) {
	case ICON_TROPHY: icon_pixels(TROPHY[(int)(t / 0.3f) & 3], x, y, px); break;
	case ICON_HOURGLASS: icon_pixels(SAND[(int)(t / 0.6f) % 3], x, y, px); break;
	case ICON_FLAG: icon_pixels((int)(t / 0.35f) & 1 ? ACH_ICON_FLAG_B : ACH_ICON_FLAG_A, x, y, px); break;
	case ICON_KEY: icon_pixels(ACH_ICON_KEY, x, y, px); break;
	default: icon_pixels(ACH_ICON_CROWN, x, y, px); break;
	}
	if (icon == ICON_KEY || icon == ICON_CROWN) {
		static const int spark[3][2] = { { 15, 0 }, { 0, 2 }, { 10, 1 } };
		int k = (int)(t / 0.3f) % 3;
		rect(x + spark[k][0] * px, y + spark[k][1] * px, x + (spark[k][0] + 1) * px, y + (spark[k][1] + 1) * px, 0xffffffff);
	}
}

/* the material: five hard bands (the PS1 has no smooth gradient), then each tier's finish */
static void draw_material(int tier, float x, float y)
{
	const Tier *k = &TIERS[tier];
	int i;
	float yy;
	for (i = 0; i < 5; i++)
		rect(x, y + CARD_H * i / 5.0f, x + CARD_W, y + CARD_H * (i + 1) / 5.0f, k->band[i]);
	switch (tier) {
	case T_SILVER:
	case T_PLAT:   /* brushed metal: a light line every 3 pixels */
		for (yy = y; yy < y + CARD_H; yy += 3) {
			rect(x, yy, x + CARD_W, yy + 1, tier == T_PLAT ? 0xffffff38 : 0xffffff29);
			rect(x, yy + 1, x + CARD_W, yy + 2, 0x2830400f);
		}
		if (tier == T_PLAT) {   /* two specular bands */
			slant(x + CARD_W * 0.24f, x + CARD_W * 0.30f, y, y + CARD_H, 0.47f, 0xffffff00, 0xffffffbf);
			slant(x + CARD_W * 0.30f, x + CARD_W * 0.36f, y, y + CARD_H, 0.47f, 0xffffffbf, 0xffffff00);
			slant(x + CARD_W * 0.57f, x + CARD_W * 0.60f, y, y + CARD_H, 0.47f, 0xffffff00, 0xffffff66);
			slant(x + CARD_W * 0.60f, x + CARD_W * 0.63f, y, y + CARD_H, 0.47f, 0xffffff66, 0xffffff00);
		}
		break;
	case T_CRYSTAL:   /* facets catching the light, and a bright inner edge */
		slant(x - CARD_H * 0.58f, x + CARD_W * 0.10f - CARD_H * 0.58f, y, y + CARD_H, 0.58f, 0xffffff8c, 0xffffff8c);
		slant(x + CARD_W * 0.28f - CARD_H * 0.58f, x + CARD_W * 0.36f - CARD_H * 0.58f, y, y + CARD_H, 0.58f, 0xffffff33, 0xffffff33);
		slant(x + CARD_W * 0.60f - CARD_H * 0.58f, x + CARD_W * 0.64f - CARD_H * 0.58f, y, y + CARD_H, 0.58f, 0xffffff57, 0xffffff57);
		slant(x + CARD_W * 0.82f - CARD_H * 0.58f, x + CARD_W * 0.90f - CARD_H * 0.58f, y, y + CARD_H, 0.58f, 0xffffff29, 0xffffff29);
		break;
	case T_FINISH: {  /* the chequered strip on the top edge: two rows of 4 px squares */
		float sx;
		rect(x, y, x + CARD_W, y + 8, 0x15171cff);
		for (sx = 0; sx < CARD_W; sx += 4) {
			int col = (int)(sx / 4);
			rect(x + sx, y + (col & 1 ? 4 : 0), x + sx + 4 > x + CARD_W ? x + CARD_W : x + sx + 4, y + (col & 1 ? 8 : 4), 0xf2f2f2ff);
		}
		break;
	}
	default:
		break;
	}
}

/* the set meter: one block per achievement (a plain bar past 60), the newest block flashing */
static void draw_meter(const Face *f, float x0, float x1, float y, int flash)
{
	const Tier *k = &TIERS[f->tier];
	float w = x1 - x0;
	unsigned b;
	rect(x0, y, x1, y + 6, k->trk);
	rect(x0, y, x0 + w * f->fill, y + 6, k->fill);
	if (f->blocks > 1)
		for (b = 1; b < f->blocks; b++) {
			float gx = x0 + w * b / f->blocks;
			rect(gx - 1, y, gx, y + 6, k->gap);
		}
	if (flash && f->blocks && f->fill > 0) {
		float bw = w / f->blocks;
		float bx = x0 + w * f->fill - bw;
		rect(bx, y - 2, bx + bw, y + 8, 0xffffffff);
	}
}

static void draw_face(const Face *f, float x, float y, float t, int block_flash)
{
	const Tier *k = &TIERS[f->tier];
	int wl = text_w(f->left, FOOT_SCALE), wr = text_w(f->right, FOOT_SCALE);
	int wp = text_w(f->pts, PTS_SCALE), wc = chip_w(f);
	float px = x + CARD_W - 12 - wp, cx = px - (wp ? 6 : 0) - wc;

	draw_material(f->tier, x, y);
	/* the icon box */
	rect(x + 10, y + 12, x + 66, y + 68, k->iboxb);
	rect(x + 12, y + 14, x + 64, y + 66, k->ibox);
	draw_icon(f->icon, x + 14, y + 16, t);
	draw_meter(f, x + 10 + wl + 8, x + CARD_W - 10 - (wr ? wr + 8 : 0), y + 79, block_flash);
	if (f->chip) {
		rect(cx, y + 13, cx + wc, y + 27, CHIP_INK[f->chip]);   /* the WIN chip's dark edge; covered by the others */
		rect(cx + 1, y + 14, cx + wc - 1, y + 26, CHIP_BG[f->chip]);
	}
	/* the text (it leaves the font's GX state: shapes_begin again after it) */
	text(x + TEXT_X, y + 10, f->title, TITLE_SCALE, k->ink, k->shadow, 1);
	text(x + TEXT_X, y + 36, f->desc, DESC_SCALE, k->ink2, 0, 0);
	text(x + 10, y + 75, f->left, FOOT_SCALE, k->mtx, 0, 0);
	text(x + CARD_W - 10 - wr, y + 75, f->right, FOOT_SCALE, k->mtx, 0, 0);
	text(px, y + 13, f->pts, PTS_SCALE, k->pts, f->tier == T_BLUE || f->tier == T_FINISH ? 0 : k->shadow, 1);
	if (f->chip) text(cx + 3, y + 16, CHIP_TEXT[f->chip], CHIP_SCALE, CHIP_INK[f->chip], 0, 0);
	shapes_begin();
	/* the frame: the tier's border, and a dark line inside it */
	rect(x, y, x + CARD_W, y + 2, k->border);
	rect(x, y + CARD_H - 2, x + CARD_W, y + CARD_H, k->border);
	rect(x, y + 2, x + 2, y + CARD_H - 2, k->border);
	rect(x + CARD_W - 2, y + 2, x + CARD_W, y + CARD_H - 2, k->border);
	rect(x + 2, y + 2, x + CARD_W - 2, y + 3, 0x0000002e);
}

/* ---- the effects ---- */

/* the glow around the card: a few widening rectangles; s = 1 at the peak, 0.45 held */
static void draw_glow(u32 glow, float x, float y, float s)
{
	static const float spread[4] = { 2, 5, 9, 14 }, weight[4] = { 0.5f, 0.3f, 0.18f, 0.08f };
	int i;
	if (!(glow & 0xff) || s <= 0) return;
	for (i = 3; i >= 0; i--)
		rect(x - spread[i], y - spread[i], x + CARD_W + spread[i], y + CARD_H + spread[i], alpha(glow, s * weight[i]));
}

/* sixteen rays from the icon, each fading out along its length */
static void draw_rays(u32 c, float cx, float cy, float scale, float rot, float a)
{
	int i;
	float r = 210 * scale;
	if (a <= 0) return;
	for (i = 0; i < 16; i++) {
		float a0 = (i * 22.5f + rot) * (float)M_PI / 180, a1 = a0 + 6 * (float)M_PI / 180;
		float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		u32 full = alpha(c, a), mid = alpha(c, a * 0.6f), none = alpha(c, 0);
		GX_Begin(GX_TRIANGLES, GX_VTXFMT7, 3);
		vtx(cx, cy, full); vtx(cx + c0 * r * 0.18f, cy + s0 * r * 0.18f, full); vtx(cx + c1 * r * 0.18f, cy + s1 * r * 0.18f, full);
		GX_End();
		GX_Begin(GX_QUADS, GX_VTXFMT7, 8);
		vtx(cx + c0 * r * 0.18f, cy + s0 * r * 0.18f, full); vtx(cx + c0 * r * 0.4f, cy + s0 * r * 0.4f, mid);
		vtx(cx + c1 * r * 0.4f, cy + s1 * r * 0.4f, mid); vtx(cx + c1 * r * 0.18f, cy + s1 * r * 0.18f, full);
		vtx(cx + c0 * r * 0.4f, cy + s0 * r * 0.4f, mid); vtx(cx + c0 * r * 0.7f, cy + s0 * r * 0.7f, none);
		vtx(cx + c1 * r * 0.7f, cy + s1 * r * 0.7f, none); vtx(cx + c1 * r * 0.4f, cy + s1 * r * 0.4f, mid);
		GX_End();
	}
}

/* the white core of the light burst: solid to half its radius, then fading out */
static void draw_core(float cx, float cy, float r, float a)
{
	int i;
	const int n = 24;
	if (a <= 0 || r <= 0) return;
	for (i = 0; i < n; i++) {
		float a0 = i * 2 * (float)M_PI / n, a1 = (i + 1) * 2 * (float)M_PI / n;
		u32 full = alpha(0xffffffff, a), none = 0xffffff00;
		GX_Begin(GX_TRIANGLES, GX_VTXFMT7, 3);
		vtx(cx, cy, full); vtx(cx + cosf(a0) * r * 0.5f, cy + sinf(a0) * r * 0.5f, full); vtx(cx + cosf(a1) * r * 0.5f, cy + sinf(a1) * r * 0.5f, full);
		GX_End();
		GX_Begin(GX_QUADS, GX_VTXFMT7, 4);
		vtx(cx + cosf(a0) * r * 0.5f, cy + sinf(a0) * r * 0.5f, full); vtx(cx + cosf(a0) * r, cy + sinf(a0) * r, none);
		vtx(cx + cosf(a1) * r, cy + sinf(a1) * r, none); vtx(cx + cosf(a1) * r * 0.5f, cy + sinf(a1) * r * 0.5f, full);
		GX_End();
	}
}

/* the sheen: one soft diagonal band crossing the card, p from 0 to 1 */
static void draw_sheen(float x, float y, float p)
{
	float c = x + CARD_W * (0.5f + 2.6f * p - 1.3f), h = CARD_W * 0.1f;
	if (p <= 0 || p >= 1) return;
	clip_to(x, y, x + CARD_W, y + CARD_H);
	slant(c - h - CARD_H * 0.27f, c - CARD_H * 0.27f, y, y + CARD_H, 0.27f, 0xffffff00, 0xffffff66);
	slant(c - CARD_H * 0.27f, c + h - CARD_H * 0.27f, y, y + CARD_H, 0.27f, 0xffffff66, 0xffffff00);
	clip_off();
}

static float ease_out(float p) { p = 1 - p; return 1 - p * p * p; }
static float between(float t, float a, float b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }

/* the rays' growth: out to `peak` by tp, then on to `end` while they fade */
static void rays_at(float t, float t0, float tp, float te, float peak, float end, float *scale, float *rot, float *a)
{
	*a = 0;
	if (t < t0 || t >= te) return;
	if (t < tp) {
		float e = ease_out(between(t, t0, tp));
		*scale = 0.15f + (peak - 0.15f) * e;
		*rot = 25 * e;
		*a = 1;
	} else {
		float p = between(t, tp, te);
		*scale = peak + (end - peak) * p;
		*rot = 25 + 10 * p;
		*a = 1 - p;
	}
}

/* ---- one frame of the toast at the head of the queue ---- */

void ach_toast_draw(void)
{
	GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
	Toast *q;
	const Face *face;
	float rate = Config.PsxType ? 50.0f : 60.0f;
	float t, dur, tin, tout, off = 0, x, y = CARD_Y;
	float flash = 0, glow = 0, sheen = 0, rs = 0, rr = 0, ra = 0, core_r = 0, core_a = 0;
	int set, block, switched = 0;

	if (!ach_toast_wanted()) return;
	q = &queue[q_head];
	/* Its clock runs only while it is drawn: a still screen that presents no frames (OpenGX
	 * presents only what the game draws) holds the toast where it is, so it is never missed.
	 * A game at 30 or 20 frames a second still moves it at full speed (2 or 3 vblanks a draw). */
	if (!q->started) {
		q->started = 1;
		q->last = frame_counter;
	}
	q->shown += frame_counter - q->last > 4 ? 4 : frame_counter - q->last;
	q->last = frame_counter;
	t = q->shown / rate;
	set = q->kind != KIND_UNLOCK;
	dur = set ? 6.0f : 5.0f;
	if (t >= dur) {   /* done: the next one starts on the next present */
		q_head = (q_head + 1) % QUEUE;
		q_len--;
		return;
	}
	if (!q->fitted) {
		fit_face(&q->f1);
		if (set) fit_face(&q->f2);
		q->fitted = 1;
	}

	/* slide in from the right, hold, slide back out */
	tin = set ? 0.36f : 0.35f;
	tout = set ? 0.6f : 0.5f;
	if (t < tin) { float u = 1 - t / tin; off = 400 * u * u * u; }
	else if (t > dur - tout) { float u = (t - (dur - tout)) / tout; off = 400 * u * u * u; }
	x = CARD_X + off;

	if (!set) {
		const int tier = q->f1.tier;
		block = (t >= 0.6f && t < 0.8f) || (t >= 0.95f && t < 1.15f);
		sheen = between(t, 0.75f, 1.3f);
		if (tier != T_BLUE)   /* the glow peaks as it lands, then holds at 0.45 */
			glow = t < 0.35f ? 0 : t < 0.8f ? 1 - 0.55f * between(t, 0.35f, 0.8f) : 0.45f;
		if (tier == T_BRONZE || tier == T_GOLD)   /* one flash as it lands */
			flash = t >= 0.35f && t < 0.45f ? 0.9f : t >= 0.45f && t < 0.52f ? 0.45f : 0;
		if (tier == T_SILVER) {
			flash = t >= 0.4f && t < 0.47f ? 0.7f : t >= 0.47f && t < 0.545f ? 0.3f : 0;
			rays_at(t, 0.375f, 0.85f, 1.2f, 0.85f, 1.0f, &rs, &rr, &ra);
		}
		if (tier == T_GOLD || tier == T_CRYSTAL)
			rays_at(t, 0.375f, 0.85f, 1.2f, 0.85f, 1.0f, &rs, &rr, &ra);
		if (tier == T_CRYSTAL && t >= 0.4f && t < 0.9f) {   /* a white core fills the card, then fades */
			core_r = 10 + 250 * ease_out(between(t, 0.4f, 0.6f));
			core_a = 1 - between(t, 0.6f, 0.9f);
		}
	} else if (q->kind == KIND_BEATEN) {   /* G1: two flickers, a white flash, Finish Line under it */
		block = (t >= 1.2f && t < 1.44f) || (t >= 1.62f && t < 1.86f);
		switched = t >= 2.46f;
		flash = t >= 2.28f && t < 2.32f ? 0.55f : t >= 2.37f && t < 2.41f ? 0.7f :
		        t >= 2.46f && t < 2.70f ? 1 : t >= 2.70f && t < 2.79f ? 0.66f : t >= 2.79f && t < 2.88f ? 0.33f : 0;
		glow = !switched ? 0 : t < 3.0f ? 1 - 0.55f * between(t, 2.46f, 3.0f) : 0.45f;
		sheen = between(t, 3.3f, 3.96f);
	} else {                                /* G2: rays and a light core, Platinum under it */
		block = (t >= 1.2f && t < 1.44f) || (t >= 1.62f && t < 1.86f);
		switched = t >= 2.58f;
		rays_at(t, 2.22f, 2.76f, 3.18f, 1.0f, 1.25f, &rs, &rr, &ra);
		if (t >= 2.34f && t < 3.0f) {
			core_r = 10 + 250 * ease_out(between(t, 2.34f, 2.58f));
			core_a = 1 - between(t, 2.58f, 3.0f);
		}
		glow = !switched ? 0 : t < 3.12f ? 1 - 0.55f * between(t, 2.58f, 3.12f) : 0.45f;
		sheen = between(t, 3.3f, 3.96f);
	}
	face = switched ? &q->f2 : &q->f1;

	/* the whole picture, in the same coordinates as IPLFont (16:9 pillarbox: 4:3 in the middle) */
	ortho_l = screenMode == SCREENMODE_16x9_PILLARBOX ? -104 : 0;
	ortho_r = screenMode == SCREENMODE_16x9_PILLARBOX ? 743 : 639;
	GX_SetViewport(0, 0, m->fbWidth, m->efbHeight, 0.0f, 1.0f);
	clip_off();
	shapes_begin();

	draw_glow(TIERS[face->tier].glow, x, y, glow);
	rect(x, y + 4, x + CARD_W, y + CARD_H + 4, 0x00000059);   /* the drop shadow */
	draw_face(face, x, y, t, block && !switched);
	draw_sheen(x, y, sheen);
	rect(x, y, x + CARD_W, y + CARD_H, alpha(0xffffffff, flash));
	if (core_a > 0) {
		clip_to(x, y, x + CARD_W, y + CARD_H);
		draw_core(x + ICON_CX, y + ICON_CY, core_r, core_a);
		clip_off();
	}
	draw_rays(TIERS[set ? q->f2.tier : q->f1.tier].ray, x + ICON_CX, y + ICON_CY, rs, rr, ra);
}
