/***************************************************************************
                           gpu.c  -  description
                             -------------------
    begin                : Sun Mar 08 2009
    copyright            : (C) 1999-2009 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

//*************************************************************************//
// History of changes:
//
// 2009/03/08 - Pete
// - generic cleanup for the Peops release
//
//*************************************************************************//

//#include "gpuStdafx.h"

//#include <mmsystem.h>
//#define _IN_GPU
#define _IN_GPU_LIB

#include <stdlib.h>
#include <malloc.h>   /* memalign */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <gccore.h>

#include "gpuExternals.h"
#include "gpuPlugin.h"
//#include "gpuDraw.h"
//#include "gpuTexture.h"
//#include "gpuPrim.h"

#include "../gpu.h" // meh
#include "../gpulib/gpu.h"
#include "../SoftGPU/oldGpuFps.h"
#include "../mem2_manager.h"
#include "../Gamecube/wiiSXconfig.h"

//#include "NoPic.h"

#include "../gpulib/stdafx.h"

#include "../database.h"
#include "../Gamecube/DEBUG.h"
#include "../Gamecube/MEM2.h"
#include "../Gamecube/perf_prof.h"

extern int gx_vout_open(void); // SoftGPU/drawGX.c
// _Bool, not bool: this file sees "#define bool unsigned short" (2 bytes,
// from gpuPlugin.h/gpulib/stdafx.h), but the real definition in
// Gamecube/libgui/GraphicsGX.cpp is genuine C++ bool (1 byte) -- writing
// "bool" here would silently declare the wrong parameter width.
extern void switchToTVMode(short dWidth, short dHeight, _Bool retMenu);

static short DrawSemiTrans=FALSE;
static short ly0,lx0,ly1,lx1,ly2,lx2,ly3,lx3;        // global psx vertex coords
static int   GlobalTextAddrX, GlobalTextAddrY, GlobalTextTP;
static long  GlobalTextABR,GlobalTextPAGE;
static BOOL  bUsingTWin=FALSE;
static unsigned short usMirror=0;                             // sprite mirror
static TWin_t         TWin;
static int   drawX,drawY,drawW,drawH;                 // offscreen drawing checkers
static int   iFakePrimBusy;
static BOOL  bIsFirstFrame=TRUE;

unsigned int  dwGPUVersion=0;
int           iGPUHeight=512;
int           iGPUHeightMask=511;
int           GlobalTextIL=0;
int           iTileCheat=0;

////////////////////////////////////////////////////////////////////////
// memory image of the PSX vram
////////////////////////////////////////////////////////////////////////

//unsigned char  *psxVSecure;
//unsigned char  *psxVub;
//signed   char  *psxVsb;
//unsigned short *psxVuw;
//unsigned short *psxVuw_eom;
//signed   short *psxVsw;
//unsigned int   *psxVul;
//signed   int   *psxVsl;

// macro for easy access to packet information
#define GPUCOMMAND(x) ((x>>24) & 0xff)

GLfloat         gl_z=0.0f;
BOOL            bNeedInterlaceUpdate=FALSE;
BOOL            bNeedRGB24Update=FALSE;

unsigned long   ulStatusControl[256];

////////////////////////////////////////////////////////////////////////
// global GPU vars
////////////////////////////////////////////////////////////////////////

static long     GPUdataRet;
static unsigned long gpuDataM[256];
static unsigned char gpuCommand = 0;


static long          gpuDataC = 0;
static long          gpuDataP = 0;

int             iDataWriteMode;
int             iDataReadMode;

int             lClearOnSwap;
int             lClearOnSwapColor;
//BOOL            bSkipNextFrame = FALSE;
int             iColDepth;
BOOL            bChangeRes;
BOOL            bWindowMode;

// possible psx display widths
short dispWidths[8] = {256,320,512,640,368,384,512,640};

short           imageX0,imageX1;
short           imageY0,imageY1;
BOOL            bDisplayNotSet = TRUE;
GLuint          uiScanLine=0;
//int             iUseScanLines=0;
//int             lSelectedSlot=0;
unsigned char * pGfxCardScreen=0;
int              cardTexBufSize = 0;
int             iBlurBuffer=0;
int             iScanBlend=0;
int             iRenderFVR=0;
int             iNoScreenSaver=0;
unsigned int    ulGPUInfoVals[16];
int             iRumbleVal    = 0;
int             iRumbleTime   = 0;

static unsigned char clearLargeRange = 0;
static unsigned short largeRangeX1 = 0;
static unsigned short largeRangeX2 = 0;
static unsigned short largeRangeY1 = 0;
static unsigned short largeRangeY2 = 0;

static unsigned short uploadAreaX1 = 0;
static unsigned short uploadAreaX2 = 0;
static unsigned short uploadAreaY1 = 0;
static unsigned short uploadAreaY2 = 0;

static unsigned short screenX = 0;
static unsigned short screenY = 0;
static unsigned short screenX1 = 320;
static unsigned short screenY1 = 240;
static unsigned short screenWidth = 320;
static unsigned short screenHeight = 240;
BOOL    canClearFrameBuf = FALSE;

static BOOL    needUploadScreen = FALSE;
static BOOL    uploadedScreen = FALSE;
static BOOL    needFlipEGL = FALSE;
static unsigned short    RGB24Uploaded = 0;
extern u32 hSyncCount, frame_counter;   /* psxcounters.c: 0 in the picture, 240+ in the vblank (trace); vblanks */
static unsigned short    GPUupdateLace5Flg = 0;

// When display window / display mode has just changed, PreviousPSXDisplay may be stale.
// Skip CheckAgainstScreen() once to avoid matching the wrong previous screen area.
static BOOL    skipPreviousDisplayCheckOnce = FALSE;

#define CHECK_SCREEN_INFO() { \
    screenX = PSXDisplay.DisplayPosition.x; \
    screenY = PSXDisplay.DisplayPosition.y; \
    screenWidth = PSXDisplay.DisplayModeNew.x; \
    screenHeight = PSXDisplay.DisplayModeNew.y; \
    screenX1 = screenX + screenWidth; \
    screenY1 = screenY + screenHeight; \
}

#define CLEAR_SCREEN(x0, y0, x1, y1)  (((screenY1 - 1) <= y1) && (screenY >= y0) && ((screenX1 - 1) <= x1) && (screenX >= x0))

#define INRANGE(x1, x2, y1, y2) ((y2 <= largeRangeY2) && (y1 >= largeRangeY1) && (x2 <= largeRangeX2) && (x1 >= largeRangeX1))

static short   texChgType = 0;

static void efb_reset(void);   /* efbSync.inc, used by gpuDraw.c */
static inline unsigned short ReadGXRGB5A3PixelRaw(
    const unsigned char *buf, int texWidth, int px, int py);
static inline unsigned short GXRGB5A3ToPSX15(unsigned short gx);
extern GXRModeObj *vmode;     /*** Graphics Mode Object ***/

#include "gpuDraw.c"
#include "gpuTexture.c"
#include "efbSync.inc"


#include "gpuPrim.c"

static void flipEGL(void);
extern void (*ogx_draw_submitted_cb)(void);
extern void ogx_state_invalidate(void);   /* deps/opengx/gc_gl.c: the GX state cache */

/* PERF_PROF_GPUSPLIT: the parts of a present, timed from here on (perf.log "gpupres:"). */
#if PERF_PROF_GPUSPLIT
static int UploadScreen_t(int p)
{ int r; PERF_TIME(pres_upload_ticks, r = UploadScreen(p)); g_perf.pres_uploads++; return r; }
static void PrepareFullScreenUpload_t(int p)
{ PERF_TIME(pres_prep_ticks, PrepareFullScreenUpload(p)); }
static int gx_vout_render_t(short c)
{ int r; PERF_TIME(pres_vout_ticks, r = gx_vout_render(c)); return r; }
#define UploadScreen UploadScreen_t
#define PrepareFullScreenUpload PrepareFullScreenUpload_t
#define gx_vout_render gx_vout_render_t
#endif

////////////////////////////////////////////////////////////////////////
// stuff to make this a true PDK module
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// snapshot funcs (saves screen to bitmap / text infos into file)
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// save text infos to file
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// GPU INIT... here starts it all (first func called by emu)
////////////////////////////////////////////////////////////////////////

#define VRAM_SIZE ((1024 * 512 * 2) + 4096)
#define VRAM_ALIGN 16
static uint16_t *vram_ptr_orig = NULL;
// The real definition (gpulib/gpulib.c) uses its own, larger VRAM_SIZE/VRAM_ALIGN
// (deliberately doubled there for an overdraw guard), so declaring a size here would
// just be a second, smaller, and wrong bound on the same object. Only &globalVram[0]
// is ever taken in this file -- never subscripted or sizeof'd -- so leave it unsized.
extern uint8_t globalVram[];

long CALLBACK GL_GPUinit()
{
memset(ulStatusControl,0,256*sizeof(unsigned long));

bChangeRes=FALSE;
bWindowMode=FALSE;

bKeepRatio = TRUE;
// different ways of accessing PSX VRAM

 //!!! ATTENTION !!!
 if (vram_ptr_orig == NULL)
 {
     //vram_ptr_orig = calloc(VRAM_SIZE + (VRAM_ALIGN-1), 1);
     vram_ptr_orig = (uint16_t *)&globalVram[0];
 }

psxVub = (unsigned char *)vram_ptr_orig;
//psxVsb=(signed char *)psxVub;
//psxVsw=(signed short *)psxVub;
//psxVsl=(signed long *)psxVub;
psxVuw=(unsigned short *)psxVub;
//psxVul=(unsigned long *)psxVub;

psxVuw_eom=psxVuw+1024*iGPUHeight;                    // pre-calc of end of vram

memset(vram_ptr_orig,0x00,VRAM_SIZE + (VRAM_ALIGN-1));
memset(ulGPUInfoVals,0x00,16*sizeof(unsigned long));

//InitFrameCap();                                       // init frame rate stuff

/* Loading another game runs this again, without a power cycle, and the fields the lines
 * below do not name kept the last game's values. updateDisplayIfChangedGl copies
 * InterlacedNew and RGB24New over the reset Interlaced and RGB24, so the next game started
 * "already interlaced": its switch to interlace never raised InterlacedTest, and every
 * line of its line-by-line screen writes went to UploadScreen as a 1-row upload, which is
 * skipped (FF7 after Medievil: 132960 upload calls, 0 done; FF7 first: 554, 553). Zero the
 * lot, as at power-on, then set what this plugin wants. */
memset(&PSXDisplay, 0, sizeof(PSXDisplay));
memset(&PreviousPSXDisplay, 0, sizeof(PreviousPSXDisplay));
memset(&xrUploadArea, 0, sizeof(xrUploadArea));
memset(&xrUploadAreaIL, 0, sizeof(xrUploadAreaIL));
bNeedInterlaceUpdate = bNeedRGB24Update = FALSE;
bNeedUploadAfter = bNeedUploadTest = bNeedWriteUpload = FALSE;
needUploadScreen = uploadedScreen = skipPreviousDisplayCheckOnce = FALSE;
RGB24Uploaded = 0;
GPUupdateLace5Flg = 0;
iLastRGB24 = 0;
clearMovieGarbageFlg = clearMovieGarbageCnt = 0;

PSXDisplay.RGB24        = 0;                          // init vars
PreviousPSXDisplay.RGB24= 0;
PSXDisplay.Interlaced   = 0;
PSXDisplay.InterlacedTest=0;
PSXDisplay.DrawOffset.x = 0;
PSXDisplay.DrawOffset.y = 0;
PSXDisplay.DrawArea.x0  = 0;
PSXDisplay.DrawArea.y0  = 0;
PSXDisplay.DrawArea.x1  = 320;
PSXDisplay.DrawArea.y1  = 240;
PSXDisplay.DisplayMode.x= 320;
PSXDisplay.DisplayMode.y= 240;
PSXDisplay.Disabled     = FALSE;
PreviousPSXDisplay.Range.x0 =0;
PreviousPSXDisplay.Range.x1 =0;
PreviousPSXDisplay.Range.y0 =0;
PreviousPSXDisplay.Range.y1 =0;
PSXDisplay.Range.x0=0;
PSXDisplay.Range.x1=0;
PSXDisplay.Range.y0=0;
PSXDisplay.Range.y1=0;
PreviousPSXDisplay.DisplayPosition.x = 1;
PreviousPSXDisplay.DisplayPosition.y = 1;
PSXDisplay.DisplayPosition.x = 1;
PSXDisplay.DisplayPosition.y = 1;
PreviousPSXDisplay.DisplayModeNew.y=0;
PSXDisplay.Double=1;
GPUdataRet=0x400;

PSXDisplay.DisplayModeNew.x=0;
PSXDisplay.DisplayModeNew.y=0;

//PreviousPSXDisplay.Height = PSXDisplay.Height = 239;

iDataWriteMode = DR_NORMAL;
/* Also as at power-on: no command half received and no busy countdown left from the last
 * game, which would change how often the next game's first status polls go round. */
iDataReadMode = DR_NORMAL;
gpuCommand = 0; gpuDataC = gpuDataP = 0;
iFakePrimBusy = 0;

// Reset transfer values, to prevent mis-transfer of data
memset(&VRAMWrite,0,sizeof(VRAMLoad_t));
memset(&VRAMRead,0,sizeof(VRAMLoad_t));

// device initialised already !
//lGPUstatusRet = 0x74000000;

STATUSREG = 0x14802000;
GPUIsIdle;
GPUIsReadyForCommands;

return 0;
}


////////////////////////////////////////////////////////////////////////
// OPEN interface func: attention!
// some emus are calling this func in their main Window thread,
// but all other interface funcs (to draw stuff) in a different thread!
// that's a problem, since OGL is thread safe! Therefore we cannot
// initialize the OGL stuff right here, we simply set a "bIsFirstFrame = TRUE"
// flag, to initialize OGL on the first real draw call.
// btw, we also call this open func ourselfes, each time when the user
// is changing between fullscreen/window mode (ENTER key)
// btw part 2: in windows the plugin gets the window handle from the
// main emu, and doesn't create it's own window (if it would do it,
// some PAD or SPU plugins would not work anymore)
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// I shot the sheriff... last function called from emu
////////////////////////////////////////////////////////////////////////

long CALLBACK GL_GPUshutdown()
{
 //if(psxVSecure) free(psxVSecure);                      // kill emulated vram memory
 //psxVSecure=0;

 if (pGfxCardScreen)
      {
          _mem2_free(pGfxCardScreen);
          pGfxCardScreen = 0;
      }

 vram_ptr_orig = NULL;

 return 0;
}

////////////////////////////////////////////////////////////////////////
// paint it black: simple func to clean up optical border garbage
////////////////////////////////////////////////////////////////////////

void PaintBlackBorders(void)
{
// short s;
// glDisable(GL_SCISSOR_TEST); glError();
// if(bTexEnabled) {glDisable(GL_TEXTURE_2D);bTexEnabled=FALSE;} glError();
// if(bOldSmoothShaded) {glShadeModel(GL_FLAT);bOldSmoothShaded=FALSE;} glError();
// if(bBlendEnable)     {glDisable(GL_BLEND);bBlendEnable=FALSE;} glError();
// glDisable(GL_ALPHA_TEST); glError();
//
// glEnable(GL_ALPHA_TEST); glError();
// glEnable(GL_SCISSOR_TEST); glError();

}

////////////////////////////////////////////////////////////////////////
// helper to draw scanlines
////////////////////////////////////////////////////////////////////////

//__inline void XPRIMdrawTexturedQuad(OGLVertex* vertex1, OGLVertex* vertex2,
//                                    OGLVertex* vertex3, OGLVertex* vertex4)
//{
//
//}

////////////////////////////////////////////////////////////////////////
// scanlines
////////////////////////////////////////////////////////////////////////

void SetScanLines(void)
{
}

////////////////////////////////////////////////////////////////////////
// blur, babe, blur (heavy performance hit for a so-so fullscreen effect)
////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////
// Update display (swap buffers)... called in interlaced mode on
// every emulated vsync, otherwise whenever the displayed screen region
// has been changed
////////////////////////////////////////////////////////////////////////

int iLastRGB24=0;                                      // special vars for checking when to skip two display updates
int iSkipTwo=0;

void GPUvSinc(void){
updateDisplayGl();
}

/* A flip by GP1 05 is presented once the display settings that come with it are in: PsyQ's
 * PutDispEnv writes GP1 05 and 08 together, and FF7 flips to its first 24-bit video frame
 * with 05 before 08 = 24-bit. Presented at the 05, that frame went out in 15-bit mode (green
 * blotches), and the 24-bit present from the 08 was skipped while that copy was in flight.
 * So 05 only marks it; the next GPU access other than GP1 06-08 presents it, or any present
 * that comes first takes its place. */
static int flip05Pending;
void updateDisplayGl(void);
static void flip05Flush(void)
{
 if (flip05Pending) updateDisplayGl();
}

/* The CPU stopped (menu, chained game, reset): the menu or the next game takes the screen,
 * and a present after the menu would show an EFB the menu has used. */
void GL_flip05Drop(void)
{
 flip05Pending = 0;
}

void updateDisplayGl(void)                               // UPDATE DISPLAY
{
BOOL bBlur=FALSE;
extern int ogx_tex_alloc_failed;

/* A texture allocation failed (MEM2 pool full, deps/opengx ogx_tex_alloc): drop every
 * cached texture here, before this frame draws, so the pool empties; the textures are
 * rebuilt from VRAM as the frame uses them. */
if (ogx_tex_alloc_failed)
 {
  ogx_tex_alloc_failed = 0;
  ResetTextureArea(TRUE);
  PERF_INC(ogx_tex_flush);
 }

if (flip05Pending)
 {
  flip05Pending = 0;
  skipPreviousDisplayCheckOnce = TRUE;
 }


bFakeFrontBuffer=FALSE;
bRenderFrontBuffer=FALSE;

//if(iRenderFVR)                                        // frame buffer read fix mode still active?
// {
//  iRenderFVR--;                                       // -> if some frames in a row without read access: turn off mode
//  if(!iRenderFVR) bFullVRam=FALSE;
// }

if(iLastRGB24 && iLastRGB24!=PSXDisplay.RGB24+1)      // (mdec) garbage check
 {
  iSkipTwo=2;                                         // -> skip two frames to avoid garbage if color mode changes
 }
iLastRGB24=0;

if(PSXDisplay.RGB24)// && !bNeedUploadAfter)          // (mdec) upload wanted?
 {
      PrepareFullScreenUpload(-1);
      UploadScreen(TRUE);                                 // -> the displayed buffer, from psx vram (PrepareFullScreenUpload)
  bNeedUploadTest=FALSE;
  bNeedInterlaceUpdate=FALSE;
  bNeedUploadAfter=FALSE;
  bNeedRGB24Update=FALSE;
 }
else
if(bNeedInterlaceUpdate)                              // smaller upload?
 {
     #ifdef DISP_DEBUG
     //sprintf(txtbuffer, "updateDisplayGl_2 %d %d %d %d %d %d %d %d %d\r\n", PSXDisplay.Disabled, lClearOnSwap, iZBufferDepth, PSXDisplay.Interlaced, bNeedRGB24Update, xrUploadArea.x0, xrUploadArea.x1, xrUploadArea.y0, xrUploadArea.y1);
     //writeLogFile(txtbuffer);
     #endif // DISP_DEBUG
  bNeedInterlaceUpdate=FALSE;
  xrUploadArea=xrUploadAreaIL;                        // -> upload this rect
  UploadScreen(TRUE);
 }


if(PreviousPSXDisplay.Range.x0||                      // paint black borders around display area, if needed
   PreviousPSXDisplay.Range.y0)
 PaintBlackBorders();

if(PSXDisplay.Disabled)                               // display disabled?
 {
  // moved here
  glDisable(GL_SCISSOR_TEST); glError();
  glClearColor2(0,0,0,128); glError();                 // -> clear whole backbuffer
  glClear(uiBufferBits); glError();
  glEnable(GL_SCISSOR_TEST); glError();
  gl_z=0.0f;
  bDisplayNotSet = TRUE;
  #ifdef DISP_DEBUG
  if (logFileEnabled()) {
  sprintf(txtbuffer, "updateDisplayGl Disabled\r\n");
  //DEBUG_print(txtbuffer, DBG_CDR1);
  writeLogFile(txtbuffer);
  }
  #endif // DISP_DEBUG

  //gc_vout_disabled();
  //return;
 }

if(iSkipTwo)                                          // we are in skipping mood?
 {
  iSkipTwo--;
  iDrawnSomething=0;                                  // -> simply lie about something drawn
 }

//if(iBlurBuffer && !bSkipNextFrame)                    // "blur display" activated?
// {BlurBackBuffer();bBlur=TRUE;}                       // -> blur it

// if(iUseScanLines) SetScanLines();                     // "scan lines" activated? do it

// if(usCursorActive) ShowGunCursor();                   // "gun cursor" wanted? show 'em

//if(dwActFixes&128)                                    // special FPS limitation mode?
// {
//  if(bUseFrameLimit) PCFrameCap();                    // -> ok, do it
////   if(bUseFrameSkip || ulKeybits&KEY_SHOWFPS)
//   PCcalcfps();
// }

// if(gTexPicName) DisplayPic();                         // some gpu info picture active? display it

// if(bSnapShot) DoSnapShot();                           // snapshot key pressed? cheeeese :)

// if(ulKeybits&KEY_SHOWFPS)                             // wanna see FPS?
 {
//   sprintf(szDispBuf,"%06.1f",fps_cur);
//   DisplayText();                                      // -> show it
 }

//----------------------------------------------------//
// main buffer swapping (well, or skip it)

if(UseFrameSkip)                                     // frame skipping active ?
 {
  if(!bSkipNextFrame)
   {
    if(iDrawnSomething)     flipEGL();
   }
//    if((fps_skip < fFrameRateHz) && !(bSkipNextFrame))
//     {bSkipNextFrame = TRUE; fps_skip=fFrameRateHz;}
//    else bSkipNextFrame = FALSE;

 }
else                                                  // no skip ?
 {
  if(iDrawnSomething)  flipEGL();
 }

iDrawnSomething=0;

//----------------------------------------------------//

//if(lClearOnSwap)                                      // clear buffer after swap?
// {
//     #ifdef DISP_DEBUG
//     sprintf(txtbuffer, "updateDisplayGl lClearOnSwap\r\n");
//     //DEBUG_print(txtbuffer, DBG_CDR1);
//     writeLogFile(txtbuffer);
//     #endif // DISP_DEBUG
//
//  unsigned char g,b,r;
//
//  if(bDisplayNotSet)                                  // -> set new vals
//   SetOGLDisplaySettings(1);
//
//  // lClearOnSwapColor (BGR)
//  g=((unsigned char)GREEN(lClearOnSwapColor));      // -> get col
//  b=((unsigned char)BLUE(lClearOnSwapColor));
//  r=((unsigned char)RED(lClearOnSwapColor));
//  glDisable(GL_SCISSOR_TEST); glError();
//  glClearColor2(r,g,b,128); glError();                 // -> clear
//  glClear(uiBufferBits); glError();
//  glEnable(GL_SCISSOR_TEST); glError();
//  lClearOnSwap=0;                                     // -> done
// }
//else
// {
////  if(bBlur) UnBlurBackBuffer();                       // unblur buff, if blurred before
//
//  if(iZBufferDepth)                                   // clear zbuffer as well (if activated)
//   {
//       #ifdef DISP_DEBUG
//     sprintf(txtbuffer, "Not lClearOnSwap\r\n");
//     //DEBUG_print(txtbuffer, DBG_CDR1);
//     writeLogFile(txtbuffer);
//     #endif // DISP_DEBUG
//
//    //glDisable(GL_SCISSOR_TEST); glError();
//    //glClear(GL_DEPTH_BUFFER_BIT); glError();
//    //glEnable(GL_SCISSOR_TEST); glError();
//   }
// }

gl_z=0.0f;

//----------------------------------------------------//
// additional uploads immediatly after swapping

if(bNeedUploadAfter)                                  // upload wanted?
 {
  bNeedUploadAfter=FALSE;
  bNeedUploadTest=FALSE;
      #ifdef DISP_DEBUG
      if (logFileEnabled()) {
      sprintf(txtbuffer, "bNeedUploadAfter %d %d %d %d\r\n", xrUploadArea.x0, xrUploadArea.x1, xrUploadArea.y0, xrUploadArea.y1);
      //DEBUG_print(txtbuffer, DBG_CDR2);
      writeLogFile(txtbuffer);
      }
      #endif // DISP_DEBUG
  UploadScreen(-1);                                   // -> upload
 }

if(bNeedUploadTest)
 {
  bNeedUploadTest=FALSE;
  if(PSXDisplay.InterlacedTest &&
     //iOffscreenDrawing>2 &&
     PreviousPSXDisplay.DisplayPosition.x==PSXDisplay.DisplayPosition.x &&
     PreviousPSXDisplay.DisplayEnd.x==PSXDisplay.DisplayEnd.x &&
     PreviousPSXDisplay.DisplayPosition.y==PSXDisplay.DisplayPosition.y &&
     PreviousPSXDisplay.DisplayEnd.y==PSXDisplay.DisplayEnd.y)
   {
       #ifdef DISP_DEBUG
       if (logFileEnabled()) {
       sprintf(txtbuffer, "bNeedUploadTest %d %d %d %d\r\n", xrUploadArea.x0, xrUploadArea.x1, xrUploadArea.y0, xrUploadArea.y1);
       //DEBUG_print(txtbuffer, DBG_CDR2);
       writeLogFile(txtbuffer);
       }
       #endif // DISP_DEBUG

    PrepareFullScreenUpload(TRUE);
    UploadScreen(TRUE);
   }
 }

//----------------------------------------------------//
// rumbling (main emu pad effect)

//if(iRumbleTime)                                       // shake screen by modifying view port
// {
//  int i1=0,i2=0,i3=0,i4=0;
//
//  iRumbleTime--;
//  if(iRumbleTime)
//   {
//    i1=((rand()*iRumbleVal)/RAND_MAX)-(iRumbleVal/2);
//    i2=((rand()*iRumbleVal)/RAND_MAX)-(iRumbleVal/2);
//    i3=((rand()*iRumbleVal)/RAND_MAX)-(iRumbleVal/2);
//    i4=((rand()*iRumbleVal)/RAND_MAX)-(iRumbleVal/2);
//   }
//
//  #ifdef DISP_DEBUG
//       sprintf(txtbuffer, "iRumbleTime\r\n");
//       writeLogFile(txtbuffer);
//       #endif // DISP_DEBUG
//  glViewport(rRatioRect.left+i1,
//             iResY-(rRatioRect.top+rRatioRect.bottom)+i2,
//             rRatioRect.right+i3,
//             rRatioRect.bottom+i4); glError();
// }

//----------------------------------------------------//



// if(ulKeybits&KEY_RESETTEXSTORE) ResetStuff();         // reset on gpu mode changes? do it before next frame is filled
}

////////////////////////////////////////////////////////////////////////
// update front display: smaller update func, if something has changed
// in the frontbuffer... dirty, but hey... real men know no pain
////////////////////////////////////////////////////////////////////////

//void updateFrontDisplayGl(void)
//{
//if(PreviousPSXDisplay.Range.x0||
//   PreviousPSXDisplay.Range.y0)
// PaintBlackBorders();
//
////if(iBlurBuffer) BlurBackBuffer();
//
////if(iUseScanLines) SetScanLines();
//
//// if(usCursorActive) ShowGunCursor();
//
//bFakeFrontBuffer=FALSE;
//bRenderFrontBuffer=FALSE;
//
//// if(gTexPicName) DisplayPic();
//// if(ulKeybits&KEY_SHOWFPS) DisplayText();
//
//if(iDrawnSomething)                                   // linux:
//      flipEGL();
//
//
////if(iBlurBuffer) UnBlurBackBuffer();
//}

////////////////////////////////////////////////////////////////////////
// check if update needed
////////////////////////////////////////////////////////////////////////
void ChangeDispOffsetsXGl(void)                          // CENTER X
{
long lx,l;short sO;

if(!PSXDisplay.Range.x1) return;                      // some range given?

l=PSXDisplay.DisplayMode.x;

l*=(long)PSXDisplay.Range.x1;                         // some funky calculation
l/=2560;lx=l;l&=0xfffffff8;

if(l==PreviousPSXDisplay.Range.x1) return;            // some change?

sO=PreviousPSXDisplay.Range.x0;                       // store old

if(lx>=PSXDisplay.DisplayMode.x)                      // range bigger?
 {
  PreviousPSXDisplay.Range.x1=                        // -> take display width
   PSXDisplay.DisplayMode.x;
  PreviousPSXDisplay.Range.x0=0;                      // -> start pos is 0
 }
else                                                  // range smaller? center it
 {
  PreviousPSXDisplay.Range.x1=l;                      // -> store width (8 pixel aligned)
   PreviousPSXDisplay.Range.x0=                       // -> calc start pos
   (PSXDisplay.Range.x0-500)/8;
  if(PreviousPSXDisplay.Range.x0<0)                   // -> we don't support neg. values yet
   PreviousPSXDisplay.Range.x0=0;

  if((PreviousPSXDisplay.Range.x0+lx)>                // -> uhuu... that's too much
     PSXDisplay.DisplayMode.x)
   {
    PreviousPSXDisplay.Range.x0=                      // -> adjust start
     PSXDisplay.DisplayMode.x-lx;
    PreviousPSXDisplay.Range.x1+=lx-l;                // -> adjust width
   }
 }

if(sO!=PreviousPSXDisplay.Range.x0)                   // something changed?
 {
  bDisplayNotSet=TRUE;                                // -> recalc display stuff
 }
}

////////////////////////////////////////////////////////////////////////

void ChangeDispOffsetsYGl(void)                          // CENTER Y
{
int iT;short sO;                                      // store previous y size

if(PSXDisplay.PAL) iT=48; else iT=28;                 // different offsets on PAL/NTSC

if(PSXDisplay.Range.y0>=iT)                           // crossed the security line? :)
 {
  PreviousPSXDisplay.Range.y1=                        // -> store width
   PSXDisplay.DisplayModeNew.y;

  sO=(PSXDisplay.Range.y0-iT-4)*PSXDisplay.Double;    // -> calc offset
  if(sO<0) sO=0;

  PSXDisplay.DisplayModeNew.y+=sO;                    // -> add offset to y size, too
 }
else sO=0;                                            // else no offset

if(sO!=PreviousPSXDisplay.Range.y0)                   // something changed?
 {
  PreviousPSXDisplay.Range.y0=sO;
  bDisplayNotSet=TRUE;                                // -> recalc display stuff
 }
}

////////////////////////////////////////////////////////////////////////
// Aspect ratio of ogl screen: simply adjusting ogl view port
////////////////////////////////////////////////////////////////////////

void SetAspectRatio(void)
{
float xs,ys,s;RECT r;

if(!PSXDisplay.DisplayModeNew.x) return;
if(!PSXDisplay.DisplayModeNew.y) return;

#if 0
xs=(float)iResX/(float)PSXDisplay.DisplayModeNew.x;
ys=(float)iResY/(float)height;

s=min(xs,ys);
r.right =(int)((float)PSXDisplay.DisplayModeNew.x*s);
r.bottom=(int)((float)height*s);
if(r.right  > iResX) r.right  = iResX;
if(r.bottom > iResY) r.bottom = iResY;
if(r.right  < 1)     r.right  = 1;
if(r.bottom < 1)     r.bottom = 1;

r.left = (iResX-r.right)/2;
r.top  = (iResY-r.bottom)/2;
if(r.bottom<rRatioRect.bottom ||
   r.right <rRatioRect.right)
 {
  RECT rC;
  glClearColor2(0,0,0,128);

  if(r.right <rRatioRect.right)
   {
    rC.left=0;
    rC.top=0;
    rC.right=r.left;
    rC.bottom=iResY;
    glScissor(rC.left,rC.top,rC.right,rC.bottom);
    glClear(uiBufferBits);
    rC.left=iResX-rC.right;
    glScissor(rC.left,rC.top,rC.right,rC.bottom);

    glClear(uiBufferBits);
   }

  if(r.bottom <rRatioRect.bottom)
   {
    rC.left=0;
    rC.top=0;
    rC.right=iResX;
    rC.bottom=r.top;
    glScissor(rC.left,rC.top,rC.right,rC.bottom);

    glClear(uiBufferBits);
    rC.top=iResY-rC.bottom;
    glScissor(rC.left,rC.top,rC.right,rC.bottom);
    glClear(uiBufferBits);
   }

  bSetClip=TRUE;
  bDisplayNotSet=TRUE;
 }

rRatioRect=r;
#else
 // pcsx-rearmed hack
 //if (rearmed_get_layer_pos != NULL)
 //  rearmed_get_layer_pos(&rRatioRect.left, &rRatioRect.top, &rRatioRect.right, &rRatioRect.bottom);
  glScissor(rRatioRect.left,
           iResY-(rRatioRect.top+rRatioRect.bottom),
           rRatioRect.right,rRatioRect.bottom);
#endif

glViewport(rRatioRect.left,
           iResY-(rRatioRect.top+rRatioRect.bottom),
           rRatioRect.right,
           rRatioRect.bottom);               // init viewport
}

////////////////////////////////////////////////////////////////////////
// big ass check, if an ogl swap buffer is needed
////////////////////////////////////////////////////////////////////////

/* The EFB size 240p mode (the TVMode setting) needs for the current PlayStation display:
 * 240 lines for a display of 288 lines or fewer, 480 otherwise or with 240p off. Returns
 * TRUE when iResX/iResY changed. This used to run only when the menu's 240p button had just
 * been pressed (displayModeChanged), while GL_GPUopen() resets the size to 640x480 at every
 * game start and switchToTVMode() puts the TV in 240p whenever the setting is on: with 240p
 * saved in the settings, or on any game after the first, the EFB was drawn at 480 lines
 * for a 240-line TV mode, and the picture was garbage (a purple screen in Spyro, a
 * shifted, cropped one in Crash 3) until the button was pressed again. */
/* switchToTVMode() for the current display, counted in perf.log ("tvmode:"). */
static void SwitchTVModeForDisplay(void)
{
#ifdef PERF_PROF
 if (g_perf.tv_calls < 8)
  {
   g_perf.tv_log[g_perf.tv_calls][0] = PSXDisplay.DisplayModeNew.x;
   g_perf.tv_log[g_perf.tv_calls][1] = PSXDisplay.DisplayModeNew.y;
   g_perf.tv_log[g_perf.tv_calls][2] = (unsigned short)frame_counter;
  }
#endif
 PERF_INC(tv_calls);
 PERF_SET(tv_w, PSXDisplay.DisplayModeNew.x);
 PERF_SET(tv_h, PSXDisplay.DisplayModeNew.y);
 PERF_SET(tv_y0, PSXDisplay.Range.y0);
 PERF_SET(tv_y1, PSXDisplay.Range.y1);
 PERF_SET(tv_height, PSXDisplay.Height);
 PERF_SET(tv_double, PSXDisplay.Double);
 gx_vout_wait_idle();
 switchToTVMode(PSXDisplay.DisplayModeNew.x, PSXDisplay.DisplayModeNew.y, 0);
}

static BOOL TVModeResolution(void)
{
 int x = 640, y = 480;
 if (originalMode == ORIGINALMODE_ENABLE && PSXDisplay.DisplayModeNew.y <= 288)
  {
   x = (PSXDisplay.DisplayModeNew.x <= 320) ? 640 : PSXDisplay.DisplayModeNew.x;
   y = 240;
  }
 displayModeChanged = 0;
 if (x == iResX && y == iResY)
  return FALSE;
 iResX = x;
 iResY = y;
 rRatioRect.right  = iResX;
 rRatioRect.bottom = iResY;
 return TRUE;
}

void updateDisplayIfChangedGl(void)
{
BOOL bUp;

if ((PSXDisplay.DisplayMode.y == PSXDisplay.DisplayModeNew.y) &&
    (PSXDisplay.DisplayMode.x == PSXDisplay.DisplayModeNew.x))
 {
  if((PSXDisplay.RGB24      == PSXDisplay.RGB24New) &&
     (PSXDisplay.Interlaced == PSXDisplay.InterlacedNew))
     return;                                          // nothing has changed? fine, no swap buffer needed

  if (PSXDisplay.RGB24 != PSXDisplay.RGB24New)
   {
    efb_before_geometry_change();
   }
 }
else                                                  // some res change?
 {
    efb_before_geometry_change();

    if (originalMode == ORIGINALMODE_ENABLE)
		SwitchTVModeForDisplay();
    TVModeResolution();   /* 240 or 480 lines, from the TVMode setting */

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity(); glError();
  glOrtho(0,PSXDisplay.DisplayModeNew.x,              // -> new psx resolution
            PSXDisplay.DisplayModeNew.y, 0, -1, 1); glError();
  #ifdef DISP_DEBUG
  if (logFileEnabled()) {
  sprintf(txtbuffer, "DisplayChanged glOrtho %d %d\r\n", PSXDisplay.DisplayModeNew.x, PSXDisplay.DisplayModeNew.y);
  writeLogFile(txtbuffer);
  sprintf(txtbuffer, "DisplayChanged GX_SetScissor %d %d %d %d\r\n", rRatioRect.left,
           iResY-(rRatioRect.top+rRatioRect.bottom),
           rRatioRect.right,rRatioRect.bottom);
  writeLogFile(txtbuffer);
  }
  #endif // DISP_DEBUG
  if(bKeepRatio) SetAspectRatio();
 }

bDisplayNotSet = TRUE;                                // re-calc offsets/display area

bUp=FALSE;
if(PSXDisplay.RGB24!=PSXDisplay.RGB24New)             // clean up textures, if rgb mode change (usually mdec on/off)
 {
  PreviousPSXDisplay.RGB24=0;                         // no full 24 frame uploaded yet
  ResetTextureArea(FALSE);
  bUp=TRUE;
 }
 #ifdef DISP_DEBUG
  if (logFileEnabled()) {
  sprintf(txtbuffer, "updateDisplayIfChangedGl %d %d\r\n", PSXDisplay.RGB24, PSXDisplay.RGB24New);
  //DEBUG_print(txtbuffer, DBG_SPU3);
  writeLogFile(txtbuffer);
  }
  #endif // DISP_DEBUG

PSXDisplay.RGB24         = PSXDisplay.RGB24New;       // get new infos
PSXDisplay.DisplayMode.y = PSXDisplay.DisplayModeNew.y;
PSXDisplay.DisplayMode.x = PSXDisplay.DisplayModeNew.x;
PSXDisplay.Interlaced    = PSXDisplay.InterlacedNew;

PSXDisplay.DisplayEnd.x=                              // calc new ends
 PSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
PSXDisplay.DisplayEnd.y=
 PSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;
PreviousPSXDisplay.DisplayEnd.x=
 PreviousPSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
PreviousPSXDisplay.DisplayEnd.y=
 PreviousPSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;

ChangeDispOffsetsXGl();

if(iFrameLimit==2) SetAutoFrameCap();                 // set new fps limit vals (depends on interlace)

 if(bUp)
{
    #ifdef DISP_DEBUG
    if (logFileEnabled()) {
    sprintf(txtbuffer, "updateDisplayIfChangedGl swap buffer\r\n");
    writeLogFile(txtbuffer);
    }
    #endif // DISP_DEBUG
    updateDisplayGl();                              // yeah, real update (swap buffer)
}

}

////////////////////////////////////////////////////////////////////////
// window mode <-> fullscreen mode (windows)
////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////
// swap update check (called by psx vsync function)
////////////////////////////////////////////////////////////////////////

//BOOL bSwapCheck(void)
//{
//static int iPosCheck=0;
//static PSXPoint_t pO;
//static PSXPoint_t pD;
//static int iDoAgain=0;
//
//if(PSXDisplay.DisplayPosition.x==pO.x &&
//   PSXDisplay.DisplayPosition.y==pO.y &&
//   PSXDisplay.DisplayEnd.x==pD.x &&
//   PSXDisplay.DisplayEnd.y==pD.y)
//     iPosCheck++;
//else iPosCheck=0;
//
//pO=PSXDisplay.DisplayPosition;
//pD=PSXDisplay.DisplayEnd;
//
//if(iPosCheck<=4) return FALSE;
//
//iPosCheck=4;
//
//if(PSXDisplay.Interlaced) return FALSE;
//
//if (bNeedInterlaceUpdate||
//    bNeedRGB24Update ||
//    bNeedUploadAfter||
//    bNeedUploadTest ||
//    iDoAgain
//   )
// {
//  iDoAgain=0;
//  if(bNeedUploadAfter)
//   iDoAgain=1;
//  if(bNeedUploadTest && PSXDisplay.InterlacedTest)
//   iDoAgain=1;
//
//  bDisplayNotSet = TRUE;
//  updateDisplayGl();
//
//  PreviousPSXDisplay.DisplayPosition.x=PSXDisplay.DisplayPosition.x;
//  PreviousPSXDisplay.DisplayPosition.y=PSXDisplay.DisplayPosition.y;
//  PreviousPSXDisplay.DisplayEnd.x=PSXDisplay.DisplayEnd.x;
//  PreviousPSXDisplay.DisplayEnd.y=PSXDisplay.DisplayEnd.y;
//  pO=PSXDisplay.DisplayPosition;
//  pD=PSXDisplay.DisplayEnd;
//
//  return TRUE;
// }
//
//return FALSE;
//}
////////////////////////////////////////////////////////////////////////
// gun cursor func: player=0-7, x=0-511, y=0-255
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// update lace is called every VSync. Basically we limit frame rate
// here, and in interlaced mode we swap ogl display buffers.
////////////////////////////////////////////////////////////////////////

#define CALLBACK
extern void CALLBACK GPUsetframelimit(unsigned long option);
static unsigned short usFirstPos=2;

void CALLBACK GL_GPUupdateLace(void)
{
flip05Flush();
if(!(dwActFixes&AUTO_FIX_CHRONO_CROSS))
 STATUSREG^=0x80000000;                               // interlaced bit toggle, if the CC game fix is not active (see gpuReadStatus)

    /* -1 is a sentinel no frameLimit value takes, so the limiter is always
     * applied on the first lace. It used to start at 1 == FRAMELIMIT_AUTO,
     * the default, so with AUTO from boot the compare was never true and
     * GPUsetframelimit() never ran: this plugin then inherited whatever
     * UseFrameLimit the previous plugin's close left behind (0), and ran
     * unthrottled at ~180% while the FPS display sat clamped at 59.94. */
    static char oldframeLimit = -1;

    if ( frameLimit[0] != oldframeLimit)
        GPUsetframelimit(0);
    oldframeLimit = frameLimit[0];

//if(!(dwActFixes&128))                                 // normal frame limit func
 OldGpuCheckFrameRate();

//if(iOffscreenDrawing==4)                              // special check if high offscreen drawing is on
// {
//  if(bSwapCheck()) return;
// }

if(PSXDisplay.Interlaced)                             // interlaced mode?
 {
  if(PSXDisplay.DisplayMode.x>0 && PSXDisplay.DisplayMode.y>0)
   {
       #ifdef DISP_DEBUG
       if (logFileEnabled()) {
       sprintf ( txtbuffer, "GPUupdateLace1 %d %x\r\n", iDrawnSomething, RGB24Uploaded);
       writeLogFile ( txtbuffer );
       }
       #endif // DISP_DEBUG
       updateDisplayGl();                                  // -> swap buffers (new frame)
   }
 }
else if(usFirstPos==1)                                // initial updates (after startup)
 {
     #ifdef DISP_DEBUG
    if (logFileEnabled()) {
    sprintf ( txtbuffer, "GPUupdateLace3\r\n");
    writeLogFile ( txtbuffer );
    }
    #endif // DISP_DEBUG
  updateDisplayGl();
 }
 else
 {
     #ifdef DISP_DEBUG
     if (logFileEnabled()) {
     sprintf ( txtbuffer, "GPUupdateLace5 %x %d %d %d %x\r\n", iDrawnSomething, PSXDisplay.Interlaced, PSXDisplay.Disabled, PSXDisplay.InterlacedTest, RGB24Uploaded);
     writeLogFile ( txtbuffer );
     }
     #endif // DISP_DEBUG
     GPUupdateLace5Flg = 0;
     {
     int fullUp = CheckFullScreenUpload() ? 1 : 0;
     /* vramio F1, one per vblank: x,y display, w,h draw offset, a = iDrawnSomething,
      * b = 1 full-screen upload, 2 a present was pending, 4 presented here, 8 drawing into
      * the displayed buffer */
     int why = fullUp | (needFlipEGL == TRUE ? 2 : 0) | (bDrawFrontBuffer ? 8 : 0);
     int drawn = iDrawnSomething;
     /* drawing into the displayed buffer: no flip will come, so what was drawn is shown
      * at the vblank (as a CRT scans VRAM out). Not for a 24-bit display: its picture
      * reaches the EFB only as the upload at each flip, and the present after that clears
      * the EFB, so a vblank present showed the game's tiles over black between the video's
      * frames (FF7's opening video flashed black on every other frame, since ec16be0). */
     if (fullUp || (needFlipEGL == TRUE && (iDrawnSomething & 0x1) == 0) ||
         (bDrawFrontBuffer && !PSXDisplay.RGB24 && (iDrawnSomething & 0x11)))
     {
         GPUupdateLace5Flg = 1;
         flipEGL();
         iDrawnSomething = 0;
         why |= 4;
     }
     perf_vram_event(0xF1, PSXDisplay.DisplayPosition.x, PSXDisplay.DisplayPosition.y,
                     PSXDisplay.GDrawOffset.x, PSXDisplay.GDrawOffset.y, drawn, why);
     }
 }
}

////////////////////////////////////////////////////////////////////////
// process read request from GPU status register
////////////////////////////////////////////////////////////////////////

unsigned long CALLBACK GL_GPUreadStatus(void)
{
if(dwActFixes&AUTO_FIX_CHRONO_CROSS)                                 // CC game fix
 {
  static int iNumRead=0;
  if((iNumRead++)==2)
   {
    iNumRead=0;
    STATUSREG^=0x80000000;                            // interlaced bit toggle... we do it on every second read status... needed by some games (like ChronoCross)
   }
 }

if(iFakePrimBusy)                                     // 27.10.2007 - emulating some 'busy' while drawing... pfff... not perfect, but since our emulated dma is not done in an extra thread...
 {
  iFakePrimBusy--;

  if(iFakePrimBusy&1)                                 // we do a busy-idle-busy-idle sequence after/while drawing prims
   {
    GPUIsBusy;
    GPUIsNotReadyForCommands;
   }
  else
   {
    GPUIsIdle;
    GPUIsReadyForCommands;
   }
 }

return STATUSREG;
}

////////////////////////////////////////////////////////////////////////
// processes data send to GPU status register
// these are always single packet commands.
////////////////////////////////////////////////////////////////////////

/* PERF_PROF_GPUSPLIT times GP1 and GPUREAD through wrappers named like the entry points,
 * which keep the entry points own names, so the plugin table and every caller use them. */
static void GL_GPUwriteStatus_(unsigned long gdata);
static unsigned long GL_GPUreadData_(void);
/* PERF_PROF_GPUSPLIT: the three things a display flip does, timed from here on. */
#if PERF_PROF_GPUSPLIT
static void updateDisplayGl_t(void)
{ PERF_TIME(flip_present_ticks, updateDisplayGl()); g_perf.flip_presents++; }
#define updateDisplayGl updateDisplayGl_t
#endif
void CALLBACK GL_GPUwriteStatus(unsigned long gdata)
{
#if PERF_PROF_GPUSPLIT
 PERF_TIME(gpu_gp1_ticks, GL_GPUwriteStatus_(gdata));
 g_perf.gpu_gp1_calls++;
#else
 GL_GPUwriteStatus_(gdata);
#endif
}
unsigned long CALLBACK GL_GPUreadData(void)
{
 unsigned long r;
#if PERF_PROF_GPUSPLIT
 PERF_TIME(gpu_read_ticks, r = GL_GPUreadData_());
 g_perf.gpu_read_calls++;
#else
 r = GL_GPUreadData_();
#endif
 return r;
}

static void GL_GPUwriteStatus_(unsigned long gdata)
{
unsigned long lCommand=(gdata>>24)&0xff;

if(bIsFirstFrame) GLinitialize(NULL, NULL);           // real ogl startup (needed by some emus)

if (lCommand < 0x06 || lCommand > 0x08) flip05Flush();

ulStatusControl[lCommand]=gdata;

switch(lCommand)
 {
  //--------------------------------------------------//
  // reset gpu
  case 0x00:
   memset(ulGPUInfoVals,0x00,16*sizeof(unsigned long));
   lGPUstatusRet=0x14802000;
   PSXDisplay.Disabled=1;
   iDataWriteMode=iDataReadMode=DR_NORMAL;
   PSXDisplay.DrawOffset.x=PSXDisplay.DrawOffset.y=0;
   drawX=drawY=0;drawW=drawH=0;
   sSetMask=0;lSetMask=0;bCheckMask=FALSE;iSetMask=0;
   usMirror=0;
   GlobalTextAddrX=0;GlobalTextAddrY=0;
   GlobalTextTP=0;GlobalTextABR=0;
   PSXDisplay.RGB24=FALSE;
   PSXDisplay.Interlaced=FALSE;
   bUsingTWin = FALSE;
   return;

  // dis/enable display
  case 0x03:
   PreviousPSXDisplay.Disabled = PSXDisplay.Disabled;
   PSXDisplay.Disabled = (gdata & 1);
   perf_prim_trace(0xF3, 0, 0, gdata, (int)(gdata & 1), 0, (int)hSyncCount, 0);   /* F3 = display on (0) / off (1); x1 = scanline */

   if(PSXDisplay.Disabled)
        STATUSREG|=GPUSTATUS_DISPLAYDISABLED;
   else STATUSREG&=~GPUSTATUS_DISPLAYDISABLED;

   if (iOffscreenDrawing==4 &&
        PreviousPSXDisplay.Disabled &&
       !(PSXDisplay.Disabled))
    {

     if(!PSXDisplay.RGB24)
      {
       PrepareFullScreenUpload(TRUE);
       #ifdef DISP_DEBUG
       if (logFileEnabled()) {
       sprintf(txtbuffer, "dis/enable display %d %d %d %d\r\n", xrUploadArea.x0, xrUploadArea.x1, xrUploadArea.y0, xrUploadArea.y1);
       //DEBUG_print(txtbuffer, DBG_CDR2);
       writeLogFile(txtbuffer);
       }
       #endif // DISP_DEBUG
       UploadScreen(TRUE);
       updateDisplayGl();
      }
    }

   return;

  // setting transfer mode
  case 0x04:
   gdata &= 0x03;                                     // only want the lower two bits

   iDataWriteMode=iDataReadMode=DR_NORMAL;
   if(gdata==0x02) iDataWriteMode=DR_VRAMTRANSFER;
   if(gdata==0x03) iDataReadMode =DR_VRAMTRANSFER;

   STATUSREG&=~GPUSTATUS_DMABITS;                     // clear the current settings of the DMA bits
   STATUSREG|=(gdata << 29);                          // set the DMA bits according to the received data

   return;

  // setting display position
  case 0x05:
   perf_prim_trace(0xF5, 0, 0, gdata, (int)(gdata & 0x3ff), (int)((gdata >> 10) & 0x1ff), (int)hSyncCount, 0);
   {
    short sx=(short)(gdata & 0x3ff);
    short sy;

    if(iGPUHeight==1024)
     {
      if(dwGPUVersion==2)
           sy = (short)((gdata>>12)&0x3ff);
      else sy = (short)((gdata>>10)&0x3ff);
     }
    else sy = (short)((gdata>>10)&0x3ff);             // really: 0x1ff, but we adjust it later

    if (sy & 0x200)
     {
      sy|=0xfc00;
      PreviousPSXDisplay.DisplayModeNew.y=sy/PSXDisplay.Double;
      sy=0;
     }
    else PreviousPSXDisplay.DisplayModeNew.y=0;

    if(sx>1000) sx=0;

    if(usFirstPos)
     {
      usFirstPos--;
      if(usFirstPos)
       {
        efb_before_geometry_change();

        PreviousPSXDisplay.DisplayPosition.x = sx;
        PreviousPSXDisplay.DisplayPosition.y = sy;
        PSXDisplay.DisplayPosition.x = sx;
        PSXDisplay.DisplayPosition.y = sy;

       }
     }

     {
      if((!PSXDisplay.Interlaced) &&
         PSXDisplay.DisplayPosition.x == sx  &&
         PSXDisplay.DisplayPosition.y == sy)
       return;
      efb_before_geometry_change();

      PreviousPSXDisplay.DisplayPosition.x = PSXDisplay.DisplayPosition.x;
      PreviousPSXDisplay.DisplayPosition.y = PSXDisplay.DisplayPosition.y;
      PSXDisplay.DisplayPosition.x = sx;
      PSXDisplay.DisplayPosition.y = sy;
     }

    PSXDisplay.DisplayEnd.x=
     PSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
    PSXDisplay.DisplayEnd.y=
     PSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;

    PreviousPSXDisplay.DisplayEnd.x=
     PreviousPSXDisplay.DisplayPosition.x+ PSXDisplay.DisplayMode.x;
    PreviousPSXDisplay.DisplayEnd.y=
     PreviousPSXDisplay.DisplayPosition.y+ PSXDisplay.DisplayMode.y+PreviousPSXDisplay.DisplayModeNew.y;


    bDisplayNotSet = TRUE;

    if (!(PSXDisplay.Interlaced))
     {
         #ifdef DISP_DEBUG
         if (logFileEnabled()) {
         sprintf(txtbuffer, "settingDispInfo05 %d %d %d %d\r\n", PSXDisplay.DisplayPosition.x, PSXDisplay.DisplayPosition.y, PSXDisplay.DisplayMode.x * PSXDisplay.Range.x1 / 2560, PSXDisplay.Height);
         //DEBUG_print(txtbuffer, DBG_CDR2);
         writeLogFile(txtbuffer);
         }
         #endif // DISP_DEBUG
         CHECK_SCREEN_INFO();

         /* vramio F5: x,y new display, w,h draw offset, a = iDrawnSomething,
          * b = 1 presented (updateDisplayGl) */
         if (GPUupdateLace5Flg && (iDrawnSomething & ~0x4) == 0)
         {
             perf_vram_event(0xF5, sx, sy, PSXDisplay.GDrawOffset.x, PSXDisplay.GDrawOffset.y, iDrawnSomething, 0);
         }
         else
         {
             perf_vram_event(0xF5, sx, sy, PSXDisplay.GDrawOffset.x, PSXDisplay.GDrawOffset.y, iDrawnSomething, 1);
             flip05Pending = 1;
         }
     }
    else
    if(PSXDisplay.InterlacedTest &&
       ((PreviousPSXDisplay.DisplayPosition.x != PSXDisplay.DisplayPosition.x)||
        (PreviousPSXDisplay.DisplayPosition.y != PSXDisplay.DisplayPosition.y)))
     PSXDisplay.InterlacedTest--;

    return;
   }

  // setting width
  case 0x06:
   perf_prim_trace(0xF6, 0, 0, gdata, (int)(gdata & 0xfff), (int)((gdata >> 12) & 0xfff), 0, 0);
   {
    short oldRangeX0 = PSXDisplay.Range.x0;
    short oldRangeX1 = PSXDisplay.Range.x1;

    PSXDisplay.Range.x0=gdata & 0x7ff;      //0x3ff;
    PSXDisplay.Range.x1=(gdata>>12) & 0xfff;//0x7ff;

    PSXDisplay.Range.x1-=PSXDisplay.Range.x0;

    if (oldRangeX0 != PSXDisplay.Range.x0 ||
        oldRangeX1 != PSXDisplay.Range.x1)
        efb_before_geometry_change();

    CHECK_SCREEN_INFO();
    #ifdef DISP_DEBUG
      if (logFileEnabled()) {
      sprintf(txtbuffer, "settingDispInfo06 width %d %d\r\n", screenWidth, screenHeight);
      writeLogFile(txtbuffer);
      }
      #endif // DISP_DEBUG
    ChangeDispOffsetsXGl();


    return;
   }

  // setting height
  case 0x07:
   perf_prim_trace(0xF7, 0, 0, gdata, (int)(gdata & 0x3ff), (int)((gdata >> 10) & 0x3ff), 0, 0);
   {

    PreviousPSXDisplay.Height = PSXDisplay.Height;

    PSXDisplay.Range.y0=gdata & 0x3ff;
    PSXDisplay.Range.y1=(gdata>>10) & 0x3ff;

    PSXDisplay.Height = PSXDisplay.Range.y1 -
                        PSXDisplay.Range.y0 +
                        PreviousPSXDisplay.DisplayModeNew.y;

    if (PreviousPSXDisplay.Height != PSXDisplay.Height)
     {
      efb_before_geometry_change();

      PSXDisplay.DisplayModeNew.y=PSXDisplay.Height*PSXDisplay.Double;
      ChangeDispOffsetsYGl();

      #ifdef DISP_DEBUG
      if (logFileEnabled()) {
      sprintf(txtbuffer, "settingDispInfo07 height %d %d\r\n", screenWidth, screenHeight);
      writeLogFile(txtbuffer);
      }
      #endif // DISP_DEBUG
      CHECK_SCREEN_INFO();

      skipPreviousDisplayCheckOnce = TRUE;
      updateDisplayIfChangedGl();

     }

    return;
   }

  // setting display infos
  case 0x08:
   perf_prim_trace(0xF8, 0, 0, gdata, 0, 0, (int)hSyncCount, 0);   /* x1 = scanline: 0 in the picture, 240+ in the vblank */
   {

    efb_before_geometry_change();

    PSXDisplay.DisplayModeNew.x = dispWidths[(gdata & 0x03) | ((gdata & 0x40) >> 4)];

   if (gdata&0x04) PSXDisplay.Double=2;
   else            PSXDisplay.Double=1;
   PSXDisplay.DisplayModeNew.y = PSXDisplay.Height*PSXDisplay.Double;

   ChangeDispOffsetsYGl();

   PSXDisplay.PAL           = (gdata & 0x08)?TRUE:FALSE; // if 1 - PAL mode, else NTSC
   PSXDisplay.RGB24New      = (gdata & 0x10)?TRUE:FALSE; // if 1 - TrueColor
   PSXDisplay.InterlacedNew = ((gdata & 0x24) ^ 0x24)?FALSE:TRUE; // if 0 - Interlace

   STATUSREG&=~GPUSTATUS_WIDTHBITS;                   // clear the width bits

   STATUSREG|=
              (((gdata & 0x03) << 17) |
              ((gdata & 0x40) << 10));                // set the width bits

   PreviousPSXDisplay.InterlacedNew=FALSE;
   if (PSXDisplay.InterlacedNew)
    {
     if(!PSXDisplay.Interlaced)
      {
       PSXDisplay.InterlacedTest=2;
       PreviousPSXDisplay.DisplayPosition.x = PSXDisplay.DisplayPosition.x;
       PreviousPSXDisplay.DisplayPosition.y = PSXDisplay.DisplayPosition.y;
       PreviousPSXDisplay.InterlacedNew=TRUE;
      }

     STATUSREG|=GPUSTATUS_INTERLACED;
    }
   else
    {
     PSXDisplay.InterlacedTest=0;
     STATUSREG&=~GPUSTATUS_INTERLACED;
    }

   if (PSXDisplay.PAL)
        STATUSREG|=GPUSTATUS_PAL;
   else STATUSREG&=~GPUSTATUS_PAL;

   if (PSXDisplay.Double==2)
        STATUSREG|=GPUSTATUS_DOUBLEHEIGHT;
   else STATUSREG&=~GPUSTATUS_DOUBLEHEIGHT;

   if (PSXDisplay.RGB24New)
        STATUSREG|=GPUSTATUS_RGB24;
   else STATUSREG&=~GPUSTATUS_RGB24;

     CHECK_SCREEN_INFO();
     #ifdef DISP_DEBUG
     if (logFileEnabled()) {
     sprintf(txtbuffer, "settingDispInfo08 %d %d %d %d\r\n", PSXDisplay.DisplayPosition.x, PSXDisplay.DisplayPosition.y, screenWidth, screenHeight);
     writeLogFile(txtbuffer);
     }
     #endif // DISP_DEBUG

   skipPreviousDisplayCheckOnce = TRUE;
   updateDisplayIfChangedGl();


   return;
   }

  //--------------------------------------------------//
  // ask about GPU version and other stuff
  case 0x10:

   gdata&=0xff;

   switch(gdata)
    {
     case 0x02:
      GPUdataRet=ulGPUInfoVals[INFO_TW];              // tw infos
      return;
     case 0x03:
      GPUdataRet=ulGPUInfoVals[INFO_DRAWSTART];       // draw start
      return;
     case 0x04:
      GPUdataRet=ulGPUInfoVals[INFO_DRAWEND];         // draw end
      return;
     case 0x05:
     case 0x06:
      GPUdataRet=ulGPUInfoVals[INFO_DRAWOFF];         // draw offset
      return;
     case 0x07:
      if(dwGPUVersion==2)
           GPUdataRet=0x01;
      else GPUdataRet=0x02;                           // gpu type
      return;
     case 0x08:
     case 0x0F:                                       // some bios addr?
      GPUdataRet=0xBFC03720;
      return;
    }
   return;
  //--------------------------------------------------//
 }
}

////////////////////////////////////////////////////////////////////////
// vram read/write helpers
////////////////////////////////////////////////////////////////////////

BOOL bNeedWriteUpload=FALSE;

/* The lowest and highest pixel the current CPU->VRAM transfer changed (the copy loop in
 * GL_GPUwriteDataMem); none while cmax is NULL. CheckWriteUpdate uses them. */
unsigned short *vw_cmin = (unsigned short *)~(uintptr_t)0, *vw_cmax = NULL;
int vw_tracked = 1;   /* 0: some of the transfer ran without noting changes: take it whole */

#if PERF_PROF_GPUSPLIT
static unsigned vw_changed;   /* pixels the current CPU->VRAM transfer changed */
static void vw_dirty(const unsigned short *p)   /* grow the changed-pixel box (FF7 probe) */
{
 int o = (int)(p - psxVuw), x = o & 1023, y = o >> 10;
 if (g_perf.vw_dx1 <= g_perf.vw_dx0) { g_perf.vw_dx0 = x; g_perf.vw_dy0 = y; g_perf.vw_dx1 = x + 1; g_perf.vw_dy1 = y + 1; return; }
 if (x < g_perf.vw_dx0) g_perf.vw_dx0 = x;
 if (y < g_perf.vw_dy0) g_perf.vw_dy0 = y;
 if (x + 1 > g_perf.vw_dx1) g_perf.vw_dx1 = x + 1;
 if (y + 1 > g_perf.vw_dy1) g_perf.vw_dy1 = y + 1;
}
#endif

static __inline void FinishedVRAMWrite(void)
{
#if PERF_PROF_GPUSPLIT
 if (vw_changed) g_perf.vload_changed++; else g_perf.vload_same++;
 g_perf.vload_px_changed += vw_changed;
 vw_changed = 0;
#endif
#if PERF_PROF_GPUSPLIT
 unsigned long long fv_t0_ = perf_now_ticks();
 g_perf.gpu_vramfin_calls++;
#endif
 efb_cpu_write(VRAMWrite.x, VRAMWrite.y, VRAMWrite.Width, VRAMWrite.Height);   /* efbSync.inc */

 if(bNeedWriteUpload)
  {
   bNeedWriteUpload=FALSE;
#if PERF_PROF_GPUSPLIT
   { unsigned long long cw_t0_ = perf_now_ticks();
#endif
   CheckWriteUpdate();
#if PERF_PROF_GPUSPLIT
   g_perf.gpu_cwu_ticks += perf_now_ticks() - cw_t0_; }
#endif
  }
 vw_cmin = (unsigned short *)~(uintptr_t)0;   /* the next transfer starts with none */
 vw_cmax = NULL;
 vw_tracked = 1;

 // set register to NORMAL operation
 iDataWriteMode = DR_NORMAL;

 // reset transfer values, to prevent mis-transfer of data
 VRAMWrite.ColsRemaining = 0;
 VRAMWrite.RowsRemaining = 0;
#if PERF_PROF_GPUSPLIT
 g_perf.gpu_vramfin_ticks += perf_now_ticks() - fv_t0_;
#endif
}

static __inline void FinishedVRAMRead(void)
{
 // set register to NORMAL operation
 iDataReadMode = DR_NORMAL;
 // reset transfer values, to prevent mis-transfer of data
 VRAMRead.x = 0;
 VRAMRead.y = 0;
 VRAMRead.Width = 0;
 VRAMRead.Height = 0;
 VRAMRead.ColsRemaining = 0;
 VRAMRead.RowsRemaining = 0;

 // indicate GPU is no longer ready for VRAM data in the STATUS REGISTER
 STATUSREG&=~GPUSTATUS_READYFORVRAM;
}

////////////////////////////////////////////////////////////////////////
// vram read check ex (reading from card's back/frontbuffer if needed...
// slow!)
////////////////////////////////////////////////////////////////////////

void CheckVRamReadEx(int x, int y, int dx, int dy)
{
    #ifdef DISP_DEBUG
    //sprintf(txtbuffer, "CheckVRamReadEx  \r\n");
    //DEBUG_print(txtbuffer, DBG_CORE2);
    #endif // DISP_DEBUG
}

////////////////////////////////////////////////////////////////////////
// vram read check (reading from card's back/frontbuffer if needed...
// slow!)
////////////////////////////////////////////////////////////////////////

// don't do GL vram read
//void CheckVRamRead(int x, int y, int dx, int dy, bool bFront)
//{
//}

void RestoreDispCopyInfo(void)
{
    /* The mode on the TV now: in 240p that is switchToTVMode()'s, not the menu's vmode,
     * which put the display copy back to 480 lines after every EFB capture. The source goes
     * first, because GX_SetDispCopyYScale() reads its height. */
    extern GXRModeObj *g_tv_mode;   /* Gamecube/libgui/GraphicsGX.cpp */
    GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
    GX_SetDispCopySrc(0,0,m->fbWidth,m->efbHeight);
    float yscale = GX_GetYScaleFactor(m->efbHeight,m->xfbHeight);
    int xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0,0,m->fbWidth,m->efbHeight);
    GX_SetDispCopyDst(VIDEO_PadFramebufferWidth(m->fbWidth),xfbHeight);
    // Honour the user's Deflicker setting rather than forcing it on: this runs
    // after every EFB snapshot capture in the VRAM-readback path, i.e. during
    // gameplay, so hardcoding GX_TRUE here silently re-enabled deflicker on
    // this plugin whenever a readback happened. (GraphicsGX.cpp deliberately
    // forces GX_TRUE for menu / return-to-menu contexts -- that stays as is.)
    GX_SetCopyFilter(m->aa,m->sample_pattern,(deflickerFilter)?GX_TRUE:GX_FALSE,m->vfilter);
    GX_SetFieldMode(m->field_rendering,((m->viHeight==2*m->xfbHeight)?GX_ENABLE:GX_DISABLE));
}

static inline unsigned short ReadGXRGB5A3PixelRaw(const unsigned char* buf, int texWidth, int px, int py)
{
    int blocksPerRow = texWidth >> 2;
    int blockIndex   = (py >> 2) * blocksPerRow + (px >> 2);
    int blockOffset  = blockIndex << 5;
    int pixelOffset  = (((py & 3) << 2) + (px & 3)) << 1;

    const unsigned char* p = buf + blockOffset + pixelOffset;

    return (unsigned short)(((unsigned short)p[0] << 8) | (unsigned short)p[1]);
}

static inline unsigned short GXRGB5A3ToPSX15(unsigned short gx)
{
    unsigned short psx;

    if (gx & 0x8000)
    {
        unsigned short r5 = (gx >> 10) & 0x1F;
        unsigned short g5 = (gx >> 5) & 0x1F;
        unsigned short b5 = gx & 0x1F;

        /* GX RGB5A3 stores RGB from high to low bits, while PS1 VRAM
         * stores red in bits 0-4 and blue in bits 10-14. */
        psx = (unsigned short)(r5 | (g5 << 5) | (b5 << 10));
    }
    else
    {
        unsigned short r4 = (gx >> 8) & 0xF;
        unsigned short g4 = (gx >> 4) & 0xF;
        unsigned short b4 = (gx >> 0) & 0xF;

        unsigned short r5 = (r4 << 1) | (r4 >> 3);
        unsigned short g5 = (g4 << 1) | (g4 >> 3);
        unsigned short b5 = (b4 << 1) | (b4 >> 3);
        psx = (unsigned short)(r5 | (g5 << 5) | (b5 << 10));
    }

    return psx;
}

static inline void CheckVRamRead(int x, int y, int dx, int dy)
{
 efb_sync(x, y, dx - x, dy - y);
}

////////////////////////////////////////////////////////////////////////
// core read from vram
////////////////////////////////////////////////////////////////////////

void CALLBACK GL_GPUreadDataMem(unsigned long * pMem, int iSize)
{
int i;

flip05Flush();

if(iDataReadMode!=DR_VRAMTRANSFER) return;

GPUIsBusy;


// adjust read ptr, if necessary
while(VRAMRead.ImagePtr>=psxVuw_eom)
 VRAMRead.ImagePtr-=iGPUHeight*1024;
while(VRAMRead.ImagePtr<psxVuw)
 VRAMRead.ImagePtr+=iGPUHeight*1024;

//if((iSize>1) &&
//   !(VRAMRead.x      == VRAMWrite.x     &&
//     VRAMRead.y      == VRAMWrite.y     &&
//     VRAMRead.Width  == VRAMWrite.Width &&
//     VRAMRead.Height == VRAMWrite.Height))
// if (iSize > 1)
// CheckVRamRead(VRAMRead.x,VRAMRead.y,
//               VRAMRead.x+VRAMRead.RowsRemaining,
//               VRAMRead.y+VRAMRead.ColsRemaining);

for(i=0;i<iSize;i++)
 {
  // do 2 seperate 16bit reads for compatibility (wrap issues)
  if ((VRAMRead.ColsRemaining > 0) && (VRAMRead.RowsRemaining > 0))
   {
    // lower 16 bit
    GPUdataRet=(unsigned long)GETLE16(VRAMRead.ImagePtr);

    VRAMRead.ImagePtr++;
    if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
    VRAMRead.RowsRemaining --;

    if(VRAMRead.RowsRemaining<=0)
     {
      VRAMRead.RowsRemaining = VRAMRead.Width;
      VRAMRead.ColsRemaining--;
      VRAMRead.ImagePtr += 1024 - VRAMRead.Width;
      if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
     }

    // higher 16 bit (always, even if it's an odd width)
    GPUdataRet|=(unsigned long)GETLE16(VRAMRead.ImagePtr)<<16;
    PUTLE32(pMem, GPUdataRet); pMem++;

    if(VRAMRead.ColsRemaining <= 0)
     {FinishedVRAMRead();goto ENDREAD_GL;}

    VRAMRead.ImagePtr++;
    if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
    VRAMRead.RowsRemaining--;
    if(VRAMRead.RowsRemaining<=0)
     {
      VRAMRead.RowsRemaining = VRAMRead.Width;
      VRAMRead.ColsRemaining--;
      VRAMRead.ImagePtr += 1024 - VRAMRead.Width;
      if(VRAMRead.ImagePtr>=psxVuw_eom) VRAMRead.ImagePtr-=iGPUHeight*1024;
     }
    if(VRAMRead.ColsRemaining <= 0)
     {FinishedVRAMRead();goto ENDREAD_GL;}
   }
  else {FinishedVRAMRead();goto ENDREAD_GL;}
 }

ENDREAD_GL:
GPUIsIdle;
 #ifdef DISP_DEBUG
 //sprintf(txtbuffer, "GL_GPUreadDataMem %08x \r\n", GPUdataRet);
 //writeLogFile(txtbuffer);
 #endif // DISP_DEBUG
}

static unsigned long GL_GPUreadData_(void)
{
 unsigned long l;
 GL_GPUreadDataMem(&l,1);
 return GPUdataRet;
}

////////////////////////////////////////////////////////////////////////
// helper table to know how much data is used by drawing commands
////////////////////////////////////////////////////////////////////////
extern const unsigned char primTableCX[];
//const unsigned char primTableCX[256] =
//{
//    // 00
//    0,0,3,0,0,0,0,0,
//    // 08
//    0,0,0,0,0,0,0,0,
//    // 10
//    0,0,0,0,0,0,0,0,
//    // 18
//    0,0,0,0,0,0,0,0,
//    // 20
//    4,4,4,4,7,7,7,7,
//    // 28
//    5,5,5,5,9,9,9,9,
//    // 30
//    6,6,6,6,9,9,9,9,
//    // 38
//    8,8,8,8,12,12,12,12,
//    // 40
//    3,3,3,3,0,0,0,0,
//    // 48
////    5,5,5,5,6,6,6,6,      //FLINE
//    254,254,254,254,254,254,254,254,
//    // 50
//    4,4,4,4,0,0,0,0,
//    // 58
////    7,7,7,7,9,9,9,9,    //    LINEG3    LINEG4
//    255,255,255,255,255,255,255,255,
//    // 60
//    3,3,3,3,4,4,4,4,    //    TILE    SPRT
//    // 68
//    2,2,2,2,3,3,3,3,    //    TILE1
//    // 70
//    2,2,2,2,3,3,3,3,
//    // 78
//    2,2,2,2,3,3,3,3,
//    // 80
//    4,0,0,0,0,0,0,0,
//    // 88
//    0,0,0,0,0,0,0,0,
//    // 90
//    0,0,0,0,0,0,0,0,
//    // 98
//    0,0,0,0,0,0,0,0,
//    // a0
//    3,0,0,0,0,0,0,0,
//    // a8
//    0,0,0,0,0,0,0,0,
//    // b0
//    0,0,0,0,0,0,0,0,
//    // b8
//    0,0,0,0,0,0,0,0,
//    // c0
//    3,0,0,0,0,0,0,0,
//    // c8
//    0,0,0,0,0,0,0,0,
//    // d0
//    0,0,0,0,0,0,0,0,
//    // d8
//    0,0,0,0,0,0,0,0,
//    // e0
//    0,1,1,1,1,1,1,0,
//    // e8
//    0,0,0,0,0,0,0,0,
//    // f0
//    0,0,0,0,0,0,0,0,
//    // f8
//    0,0,0,0,0,0,0,0
//};

////////////////////////////////////////////////////////////////////////
// processes data send to GPU data register
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// Off-screen primitives -> software rasterizer.
//
// GX only renders what lands in the display buffers; a primitive whose
// destination is VRAM outside both buffers never reaches the EFB and the
// software copy of VRAM (psxVuw) does not get it either. Games and the BIOS
// shell draw into such areas on purpose -- the shell renders its spheres and
// button splashes off-screen and then paints them as 15-bit textures, which
// this port then sampled as black. Those primitives are rasterized here with
// the New Soft renderer (SoftGPU/gpulib_if.c, same psxVuw), preceded by the
// current E1..E6 state so its clip/texture/offset match. Lines are left to
// GX (their word count is only known to the primitive itself).
////////////////////////////////////////////////////////////////////////

/* do_cmd_list() comes from ../gpulib/gpu.h (SoftGPU/gpulib_if.c implements it) */
static uint32_t g_gp0TexWindow;            /* last GP0 E2 word (little-endian) */

static inline int Gp0SignExtend11(unsigned int v)
{
    return ((int)(v & 0x7FF) << 21) >> 21;
}

static int OffscreenPrimBounds(unsigned char cmd, const unsigned long *d, int n,
                               int *bx0, int *by0, int *bx1, int *by1)
{
    int x0 = 4096, y0 = 4096, x1 = -4096, y1 = -4096;
    int k, nv, stride, idx;
    uint32_t w;

    if (cmd >= 0x20 && cmd <= 0x3F)
    {
        nv = (cmd & 0x08) ? 4 : 3;
        stride = 1 + ((cmd & 0x04) ? 1 : 0) + ((cmd & 0x10) ? 1 : 0);
        if (1 + (nv - 1) * stride >= n) return 0;
        for (k = 0, idx = 1; k < nv; k++, idx += stride)
        {
            int x, y;
            w = GETLE32(&d[idx]);
            x = Gp0SignExtend11(w) + PSXDisplay.DrawOffset.x;
            y = Gp0SignExtend11(w >> 16) + PSXDisplay.DrawOffset.y;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x + 1 > x1) x1 = x + 1;
            if (y + 1 > y1) y1 = y + 1;
        }
    }
    else if (cmd >= 0x60 && cmd <= 0x7F)
    {
        int sw, sh;
        if (n < 2) return 0;
        w = GETLE32(&d[1]);
        x0 = Gp0SignExtend11(w) + PSXDisplay.DrawOffset.x;
        y0 = Gp0SignExtend11(w >> 16) + PSXDisplay.DrawOffset.y;
        switch ((cmd >> 3) & 3)
        {
            case 0:
                idx = 2 + ((cmd & 0x04) ? 1 : 0);
                if (idx >= n) return 0;
                w = GETLE32(&d[idx]);
                sw = w & 0x3FF; sh = (w >> 16) & 0x1FF;
                break;
            case 1: sw = sh = 1; break;
            case 2: sw = sh = 8; break;
            default: sw = sh = 16; break;
        }
        x1 = x0 + sw; y1 = y0 + sh;
    }
    else
        return 0;

    if (x1 <= x0 || y1 <= y0) return 0;
    *bx0 = x0; *by0 = y0; *bx1 = x1; *by1 = y1;
    return 1;
}

/* GX draws into the EFB, so there it no longer mirrors psxVuw. A primitive inside the
 * displayed rectangle only grows mirror_gx (the box the next partial upload also covers,
 * gpuPrim.c CheckWriteUpdate), from the first upload of a mirror (ogx_efb_mirror 2) on:
 * FF7's logos draw one fade band a frame over the field they upload, so the mirror never
 * got whole and each of those frames was uploaded whole. Anything else ends the mirror. */
int mirror_gx_x0, mirror_gx_y0, mirror_gx_x1, mirror_gx_y1, mirror_gx_any;
static void mirror_gx_draw(int x0, int y0, int x1, int y1)
{
    x0 = max(x0, PSXDisplay.DrawArea.x0); y0 = max(y0, PSXDisplay.DrawArea.y0);
    x1 = min(x1, PSXDisplay.DrawArea.x1 + 1); y1 = min(y1, PSXDisplay.DrawArea.y1 + 1);
    if (x1 <= x0 || y1 <= y0 || !ogx_efb_mirror)
        return;               /* clipped away; or no mirror, and the uploads that make one cover it */
    if (PSXDisplay.RGB24 ||
        x0 < PSXDisplay.DisplayPosition.x || y0 < PSXDisplay.DisplayPosition.y ||
        x1 > PSXDisplay.DisplayEnd.x || y1 > PSXDisplay.DisplayEnd.y)
    {
        MIRROR_OFF(1);
        return;
    }
    /* GX may fill the right and bottom edge a PS1 leaves out: one pixel more each way */
    x0 = max(x0 - 1, PSXDisplay.DisplayPosition.x); y0 = max(y0 - 1, PSXDisplay.DisplayPosition.y);
    x1 = min(x1 + 1, PSXDisplay.DisplayEnd.x);      y1 = min(y1 + 1, PSXDisplay.DisplayEnd.y);
    if (!mirror_gx_any)
    {
        mirror_gx_x0 = x0; mirror_gx_y0 = y0; mirror_gx_x1 = x1; mirror_gx_y1 = y1;
        mirror_gx_any = 1;
        return;
    }
    mirror_gx_x0 = min(mirror_gx_x0, x0); mirror_gx_y0 = min(mirror_gx_y0, y0);
    mirror_gx_x1 = max(mirror_gx_x1, x1); mirror_gx_y1 = max(mirror_gx_y1, y1);
}

/* Before a GX primitive (20h-7Fh) from the display list: it writes the buffer being drawn.
 * The display offset is applied first, as the primitive's offset function would, so the EFB
 * sync sees the buffer it lands in; its bounds come from its GP0 words (lines, whose bounds
 * are not worked out here, take the drawing area). */
static void EfbBeforeGxCommand(unsigned char cmd, unsigned long *data, int n)
{
    int x0, y0, x1, y1;

    if (cmd < 0x20 || cmd > 0x7F)
        return;
    if (bDisplayNotSet)
        SetOGLDisplaySettings(1);
    if (!OffscreenPrimBounds(cmd, data, n, &x0, &y0, &x1, &y1))
    {
        x0 = PSXDisplay.DrawArea.x0; y0 = PSXDisplay.DrawArea.y0;
        x1 = PSXDisplay.DrawArea.x1 + 1; y1 = PSXDisplay.DrawArea.y1 + 1;
    }
    efb_gx_draw(x0, y0, x1, y1);
    mirror_gx_draw(x0, y0, x1, y1);
}

/* Returns 1 when the primitive was consumed by the software rasterizer. */
static int OffscreenSoftDraw(unsigned char cmd, unsigned long *data, int n)
{
    uint32_t list[6 + 16];
    int x0, y0, x1, y1, k, cs = 0, cl = 0, lc = 0;
    EfbGeom g;

    if (cmd < 0x20 || cmd > 0x7F || (cmd >= 0x40 && cmd <= 0x5F) || n > 16)
        return 0;
    /* no display set up yet (boot): nothing is "off-screen" */
    if (PSXDisplay.DisplayEnd.x <= PSXDisplay.DisplayPosition.x ||
        PSXDisplay.DisplayEnd.y <= PSXDisplay.DisplayPosition.y)
        return 0;
    /* The usual case: the drawing area is inside the buffer GX draws. Then a primitive
     * either reaches that buffer or is clipped away (see below), so none is drawn here, and
     * its bounds need not be worked out. */
    if (bDisplayNotSet)
        SetOGLDisplaySettings(1);
    efb_geom_now(&g);
    if (!efb_geom_ok(&g))
        return 0;
    if (PSXDisplay.DrawArea.x0 >= g.x && PSXDisplay.DrawArea.x1 < g.x + g.w &&
        PSXDisplay.DrawArea.y0 >= g.y && PSXDisplay.DrawArea.y1 < g.y + g.h)
    {
        PERF_INC(off_soft_inside);
        return 0;
    }
    if (!OffscreenPrimBounds(cmd, data, n, &x0, &y0, &x1, &y1))
        return 0;
    /* GX draws only the buffer the EFB holds (efbSync.inc "live": the one at GDrawOffset);
     * a primitive anywhere else -- off-screen, or in the displayed buffer while the game
     * draws the other one (Ape Escape darkening its paused frame) -- is drawn in software
     * into VRAM. It used to go to GX unless it missed both display buffers, and GX put a
     * primitive outside live at coordinates outside the EFB: it was lost. */
    if (x0 < g.x + g.w && x1 > g.x && y0 < g.y + g.h && y1 > g.y)
        return 0;
    /* The GPU clips every primitive to the drawing area (E4's end is inclusive), so only
     * that part can change VRAM. A primitive whose box went past the edge of VRAM used to
     * be sent to GX, which never writes VRAM: MediEvil's headstone draws its menu text
     * off-screen that way and lost part of every letter. */
    if (x0 < PSXDisplay.DrawArea.x0) x0 = PSXDisplay.DrawArea.x0;
    if (y0 < PSXDisplay.DrawArea.y0) y0 = PSXDisplay.DrawArea.y0;
    if (x1 > PSXDisplay.DrawArea.x1 + 1) x1 = PSXDisplay.DrawArea.x1 + 1;
    if (y1 > PSXDisplay.DrawArea.y1 + 1) y1 = PSXDisplay.DrawArea.y1 + 1;
    if (x1 <= x0 || y1 <= y0)
    {
        PERF_INC(off_soft_rejected);   /* draws nothing: GX culls it too */
        return 0;
    }

    /* E1 texpage/dither/mask-out-of-status, E2 window, E3/E4 draw area,
     * E5 offset, E6 mask -- the state the software primitive functions read */
    PUTLE32(&list[0], 0xE1000000u | (lGPUstatusRet & 0x7FFu) | (((lGPUstatusRet >> 15) & 1u) << 11));
    list[1] = g_gp0TexWindow ? g_gp0TexWindow : 0;
    if (!list[1]) PUTLE32(&list[1], 0xE2000000u);
    PUTLE32(&list[2], 0xE3000000u | ((uint32_t)(PSXDisplay.DrawArea.y0 & 0x3FF) << 10) | (PSXDisplay.DrawArea.x0 & 0x3FF));
    PUTLE32(&list[3], 0xE4000000u | ((uint32_t)(PSXDisplay.DrawArea.y1 & 0x3FF) << 10) | (PSXDisplay.DrawArea.x1 & 0x3FF));
    PUTLE32(&list[4], 0xE5000000u | ((uint32_t)(PSXDisplay.DrawOffset.y & 0x7FF) << 11) | (PSXDisplay.DrawOffset.x & 0x7FF));
    PUTLE32(&list[5], 0xE6000000u | ((lGPUstatusRet >> 11) & 3u));
    for (k = 0; k < n; k++)
        list[6 + k] = (uint32_t)data[k];          /* already little-endian (PUTLE32 on receive) */

    /* the pixels under it may be GX's (a frame in a snapshot): VRAM gets them first, so a
     * semi-transparent primitive blends with the real picture and the tiles it touches
     * can be marked as written by the CPU without losing GX pixels next to it */
    efb_sync(x0, y0, x1 - x0, y1 - y0);

    do_cmd_list(list, 6 + n, &cs, &cl, &lc);

    ogx_inv_src = 6;
    InvalidateTextureArea(x0, y0, x1 - x0, y1 - y0);
    efb_cpu_write(x0, y0, x1 - x0, y1 - y0);
    PERF_INC(off_soft_prims);
#if PERF_PROF_GPU
    perf_prim_trace(0xC8, cmd & 0x04 ? 2 : 0, 0, (unsigned)cmd, x0, y0, x1, y1);   /* C8 = software-rasterized off-screen primitive */
#endif
    return 1;
}

/* PERF_PROF_GPUSPLIT: time the parts of the loop below that do the work. Whatever is
 * left over is the reading of the display list itself, charged to `parse` at the end, so
 * the parts add up to the whole call. */
#if PERF_PROF_GPUSPLIT
static unsigned long long gp_last_;     /* what the last GPU_PART took */
#define GPU_PART(field, calls, expr) do { \
	unsigned long long gp_t0_ = perf_now_ticks(); \
	expr; \
	gp_last_ = perf_now_ticks() - gp_t0_; \
	g_perf.field += gp_last_; \
	gp_other_ += gp_last_; \
	g_perf.calls++; \
} while (0)
#else
#define GPU_PART(field, calls, expr) do { expr; } while (0)
#endif

void CALLBACK GL_GPUwriteDataMem(unsigned long * pMem, int iSize)
{
unsigned char command;
unsigned long gdata=0;

flip05Flush();
int i=0;
#if PERF_PROF_GPUSPLIT
/* The image transfer is a loop with a goto through it, so it is timed by hand rather
 * than with GPU_PART: opened where the transfer starts, closed at ENDVRAM_GL. */
unsigned long long gp_call_ = perf_now_ticks(), gp_other_ = 0, gp_vram_t0_ = 0;
int gp_vram_i0_ = 0, gp_vram_open_ = 0;
#endif
GPUIsBusy;
GPUIsNotReadyForCommands;

STARTVRAM_GL:

if(iDataWriteMode==DR_VRAMTRANSFER)
 {
#if PERF_PROF_GPUSPLIT
  gp_vram_t0_ = perf_now_ticks(); gp_vram_i0_ = i; gp_vram_open_ = 1;
#endif
//    #if defined(DISP_DEBUG)
//    sprintf ( txtbuffer, "GPUwriteDataMem DR_VRAMTRANSFER %d \r\n", iSize );
//    writeLogFile(txtbuffer);
//    #endif // DISP_DEBUG

  // make sure we are in vram
  while(VRAMWrite.ImagePtr>=psxVuw_eom)
   VRAMWrite.ImagePtr-=iGPUHeight*1024;
  while(VRAMWrite.ImagePtr<psxVuw)
   VRAMWrite.ImagePtr+=iGPUHeight*1024;

  /* The transfer runs on local copies of VRAMWrite. PUTLE16 is an asm store with a
   * "memory" clobber (gpulib/gpu.h), so with the globals every field, psxVuw_eom and
   * iGPUHeight were stored and loaded again around each pixel: about 63 cycles a word on a
   * Wii (FF7: 3.7 s of 67). The steps are the same; the copies go back at every exit.
   * It notes the pixels it changes (CheckWriteUpdate) only while the EFB mirrors psxVuw
   * (ogx_efb_mirror 1): only then does that range save work, and comparing every pixel
   * doubled the loop for an FMV, which rewrites the whole frame (Gex 1.3 -> 2.4 s). Else
   * the transfer counts as changed everywhere (vw_tracked 0). The address only grows on
   * the change path (a wrapped transfer is taken whole), so cmax is simply the last one. */
  {
   unsigned short *ip = VRAMWrite.ImagePtr;
   unsigned short * const eom = psxVuw_eom;
   short rr = VRAMWrite.RowsRemaining, cr = VRAMWrite.ColsRemaining;
   const short wd = VRAMWrite.Width;
   const int wx = VRAMWrite.Width + VRAMWrite.x, wrap = iGPUHeight * 1024;
   unsigned short *cmin = vw_cmin, *cmax = vw_cmax;
#define VW_SAVE() (VRAMWrite.ImagePtr = ip, VRAMWrite.RowsRemaining = rr, VRAMWrite.ColsRemaining = cr, \
                   vw_cmin = cmin, vw_cmax = cmax)
#if PERF_PROF_GPUSPLIT   /* probes: pixels this transfer changed (texinv:), their box (uplcheck:) */
#define VW_SEEN(d) (vw_changed++, vw_dirty(d))
#else
#define VW_SEEN(d) ((void)0)
#endif
#define VW_PUT_TRACK(d, v) do { unsigned short *d_ = (d); unsigned short v_ = (v); \
                                if (GETLE16(d_) != v_) { if (!cmax) cmin = d_; cmax = d_; VW_SEEN(d_); } \
                                PUTLE16(d_, v_); } while (0)
#define VW_PUT_PLAIN(d, v) PUTLE16((d), (v))
#define VW_LOOP(VW_PUT) \
   while (cr > 0) \
    { \
     while (rr > 0) \
      { \
       if (i >= iSize) { VW_SAVE(); goto ENDVRAM_GL; } \
       i++; \
       gdata = GETLE32(pMem); pMem++; \
       /* odd pixel; past the GPU width it wraps to the start of the row */ \
       if (wx - rr >= 1024) \
        VW_PUT((ip++) - 1024, (unsigned short)gdata); \
       else \
        VW_PUT(ip++, (unsigned short)gdata); \
       if (ip >= eom) ip -= wrap; \
       rr--; \
       if (rr <= 0) \
        { \
         cr--; \
         if (cr <= 0)                                 /* last pixel is odd width */ \
          { \
           gdata = (gdata & 0xFFFF) | (((unsigned long)GETLE16(ip)) << 16); \
           VW_SAVE(); \
           FinishedVRAMWrite(); \
           goto ENDVRAM_GL; \
          } \
         rr = wd; \
         ip += 1024 - wd; \
        } \
       /* even pixel */ \
       if (wx - rr >= 1024) \
        VW_PUT((ip++) - 1024, (unsigned short)(gdata >> 16)); \
       else \
        VW_PUT(ip++, (unsigned short)(gdata >> 16)); \
       if (ip >= eom) ip -= wrap; \
       rr--; \
      } \
     rr = wd; \
     cr--; \
     ip += 1024 - wd; \
    }

   if (ogx_efb_mirror == 1 && !PSXDisplay.RGB24)
    {
     VW_LOOP(VW_PUT_TRACK)
    }
   else
    {
     vw_tracked = 0;
     VW_LOOP(VW_PUT_PLAIN)
    }
   VW_SAVE();
#undef VW_LOOP
#undef VW_PUT_TRACK
#undef VW_PUT_PLAIN
#undef VW_SAVE
#undef VW_SEEN
  }

  FinishedVRAMWrite();
 }

ENDVRAM_GL:

#if PERF_PROF_GPUSPLIT
if(gp_vram_open_)
 {
  unsigned long long gp_dt_ = perf_now_ticks() - gp_vram_t0_;
  g_perf.gpu_vram_ticks += gp_dt_;
  gp_other_ += gp_dt_;
  g_perf.gpu_vram_words += (unsigned)(i - gp_vram_i0_);
  gp_vram_open_ = 0;
 }
#endif

if(iDataWriteMode==DR_NORMAL)
 {
//    #if defined(DISP_DEBUG)
//    sprintf ( txtbuffer, "GPUwriteDataMem DR_NORMAL %d \r\n", iSize );
//    writeLogFile(txtbuffer);
//    #endif // DISP_DEBUG

  void (* *primFunc)(unsigned char *);
  if(bSkipNextFrame) primFunc=primTableSkipGx;
  else               primFunc=primTableJGx;

  for(;i<iSize;)
   {
    if(iDataWriteMode==DR_VRAMTRANSFER) goto STARTVRAM_GL;

     gdata=GETLE32(pMem); pMem++; i++;

    if(gpuDataC == 0)
     {
      command = (unsigned char)((gdata>>24) & 0xff);

      if(primTableCX[command])
       {
        gpuDataC = primTableCX[command];
        gpuCommand = command;
         PUTLE32(&gpuDataM[0], gdata);
        gpuDataP = 1;
        if (command == 0xE2) PUTLE32(&g_gp0TexWindow, gdata);   /* for OffscreenSoftDraw */
       }
      else continue;
     }
    else
     {
       PUTLE32(&gpuDataM[gpuDataP], gdata);
      if(gpuDataC>128)
       {
        if((gpuDataC==254 && gpuDataP>=3) ||
           (gpuDataC==255 && gpuDataP>=4 && !(gpuDataP&1)))
         {
           if((gdata & 0xF000F000) == 0x50005000)
           gpuDataP=gpuDataC-1;
         }
       }
      gpuDataP++;
     }

    if(gpuDataP == gpuDataC)
     {
      int nWords = gpuDataC;
      gpuDataC=gpuDataP=0;
      if (nWords <= 128)
       {
        int gp_done_ = 0;
        GPU_PART(gpu_off_ticks, gpu_off_calls,
                 gp_done_ = OffscreenSoftDraw(gpuCommand, gpuDataM, nWords));
        if (gp_done_)
         continue;                                 /* drawn into VRAM by the software rasterizer */
       }
      GPU_PART(gpu_prim_ticks, gpu_prim_calls,
               (EfbBeforeGxCommand(gpuCommand, gpuDataM, nWords),
                primFunc[gpuCommand]((unsigned char *)gpuDataM)));
#if PERF_PROF_GPUSPLIT
      /* GPU_PART just charged gp_other_ with this call; split the same amount out by
       * class of command, so the classes add up to prim. */
      g_perf.gpu_cls_ticks[gpuCommand >> 5] += gp_last_;
      g_perf.gpu_cls_calls[gpuCommand >> 5]++;
#endif

       if (dwActFixes & AUTO_FIX_GPU_BUSY)      // hack for emulating "gpu busy" in some games
       iFakePrimBusy=4;
     }
   }
 }

GPUdataRet=gdata;

GPUIsReadyForCommands;
GPUIsIdle;
#if PERF_PROF_GPUSPLIT
g_perf.gpu_parse_ticks += (perf_now_ticks() - gp_call_) - gp_other_;
#endif
}

////////////////////////////////////////////////////////////////////////

void CALLBACK GL_GPUwriteData(unsigned long gdata)
{
 PUTLE32(&gdata, gdata);
 GL_GPUwriteDataMem(&gdata,1);
}


////////////////////////////////////////////////////////////////////////
// Pete Special: make an 'intelligent' dma chain check (<-Tekken3)
////////////////////////////////////////////////////////////////////////

static unsigned long lUsedAddr[3];

static __inline BOOL CheckForEndlessLoop(unsigned long laddr)
{
if(laddr==lUsedAddr[1]) return TRUE;
if(laddr==lUsedAddr[2]) return TRUE;

if(laddr<lUsedAddr[0]) lUsedAddr[1]=laddr;
else                   lUsedAddr[2]=laddr;
lUsedAddr[0]=laddr;
return FALSE;
}

////////////////////////////////////////////////////////////////////////
// core gives a dma chain to gpu: same as the gpuwrite interface funcs
////////////////////////////////////////////////////////////////////////

#include "../gpulib/gpu_timing.h"

/* The GPU time of one DMA packet, with the costs gpulib gives its commands. The core keeps
 * the GPU busy for this time (psxdma.c: gpuIdleAfter) and ends the DMA after it. Used when
 * GpuTiming is Accurate; Fast returns the list's length in words, as P.E.Op.S. did, which
 * ends every list early. */
static void gl_dma_cost(const uint32_t *list, int count, int *sum, int *last)
{
 const uint32_t *end = list + count;
 while (list < end)
  {
   uint32_t w = GETLE32(list);
   unsigned int cmd = w >> 24, len = cmd_lengths[cmd], c;
   if (list + 1 + len > end || (cmd >= 0x80 && cmd < 0xe0)) break;
   switch (cmd >> 2)
    {
     case 0x02>>2: c = cmd == 0x02 ? gput_fill(GETLE32(&list[2]) & 0x3ff, (GETLE32(&list[2]) >> 16) & 0x1ff) : 0; break;
     case 0x20>>2: case 0x28>>2: c = gput_poly_base(); break;
     case 0x24>>2: case 0x2C>>2: c = gput_poly_base_t(); break;
     case 0x30>>2: case 0x38>>2: c = gput_poly_base_g(); break;
     case 0x34>>2: case 0x3C>>2: c = gput_poly_base_gt(); break;
     case 0x40>>2: case 0x44>>2: case 0x50>>2: case 0x54>>2: c = gput_line(0); break;
     case 0x48>>2: case 0x4C>>2: case 0x58>>2: case 0x5C>>2:
      {
       /* A polyline ends at the 0x5xxx5xxx word; one line per vertex after the first two. */
       unsigned int step = (cmd & 0x10) ? 2 : 1;
       const uint32_t *v = list + 1 + len;
       while (v < end && (GETLE32(v) & 0xf000f000) != 0x50005000) { *sum += *last; *last = gput_line(0); v += step; }
       if (v >= end) return;
       len = v - list;
       c = gput_line(0);
       break;
      }
     case 0x60>>2: c = gput_sprite(GETLE32(&list[2]) & 0x3ff, (GETLE32(&list[2]) >> 16) & 0x1ff); break;
     case 0x64>>2: c = gput_sprite(GETLE32(&list[3]) & 0x3ff, (GETLE32(&list[3]) >> 16) & 0x1ff); break;
     case 0x68>>2: case 0x6C>>2: c = gput_sprite(1, 1); break;
     case 0x70>>2: case 0x74>>2: c = gput_sprite(8, 8); break;
     case 0x78>>2: case 0x7C>>2: c = gput_sprite(16, 16); break;
     default: c = 0; break;
    }
   if (c) gput_sum(*sum, *last, c);
   list += 1 + len;
  }
}

long CALLBACK GL_GPUdmaChain(unsigned long * baseAddrL, unsigned long addr, uint32_t *progress_addr, int32_t *cycles_last_cmd)
{
 unsigned char * baseAddrB;
 unsigned int DMACommandCounter = 0;
 long dmaWords = 0;
 int cyc_sum = 0, cyc_last = 0, accurate = gpuTiming == GPU_TIMING_ACCURATE;


if(bIsFirstFrame) GLinitialize(NULL, NULL);

GPUIsBusy;

lUsedAddr[0]=lUsedAddr[1]=lUsedAddr[2]=0xffffff;

baseAddrB = (unsigned char*) baseAddrL;

do
 {
  if(iGPUHeight==512) addr&=0x1FFFFC;

  if(DMACommandCounter++ > 2000000) break;
  if(CheckForEndlessLoop(addr)) break;

   short count = baseAddrB[addr+3];
   dmaWords += 1 + count;
   cyc_sum += 10;

   unsigned long dmaMem=addr+4;

  if(count>0)
   {
    cyc_sum += 5 + count;
    if (accurate) gl_dma_cost(&baseAddrL[dmaMem>>2], count, &cyc_sum, &cyc_last);
    GL_GPUwriteDataMem(&baseAddrL[dmaMem>>2],count);
   }

   addr = GETLE32(&baseAddrL[addr>>2])&0xffffff;
  }
 while (!(addr & 0x800000)); // contrary to some documentation, the end-of-linked-list marker is not actually 0xFF'FFFF
                             // any pointer with bit 23 set will do.

 GPUIsIdle;

 if (!accurate) return dmaWords;
 *cycles_last_cmd = cyc_last;
 return cyc_sum;
}

////////////////////////////////////////////////////////////////////////
// save state funcs
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////

long CALLBACK GL_GPUfreeze(unsigned long ulGetFreezeData,GPUFreeze_t * pF)
{
flip05Flush();
if(ulGetFreezeData==2)
 {
  long lSlotNum=*((long *)pF);
  if(lSlotNum<0) return 0;
  if(lSlotNum>8) return 0;
  //lSelectedSlot=lSlotNum+1;
  return 1;
 }

if(!pF)                    return 0;
if(pF->ulFreezeVersion!=1) return 0;

if(ulGetFreezeData==1)
 {
  pF->ulStatus=STATUSREG;
  memcpy(pF->ulControl,ulStatusControl,256*sizeof(unsigned long));
  //memcpy(pF->psxVRam,  psxVub,         1024*iGPUHeight*2);

  /* The drawing environment, GP0 E1-E6, in slots 0xE1-0xE6 that GP1 never uses -- where
   * gpulib keeps it too. Without it a state loaded into another boot drew with that boot's
   * draw area, offset and texture window until the game set its own again. */
  pF->ulControl[0xE1] = 0xE1000000u | (STATUSREG & 0x7FFu) | (((STATUSREG >> 15) & 1u) << 11);
  pF->ulControl[0xE2] = g_gp0TexWindow ? GETLE32(&g_gp0TexWindow) : 0xE2000000u;
  pF->ulControl[0xE3] = 0xE3000000u | ((uint32_t)(PSXDisplay.DrawArea.y0 & 0x3FF) << 10) | (PSXDisplay.DrawArea.x0 & 0x3FF);
  pF->ulControl[0xE4] = 0xE4000000u | ((uint32_t)(PSXDisplay.DrawArea.y1 & 0x3FF) << 10) | (PSXDisplay.DrawArea.x1 & 0x3FF);
  pF->ulControl[0xE5] = 0xE5000000u | ((uint32_t)(PSXDisplay.DrawOffset.y & 0x7FF) << 11) | (PSXDisplay.DrawOffset.x & 0x7FF);
  pF->ulControl[0xE6] = 0xE6000000u | ((STATUSREG >> 11) & 3u);
  return 1;
 }

if(ulGetFreezeData!=0) return 0;
MIRROR_OFF(4);                                        /* a state load: psxVuw changes under the EFB */

STATUSREG=pF->ulStatus;
memcpy(ulStatusControl,pF->ulControl,256*sizeof(unsigned long));
//memcpy(psxVub,         pF->psxVRam,  1024*iGPUHeight*2);

ResetTextureArea(TRUE);

 {
  /* The replay starts with GP1(00), a reset, which clears the draw-mode bits of the status
   * (texture page, dither, mask, texture disable: bits 0-12 and 15) that GP0 E1 and E6 set.
   * They are put back after it; a game that reads the status before its next E1 sees them. */
  unsigned long drawmode = pF->ulStatus & 0x9FFF;
 GL_GPUwriteStatus(ulStatusControl[0]);
 GL_GPUwriteStatus(ulStatusControl[1]);
 GL_GPUwriteStatus(ulStatusControl[2]);
 GL_GPUwriteStatus(ulStatusControl[3]);
 GL_GPUwriteStatus(ulStatusControl[8]);
 GL_GPUwriteStatus(ulStatusControl[6]);
 GL_GPUwriteStatus(ulStatusControl[7]);
 GL_GPUwriteStatus(ulStatusControl[5]);
 GL_GPUwriteStatus(ulStatusControl[4]);
  STATUSREG = (STATUSREG & ~0x9FFFUL) | drawmode;
 }
 /* The drawing environment (above). An older state has zeros there: it keeps the one it had. */
 if ((pF->ulControl[0xE1] >> 24) == 0xE1)
  {
   int k;
   for (k = 0; k < 6; k++)
    {
     uint32_t w;
     PUTLE32(&w, (uint32_t)pF->ulControl[0xE1 + k]);
     if (k == 1) g_gp0TexWindow = w;
     primTableJGx[0xE1 + k]((unsigned char *)&w);
    }
  }
 memset(&ulStatusControl[0xE1], 0, 6 * sizeof(ulStatusControl[0]));   /* not GP1 history */
 return 1;
}

/* Save states, section GPU1 (misc.c): what this plugin keeps between GP0/GP1 accesses and
 * GL_GPUfreeze does not -- a command half received, a VRAM transfer part done, and the
 * busy/ready countdown a status poll sees after drawing (iFakePrimBusy), which decides how
 * many times a game's GPUSTAT loop goes round. gpulib flushes the first two on a save; here
 * all three are kept. Only a state saved and loaded with this plugin carries it. */
struct gl_extra {
 uint32_t version;
 int32_t  fakebusy, dataret, command, dataC, dataP, writeMode, readMode;
 int16_t  w[6], r[6];      /* VRAMWrite, VRAMRead: x y Width Height RowsRemaining ColsRemaining */
 int32_t  wptr, rptr;      /* their ImagePtr as an offset into VRAM, -1 = none */
 uint32_t status;          /* the whole status register */
 uint32_t dataM[256];
};

static void gl_vram_get(int16_t *o, int32_t *ptr, const VRAMLoad_t *v)
{
 o[0] = v->x; o[1] = v->y; o[2] = v->Width; o[3] = v->Height;
 o[4] = v->RowsRemaining; o[5] = v->ColsRemaining;
 *ptr = v->ImagePtr ? (int32_t)(v->ImagePtr - psxVuw) : -1;
}

static void gl_vram_put(VRAMLoad_t *v, const int16_t *o, int32_t ptr)
{
 v->x = o[0]; v->y = o[1]; v->Width = o[2]; v->Height = o[3];
 v->RowsRemaining = o[4]; v->ColsRemaining = o[5];
 v->ImagePtr = (ptr >= 0 && ptr < 1024 * iGPUHeight) ? psxVuw + ptr : NULL;
}

int GL_GPUfreezeExtra(int save, void *data, int len)
{
 struct gl_extra e;
 int k;

 if (!data) return sizeof(e);
 if (save) {
  e.version = 1;
  e.fakebusy = iFakePrimBusy;
  e.dataret = GPUdataRet;
  e.command = gpuCommand;
  e.dataC = gpuDataC;
  e.dataP = gpuDataP;
  e.writeMode = iDataWriteMode;
  e.readMode = iDataReadMode;
  gl_vram_get(e.w, &e.wptr, &VRAMWrite);
  gl_vram_get(e.r, &e.rptr, &VRAMRead);
  e.status = (uint32_t)STATUSREG;
  for (k = 0; k < 256; k++) e.dataM[k] = (uint32_t)gpuDataM[k];
  memcpy(data, &e, sizeof(e));
  return sizeof(e);
 }
 if (len != (int)sizeof(e)) return -1;
 memcpy(&e, data, sizeof(e));
 if (e.version != 1) return -1;
 iFakePrimBusy = e.fakebusy;
 GPUdataRet = e.dataret;
 gpuCommand = (unsigned char)e.command;
 gpuDataC = e.dataC;
 gpuDataP = e.dataP;
 iDataWriteMode = e.writeMode;
 iDataReadMode = e.readMode;
 gl_vram_put(&VRAMWrite, e.w, e.wptr);
 gl_vram_put(&VRAMRead, e.r, e.rptr);
 STATUSREG = e.status;
 for (k = 0; k < 256; k++) gpuDataM[k] = e.dataM[k];
 return 0;
}

////////////////////////////////////////////////////////////////////////
// special "emu infos" / "emu effects" functions
////////////////////////////////////////////////////////////////////////

// pcsx-rearmed callbacks
void CALLBACK GL_GPUrearmedCallbacks(const struct rearmed_cbs *_cbs)
{
   #ifdef DISP_DEBUG
 //writeLogFile("GL_GPUrearmedCallbacks 0\r\n");
 #endif // DISP_DEBUG
//   gpu.frameskip.set = _cbs->frameskip;
//  gpu.frameskip.advice = &_cbs->fskip_advice;
//  gpu.frameskip.force = &_cbs->fskip_force;
//  gpu.frameskip.dirty = (void *)&_cbs->fskip_dirty;
//  gpu.frameskip.active = 0;
//  gpu.frameskip.frame_ready = 1;
//  gpu.state.hcnt = _cbs->gpu_hcnt;
//  gpu.state.frame_count = _cbs->gpu_frame_count;
//  gpu.state.allow_interlace = _cbs->gpu_neon.allow_interlace;
//  gpu.state.enhancement_enable = _cbs->gpu_neon.enhancement_enable;
//  if (gpu.state.screen_centering_type != _cbs->screen_centering_type
//      || gpu.state.screen_centering_x != _cbs->screen_centering_x
//      || gpu.state.screen_centering_y != _cbs->screen_centering_y) {
//    gpu.state.screen_centering_type = _cbs->screen_centering_type;
//    gpu.state.screen_centering_x = _cbs->screen_centering_x;
//    gpu.state.screen_centering_y = _cbs->screen_centering_y;
//    update_width();
//    update_height();
//  }
//
//  gpu.mmap = _cbs->mmap;
//  gpu.munmap = _cbs->munmap;
//  gpu.gpu_state_change = _cbs->gpu_state_change;
//
//  // delayed vram mmap
//  if (gpu.vram == NULL)
//    map_vram();
//
//  if (_cbs->pl_vout_set_raw_vram)
//    _cbs->pl_vout_set_raw_vram(gpu.vram);
  #ifdef DISP_DEBUG
 //writeLogFile("GL_GPUrearmedCallbacks 1\r\n");
 #endif // DISP_DEBUG
  renderer_set_config(_cbs);
  #ifdef DISP_DEBUG
 //writeLogFile("GL_GPUrearmedCallbacks 2\r\n");
 #endif // DISP_DEBUG
  vout_set_config(_cbs);
}

/* The FPS and debug text go on the picture sent to the TV, not into the game's frame.
 * They are drawn into the EFB just before its copy to the XFB, and the EFB is also the
 * frame the game goes on drawing into when it does not clear it. The text used to stay
 * there: it was only drawn on frames where the game had redrawn the top-left corner (a
 * guess, so it flashed on and off), and the frame was marked unfit for readback while it
 * was there (Ape Escape's pause screen reads its frame back: it got the blue clear).
 * Now the EFB is copied to a texture before the text, and drawn back after the copy to
 * the XFB -- unless that copy clears the EFB anyway. The cost, only with the overlay on
 * and only on such presents: one EFB-sized copy and one full-screen textured quad. */
static void *ovl_buf;
static int ovl_cap, ovl_w, ovl_h;

static int OverlaySaveEfb(void)
{
    extern GXRModeObj *g_tv_mode;   /* Gamecube/libgui/GraphicsGX.cpp: the mode on the TV */
    GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
    int size = GX_GetTexBufferSize(m->fbWidth, m->efbHeight, GX_TF_RGBA8, 0, GX_FALSE);

    if (size > ovl_cap)
    {
        free(ovl_buf);
        ovl_buf = memalign(32, size);
        ovl_cap = ovl_buf ? size : 0;
        if (!ovl_buf) return 0;
        /* only the GP touches it from here on: no CPU line may be written back over it */
        DCInvalidateRange(ovl_buf, size);
    }
    ovl_w = m->fbWidth;
    ovl_h = m->efbHeight;
    /* no vertical filter or deflicker: they would soften the frame each time it is kept */
    GX_SetCopyFilter(m->aa, m->aa ? m->sample_pattern : NULL, GX_FALSE, NULL);
    GX_SetTexCopySrc(0, 0, ovl_w, ovl_h);
    GX_SetTexCopyDst(ovl_w, ovl_h, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(ovl_buf, GX_FALSE);
    GX_PixModeSync();
    RestoreDispCopyInfo();
    return 1;
}

static void OverlayRestoreEfb(void)
{
    extern GXRModeObj *g_tv_mode;
    GXRModeObj *m = g_tv_mode ? g_tv_mode : vmode;
    GXTexObj tex;
    Mtx44 proj;
    Mtx mv;

    /* the mode changed between the save and now (SwitchTVModeForDisplay): nothing to restore */
    if (m->fbWidth != ovl_w || m->efbHeight != ovl_h)
        return;

    GX_InvalidateTexAll();   /* the texture cache may hold last present's copy at this address */
    GX_InitTexObj(&tex, ovl_buf, ovl_w, ovl_h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&tex, GX_NEAR, GX_NEAR);
    GX_LoadTexObj(&tex, GX_TEXMAP0);

    /* EFB pixels exactly: its own viewport and projection, the plugin's put back below */
    GX_SetViewport(0, 0, ovl_w, ovl_h, 0.0f, 1.0f);
    GX_SetScissor(0, 0, ovl_w, ovl_h);
    guOrtho(proj, 0, ovl_h, 0, ovl_w, 0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    /* the copy holds the EFB's own channel order: no swap on the way back */
    GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);

    GX_Begin(GX_QUADS, GX_VTXFMT7, 4);
    GX_Position2f32(0, 0);                         GX_TexCoord2f32(0, 0);
    GX_Position2f32((f32)ovl_w, 0);                GX_TexCoord2f32(1, 0);
    GX_Position2f32((f32)ovl_w, (f32)ovl_h);       GX_TexCoord2f32(1, 1);
    GX_Position2f32(0, (f32)ovl_h);                GX_TexCoord2f32(0, 1);
    GX_End();

    /* the plugin's state: the BGR swap table (gx_vout_render), its viewport, its clip */
    GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);
    glViewport(rRatioRect.left, iResY - (rRatioRect.top + rRatioRect.bottom),
               rRatioRect.right, rRatioRect.bottom);
    bSetClip = TRUE;
}

static void flipEGL(void)
{
    int presentSubmitted;
    int overlaySaved = 0;
    unsigned long long flip_t0 = perf_now_us();
    #ifdef DISP_DEBUG
    sprintf(txtbuffer, "flipEGL %d \r\n", canClearFrameBuf);
    DEBUG_print(txtbuffer, DBG_SPU3);
    writeLogFile(txtbuffer);
    #endif // DISP_DEBUG

    efb_present();   /* efbSync.inc: keep the frame the copy to the XFB may clear */

    /* a frame that will not reach the TV gets no text */
    if (showFPSonScreen == 1 && !gx_vout_busy())
    {
        /* a copy that clears the EFB takes the text with it; otherwise keep what it covers */
        if (!canClearFrameBuf)
            overlaySaved = OverlaySaveEfb();
        if (overlaySaved || canClearFrameBuf)
            showFpsAndDebugInfo();
    }

    /* The 240p button was pressed in the menu: a game that does not change its display mode
     * again would otherwise keep the old EFB size. */
    if (displayModeChanged && TVModeResolution() && bKeepRatio)
        SetAspectRatio();

    // Check if TVMode needs to be changed (240 or 480 lines)
    if (originalMode == ORIGINALMODE_ENABLE)
    {
        extern int backFromMenu;
        if(backFromMenu)
        {
            backFromMenu = 0;
            SwitchTVModeForDisplay();
        }
    }

    presentSubmitted = gx_vout_render(canClearFrameBuf);
    if (presentSubmitted && canClearFrameBuf)
        MIRROR_OFF(2);       /* the copy cleared the EFB */
    /* the text is in the XFB copy (or the copy was skipped): take it out of the game's frame */
    if (overlaySaved && (!presentSubmitted || !canClearFrameBuf))
        OverlayRestoreEfb();

#ifdef PERF_PROF
    /* EC = one entry per present: what the present did (flags), how much had been
     * drawn, and the emulated vblank it happened at -- the vblank is what tells an
     * even 30 fps cadence from a stutter, since a frame dump is one image per
     * PRESENT, not one per vblank. */
    perf_prim_trace(0xEC,
                    (canClearFrameBuf ? 1 : 0) | (presentSubmitted ? 2 : 0) |
                    (uploadedScreen ? 4 : 0) | (needFlipEGL ? 8 : 0),
                    (unsigned)(iDrawnSomething & 0xff), (unsigned)g_perf.vblanks,
                    PreviousPSXDisplay.DisplayPosition.x, PreviousPSXDisplay.DisplayPosition.y,
                    PSXDisplay.DisplayPosition.x, PSXDisplay.DisplayPosition.y);
    PERF_INC(pres_total);
    if (canClearFrameBuf) PERF_INC(pres_clear);
    if (!presentSubmitted) PERF_INC(pres_skipped);
#endif


    clearLargeRange = 0;
    uploadedScreen = FALSE;
    needFlipEGL = presentSubmitted ? FALSE : TRUE;
    if (presentSubmitted)
        canClearFrameBuf = FALSE;
    RGB24Uploaded = 0;
    glSetLoadMtxFlg();

    extern void resetTexCacheInfo(void);
    resetTexCacheInfo();
    /* The copy to the TV and the FPS overlay above set GX state with raw GX calls, past
     * OpenGX's state cache: it has to forget what it last set (deps/opengx/gc_gl.c). */
    ogx_state_invalidate();
    perf_present_tick(perf_now_us() - flip_t0);
}

#include "../Gamecube/wiiSXconfig.h"
extern char screenMode;

long GL_GPUopen()
{
 MIRROR_OFF(3);
 int ret;

 InitFPS();

 GPUsetframelimit(0);

 iResX = 640;
 iResY = 480;
 rRatioRect.left   = 0;
// if (screenMode != SCREENMODE_4x3)
// {
//     rRatioRect.left   = -104;
//     iResX = 744;
// }
 iOffscreenDrawing = 0;
 rRatioRect.top=0;
 rRatioRect.right  = iResX;
 rRatioRect.bottom = iResY;

 bIsFirstFrame = TRUE;
 bDisplayNotSet = TRUE;
 bSetClip = TRUE;
 CSTEXTURE = CSVERTEX = CSCOLOR = 0;
 canClearFrameBuf = FALSE;

 InitializeTextureStore();                             // init texture mem

 ret = GLinitialize(NULL, NULL);

 gx_vout_open();

 return ret;
}

long GL_GPUclose(void)
{
 MIRROR_OFF(3);
 efb_reset();
 GLcleanup();                                          // close OGL
 return 0;
}

gpu_t glesGpu = {
    GL_GPUopen,
    GL_GPUinit,
    GL_GPUshutdown,
    GL_GPUclose,
    GL_GPUwriteStatus,
    GL_GPUwriteData,
    GL_GPUreadStatus,
    GL_GPUreadData,
    GL_GPUdmaChain,
    GL_GPUupdateLace,
    GL_GPUfreeze,
    GL_GPUreadDataMem,
    GL_GPUwriteDataMem,
    GPUsetframelimit
};
