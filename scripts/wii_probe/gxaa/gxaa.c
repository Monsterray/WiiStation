/* gxaa.c -- GX anti-aliasing test bench for the real Wii (Docs/GX_QUALITY.md, "AA loop").
 *
 * Sets up WiiStation's 240p video mode (Gamecube/libgui/GraphicsGX.cpp switchToTVMode), draws a
 * fixed test pattern with GX (gradients, thin slivers, near-horizontal and near-vertical edges,
 * a depth test, an EFB-to-texture readback) and copies it to the XFB. Each wiiload argument is
 * one variant, a comma list of key=value; the program shows each for HOLD frames in turn and
 * loops until `hbc.py exit` (or 150 s). The CPU stamps the variant's number and the readback
 * texel into the XFB's top-left after each copy, so `hbc.py screen` pictures say which variant
 * they show even when the GX picture is black. Keys (default = WiiStation's GxColor=2 path):
 *
 *   fmt=565|8|6     EFB pixel format: RGB565_Z16 (multisampled), RGB8_Z24, RGBA6_Z24
 *   aa=1|0          GX_SetCopyFilter aa flag
 *   pat=ws|c        sample pattern: ws = libogc's 240p AA modes {3,2},{9,6},{3,10}...; c = all {6,6}
 *   vf=0|1|df       vertical filter: 0 = vf off (0 0 21 22 21 0 0), 1 = the mode's taps (same),
 *                   df = 8 8 10 12 10 8 8 (interlaced deflicker)
 *   w=640 h=240     EFB/XFB size (CTR in 240p: w=512,h=216)
 *   ys=0|1          1: GX_SetDispCopyYScale(GX_GetYScaleFactor(h,h)) (WiiStation); 0: not called
 *   fm=a|0|1        GX_SetFieldMode half-aspect: a = WiiStation's rule (viHeight == 2*xfbHeight)
 *   sync=1|0        GX_DrawDone before switching the pixel format (0: switch with a frame queued)
 *   z=1|0           draw the depth-test pair (green in front must win)
 *   tex=565|8|0     copy the EFB to a texture (GX_TF_RGB565 / GX_TF_RGBA8) and draw it as an inset
 *   clamp=3|0       GX_SetCopyClamp (top|bottom)
 *   clr=1|0         clear on the display copy
 *   zfmt=0|1..      GX_SetPixelFmt z format (0 = GX_ZC_LINEAR)
 *   dither=0|1      GX_SetDither
 *   scis=0|N        GX_SetScissorBoxOffset(0, N)  (default 0 = not called)
 *   vp=0|1          viewport jitter: GX_SetViewportJitter(...,field) instead of GX_SetViewport
 *
 * Build: scripts/wii_probe/gxaa/build.sh. Run: scripts/wii_probe/gxaa/run.py (a queue job). */
#include <gccore.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogc/lwp_watchdog.h>
#include "hbc_agent.h"

#define HOLD 150   /* frames per variant (2.5 s at 60 Hz) */

extern void agent_set_screen_size(u16 w, u16 h);   /* hbc_agent/agent.c: hbc.py screen's size */

typedef struct {
	int fmt, aa, pat, vf, w, h, ys, fm, sync, z, tex, clamp, clr, zfmt, dither, scis, vp;
} Var;

static Var parse(const char *s)
{
	Var v = { 565, 1, 0, 0, 640, 240, 1, -1, 1, 1, 565, 3, 1, 0, 0, 0, 0 };
	char buf[256], *tok, *save;
	strncpy(buf, s, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		char *eq = strchr(tok, '=');
		const char *val;
		int n;
		if (!eq) continue;
		*eq = 0;
		val = eq + 1;
		n = atoi(val);
		if (!strcmp(tok, "fmt")) v.fmt = n;
		else if (!strcmp(tok, "aa")) v.aa = n;
		else if (!strcmp(tok, "pat")) v.pat = !strcmp(val, "c");
		else if (!strcmp(tok, "vf")) v.vf = !strcmp(val, "df") ? 2 : n;
		else if (!strcmp(tok, "w")) v.w = n;
		else if (!strcmp(tok, "h")) v.h = n;
		else if (!strcmp(tok, "ys")) v.ys = n;
		else if (!strcmp(tok, "fm")) v.fm = !strcmp(val, "a") ? -1 : n;
		else if (!strcmp(tok, "sync")) v.sync = n;
		else if (!strcmp(tok, "z")) v.z = n;
		else if (!strcmp(tok, "tex")) v.tex = n;
		else if (!strcmp(tok, "clamp")) v.clamp = n;
		else if (!strcmp(tok, "clr")) v.clr = n;
		else if (!strcmp(tok, "zfmt")) v.zfmt = n;
		else if (!strcmp(tok, "dither")) v.dither = n;
		else if (!strcmp(tok, "scis")) v.scis = n;
		else if (!strcmp(tok, "vp")) v.vp = n;
	}
	return v;
}

/* WiiStation's TVMODE_240p (GraphicsGX.cpp), NTSC/EuRGB60 double-strike */
static GXRModeObj mode = {
	VI_TVMODE_EURGB60_DS, 640, 240, 240,
	(VI_MAX_WIDTH_NTSC - 640) / 2, (VI_MAX_HEIGHT_NTSC / 2 - 480 / 2) / 2,
	640, 480, VI_XFBMODE_SF, GX_FALSE, GX_FALSE,
	{ {6,6},{6,6},{6,6}, {6,6},{6,6},{6,6}, {6,6},{6,6},{6,6}, {6,6},{6,6},{6,6} },
	{ 0, 0, 21, 22, 21, 0, 0 }
};
static const u8 patWS[12][2] = { {3,2},{9,6},{3,10}, {3,2},{9,6},{3,10},
                                 {9,2},{3,6},{9,10}, {9,2},{3,6},{9,10} };
static const u8 patC[12][2] = { {6,6},{6,6},{6,6}, {6,6},{6,6},{6,6},
                                {6,6},{6,6},{6,6}, {6,6},{6,6},{6,6} };
static u8 tapsDF[7] = { 8, 8, 10, 12, 10, 8, 8 };

static u32 *xfb[2];
static int cur;
static void *texbuf;          /* EFB readback, sized for 640x264 RGBA8 */
static u16 readback;          /* texel read back at the red square */

static void vtx(f32 x, f32 y, f32 z, u32 c)
{
	GX_Position3f32(x, y, z);
	GX_Color4u8(c >> 24, c >> 16, c >> 8, c);
}

static void quad(f32 x0, f32 y0, f32 x1, f32 y1, f32 z, u32 c0, u32 c1, u32 c2, u32 c3)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vtx(x0, y0, z, c0); vtx(x1, y0, z, c1); vtx(x1, y1, z, c2); vtx(x0, y1, z, c3);
	GX_End();
}

static void tri(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy, u32 c)
{
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
	vtx(ax, ay, -0.5f, c); vtx(bx, by, -0.5f, c); vtx(cx, cy, -0.5f, c);
	GX_End();
}

static void colour_state(void)
{
	Mtx44 p;
	Mtx m;
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetColorUpdate(GX_TRUE);
	guOrtho(p, 0, mode.efbHeight, 0, mode.fbWidth, 0, 10);
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);
	guMtxIdentity(m);
	GX_LoadPosMtxImm(m, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
}

static void scene(const Var *v)
{
	f32 W = mode.fbWidth, H = mode.efbHeight, cx, cy;
	int i;

	colour_state();
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	/* background: vertical gradient blue -> red (565 banding shows here) */
	quad(0, 0, W, H, -5, 0x101060ff, 0x101060ff, 0x601010ff, 0x601010ff);
	/* grey ramp black -> white, 16 lines high, under the markers */
	quad(0, 20, W, 36, -5, 0x000000ff, 0xffffffff, 0xffffffff, 0x000000ff);
	/* primaries: red (the readback target), green, blue, white */
	quad(16, 44, 56, 84, -5, 0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff);
	quad(64, 44, 104, 84, -5, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff);
	quad(112, 44, 152, 84, -5, 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff);
	quad(160, 44, 200, 84, -5, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
	/* pinwheel of thin slivers: AA shows on every edge */
	cx = W * 0.5f; cy = H * 0.55f;
	for (i = 0; i < 24; i++) {
		f32 a = i * (2 * M_PI / 24), b = a + 0.09f, r = H * 0.38f;
		tri(cx, cy, cx + r * cosf(a), cy + r * sinf(a), cx + r * cosf(b), cy + r * sinf(b),
		    (i & 1) ? 0xffff00ff : 0x00ffffff);
	}
	/* near-horizontal and near-vertical edges: the classic staircase */
	tri(W - 230, H - 20, W - 10, H - 34, W - 10, H - 20, 0xffffffff);
	tri(W - 30, 44, W - 16, 44, W - 30, H - 50, 0xffffffff);
	/* depth: green (near) first, then red (far) over it; green must stay in front */
	if (v->z) {
		GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
		quad(220, 44, 280, 84, -1.0f, 0x00c000ff, 0x00c000ff, 0x00c000ff, 0x00c000ff);
		quad(250, 54, 310, 94, -9.0f, 0xc00000ff, 0xc00000ff, 0xc00000ff, 0xc00000ff);
		GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	}
}

/* EFB -> texture (efbSync.inc's snapshot), drawn back at quarter size, bottom left */
static void readback_inset(const Var *v)
{
	u8 fmt = v->tex == 8 ? GX_TF_RGBA8 : GX_TF_RGB565;
	u16 w = mode.fbWidth, h = mode.efbHeight;
	GXTexObj t;

	GX_SetCopyFilter(mode.aa, mode.sample_pattern, GX_FALSE, NULL);   /* as efbSync.inc */
	GX_SetTexCopySrc(0, 0, w, h);
	GX_SetTexCopyDst(w, h, fmt, GX_FALSE);
	GX_CopyTex(texbuf, GX_FALSE);
	GX_PixModeSync();
	GX_DrawDone();
	DCInvalidateRange(texbuf, GX_GetTexBufferSize(w, h, fmt, GX_FALSE, 0));
	/* the red square's centre, (36,64), as RGB565 */
	{
		int x = 36, y = 64, bw = 4, blk = ((y >> 2) * (w / bw) + (x >> 2));
		if (fmt == GX_TF_RGB565) {
			u16 *p = (u16 *)texbuf + blk * 16 + (y & 3) * 4 + (x & 3);
			readback = *p;
		} else {   /* RGBA8: AR block then GB block, 64 bytes per 4x4 */
			u8 *p = (u8 *)texbuf + blk * 64 + ((y & 3) * 4 + (x & 3)) * 2;
			u8 r = p[1], g = p[32], b = p[33];
			readback = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		}
	}
	GX_InitTexObj(&t, texbuf, w, h, fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(&t, GX_LINEAR, GX_LINEAR);
	GX_InvalidateTexAll();
	GX_LoadTexObj(&t, GX_TEXMAP0);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetNumChans(0);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	{
		f32 x0 = 8, y0 = h - 8 - h / 4.0f, x1 = 8 + w / 4.0f, y1 = h - 8;
		GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
		GX_Position3f32(x0, y0, -0.5f); GX_TexCoord2f32(0, 0);
		GX_Position3f32(x1, y0, -0.5f); GX_TexCoord2f32(1, 0);
		GX_Position3f32(x1, y1, -0.5f); GX_TexCoord2f32(1, 1);
		GX_Position3f32(x0, y1, -0.5f); GX_TexCoord2f32(0, 1);
		GX_End();
	}
}

static const u8 *taps(const Var *v)
{
	return v->vf == 2 ? tapsDF : mode.vfilter;
}

static void apply(const Var *v)
{
	int fmt = v->fmt == 8 ? GX_PF_RGB8_Z24 : v->fmt == 6 ? GX_PF_RGBA6_Z24 : GX_PF_RGB565_Z16;
	u32 xfbH;

	if (v->sync)
		GX_DrawDone();
	mode.fbWidth = v->w;
	mode.efbHeight = mode.xfbHeight = v->h;
	mode.viYOrigin = (VI_MAX_HEIGHT_NTSC / 2 - v->h) / 2;
	mode.aa = v->aa;
	memcpy(mode.sample_pattern, v->pat ? patC : patWS, sizeof patWS);

	GX_SetCopyFilter(mode.aa, mode.sample_pattern, v->vf ? GX_TRUE : GX_FALSE, (u8 *)taps(v));
	GX_SetCopyClear((GXColor){ 0, 0, 0, 0xff }, 0x00ffffff);
	if (v->vp)
		GX_SetViewportJitter(0, 0, mode.fbWidth, mode.efbHeight, 0, 1, 1);
	else
		GX_SetViewport(0, 0, mode.fbWidth, mode.efbHeight, 0, 1);
	GX_SetDispCopySrc(0, 0, mode.fbWidth, mode.efbHeight);
	xfbH = mode.xfbHeight;
	if (v->ys)
		xfbH = GX_SetDispCopyYScale(GX_GetYScaleFactor(mode.efbHeight, mode.xfbHeight));
	GX_SetScissor(0, 0, mode.fbWidth, mode.efbHeight);
	if (v->scis)
		GX_SetScissorBoxOffset(0, v->scis);
	GX_SetPixelFmt(fmt, v->zfmt);
	GX_SetDither(v->dither);
	GX_SetDispCopyDst(VIDEO_PadFramebufferWidth(mode.fbWidth), xfbH);
	GX_SetFieldMode(mode.field_rendering,
		v->fm < 0 ? (mode.viHeight == 2 * mode.xfbHeight) : v->fm);
	GX_SetCopyClamp(v->clamp);
	GX_SetDispCopyGamma(GX_GM_1_0);

	VIDEO_Configure(&mode);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();
	agent_set_screen_size(VIDEO_PadFramebufferWidth(mode.fbWidth), mode.xfbHeight);
}

/* the variant's number and the readback texel, CPU-written into the XFB after the copy:
 * row 0-7: white, 8 bits of the index (MSB first), white; rows 8-15: 16 bits of the texel */
static void stamp(u32 *fb, int idx, u16 texel)
{
	u32 *p = fb;   /* already uncached */
	int pitch = VIDEO_PadFramebufferWidth(mode.fbWidth) / 2;   /* u32 = 2 pixels */
	int y, b, k;
	for (y = 0; y < 16; y++)
		for (b = 0; b < 16; b++) {
			int bit;
			if (y < 8)
				bit = b == 0 || b == 9 ? 1 : b <= 8 ? (idx >> (8 - b)) & 1 : 0;
			else
				bit = (texel >> (15 - b)) & 1;
			for (k = 0; k < 8; k++)
				p[y * pitch + b * 8 + k] = bit ? 0xEB80EB80 : 0x10801080;
		}
}

int main(int argc, char **argv)
{
	static hbc_agent_config cfg;
	Var vars[64];
	int nv = 0, i;
	u64 t0;
	void *fifo;

	VIDEO_Init();
	if (CONF_GetVideo() == CONF_VIDEO_NTSC)
		mode.viTVMode = VI_TVMODE_NTSC_DS;
	else if (CONF_GetVideo() == CONF_VIDEO_MPAL)
		mode.viTVMode = VI_TVMODE_MPAL_DS;
	/* EuRGB60 keeps VI_TVMODE_EURGB60_DS; PAL 50 Hz is not handled (the bench Wii is 60 Hz) */
	for (i = 0; i < 2; i++) {
		xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(&(GXRModeObj){ .fbWidth = 640, .xfbHeight = 480 }));
		memset(xfb[i], 0, 640 * 480 * 2);
	}
	texbuf = memalign(32, 640 * 264 * 4);
	DCInvalidateRange(texbuf, 640 * 264 * 4);

	for (i = 1; i < argc && nv < 64; i++)
		vars[nv++] = parse(argv[i]);
	if (!nv)
		vars[nv++] = parse("");

	VIDEO_Configure(&mode);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();

	cfg.name = "gxaa";
	cfg.app_polls_exit = true;
	hbc_agent_init(&cfg);

	fifo = memalign(32, 256 * 1024);
	memset(fifo, 0, 256 * 1024);
	GX_Init(fifo, 256 * 1024);
	GX_SetDispCopyGamma(GX_GM_1_0);

	t0 = gettime();
	for (i = 0; !hbc_agent_exit_requested() && diff_sec(t0, gettime()) < 150; i = (i + 1) % nv) {
		const Var *v = &vars[i];
		int f;
		if (!v->sync) {   /* a frame of RGB8 queued, the switch lands behind it */
			GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
			scene(v);
			GX_CopyDisp(xfb[cur ^ 1], GX_TRUE);
		}
		apply(v);
		for (f = 0; f < HOLD && !hbc_agent_exit_requested(); f++) {
			readback = 0;
			scene(v);
			if (v->tex)
				readback_inset(v);
			GX_SetCopyFilter(mode.aa, mode.sample_pattern, v->vf ? GX_TRUE : GX_FALSE, (u8 *)taps(v));
			cur ^= 1;
			GX_CopyDisp(xfb[cur], v->clr);
			GX_DrawDone();
			stamp(xfb[cur], i, readback);
			VIDEO_SetNextFramebuffer(xfb[cur]);
			VIDEO_Flush();
			VIDEO_WaitVSync();
			hbc_agent_alive();
		}
	}
	GX_DrawDone();
	return 0;
}
