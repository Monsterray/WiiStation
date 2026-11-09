/**
 * WiiSX - GraphicsGX.cpp
 * Copyright (C) 2009, 2010 sepp256
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "GraphicsGX.h"
extern "C" {
#include "../../mem2_manager.h"
}
#include "../wiiSXconfig.h"
extern "C" {
    #include "../../gpu.h"
}


GXRModeObj TVMODE_240p =
{
	VI_TVMODE_EURGB60_DS, 	// viDisplayMode
	640, 					// fbWidth
	240, 					// efbHeight
	240, 					// xfbHeight
	(VI_MAX_WIDTH_NTSC - 640)/2, 		// viXOrigin
	(VI_MAX_HEIGHT_NTSC/2 - 480/2)/2, 	// viYOrigin
	640, 					// viWidth
	480, 					// viHeight
	VI_XFBMODE_SF, 			// xFBmode
	GX_FALSE, 				// field_rendering
	GX_FALSE, 				// aa
	// sample points arranged in increasing Y order
	{
		{6,6},{6,6},{6,6},  // pix 0, 3 sample points, 1/12 units, 4 bits each
		{6,6},{6,6},{6,6},  // pix 1
		{6,6},{6,6},{6,6},  // pix 2
		{6,6},{6,6},{6,6}   // pix 3
	},
	// vertical filter[7], 1/64 units, 64=1.0
	{
		0, 		// line n-1
		0, 		// line n-1
		21, 	// line n
		22, 	// line n
		21, 	// line n+1
		0, 		// line n+1
		0 		// line n+2
	}
};
extern "C" unsigned int usleep(unsigned int us);
void video_mode_init(GXRModeObj *rmode, u32 *fb1, u32 *fb2, u32 *fb3);
extern "C" {
#include "../vi_encoder.h"
}

/* The VI gamma the Gamma setting names. One I2C write; the callers run it when the setting
 * or the video mode changes, never per frame. */
void applyVideoGamma(void)
{
	VIDEO_SetGamma((VIGamma)(VI_GM_1_0 + gammaLevel));
}

/* The taps of the frame copy's vertical filter while Deflicker is on, in 64ths. Taps 0-1 weigh
 * the line above, 2-4 the line itself, 5-6 the line below (libogc's mode tables; Dolphin's
 * copy filter adds them up the same way). 0 0 21 22 21 0 0 is no filter at all: the 240p and
 * 480p modes have it, and GX_SetCopyFilter writes it when Deflicker is off. A mode like that
 * keeps it: with no interlace there is no flicker to filter. An interlaced mode gets Normal (its
 * own 8 8 10 12 10 8 8: 16/32/16), Light (8/48/8) or Strong (21/22/21). Until the Strong level
 * came, Light was the no-filter taps, so it looked the same as Deflicker off. */
extern "C" u8 *videoCopyTaps(u8 *modeTaps)
{
	static u8 light[7]  = { 4, 4, 16, 16, 16, 4, 4 };
	static u8 strong[7] = { 10, 11, 7, 8, 7, 11, 10 };
	if (!modeTaps[0] && !modeTaps[1] && !modeTaps[5] && !modeTaps[6])
		return modeTaps;
	return deflickerLevel == DEFLICKERLEVEL_LIGHT ? light
	     : deflickerLevel == DEFLICKERLEVEL_STRONG ? strong : modeTaps;
}

GXRModeObj gvmode;
GXRModeObj mgvmode;

GXTexRegion texCacheRegionS[8];
short      texCacheUsedInfo[8];


extern u32* xfb[3];
extern "C" void gx_vout_mode_changed(void);   /* SoftGPU/drawGX.c */
/* The XFB size switchToTVMode() last set up (bytes per line / 2, lines): perf_prof.c's
 * 'dump <vblank>' writes the front XFB with it (xfb.bin, scripts/xfb2png.py). */
extern "C" { unsigned g_xfb_w = 640, g_xfb_h = 480; }
/* The video mode switchToTVMode() last set (NULL until then: the menu's vmode). A display
 * copy reset during a game has to use this one, not vmode (GlesGpu RestoreDispCopyInfo). */
extern "C" { GXRModeObj *g_tv_mode = NULL; }
extern GXRModeObj *vmode;	/* GamecubeMain.cpp: the menu's mode */
extern "C" char gxColor;	/* GamecubeMain.cpp: GxColor, Docs/GX_QUALITY.md */

/* The EFB format: RGB8 for the menu and by default; with OpenGX, GxColor 1 = RGBA6 (GX dithers
 * it where the game asks: GlesGpu/gpuPrim.c GxDitherPrim), 2 = RGB565 multisampled where the
 * mode on the TV is anti-aliased (switchToTVMode). Docs/GX_QUALITY.md. switchToTVMode runs
 * only in 240p: MainFrame applies it again before a game resumes. */
extern "C" void gxApplyEfbFormat(int game)
{
	int ogx = game && gpuPlugin == OPEN_GX;
	GX_SetPixelFmt((ogx && g_tv_mode && g_tv_mode->aa) ? GX_PF_RGB565_Z16
	               : (ogx && gxColor == 1) ? GX_PF_RGBA6_Z24 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDither(GX_FALSE);
}

/* The Deflicker setting on the frame copy, now: for the mode on the TV. The menu's changes go
 * through here, and so does every game start or resume (go()), since in 480i nothing else
 * sets the copy filter during a game. GlesGpu's RestoreDispCopyInfo calls it after a readback. */
extern "C" void applyCopyFilter(void)
{
	GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
	if (m)
		GX_SetCopyFilter(m->aa, m->sample_pattern, deflickerFilter ? GX_TRUE : GX_FALSE, videoCopyTaps(m->vfilter));
}

/*  Filter Tour  */

#include "IPLFontC.h"
extern "C" {
extern u32 frame_counter;					/* psxcounters.c: the game's vblanks */
extern unsigned int gTexMovieName;			/* GlesGpu/gpuTexture.c */
void glChgTextureFilter(unsigned int gTexMovieName);	/* deps/opengx/gc_gl.c */
void resetTexCacheInfo(void);
}

/* The texture filter (Bilinear setting) on the textures OpenGX holds, as the menu's button does.
 * The soft plugins read the setting at each present. */
static void applyTexFilter(void)
{
	if (gpuPlugin == OPEN_GX) {
		glChgTextureFilter(gTexMovieName);
		resetTexCacheInfo();	/* a texture still in TMEM is loaded again, with the new filter */
	}
}

/* GxColor 2 (Docs/GX_QUALITY.md): OpenGX draws into a multisampled RGB565 EFB, which GX allows
 * up to 264 lines: the 240p modes only. The copy filter averages the three samples of a pixel
 * (taps 2-4) and keeps the mode's own no-filter taps. switchToTVMode and the Filter Tour. */
static void gxSetAa(GXRModeObj *m, bool game)
{
	m->aa = game && gpuPlugin == OPEN_GX && gxColor == 2 && m->efbHeight <= 264;
	if (m->aa) {
		static const u8 aaPattern[12][2] = { {3,2},{9,6},{3,10}, {3,2},{9,6},{3,10},
		                                     {9,2},{3,6},{9,10}, {9,2},{3,6},{9,10} };
		memcpy(m->sample_pattern, aaPattern, sizeof aaPattern);
	}
}

/* The game runs in 240p on a mode that can take GxColor 2 */
static bool tourAa240p(void)
{
	return originalMode == ORIGINALMODE_ENABLE && g_tv_mode == &TVMODE_240p && g_tv_mode->efbHeight <= 264;
}

/* Each setting alone, then a few combinations. on: the renderers a step is for. A step that
 * would change nothing is skipped: Bilinear in 240p (every renderer draws Near there), AA
 * outside it. The first step, the base, fits everywhere.
 * soft: SoftGpuMode, gpu: GpuTiming, far: GxTexMin, gxc: GxColor. */
enum { T_SOFT = 1, T_GX = 2 };
#define N BILINEARFILTER_NEAR
#define B BILINEARFILTER_ENABLE
struct TourStep { signed char tex, soft, gpu, far, gxc, on; };	/* signed: char is unsigned on the PowerPC */
static const TourStep TOUR[] = {
	{ N, 0, 0, 0, 0, T_SOFT | T_GX },
	{ B, 0, 0, 0, 0, T_SOFT | T_GX },
	{ N, 0, 1, 0, 0, T_SOFT | T_GX },
	{ N, 1, 0, 0, 0, T_SOFT },
	{ N, 2, 0, 0, 0, T_SOFT },
	{ B, 1, 0, 0, 0, T_SOFT },
	{ N, 1, 1, 0, 0, T_SOFT },
	{ N, 0, 0, 1, 0, T_GX },
	{ N, 0, 0, 2, 0, T_GX },
	{ N, 0, 0, 3, 0, T_GX },
	{ N, 0, 0, 0, 1, T_GX },
	{ N, 0, 0, 0, 2, T_GX },
	{ B, 0, 0, 3, 0, T_GX },
	{ N, 0, 0, 3, 1, T_GX },
	{ B, 0, 0, 3, 1, T_GX },
	{ N, 0, 0, 3, 2, T_GX },
};
#undef N
#undef B
#define TOUR_STEPS ((int)(sizeof(TOUR) / sizeof(TOUR[0])))

static bool tourFits(const TourStep &t)
{
	return (t.on & (gpuPlugin == OPEN_GX ? T_GX : T_SOFT))
	    && (t.tex == BILINEARFILTER_NEAR || originalMode != ORIGINALMODE_ENABLE)
	    && (t.gxc != 2 || tourAa240p());
}

/* Vblanks per step: 4 s at 60 Hz. autoinput.txt "filtertour <seconds>" sets it (PadWiiSX.c). */
extern "C" { unsigned filterTourVbl = 240; }
extern "C" char softGpuMode, gxTexMin;	/* GamecubeMain.cpp */
static bool tourOn;
static int tourStep;
static u32 tourAt;
static char tourSaved[6];		/* the player's Bilinear, Soft Mode, GPU Timing, Far Textures, GX Color, FPS */
static char tourText[80];
static signed char tourGxColor = -1;	/* a GX Color waiting for the next copy to the XFB, or -1 */

/* The game starts or resumes (go()). The text is drawn with the FPS counter, so that is on. */
extern "C" void filterTourStart(void)
{
	if (!filterTour || tourOn)
		return;
	tourSaved[0] = bilinearFilter;
	tourSaved[1] = softGpuMode;
	tourSaved[2] = gpuTiming;
	tourSaved[3] = gxTexMin;
	tourSaved[4] = gxColor;
	tourSaved[5] = showFPSonScreen;
	showFPSonScreen = FPS_SHOW;
	tourStep = -1;
	tourOn = true;
}

/* The game stops: the player's values come back before the menu shows or saves them. The EFB
 * format follows when the game resumes (MainFrame, switchToTVMode in 240p). */
extern "C" void filterTourStop(void)
{
	if (!tourOn)
		return;
	tourOn = false;
	tourGxColor = -1;
	bilinearFilter  = tourSaved[0];
	softGpuMode     = tourSaved[1];
	gpuTiming       = tourSaved[2];
	gxTexMin        = tourSaved[3];
	gxColor         = tourSaved[4];
	showFPSonScreen = tourSaved[5];
	if (g_tv_mode == &TVMODE_240p)
		gxSetAa(g_tv_mode, tourAa240p());
	applyTexFilter();
	applyCopyFilter();
}

/* OpenGX queued the copy of its frame to the XFB (gx_vout_render, SoftGPU/drawGX.c): the
 * point between frames for a new EFB format. Pixels left in the EFB would be read in the other
 * format, so a copy that kept them is made again with the clear (the same picture, the EFB
 * cleared), as a present that clears does, and the EFB no longer mirrors the uploaded screen
 * (GlesGpu MIRROR_OFF(2)): a game that draws over the whole screen, or uploads it, shows
 * nothing wrong; one that redraws only a part has the rest black for that frame, as after
 * the menu. Then the copy filter for the mode. */
extern "C" int ogx_efb_mirror;	/* deps/opengx/gc_gl.c */
extern "C" void filterTourCopied(u32 *xfbBack, int cleared)
{
	if (tourGxColor < 0)
		return;
	if (!cleared)
		GX_CopyDisp(xfbBack, GX_TRUE);
	ogx_efb_mirror = 0;
	gxColor = tourGxColor;
	tourGxColor = -1;
	if (g_tv_mode == &TVMODE_240p)
		gxSetAa(g_tv_mode, tourAa240p());
	gxApplyEfbFormat(1);	/* dither off: GxDitherPrim's cache may be stale until a prim changes it */
	applyCopyFilter();
}

/* Once per present, from showFpsAndDebugInfo (SoftGPU/drawGX.c), before the copy to the XFB:
 * the next step when its time is up, then its names under the FPS counter, in bold. Soft Mode
 * and GPU Timing are read live; Bilinear and Far Textures go on OpenGX's textures now (each
 * one loaded again at its next bind); GX Color waits for filterTourCopied. */
extern "C" void filterTourDraw(void)
{
	static const char *const soft[] = { "Accurate", "Hi-Fi", "Fast" };
	static const char *const far[] = { "Off", "Linear", "Aniso 2x", "Aniso 4x" };
	static const char *const gxc[] = { "24-bit", "6-bit dither", "AA" };
	if (!tourOn)
		return;
	if (tourStep < 0 || frame_counter - tourAt >= filterTourVbl) {	/* a reset counter steps on */
		int i, num = 0, total = 0, ogx = gpuPlugin == OPEN_GX;
		do
			tourStep = (tourStep + 1) % TOUR_STEPS;
		while (!tourFits(TOUR[tourStep]));		/* the first step fits every renderer */
		for (i = 0; i < TOUR_STEPS; i++)
			if (tourFits(TOUR[i])) {
				total++;
				num += i <= tourStep;
			}
		const TourStep &t = TOUR[tourStep];
		tourAt = frame_counter;
		bilinearFilter = t.tex;
		softGpuMode = t.soft;
		gpuTiming = t.gpu ? GPU_TIMING_ACCURATE : GPU_TIMING_FAST;
		gxTexMin = t.far;
		applyTexFilter();
		if (ogx && t.gxc != gxColor)
			tourGxColor = t.gxc;
		int n = snprintf(tourText, sizeof tourText, "Tour %d/%d: %s", num, total,
		                 t.tex == BILINEARFILTER_NEAR ? "Near" : "Bilinear");
		if (ogx)
			snprintf(tourText + n, sizeof tourText - n, ", Far %s, %s%s", far[(int)t.far], gxc[(int)t.gxc],
			         t.gpu ? ", GPU Accurate" : "");
		else
			snprintf(tourText + n, sizeof tourText - n, ", Soft %s%s", soft[(int)t.soft],
			         t.gpu ? ", GPU Accurate" : "");
	}
	IplFont_drawInit((GXColor){ 255, 255, 120, 255 });
	IplFont_drawString(10, 60, tourText, 1.0, false);
	IplFont_drawString(11, 60, tourText, 1.0, false);	/* bold: TextBox draws it the same way */
}

enum {
	FB_BACK,
	FB_NEXT,
	FB_FRONT,
};

extern "C" void switchToTVMode(short dWidth, short dHeight, bool retMenu){
	GXRModeObj *rmode;
	f32 yscale;
	u32 xfbHeight;
    u16 xfbWidth;
	u32 width = 0;
	u32 height = 0;
	Mtx44 perspective;
	GXColor background = {0, 0, 0, 0xff};
	if (dWidth <= 320)
		dWidth *= 2;

	rmode = &mgvmode;
	if (dWidth > rmode->viWidth)
		dWidth = rmode->viWidth;
	if (dHeight > rmode->viHeight)
		dHeight = rmode->viHeight;


	if ((dHeight > 288))
	{
		if(!retMenu)
		{
			rmode = &gvmode;
			rmode->fbWidth = dWidth;
			rmode->efbHeight = dHeight;
			rmode->xfbHeight = dHeight;
			rmode->viYOrigin = (VI_MAX_HEIGHT_NTSC - dHeight)/2;
			if ((CONF_GetEuRGB60() == 0) && (CONF_GetVideo() == CONF_VIDEO_PAL))
				rmode->viYOrigin = (VI_MAX_HEIGHT_PAL - dHeight)/2;

			if (interlacedMode){
				rmode->viTVMode =	(mgvmode.viTVMode & ~0x03) + VI_INTERLACE;
				rmode->xfbMode = VI_XFBMODE_DF;
			}
			else{
				rmode->viTVMode =	mgvmode.viTVMode;
				rmode->xfbMode = mgvmode.xfbMode;
			}
		}
		xfbWidth = VIDEO_PadFramebufferWidth(rmode->fbWidth);
		width = xfbWidth;
	}
	else
	{
		if (CONF_GetVideo() == CONF_VIDEO_NTSC)
		{
			rmode = &TVMODE_240p;
			rmode->viTVMode = VI_TVMODE_NTSC_DS;
			rmode->viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - dHeight)/2;

		}
		else if (CONF_GetEuRGB60() > 0)
		{
			rmode = &TVMODE_240p;
			rmode->viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - dHeight)/2;
		}
		else if (CONF_GetVideo() == CONF_VIDEO_MPAL)
		{
			rmode = &TVMODE_240p;
			rmode->viTVMode = VI_TVMODE_MPAL_DS;
			rmode->viYOrigin = (VI_MAX_HEIGHT_NTSC/2 - dHeight)/2;
		}
		else
		{
			rmode = &TVMODE_240p;
			rmode->viTVMode = VI_TVMODE_PAL_DS;
			rmode->viXOrigin = (VI_MAX_WIDTH_PAL - 640)/2;
			rmode->viYOrigin = (VI_MAX_HEIGHT_PAL/2 - dHeight)/2;
		}
		rmode->efbHeight = dHeight;
		rmode->xfbHeight = dHeight;

		rmode->fbWidth = dWidth;
		width = rmode->fbWidth;

		gxSetAa(rmode, !retMenu);	/* GxColor 2 */
	}

	GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,(deflickerFilter)?GX_TRUE:GX_FALSE,videoCopyTaps(rmode->vfilter));

	GX_SetCopyClear(background, 0x00ffffff);
	GX_SetViewport(0.0F, 0.0F, rmode->fbWidth, rmode->efbHeight, 0.0F, 1.0F);
	/* The source first: GX_SetDispCopyYScale() works out the XFB's line count from the copy
	 * source height already set. Called before it, it used the previous mode's height, so
	 * a switch from 480 lines to 240p copied 480 lines into a 240-line XFB: the picture came
	 * out as four narrow copies with interleaved lines. */
	GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);
	yscale = GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);
	GX_SetDispCopyYScale(yscale);
	/* Not its return value: libogc v3.1.0 reads the source height at bit 12 instead of 10
	 * (gx.c, GX_SetDispCopyYScale), so a 216-line copy returned 54 lines. The copy itself
	 * uses the register and GX_SetDispCopyDst ignores the height; only the 'xfb' dump read
	 * the wrong count. efbHeight == xfbHeight here, so the scale is 1 and the lines exact. */
	xfbHeight = rmode->xfbHeight;
	xfbWidth = VIDEO_PadFramebufferWidth(rmode->fbWidth);
	GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);
	g_tv_mode = rmode;
	gxApplyEfbFormat(!retMenu);
	GX_SetDispCopyDst(xfbWidth, xfbHeight);
	g_xfb_w = xfbWidth;
	g_xfb_h = xfbHeight;
	g_tv_mode = rmode;
	GX_SetFieldMode(rmode->field_rendering,((rmode->viHeight==2*rmode->xfbHeight)?GX_ENABLE:GX_DISABLE));
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	height = rmode->efbHeight;
	guOrtho(perspective,0,height,0,width,0,300);
	GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);
	GX_SetDither(GX_FALSE);

	/* The copy settings for the new mode are in place before the VI takes it, and the VI takes
	 * it showing a black frame copied with them (SoftGPU/drawGX.c). The VI used to switch
	 * first and wait a vsync or two showing the last copy, made for the old mode: on a Wii a
	 * garbled frame, in Dolphin -- whose XFB cache has no copy of the size the VI then reads --
	 * a solid purple one (MediEvil, each 320 <-> 512 switch around its loading screen). The old
	 * soft GPU presents through its own PeopsSoftGPU/drawGX.c and is left as it was. */
	if (!retMenu && gpuPlugin != OLD_SOFT)
		gx_vout_mode_changed();

	VIDEO_Configure (rmode);
	VIDEO_Flush();
	if (retMenu){
		GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);
		VIDEO_ClearFrameBuffer (rmode, xfb[FB_FRONT], COLOR_BLACK);
		VIDEO_Flush ();
	}
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
}


extern "C" void resetTexCacheInfo(void)
{
    texCacheUsedInfo[0] = -1;
    texCacheUsedInfo[1] = -1;
    texCacheUsedInfo[2] = -1;
    texCacheUsedInfo[3] = -1;
    texCacheUsedInfo[4] = -1;
    texCacheUsedInfo[5] = -1;
    texCacheUsedInfo[6] = -1;
    texCacheUsedInfo[7] = -1;
}

namespace menu {

Graphics::Graphics(GXRModeObj *rmode)
		: vmode(rmode),
		  which_fb(0),
		  first_frame(true),
		  depth(1.0f),
		  transparency(1.0f),
		  viewportWidth(640.0f),
		  viewportHeight(480.0f)
{
//	printf("Graphics constructor\n");

	setColor((GXColor) {0,0,0,0});

#ifdef HW_RVL
	CONF_Init();
#endif
	VIDEO_Init();
	/* 480p needs the component cable: without it the TV gets no picture, and the menu that
	 * could put the setting back could not be seen. */
	switch (videoMode == VIDEOMODE_PROGRESSIVE && !VIDEO_HaveComponentCable() ? VIDEOMODE_AUTO : videoMode)
	{
	case VIDEOMODE_AUTO:
		//vmode = VIDEO_GetPreferredMode(NULL);
		vmode = VIDEO_GetPreferredMode(&vmode_phys);
#if 1
		if(CONF_GetAspectRatio()) {
			vmode->viWidth = 678;
			vmode->viXOrigin = (VI_MAX_WIDTH_PAL - 678) / 2;
		}
#endif
		if (memcmp( &vmode_phys, &TVPal528IntDf, sizeof(GXRModeObj)) == 0)
			memcpy( &vmode_phys, &TVPal576IntDfScale, sizeof(GXRModeObj));
		break;
	case VIDEOMODE_NTSC:
		vmode = &TVNtsc480IntDf;
		memcpy( &vmode_phys, vmode, sizeof(GXRModeObj));
		break;
	case VIDEOMODE_PAL50:
		vmode = &TVPal576IntDfScale;
		memcpy( &vmode_phys, vmode, sizeof(GXRModeObj));
		break;
	case VIDEOMODE_PAL60:
		vmode = &TVEurgb60Hz480IntDf;
		memcpy( &vmode_phys, vmode, sizeof(GXRModeObj));
		break;
	case VIDEOMODE_PROGRESSIVE:
		vmode = &TVNtsc480Prog;
		memcpy( &vmode_phys, vmode, sizeof(GXRModeObj));
		break;
	}

	//vmode->efbHeight = viewportHeight; // Note: all possible modes have efbHeight of 480
	memcpy( &gvmode, vmode, sizeof(GXRModeObj));
	memcpy( &mgvmode, vmode, sizeof(GXRModeObj));
	if (trapFilter)
		VIDEO_SetTrapFilter(1);
	if (gammaLevel)
		applyVideoGamma();

	VIDEO_Configure(vmode);

	/* Three XFBs: the menu flips two, a game uses all three (SoftGPU/drawGX.c: one being copied
	 * to, one finished and waiting for the retrace, one on the TV), so a present never waits
	 * for the retrace. In MEM2: 1.8 MB (PAL 2.1 MB) of MEM1 back, and the VI reads MEM2
	 * addresses (libogc's __setFbbRegs shifts any address over 16 MB). */
	for (int i = 0; i < 3; i++) {
		void *fb = _mem2_memalign(32, VIDEO_GetFrameBufferSize(vmode));
		xfb[i] = (u32 *)MEM_K0_TO_K1(fb ? fb : SYS_AllocateFramebuffer(vmode));
	}

	console_init (xfb[0], 20, 64, vmode->fbWidth, vmode->xfbHeight, vmode->fbWidth * 2);

	VIDEO_SetNextFramebuffer(xfb[which_fb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	which_fb ^= 1;

	//Pass vmode, xfb[0] and xfb[1] back to main program
	video_mode_init(vmode, (u32*)xfb[0], (u32*)xfb[1], (u32*)xfb[2]);

	//Perform GX init stuff here?
	//GX_init here or in main?
	//GX_SetViewport( 0.0f, 0.0f, viewportWidth, viewportHeight );
	init();
}

Graphics::~Graphics()
{
}


/* Signature must match libogc's GXTexRegionCallback typedef, which takes a const GXTexObj*. */
GXTexRegion * GXTexRegionCallback(const GXTexObj *obj, u8 mapid)
{
    return &texCacheRegionS[0];
}


void Graphics::init()
{

	f32 yscale;
	u32 xfbHeight;
	void *gpfifo = NULL;
	GXColor background = {0, 0, 0, 0xff};

	gpfifo = memalign(32,GX_FIFO_MINSIZE);
	memset(gpfifo,0,GX_FIFO_MINSIZE);
	GX_Init(gpfifo,GX_FIFO_MINSIZE);
	GX_SetCopyClear(background, GX_MAX_Z24);

	GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
	GX_SetDispCopySrc(0,0,vmode->fbWidth,vmode->efbHeight);   /* before the scale: see switchToTVMode */
	yscale = GX_GetYScaleFactor(vmode->efbHeight,vmode->xfbHeight);
	GX_SetDispCopyYScale(yscale);
	xfbHeight = vmode->xfbHeight;   /* not the return value: libogc v3.1.0 gets it wrong (see switchToTVMode) */
	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth,xfbHeight);
	GX_SetCopyFilter(vmode->aa,vmode->sample_pattern,GX_TRUE,vmode->vfilter);
	GX_SetFieldMode(vmode->field_rendering,((vmode->viHeight==2*vmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (vmode->aa)
			GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
		else
			GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	GX_InvVtxCache();
	GX_InvalidateTexAll();

    // Init custom texture cache region
    GX_InitTexCacheRegion(&texCacheRegionS[0], GX_FALSE, 0x0, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[1], GX_FALSE, 0x20000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[2], GX_FALSE, 0x40000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[3], GX_FALSE, 0x60000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[4], GX_FALSE, 0x80000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[5], GX_FALSE, 0xA0000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[6], GX_FALSE, 0xC0000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);
    GX_InitTexCacheRegion(&texCacheRegionS[7], GX_FALSE, 0xE0000, GX_TEXCACHE_128K, 0, GX_TEXCACHE_NONE);

    resetTexCacheInfo();

    GX_SetTexRegionCallback(GXTexRegionCallback);

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

	setTEV(GX_PASSCLR);
	newModelView();
	loadModelView();
	loadOrthographic();
}

void Graphics::drawInit()
{
	// Reset various parameters from gfx plugin
	GX_SetZTexture(GX_ZT_DISABLE,GX_TF_Z16,0); //GX_ZT_DISABLE or GX_ZT_REPLACE; set in gDP.cpp
	GX_SetZCompLoc(GX_FALSE); // Do Z-compare after texturing.
	GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,255});
	GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	GX_SetZMode(GX_ENABLE,GX_ALWAYS,GX_TRUE);

	//set blend mode
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
	GX_SetAlphaUpdate(GX_ENABLE);
	GX_SetDstAlpha(GX_DISABLE, 0xFF);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);

	GX_InvVtxCache();
	//GX_InvalidateTexAll();

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

	setTEV(GX_PASSCLR);
	newModelView();
	loadModelView();
	loadOrthographic();
}

void Graphics::swapBuffers()
{
//	printf("Graphics swapBuffers\n");
//	if(which_fb==1) usleep(1000000);
	GX_SetCopyClear((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);
	GX_CopyDisp(xfb[which_fb],GX_TRUE);
	GX_Flush();

	VIDEO_SetNextFramebuffer(xfb[which_fb]);
	if(first_frame) {
		first_frame = false;
		VIDEO_SetBlack(GX_FALSE);
	}
	VIDEO_Flush();
 	VIDEO_WaitVSync();
	which_fb ^= 1;
//	printf("Graphics endSwapBuffers\n");
}

void Graphics::clearEFB(GXColor color, u32 zvalue)
{
	GX_SetColorUpdate(GX_ENABLE);
	GX_SetAlphaUpdate(GX_ENABLE);
	GX_SetZMode(GX_ENABLE,GX_ALWAYS,GX_TRUE);
	GX_SetCopyClear(color, zvalue);
	GX_CopyDisp(xfb[which_fb],GX_TRUE);
	GX_Flush();
}

void Graphics::newModelView()
{
	guMtxIdentity(currentModelViewMtx);
}

void Graphics::translate(float x, float y, float z)
{
	Mtx tmp;
	guMtxTrans (tmp, x, y, z);
	guMtxConcat (currentModelViewMtx, tmp, currentModelViewMtx);
}

void Graphics::translateApply(float x, float y, float z)
{
	guMtxTransApply(currentModelViewMtx,currentModelViewMtx,x,y,z);
}

void Graphics::rotate(float degrees)
{
	guMtxRotDeg(currentModelViewMtx,'Z',degrees);
}

void Graphics::loadModelView()
{
	GX_LoadPosMtxImm(currentModelViewMtx,GX_PNMTX0);
}

void Graphics::loadOrthographic()
{
	if(screenMode)	guOrtho(currentProjectionMtx, 0, 479, -104, 743, 0, 700);
	else			guOrtho(currentProjectionMtx, 0, 479, 0, 639, 0, 700);
	GX_LoadProjectionMtx(currentProjectionMtx, GX_ORTHOGRAPHIC);
}

void Graphics::setDepth(float newDepth)
{
	depth = newDepth;
}

float Graphics::getDepth()
{
	return depth;
}

void Graphics::setColor(GXColor color)
{
	for (int i = 0; i < 4; i++){
		currentColor[i].r = color.r;
		currentColor[i].g = color.g;
		currentColor[i].b = color.b;
		currentColor[i].a = color.a;
	}
	applyCurrentColor();
}

void Graphics::setColor(GXColor* color)
{
	for (int i = 0; i < 4; i++){
		currentColor[i].r = color[i].r;
		currentColor[i].g = color[i].g;
		currentColor[i].b = color[i].b;
		currentColor[i].a = color[i].a;
	}
	applyCurrentColor();
}

void Graphics::drawRect(int x, int y, int width, int height)
{
	GX_Begin(GX_LINESTRIP, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) (x+width),(float) y, depth );
		GX_Color4u8(appliedColor[1].r, appliedColor[1].g, appliedColor[1].b, appliedColor[1].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) (x+width),(float) (y+height), depth );
		GX_Color4u8(appliedColor[2].r, appliedColor[2].g, appliedColor[2].b, appliedColor[2].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) x,(float) (y+height), depth );
		GX_Color4u8(appliedColor[3].r, appliedColor[3].g, appliedColor[3].b, appliedColor[3].a);
		GX_TexCoord2f32(0.0f,0.0f);
	GX_End();
}

void Graphics::fillRect(int x, int y, int width, int height)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) (x+width),(float) y, depth );
		GX_Color4u8(appliedColor[1].r, appliedColor[1].g, appliedColor[1].b, appliedColor[1].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) (x+width),(float) (y+height), depth );
		GX_Color4u8(appliedColor[2].r, appliedColor[2].g, appliedColor[2].b, appliedColor[2].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) x,(float) (y+height), depth );
		GX_Color4u8(appliedColor[3].r, appliedColor[3].g, appliedColor[3].b, appliedColor[3].a);
		GX_TexCoord2f32(0.0f,0.0f);
	GX_End();
}

void Graphics::fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
		GX_Position3f32((float) x1,(float) y1, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) x2,(float) y2, depth );
		GX_Color4u8(appliedColor[1].r, appliedColor[1].g, appliedColor[1].b, appliedColor[1].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) x3,(float) y3, depth );
		GX_Color4u8(appliedColor[2].r, appliedColor[2].g, appliedColor[2].b, appliedColor[2].a);
		GX_TexCoord2f32(0.0f,0.0f);
	GX_End();
}

void Graphics::drawImage(int textureId, int x, int y, int width, int height, float s1, float s2, float t1, float t2)
{
	//Init texture here or in calling code?
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y, depth );
		GX_Color4u8(appliedColor[1].r, appliedColor[1].g, appliedColor[1].b, appliedColor[1].a);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height), depth );
		GX_Color4u8(appliedColor[2].r, appliedColor[2].g, appliedColor[2].b, appliedColor[2].a);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height), depth );
		GX_Color4u8(appliedColor[3].r, appliedColor[3].g, appliedColor[3].b, appliedColor[3].a);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

void Graphics::drawLine(int x1, int y1, int x2, int y2)
{
	GX_Begin(GX_LINES, GX_VTXFMT0, 2);
		GX_Position3f32((float) x1,(float) y1, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32((float) x2,(float) y2, depth );
		GX_Color4u8(appliedColor[1].r, appliedColor[1].g, appliedColor[1].b, appliedColor[1].a);
		GX_TexCoord2f32(0.0f,0.0f);
	GX_End();
}

#ifndef PI
#define PI 3.14159f
#endif

void Graphics::drawCircle(int x, int y, int radius, int numSegments)
{

	GX_Begin(GX_LINESTRIP, GX_VTXFMT0, numSegments+1);

	for (int i = 0; i<=numSegments; i++)
	{
		float angle, point_x, point_y;
		angle = 2*PI * i/numSegments;
		point_x = (float)x + (float)radius * cos( angle );
		point_y = (float)y + (float)radius * sin( angle );

		GX_Position3f32((float) point_x,(float) point_y, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
	}

	GX_End();
}

void Graphics::fillRing(int x, int y, int inner, int outer, int numSegments)
{
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (numSegments + 1));
	for (int i = 0; i <= numSegments; i++)
	{
		float angle = 2*PI * i/numSegments, c = cos(angle), s = sin(angle);
		GX_Position3f32((float)x + outer * c, (float)y + outer * s, depth);
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f, 0.0f);
		GX_Position3f32((float)x + inner * c, (float)y + inner * s, depth);
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f, 0.0f);
	}
	GX_End();
}

void Graphics::drawString(int x, int y, const std::string &str)
{
	//todo
}

void Graphics::drawPoint(int x, int y, int radius)
{
	GX_SetPointSize(u8 (radius *3 ),GX_TO_ZERO);
	GX_Begin(GX_POINTS, GX_VTXFMT0, 1);
		GX_Position3f32((float) x,(float) y, depth );
		GX_Color4u8(appliedColor[0].r, appliedColor[0].g, appliedColor[0].b, appliedColor[0].a);
		GX_TexCoord2f32(0.0f,0.0f);
	GX_End();
}

void Graphics::setLineWidth(int width)
{
	GX_SetLineWidth((u8) (width * 6), GX_TO_ZERO );
}

void Graphics::pushDepth(float d)
{
	depthStack.push(getDepth());
	setDepth(d);
}

void Graphics::popDepth()
{
	depthStack.pop();
	if(depthStack.size() != 0)
	{
		setDepth(depthStack.top());
	}
	else
	{
		setDepth(1.0f);
	}
}

void Graphics::enableScissor(int x, int y, int width, int height)
{
	if(screenMode)
	{
		int x1 = (x+104)*640/848;
		int x2 = (x+width+104)*640/848;
		GX_SetScissor((u32) x1,(u32) y,(u32) x2-x1,(u32) height);
	}
	else
		GX_SetScissor((u32) x,(u32) y,(u32) width,(u32) height);
}

void Graphics::disableScissor()
{
	GX_SetScissor((u32) 0,(u32) 0,(u32) viewportWidth,(u32) viewportHeight); //Set to the same size as the viewport.
}

void Graphics::enableBlending(bool blend)
{
	if (blend)
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	else
		GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
}

void Graphics::setTEV(int tev_op)
{
	GX_SetNumTevStages(1);
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp (GX_TEVSTAGE0, tev_op);
	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
	GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
}

void Graphics::pushTransparency(float f)
{
	transparencyStack.push(getTransparency());
	setTransparency(f);
}

void Graphics::popTransparency()
{
	transparencyStack.pop();
	if(transparencyStack.size() != 0)
	{
		setTransparency(transparencyStack.top());
	}
	else
	{
		setTransparency(1.0f);
	}
}

void Graphics::setTransparency(float f)
{
	transparency = f;
	applyCurrentColor();
}

float Graphics::getTransparency()
{
	return transparency;
}

void Graphics::applyCurrentColor()
{
	for (int i = 0; i < 4; i++){
		appliedColor[i].r = currentColor[i].r;
		appliedColor[i].g = currentColor[i].g;
		appliedColor[i].b = currentColor[i].b;
		appliedColor[i].a = (u8) (getCurrentTransparency(i) * 255.0f);
	}
}

float Graphics::getCurrentTransparency(int index)
{
	float alpha = (float)currentColor[index].a/255.0f;
	float val = alpha * transparency;
	return val;
}

} //namespace menu
