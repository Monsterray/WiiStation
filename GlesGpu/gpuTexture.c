/***************************************************************************
                          texture.c  -  description
                             -------------------
    begin                : Sun Mar 08 2009
    copyright            : (C) 1999-2009 by Pete Bernert
    web                  : www.pbernert.com
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


////////////////////////////////////////////////////////////////////////////////////
// Texture related functions are here !
//
// The texture handling is heart and soul of this gpu. The plugin was developed
// 1999, by this time no shaders were available. Since the psx gpu is making
// heavy use of CLUT (="color lookup tables", aka palettized textures), it was
// an interesting task to get those emulated at good speed on NV TNT cards
// (which was my major goal when I created the first "gpuPeteTNT"). Later cards
// (Geforce256) supported texture palettes by an OGL extension, but at some point
// this support was dropped again by gfx card vendors.
// Well, at least there is a certain advatage, if no texture palettes extension can
// be used: it is possible to modify the textures in any way, allowing "hi-res"
// textures and other tweaks.
//
// My main texture caching is kinda complex: the plugin is allocating "n" 256x256 textures,
// and it places small psx texture parts inside them. The plugin keeps track what
// part (with what palette) it had placed in which texture, so it can re-use this
// part again. The more ogl textures it can use, the better (of course the managing/
// searching will be slower, but everything is faster than uploading textures again
// and again to a gfx card). My first card (TNT1) had 16 MB Vram, and it worked
// well with many games, but I recommend nowadays 64 MB Vram to get a good speed.
//
// Sadly, there is also a second kind of texture cache needed, for "psx texture windows".
// Those are "repeated" textures, so a psx "texture window" needs to be put in
// a whole texture to use the GL_TEXTURE_WRAP_ features. This cache can get full very
// fast in games which are having an heavy "texture window" usage, like RRT4. As an
// alternative, this plugin can use the OGL "palette" extension on texture windows,
// if available. Nowadays also a fragment shader can easily be used to emulate
// texture wrapping in a texture atlas, so the main cache could hold the texture
// windows as well (that's what I am doing in the OGL2 plugin). But currently the
// OGL1 plugin is a "shader-free" zone, so heavy "texture window" games will cause
// much texture uploads.
//
// Some final advice: take care if you change things in here. I've removed my ASM
// handlers (they didn't cause much speed gain anyway) for readability/portability,
// but still the functions/data structures used here are easy to mess up. I guess it
// can be a pain in the ass to port the plugin to another byte order :)
//
////////////////////////////////////////////////////////////////////////////////////

#ifdef _IN_GPU_LIB


#define _IN_TEXTURE

#include "../gpulib/stdafx.h"

#include "gpuDraw.h"
//#include "plugins.h"
#include "gpuExternals.h"
#include "gpuTexture.h"
#include "gpuClutKey.h"
#include "texInval.h"
#include "gpuPlugin.h"
#include "gpuPrim.h"

#include "../Gamecube/DEBUG.h"
#include "../Gamecube/wiiSXconfig.h"   /* fmvColour */
#include "../gpulib/gpu.h"

////////////////////////////////////////////////////////////////////////
// texture conversion buffer ..
////////////////////////////////////////////////////////////////////////

GLubyte       ubPaletteBuffer[256][4];
GLuint        gTexMovieName=0;
GLuint        gTexBlurName=0;
GLuint        gTexFrameName=0;
int           iTexGarbageCollection=1;
unsigned int  dwTexPageComp=0;
int           iVRamSize=0;
int           iClampType=GL_CLAMP_TO_EDGE;
int iFilter = GL_LINEAR;
void               (*LoadSubTexFn) (int,int,short,short);
//unsigned int       (*PalTexturedColourFn)  (unsigned int);

////////////////////////////////////////////////////////////////////////
// defines
////////////////////////////////////////////////////////////////////////

//#define PALCOL(x) PalTexturedColourFn (x)

#define CSUBSIZE  2048
//#define CSUBSIZEA 8192
#define CSUBSIZES 4096

//#define OFFA 0
//#define OFFB 2048
//#define OFFC 4096
//#define OFFD 6144

//#define XOFFA 0
//#define XOFFB 512
//#define XOFFC 1024
//#define XOFFD 1536

#define SOFFA 0
#define SOFFB 1024
#define SOFFC 2048
#define SOFFD 3072

//#define MAXWNDTEXCACHE 64

#define XCHECK(pos1,pos2) ((pos1.c.y2>=pos2.c.y1)&&(pos1.c.y1<=pos2.c.y2)&&(pos1.c.x2>=pos2.c.x1)&&(pos1.c.x1<=pos2.c.x2))
#define INCHECK(pos2,pos1) ((pos1.c.y2<=pos2.c.y2) && (pos1.c.y1>=pos2.c.y1) && (pos1.c.x2<=pos2.c.x2) && (pos1.c.x1>=pos2.c.x1))

////////////////////////////////////////////////////////////////////////

struct textureSubCacheEntryTagS;
struct textureSubCacheEntryTagS *CheckTextureInSubSCache(
 int TextureMode,uint64_t clutKey,unsigned short *pCache);
void            LoadSubTexturePageSort(int pageid, int mode, short cx, short cy);
//void            LoadPackedSubTexturePageSort(int pageid, int mode, short cx, short cy);
void            DefineSubTextureSort(void);

////////////////////////////////////////////////////////////////////////
// some globals
////////////////////////////////////////////////////////////////////////

int  GlobalTexturePage;
GLint XTexS;
GLint YTexS;
GLint DXTexS;
GLint DYTexS;
int   iSortTexCnt=32;
BOOL  bUseFastMdec=FALSE;
BOOL  bUse15bitMdec=FALSE;
int   iFrameTexType=0;
int   iFrameReadType=0;

unsigned int  (*TCF[2]) (unsigned int);
//unsigned short (*PTCF[2]) (unsigned short);

////////////////////////////////////////////////////////////////////////
// texture cache implementation
////////////////////////////////////////////////////////////////////////

// "texture window" cache entry

typedef struct textureWndCacheEntryTag
{
 uint64_t       ClutKey;
 short          pageid;
 short          textureMode;
 short          Opaque;
 short          used;
 EXLong         pos;
 GLuint         texname;
 unsigned int   textureType;
} textureWndCacheEntry;

// "standard texture" cache entry (16 bytes per entry; keep this compact)

typedef struct textureSubCacheEntryTagS
{
 uint64_t         ClutKey;
 EXLong          pos;
 unsigned char   posTX;
 unsigned char   posTY;
 unsigned char   cTexID;
 unsigned char   drawInfo;
} textureSubCacheEntryS;

typedef char textureSubCacheEntryMustRemain16Bytes[
 sizeof(textureSubCacheEntryS)==16 ? 1 : -1];

#define SUBCACHE_COUNT_PTR(entry) ((char *)&((entry)->pos))


//---------------------------------------------

#define MAXWNDTEXCACHE 64
#define MAXTPAGES_MAX  64
#define MAXSORTTEX_MAX 64

//---------------------------------------------

textureWndCacheEntry     wcWndtexStore[MAXWNDTEXCACHE];    // only 64 entries
textureSubCacheEntryS *  pscSubtexStore[3][MAXTPAGES_MAX]; // 3 * (64 / 2) * 4096 * 16 = 6M MB
EXLong *                 pxSsubtexLeft [MAXSORTTEX_MAX];   // 196 * 2048 * 4 = 1568 KB
GLuint                   uiStexturePage[MAXSORTTEX_MAX];   // 196 * 4 = 160 B

unsigned short           usLRUTexPage=0;

int                      iMaxTexWnds=0;
int                      iTexWndTurn=0;
int                      iTexWndLimit=MAXWNDTEXCACHE/2;

GLubyte                  texturepart[256 * 256 *4];
//GLubyte *                texturebuffer=NULL;
unsigned int             g_x1,g_y1,g_x2,g_y2;
unsigned char            ubOpaqueDraw=0;

unsigned short MAXTPAGES     = MAXTPAGES_MAX / 2;
unsigned short CLUTMASK      = 0x7fff;
unsigned short CLUTYMASK     = 0x1ff;
unsigned short MAXSORTTEX    = MAXSORTTEX_MAX;

static uint64_t BuildClutCacheKey(unsigned int rawClutId,int textureMode,
                                  int drawSemiTrans)
{
 const void *palette=NULL;

 if(textureMode!=2)
  {
   short cx=((rawClutId<<4)&0x3F0);
   short cy=((rawClutId>>6)&CLUTYMASK);

   palette=psxVuw+cx+(cy*1024);
  }
 return ClutKeyBuild(rawClutId,textureMode,drawSemiTrans,palette);
}

static unsigned char PackDrawInfo(unsigned char opaque,
                                  unsigned int textureType)
{
 return (opaque&0x3fU)|((textureType&3U)<<6);
}

static unsigned char DrawInfoOpaque(unsigned char drawInfo)
{
 return drawInfo&0x3fU;
}

static unsigned char DrawInfoTextureType(unsigned char drawInfo)
{
 return drawInfo>>6;
}

////////////////////////////////////////////////////////////////////////
// Texture color conversions... all my ASM funcs are removed for easier
// porting... and honestly: nowadays the speed gain would be pointless
////////////////////////////////////////////////////////////////////////

// BGR => big endian(argb) (for movie PSXDisplay.RGB24 = false)
// bgr555 => bgr5A3(In order to be consistent with [PSXDisplay.RGB24=true], 4 bytes were used)
// Non-Transparent
unsigned int XP8RGBA_0(unsigned int BGR)
{
    if (!(BGR & 0x7fff)) return 0xffff0000;
    //return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xff000000;
    return BGR | 0xffff0000;
}
// Semi-Transparent
unsigned int XP8RGBA_1(unsigned int BGR)
{
    if (!(BGR & 0xffff))
    {
        return 0xffff0000;
    }

//    if (sSetMask)
//    {
//        //return BGR | 0xffff8000;
//        return (0x6 << 12) | 0xffff0000 | ((BGR & 0x1f) >> 1) | ((BGR & 0x3C0) >> 2) | ((BGR & 0x7800) >> 3);
//    }
//
//    if (!(BGR & 0x8000))
//    {
//        return (0x6 << 12) | 0xffff0000 | ((BGR & 0x1f) >> 1) | ((BGR & 0x3C0) >> 2) | ((BGR & 0x7800) >> 3);
//    }
    return BGR | 0xffff0000;
}

// BGR => big endian(argb)
unsigned int CP8RGBA_0(unsigned int BGR)
{
 unsigned int l;

 if(!(BGR&0xffff)) return 0x50000000;
 l=(((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xff000000;
 if(l==0xff00f8f8) l=0xff000000;
 return l;
}

//// BGR => big endian(argb)
//unsigned int XP8RGBA_1(unsigned int BGR)
//{
// if(!(BGR&0xffff)) return 0x50000000;
// if(!(BGR&0x8000)) {ubOpaqueDraw=1;return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7));}
// return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xff000000;
//}

// BGR => big endian(argb) (for other texture)
// bgr555 => bgr5A3(2 bytes were used, Can save half of the memory)
// Semi-Transparent
unsigned int P8RGBA_1(unsigned int BGR)
{
    if (!(BGR & 0x7fff))
    {
        return 0;
    }

//    if (sSetMask)
//    {
//        return BGR & 0x7fff;
//        //return (0x6 << 12) | ((BGR & 0x1f) >> 1) | ((BGR & 0x3C0) >> 2) | ((BGR & 0x7800) >> 3);
//    }

//    if (!(BGR & 0x8000))
//    {
//        //return (0x6 << 12) | ((BGR & 0x1f) >> 1) | ((BGR & 0x3C0) >> 2) | ((BGR & 0x7800) >> 3);
//        return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xf0000000;
//    }
//    return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xff000000;
    return BGR;
}

// Non-Transparent (BGR => ARGB)
unsigned int P8RGBA_0(unsigned int BGR)
{
    if (!(BGR&0xffff)) return 0;
    //return (((BGR&0x1f)<<19)|((BGR&0x3E0)<<6)|((BGR&0x7C00)>>7))|0xff000000;
    return BGR;
}

static inline unsigned short MOV24to16 (unsigned char *BGR)
{
    return (((uint16_t)(*(BGR + 2)>>3) << 10)|((uint16_t)(*(BGR + 1)>>3) << 5)|((uint16_t)(*(BGR)>>3)));
}

////////////////////////////////////////////////////////////////////////
// CHECK TEXTURE MEM (on plugin startup)
////////////////////////////////////////////////////////////////////////

//int iFTexA=512;
//int iFTexB=512;

void CheckTextureMemory(void)
{
 GLboolean b;GLboolean * bDetail;
 int i,iCnt,iRam=iVRamSize*1024*1024;
 int iTSize;char * p;


// if(iVRamSize)
//  {
//   int ts;
//
//   iRam-=(iResX*iResY*8);
//   iRam-=(iResX*iResY*(iZBufferDepth/8));
//
//	   ts=4;
//	   iSortTexCnt=iRam/(256*256*ts);
//
//   if(iSortTexCnt>MAXSORTTEX)
//    {
//     iSortTexCnt=MAXSORTTEX-min(1,0);
//    }
//   else
//    {
//     iSortTexCnt-=3+min(1,0);
//     if(iSortTexCnt<8) iSortTexCnt=8;
//    }
//
//   for(i=0;i<MAXSORTTEX;i++)
//    uiStexturePage[i]=0;
//
//   return;
//  }

#if 1
 iSortTexCnt=MAXSORTTEX;
#else // below vram detector supposedly crashes some drivers
#endif
}

////////////////////////////////////////////////////////////////////////
// Main init of textures
////////////////////////////////////////////////////////////////////////

void InitializeTextureStore()
{
 int i,j;

// if(iGPUHeight==1024)
//  {
//   MAXTPAGES     = MAXTPAGES_MAX;
//   CLUTMASK      = 0xffff;
//   CLUTYMASK     = 0x3ff;
//   MAXSORTTEX    = MAXSORTTEX_MAX;
//   iTexGarbageCollection=0;
//  }
// else
  {
   MAXTPAGES     = MAXTPAGES_MAX / 2;
   CLUTMASK      = 0x7fff;
   CLUTYMASK     = 0x1ff;
   MAXSORTTEX    = MAXSORTTEX_MAX;
  }

 memset(vertex,0,4*sizeof(OGLVertex));                 // init vertices

 gTexName=0;                                           // init main tex name

 iTexWndLimit=MAXWNDTEXCACHE;
/* if(!iUsePalTextures) */iTexWndLimit/=2;

 memset(wcWndtexStore,0,sizeof(textureWndCacheEntry)*
                        MAXWNDTEXCACHE);
 //texturepart=(GLubyte *)_mem2_malloc(256*256*4);
 memset(texturepart,0,256*256*4);
	 //texturebuffer=NULL;

 for(i=0;i<3;i++)                                    // -> info for 32*3
  for(j=0;j<MAXTPAGES;j++)
   {
    pscSubtexStore[i][j]=(textureSubCacheEntryS *)_mem2_malloc(CSUBSIZES*sizeof(textureSubCacheEntryS));
    if(pscSubtexStore[i][j])
     memset(pscSubtexStore[i][j],0,CSUBSIZES*sizeof(textureSubCacheEntryS));
   }
 for(i=0;i<MAXSORTTEX;i++)                           // -> info 0..511
  {
   pxSsubtexLeft[i]=(EXLong *)_mem2_malloc(CSUBSIZE*sizeof(EXLong));
   if(pxSsubtexLeft[i])
    memset(pxSsubtexLeft[i],0,CSUBSIZE*sizeof(EXLong));
   uiStexturePage[i]=0;
  }
}

////////////////////////////////////////////////////////////////////////
// Clean up on exit
////////////////////////////////////////////////////////////////////////

void CleanupTextureStore()
{
 int i,j;textureWndCacheEntry * tsx;
 //----------------------------------------------------//
 glBindTextureBef(GL_TEXTURE_2D,0);
 glError();
 //----------------------------------------------------//
 //_mem2_free(texturepart);                                    // free tex part
 //texturepart=0;
// if(texturebuffer)
//  {
//   _mem2_free(texturebuffer);
//   texturebuffer=0;
//  }
 //----------------------------------------------------//
 tsx=wcWndtexStore;                                    // loop tex window cache
 for(i=0;i<MAXWNDTEXCACHE;i++,tsx++)
  {
   if(tsx->texname)                                    // -> some tex?
    glDeleteTextures(1,&tsx->texname);                 // --> delete it
    glError();
  }
 iMaxTexWnds=0;                                        // no more tex wnds
 //----------------------------------------------------//
 if(gTexMovieName!=0)                                  // some movie tex?
  glDeleteTextures(1, &gTexMovieName);                 // -> delete it
  glError();
 gTexMovieName=0;                                      // no more movie tex
 //----------------------------------------------------//
 if(gTexFrameName!=0)                                  // some 15bit framebuffer tex?
  glDeleteTextures(1, &gTexFrameName);                 // -> delete it
  glError();
 gTexFrameName=0;                                      // no more movie tex
 //----------------------------------------------------//
 if(gTexBlurName!=0)                                   // some 15bit framebuffer tex?
  glDeleteTextures(1, &gTexBlurName);                  // -> delete it
  glError();
 gTexBlurName=0;                                       // no more movie tex
 //----------------------------------------------------//
 for(i=0;i<3;i++)                                    // -> loop
  for(j=0;j<MAXTPAGES;j++)                           // loop tex pages
   {
    _mem2_free(pscSubtexStore[i][j]);                      // -> clean mem
    pscSubtexStore[i][j]=0;
   }
 for(i=0;i<MAXSORTTEX;i++)
  {
   if(uiStexturePage[i])                             // --> tex used ?
    {
     glDeleteTextures(1,&uiStexturePage[i]);
     glError();
     uiStexturePage[i]=0;                            // --> delete it
    }
   _mem2_free(pxSsubtexLeft[i]);                           // -> clean mem
   pxSsubtexLeft[i]=0;
  }
 //----------------------------------------------------//
}

////////////////////////////////////////////////////////////////////////
// Reset textures in game...
////////////////////////////////////////////////////////////////////////

void ResetTextureArea(BOOL bDelTex)
{
 int i,j;textureSubCacheEntryS * tss;EXLong * lu;
 textureWndCacheEntry * tsx;
 //----------------------------------------------------//

 dwTexPageComp=0;

 //----------------------------------------------------//
 if(bDelTex) {glBindTextureBef(GL_TEXTURE_2D,0); glError();gTexName=0;}
 //----------------------------------------------------//
 tsx=wcWndtexStore;
 for(i=0;i<MAXWNDTEXCACHE;i++,tsx++)
  {
   tsx->used=0;
   if(bDelTex && tsx->texname)
    {
     glDeleteTextures(1,&tsx->texname); glError();
     tsx->texname=0;
    }
  }
 iMaxTexWnds=0;
 //----------------------------------------------------//

 for(i=0;i<3;i++)
  for(j=0;j<MAXTPAGES;j++)
   {
    tss=pscSubtexStore[i][j];
    (tss+SOFFA)->pos.l=0;
    (tss+SOFFB)->pos.l=0;
    (tss+SOFFC)->pos.l=0;
    (tss+SOFFD)->pos.l=0;
   }

 for(i=0;i<iSortTexCnt;i++)
  {
   lu=pxSsubtexLeft[i];
   lu->l=0;
   if(bDelTex && uiStexturePage[i])
    {glDeleteTextures(1,&uiStexturePage[i]); glError();uiStexturePage[i]=0;}
  }
}


////////////////////////////////////////////////////////////////////////
// Invalidate tex windows
////////////////////////////////////////////////////////////////////////

/* Drop every texture-window entry whose page shares a halfword with [X0, X1] x [Y0, Y1]
 * (inclusive, inside VRAM). A page of depth k spans 64 << k halfwords (texInval.h): an
 * 8- or 15-bit window whose page starts left of the write is reached too, which the old
 * page-column range missed. */
void InvalidateWndTextureArea(int X0,int Y0,int X1,int Y1)
{
 int i;
 textureWndCacheEntry * tsw=wcWndtexStore;

 for(i=0;i<iMaxTexWnds;i++,tsw++)
  if(tsw->used &&
     texinval_page_hit((tsw->pageid & 15) << 6, (tsw->pageid >> 4) << 8, tsw->textureMode,
                       X0, Y0, X1, Y1))
   tsw->used=0;

 tsw=wcWndtexStore+iMaxTexWnds-1;
 while(iMaxTexWnds && !tsw->used) {iMaxTexWnds--;tsw--;}
}



////////////////////////////////////////////////////////////////////////
// same for sort textures
////////////////////////////////////////////////////////////////////////

void MarkFree(textureSubCacheEntryS * tsx)
{
 EXLong * ul, * uls;
 int j,iMax;unsigned char x1,y1,dx,dy;

 uls=pxSsubtexLeft[tsx->cTexID];
 iMax=GETLE32((unsigned long *)(uls));ul=uls+1;

 if(!iMax) return;

 for(j=0;j<iMax;j++,ul++)
  if(ul->l==0xffffffff) break;

 if(j<CSUBSIZE-2)
  {
   if(j==iMax) PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);

   x1=tsx->posTX;dx=tsx->pos.c.x2-tsx->pos.c.x1;
   if(tsx->posTX) {x1--;dx+=3;}
   y1=tsx->posTY;dy=tsx->pos.c.y2-tsx->pos.c.y1;
   if(tsx->posTY) {y1--;dy+=3;}

   ul->c.x1=x1;
   ul->c.x2=dx;
   ul->c.y1=y1;
   ul->c.y2=dy;
  }
}

int ogx_inv_src = 0;

#if PERF_PROF_GPUSPLIT
static void inv_rect_note(int x, int y, int w, int h, int mode, int page, const textureSubCacheEntryS *t)
{
 unsigned i;
 for (i = 0; i < g_perf.inv_rect_n; i++)
  if (g_perf.inv_rect[i].x == x && g_perf.inv_rect[i].y == y && g_perf.inv_rect[i].w == w &&
      g_perf.inv_rect[i].h == h && g_perf.inv_rect[i].src == ogx_inv_src) { g_perf.inv_rect[i].n++; return; }
 if (i >= 8) return;
 g_perf.inv_rect_n++;
 g_perf.inv_rect[i].x = x; g_perf.inv_rect[i].y = y; g_perf.inv_rect[i].w = w; g_perf.inv_rect[i].h = h;
 g_perf.inv_rect[i].src = ogx_inv_src; g_perf.inv_rect[i].mode = mode; g_perf.inv_rect[i].page = page;
 g_perf.inv_rect[i].ex1 = t->pos.c.x1; g_perf.inv_rect[i].ey1 = t->pos.c.y1;
 g_perf.inv_rect[i].ex2 = t->pos.c.x2; g_perf.inv_rect[i].ey2 = t->pos.c.y2;
 g_perf.inv_rect[i].n = 1;
}
#endif

/* Drop every cached sub-texture with a texel in the halfwords [X0, X1] x lines [Y0, Y1]
 * (inclusive, inside VRAM: InvalidateTextureArea splits a write at the edges). Per page and
 * depth, texInval.h gives the texels the write reaches; tests/texinval_test.c checks it
 * against brute force. It used to test only the first texel of the last written halfword
 * (a 4/8-bit entry starting in the rest stayed stale) and, with the width-1 convention
 * some callers did not follow, one column and row past a fill. */
void InvalidateSubSTextureArea(int X0,int Y0,int X1,int Y1)
{
 PERF_INC(ogx_vram_wr);
#if PERF_PROF_GPUSPLIT
 g_perf.inv_src_calls[ogx_inv_src & 7]++;
#define INV_DROP(t) (g_perf.inv_src_drop[ogx_inv_src & 7]++,   g_perf.inv_src_texels[ogx_inv_src & 7] += (unsigned)((t)->pos.c.x2 - (t)->pos.c.x1 + 1) * ((t)->pos.c.y2 - (t)->pos.c.y1 + 1),   inv_rect_note(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1, k, j, (t)))
#else
#define INV_DROP(t) ((void)0)
#endif
 int i,j,k,iMax,px,py,py1,py2,iYM=1;
 EXLong npos;textureSubCacheEntryS * tsb;

 if(iGPUHeight==1024) iYM=3;
 py1=min(iYM,Y0>>8);
 py2=min(iYM,Y1>>8);

 for(py=py1;py<=py2;py++)
  {
   int y1=max(Y0,py<<8), y2=min(Y1,(py<<8)+255);
   int vy=((y1&255)<<8)|(y2&255);
   int pxa=max(0,(X0>>6)-3), pxb=min(15,X1>>6);   /* a 15-bit page reaches 3 columns right */

   for(px=pxa;px<=pxb;px++)
    {
     j=(py<<4)+px;
     for(k=0;k<3;k++)
      {
       int t0,t1;
       if(!texinval_texels(px<<6,k,X0,X1,&t0,&t1)) continue;

       if (dwGPUVersion == 2)
        npos.l=SWAP32_C(0x00ff00ff);
       else
        PUTLE32(&npos.l, (t0<<24)|(t1<<16)|vy);

        {
         tsb=pscSubtexStore[k][j]+SOFFA;iMax=GETLE32(SUBCACHE_COUNT_PTR(tsb));tsb++;
#if PERF_PROF_GPUSPLIT
         g_perf.gpu_inv_scan += iMax;   /* list A; B-D below are the same length or less */
#endif
         for(i=0;i<iMax;i++,tsb++)
          if(tsb->ClutKey && XCHECK(tsb->pos,npos)) {INV_DROP(tsb);tsb->ClutKey=0;MarkFree(tsb);PERF_INC(ogx_inval);}

//         if(npos.l & 0x00800000)
          {
           tsb=pscSubtexStore[k][j]+SOFFB;iMax=GETLE32(SUBCACHE_COUNT_PTR(tsb));tsb++;
           for(i=0;i<iMax;i++,tsb++)
            if(tsb->ClutKey && XCHECK(tsb->pos,npos)) {INV_DROP(tsb);tsb->ClutKey=0;MarkFree(tsb);PERF_INC(ogx_inval);}
          }

//         if(npos.l & 0x00000080)
          {
           tsb=pscSubtexStore[k][j]+SOFFC;iMax=GETLE32(SUBCACHE_COUNT_PTR(tsb));tsb++;
           for(i=0;i<iMax;i++,tsb++)
            if(tsb->ClutKey && XCHECK(tsb->pos,npos)) {INV_DROP(tsb);tsb->ClutKey=0;MarkFree(tsb);PERF_INC(ogx_inval);}
          }

//         if(npos.l & 0x00800080)
          {
           tsb=pscSubtexStore[k][j]+SOFFD;iMax=GETLE32(SUBCACHE_COUNT_PTR(tsb));tsb++;
           for(i=0;i<iMax;i++,tsb++)
            if(tsb->ClutKey && XCHECK(tsb->pos,npos)) {INV_DROP(tsb);tsb->ClutKey=0;MarkFree(tsb);PERF_INC(ogx_inval);}
          }
        }
      }
    }
  }
#undef INV_DROP
}

////////////////////////////////////////////////////////////////////////
// Invalidate some parts of cache: main routine
////////////////////////////////////////////////////////////////////////

void InvalidateTextureAreaEx(void)
{
 ogx_inv_src = 4;                 /* sxmin..sxmax, symin..symax: inclusive primitive bounds */
 InvalidateTextureArea(sxmin, symin, sxmax - sxmin + 1, symax - symin + 1);
}

////////////////////////////////////////////////////////////////////////

/* Drop the cached textures under the VRAM rectangle x, y, w x h: real sizes, not w-1 (the
 * P.E.Op.S. convention some callers followed and some did not). VRAM wraps: the part past
 * the right or bottom edge is at x = 0 or y = 0, as for efb_cpu_write. */
void InvalidateTextureArea(int X,int Y,int W, int H)
{
 /* probe: every VRAM area whose cached textures are dropped (image loads,
  * moves, CPU writes) -- i.e. every way VRAM gets new content. */
#if PERF_PROF_GPU
 PERF_INC(ogx_va_all);
 if (X <= 895 && X + W >= 768 && Y <= 255) {
  PERF_INC(ogx_va_roi);
  if (g_perf.ogx_va_n < 8) {
   unsigned k = g_perf.ogx_va_n++;
   g_perf.ogx_va[k].x0 = X; g_perf.ogx_va[k].y0 = Y; g_perf.ogx_va[k].x1 = X + W; g_perf.ogx_va[k].y1 = Y + H;
  }
 }
#endif
 if(W<=0 || H<=0) { ogx_inv_src = 0; return; }
 /* every way VRAM gets new content comes here: all but a CPU load (whose changed pixels
  * CheckWriteUpdate uploads) also leave the EFB not mirroring psxVuw */
 if(ogx_inv_src != 1) MIRROR_OFF(6);
 if(W>1024) W=1024;
 if(H>iGPUHeight) H=iGPUHeight;
 X&=1023;
 Y&=iGPUHeightMask;
 if(X+W>1024)       { int s0=ogx_inv_src; InvalidateTextureArea(0,Y,X+W-1024,H); ogx_inv_src=s0; W=1024-X; }
 if(Y+H>iGPUHeight) { int s0=ogx_inv_src; InvalidateTextureArea(X,0,W,Y+H-iGPUHeight); ogx_inv_src=s0; H=iGPUHeight-Y; }

#if PERF_PROF_GPUSPLIT
 {
 unsigned long long inv_t0_ = perf_now_ticks();
 g_perf.gpu_inv_calls++;
#endif
 if(iMaxTexWnds) InvalidateWndTextureArea(X,Y,X+W-1,Y+H-1);

 InvalidateSubSTextureArea(X,Y,X+W-1,Y+H-1);
#if PERF_PROF_GPUSPLIT
 g_perf.gpu_inv_ticks += perf_now_ticks() - inv_t0_;
 }
#endif
 ogx_inv_src = 0;
}


////////////////////////////////////////////////////////////////////////
// tex window: define
////////////////////////////////////////////////////////////////////////

void DefineTextureWnd(void)
{
    glSetRGB24( 0 );

    if (gTexName==0)
    {
        glGenTextures(1, &gTexName);
        glError();
        glBindTextureBef(GL_TEXTURE_2D, gTexName);glError();
        glError();
    }
    else
    {
        glBindTextureBef(GL_TEXTURE_2D, gTexName);glError();
    }

    int textureType;
    textureType = glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB,
        TWin.Position.x1,
        TWin.Position.y1,
        0, GL_RGB, GL_UNSIGNED_BYTE, texturepart); glError();
    gl_ux[8] = textureType;
    #ifdef DISP_DEBUG
    if (logFileEnabled()) {
    sprintf ( txtbuffer, "DefineTextureWnd %d %d %d\r\n", TWin.Position.x1, TWin.Position.y1, textureType);
    writeLogFile ( txtbuffer );
    }
    #endif // DISP_DEBUG

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glError();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter);
    glError();
}

////////////////////////////////////////////////////////////////////////
// tex window: load stretched
////////////////////////////////////////////////////////////////////////

void LoadStretchWndTexturePage(int pageid, int mode, short cx, short cy)
{
 unsigned int start,row,column,j,sxh,sxm,ldx,ldy,ldxo,s;
 unsigned int   palstart;
 unsigned int  *px,*pa,*ta;
 unsigned char  *cSRCPtr,*cOSRCPtr;
 unsigned short *wSRCPtr,*wOSRCPtr;
 unsigned int  LineOffset;
 int pmult=pageid/16;
 unsigned int (*LTCOL)(unsigned int);

 LTCOL=TCF[DrawSemiTrans];

 ldxo=TWin.Position.x1-TWin.OPosition.x1;
 ldy =TWin.Position.y1-TWin.OPosition.y1;

 pa=px=(unsigned int *)ubPaletteBuffer;
 ta=(unsigned int *)texturepart;
 palstart=cx+(cy*1024);

 ubOpaqueDraw=0;

 switch(mode)
  {
   //--------------------------------------------------//
   // 4bit texture load ..
   case 0:
    //------------------- ZN STUFF

    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=4;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      column=g_y2-ldy;
      for(TXV=g_y1;TXV<=column;TXV++)
       {
        ldx=ldxo;
        for(TXU=g_x1;TXU<=g_x2-ldxo;TXU++)
         {
  		  n_xi = ( ( TXU >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
		  n_yi = ( TXV & ~0xf ) + ( ( TXU >> 4 ) & 0xf );

          s = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x03 ) << 2 ) ) & 0x0f ));
          *ta++ = s;

          if(ldx) {
            *ta++ = s;
            ldx--;
          }
         }

        if(ldy)
         {ldy--;
          for(TXU=g_x1;TXU<=g_x2;TXU++)
           *ta = *(ta-(g_x2-g_x1)); ta++; // split: reading and incrementing ta in one expr is unsequenced UB
         }
       }

      DefineTextureWnd();

      break;
     }

    //-------------------

    start=((pageid-16*pmult)*128)+256*2048*pmult;
    // convert CLUT to 32bits .. and then use THAT as a lookup table

    wSRCPtr=psxVuw+palstart;
    for(row=0;row<16;row++)
     //*px++=LTCOL(*wSRCPtr++);
     *px++=LTCOL(GETLE16((unsigned short *)(wSRCPtr++)));

    sxm=g_x1&1;sxh=g_x1>>1;
    if(sxm) j=g_x1+1; else j=g_x1;
    cSRCPtr = psxVub + start + (2048*g_y1) + sxh;
    for(column=g_y1;column<=g_y2;column++)
     {
      cOSRCPtr=cSRCPtr;ldx=ldxo;
      if(sxm) *ta++=*(pa+((*cSRCPtr++ >> 4) & 0xF));

      for(row=j;row<=g_x2-ldxo;row++)
       {
        s=*(pa+(*cSRCPtr & 0xF));
        *ta++=s;
        if(ldx) {*ta++=s;ldx--;}
        row++;
        if(row<=g_x2-ldxo)
         {
          s=*(pa+((*cSRCPtr >> 4) & 0xF));
          *ta++=s;
          if(ldx) {*ta++=s;ldx--;}
         }
        cSRCPtr++;
       }
      if(ldy && column&1)
           {ldy--;cSRCPtr = cOSRCPtr;}
      else cSRCPtr = psxVub + start + (2048*(column+1)) + sxh;
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // 8bit texture load ..
   case 1:
    //------------ ZN STUFF
    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=64;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      column=g_y2-ldy;
      for(TXV=g_y1;TXV<=column;TXV++)
       {
        ldx=ldxo;
        for(TXU=g_x1;TXU<=g_x2-ldxo;TXU++)
         {
  		  n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
		  n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

          //s=*(pa+((*( psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi ) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff));
          //*ta++=s;
          s = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff ));
          *ta++ = s;
          if(ldx) {
            *ta++ = s;
            ldx--;
          }
         }

        if(ldy)
         {ldy--;
          for(TXU=g_x1;TXU<=g_x2;TXU++)
           *ta = *(ta-(g_x2-g_x1)); ta++; // split: reading and incrementing ta in one expr is unsequenced UB
         }

       }

      DefineTextureWnd();

      break;
     }
    //------------

    start=((pageid-16*pmult)*128)+256*2048*pmult;

    // not using a lookup table here... speeds up smaller texture areas
    cSRCPtr = psxVub + start + (2048*g_y1) + g_x1;
    LineOffset = 2048 - (g_x2-g_x1+1) +ldxo;

    for(column=g_y1;column<=g_y2;column++)
     {
      cOSRCPtr=cSRCPtr;ldx=ldxo;
      for(row=g_x1;row<=g_x2-ldxo;row++)
       {
        s=LTCOL(GETLE16(&psxVuw[palstart+ *cSRCPtr++]));
        *ta++=s;
        if(ldx) {*ta++=s;ldx--;}
       }
      if(ldy && column&1) {ldy--;cSRCPtr=cOSRCPtr;}
      else                cSRCPtr+=LineOffset;
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // 16bit texture load ..
   case 2:
    start=((pageid-16*pmult)*64)+256*1024*pmult;

    wSRCPtr = psxVuw + start + (1024*g_y1) + g_x1;
    LineOffset = 1024 - (g_x2-g_x1+1) +ldxo;

    for(column=g_y1;column<=g_y2;column++)
     {
      wOSRCPtr=wSRCPtr;ldx=ldxo;
      for(row=g_x1;row<=g_x2-ldxo;row++)
       {
        s=LTCOL(GETLE16((unsigned short *)(wSRCPtr++)));
        *ta++=s;

        if(ldx) {
            *ta++=s;
            ldx--;
        }
       }
      if(ldy && column&1) {ldy--;wSRCPtr=wOSRCPtr;}
      else                 wSRCPtr+=LineOffset;
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // others are not possible !
  }
}

////////////////////////////////////////////////////////////////////////
// tex window: load simple
////////////////////////////////////////////////////////////////////////

void LoadWndTexturePage(int pageid, int mode, short cx, short cy)
{
 unsigned int start,row,column,j,sxh,sxm;
 unsigned int   palstart;
 unsigned int  *px,*pa,*ta;
 unsigned char  *cSRCPtr;
 unsigned short *wSRCPtr;
 unsigned int  LineOffset;
 int pmult=pageid/16;
 unsigned int (*LTCOL)(unsigned int);

 LTCOL=TCF[DrawSemiTrans];

 pa=px=(unsigned int *)ubPaletteBuffer;
 ta=(unsigned int *)texturepart;
 palstart=cx+(cy*1024);

 ubOpaqueDraw=0;

 switch(mode)
  {
   //--------------------------------------------------//
   // 4bit texture load ..
   case 0:
    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=4;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      for(TXV=g_y1;TXV<=g_y2;TXV++)
       {
        for(TXU=g_x1;TXU<=g_x2;TXU++)
         {
  		  n_xi = ( ( TXU >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
		  n_yi = ( TXV & ~0xf ) + ( ( TXU >> 4 ) & 0xf );

          //*ta++=*(pa+((*( psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi ) >> ( ( TXU & 0x03 ) << 2 ) ) & 0x0f ));
          *ta++ = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x03 ) << 2 ) ) & 0x0f ));
         }
       }

      DefineTextureWnd();

      break;
     }

    start=((pageid-16*pmult)*128)+256*2048*pmult;

    // convert CLUT to 32bits .. and then use THAT as a lookup table

    wSRCPtr=psxVuw+palstart;
    for(row=0;row<16;row++)
     *px++=LTCOL(GETLE16((unsigned short *)(wSRCPtr++)));

    sxm=g_x1&1;sxh=g_x1>>1;
    if(sxm) j=g_x1+1; else j=g_x1;
    cSRCPtr = psxVub + start + (2048*g_y1) + sxh;
    for(column=g_y1;column<=g_y2;column++)
     {
      cSRCPtr = psxVub + start + (2048*column) + sxh;

      if(sxm) *ta++=*(pa+((*cSRCPtr++ >> 4) & 0xF));

      for(row=j;row<=g_x2;row++)
       {
        *ta++=*(pa+(*cSRCPtr & 0xF)); row++;
        if(row<=g_x2) *ta++=*(pa+((*cSRCPtr >> 4) & 0xF));
        cSRCPtr++;
       }
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // 8bit texture load ..
   case 1:
    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=64;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      for(TXV=g_y1;TXV<=g_y2;TXV++)
       {
        for(TXU=g_x1;TXU<=g_x2;TXU++)
         {
  		  n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
		  n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

          //*ta++=*(pa+((*( psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi ) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff));
          *ta++ = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff ));
         }
       }

      DefineTextureWnd();

      break;
     }

    start=((pageid-16*pmult)*128)+256*2048*pmult;

    // not using a lookup table here... speeds up smaller texture areas
    cSRCPtr = psxVub + start + (2048*g_y1) + g_x1;
    LineOffset = 2048 - (g_x2-g_x1+1);

    for(column=g_y1;column<=g_y2;column++)
     {
      for(row=g_x1;row<=g_x2;row++)
       *ta++=LTCOL(GETLE16(&psxVuw[palstart+ *cSRCPtr++]));
      cSRCPtr+=LineOffset;
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // 16bit texture load ..
   case 2:
    start=((pageid-16*pmult)*64)+256*1024*pmult;

    wSRCPtr = psxVuw + start + (1024*g_y1) + g_x1;
    LineOffset = 1024 - (g_x2-g_x1+1);

    for(column=g_y1;column<=g_y2;column++)
     {
      for(row=g_x1;row<=g_x2;row++)
       *ta++=LTCOL(GETLE16((unsigned short *)(wSRCPtr++)));
      wSRCPtr+=LineOffset;
     }

    DefineTextureWnd();
    break;
   //--------------------------------------------------//
   // others are not possible !
  }
}

////////////////////////////////////////////////////////////////////////
// tex window: main selecting, cache handler included
////////////////////////////////////////////////////////////////////////

GLuint LoadTextureWnd(int pageid,int TextureMode,unsigned int GivenClutId)
{
 textureWndCacheEntry * ts, * tsx=NULL;
 uint64_t clutKey;
 int i;short cx,cy;
 EXLong npos;

 npos.c.x1=TWin.Position.x0;
 npos.c.x2=TWin.OPosition.x1;
 npos.c.y1=TWin.Position.y0;
 npos.c.y2=TWin.OPosition.y1;

 g_x1=TWin.Position.x0;g_x2=g_x1+TWin.Position.x1-1;
 g_y1=TWin.Position.y0;g_y2=g_y1+TWin.Position.y1-1;

 if(TextureMode==2) {cx=cy=0;}
 else
  {
   cx=((GivenClutId << 4) & 0x3F0);cy=((GivenClutId >> 6) & CLUTYMASK);
  }
 clutKey=BuildClutCacheKey(GivenClutId,TextureMode,DrawSemiTrans);

 ts=wcWndtexStore;

 for(i=0;i<iMaxTexWnds;i++,ts++)
  {
   if(ts->used)
    {
     if(ts->pos.l==npos.l &&
        ts->pageid==pageid &&
        ts->textureMode==TextureMode)
      {
       if(ts->ClutKey==clutKey)
        {
         ubOpaqueDraw=ts->Opaque;
         gl_ux[8]=ts->textureType;
         return ts->texname;
        }
      }
    }
   else tsx=ts;
  }

 if(!tsx)
  {
   if(iMaxTexWnds==iTexWndLimit)
    {
     tsx=wcWndtexStore+iTexWndTurn;
     iTexWndTurn++;
     if(iTexWndTurn==iTexWndLimit) iTexWndTurn=0;
    }
   else
    {
     tsx=wcWndtexStore+iMaxTexWnds;
     iMaxTexWnds++;
    }
  }

 gTexName=tsx->texname;
 texChgType = 1;
 //GX_Flush();
 //GX_SetDrawDone();

 if(TWin.OPosition.y1==TWin.Position.y1 &&
    TWin.OPosition.x1==TWin.Position.x1)
  {
    LoadWndTexturePage(pageid,TextureMode,cx,cy);
  }
 else
  {
    LoadStretchWndTexturePage(pageid,TextureMode,cx,cy);
  }

 tsx->Opaque=ubOpaqueDraw;
 tsx->pos.l=npos.l;
 tsx->ClutKey=clutKey;
 tsx->pageid=pageid;
 tsx->textureMode=TextureMode;
 tsx->texname=gTexName;
 tsx->used=1;
 tsx->textureType=gl_ux[8];

 return gTexName;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// movie texture: define
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////

static inline void DefineTextureMovie(void)
{
 if(gTexMovieName==0)
  {
   glGenTextures(1, &gTexMovieName); glError();
   gTexName=gTexMovieName;
   glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();

    glResetMovieTexPtr();
    gl_ux[8] = glInitMovieTextures((xrMovieArea.x1-xrMovieArea.x0), (xrMovieArea.y1-xrMovieArea.y0), texturepart);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iClampType); glError();
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iClampType); glError();

//   if(!bUseFastMdec)
//    {
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glError();
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glError();
//    }
//   else
//    {
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter); glError();
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter); glError();
//    }
   //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, texturepart); glError();

  }
 else
  {
   gTexName=gTexMovieName;glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();
   gl_ux[8] = glInitMovieTextures((xrMovieArea.x1-xrMovieArea.x0), (xrMovieArea.y1-xrMovieArea.y0), texturepart);
  }

//  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
//                 (xrMovieArea.x1-xrMovieArea.x0),
//                 (xrMovieArea.y1-xrMovieArea.y0),
//                 GL_RGBA, GL_UNSIGNED_BYTE, texturepart); glError();


 //LOGE("DefineTextureMovie x:%d y:%d",(xrMovieArea.x1-xrMovieArea.x0),(xrMovieArea.y1-xrMovieArea.y0));
}

////////////////////////////////////////////////////////////////////////
// movie texture: load
////////////////////////////////////////////////////////////////////////

#define MRED(x)   ((x>>3) & 0x1f)
#define MGREEN(x) ((x>>6) & 0x3e0)
#define MBLUE(x)  ((x>>9) & 0x7c00)

#define XMGREEN(x) ((x>>5)  & 0x07c0)
#define XMRED(x)   ((x<<8)  & 0xf800)
#define XMBLUE(x)  ((x>>18) & 0x003e)

////////////////////////////////////////////////////////////////////////
// movie texture: load
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////

GLuint LoadTextureMovie(void)
{
    texChgType = 2;
    //GX_Flush();
    //GX_SetDrawDone();

 short row,column,dx;
 unsigned int startxy;
 BOOL b_X,b_Y;

 //if(bUseFastMdec) return LoadTextureMovieFast();

// b_X=FALSE;b_Y=FALSE;
//
// if((xrMovieArea.x1-xrMovieArea.x0)<255)  b_X=TRUE;
// if((xrMovieArea.y1-xrMovieArea.y0)<255)  b_Y=TRUE;

{
   if(PSXDisplay.RGB24)
    {
        #ifdef DISP_DEBUG
        //sprintf(txtbuffer, "LoadMovie1 %d %d %d %d %d %d\r\n",   xrMovieArea.x0, xrMovieArea.y0, xrMovieArea.x1, xrMovieArea.y1, b_X, b_Y);
        //DEBUG_print(txtbuffer, DBG_SPU2);
        //writeLogFile(txtbuffer);
        #endif // DISP_DEBUG

     unsigned char * pD;
     unsigned int * ta=(unsigned int *)texturepart;

//     if(b_X)
//      {
//       for(column=xrMovieArea.y0;column<xrMovieArea.y1;column++)
//        {
//         startxy=((1024)*column)+xrMovieArea.x0;
//         pD=(unsigned char *)&psxVuw[startxy];
//         for(row=xrMovieArea.x0;row<xrMovieArea.x1;row++)
//          {
//           PUTLE32(ta++, *((unsigned int *)pD)|SWAP32_C(0xff000000));
//           pD+=3;
//          }
//         *ta++=*(ta-1);
//        }
//       if(b_Y)
//        {
//         dx=xrMovieArea.x1-xrMovieArea.x0+1;
//         for(row=xrMovieArea.x0;row<xrMovieArea.x1;row++)
//          *ta++=*(ta-dx);
//         *ta++=*(ta-1);
//        }
//      }
//     else
      /* 24BIT: opengx tiles the RGBA8 texture straight from these VRAM rows (2048 bytes
       * apart, 3 bytes a pixel): one pass instead of a 32-bit copy here and a second pass
       * over it (gc_gl.c glSetMovieSourceRGB24). 15BIT: RGB5A3 in the low half. */
      if (fmvColour == FMVCOLOUR_24BIT)
       {
        extern void glSetMovieSourceRGB24(const void *src, int pitch);
        glSetMovieSourceRGB24(&psxVuw[(1024*xrMovieArea.y0)+xrMovieArea.x0], 2048);
       }
      else
      {
       for(column=xrMovieArea.y0;column<xrMovieArea.y1;column++)
        {
         startxy=((1024)*column)+xrMovieArea.x0;
         pD=(unsigned char *)&psxVuw[startxy];
         for(row=xrMovieArea.x0;row<xrMovieArea.x1;row++)
          {
           *ta++ = MOV24to16(pD)  | 0x8000;
           pD+=3;
          }
        }
//       if(b_Y)
//        {
//         dx=xrMovieArea.x1-xrMovieArea.x0;
//         for(row=xrMovieArea.x0;row<xrMovieArea.x1;row++)
//          *ta++=*(ta-dx);
//        }
      }
    }
   else
    {
     /* 16BIT: opengx tiles the RGB5A3 texture straight from VRAM, in one pass
      * (gc_gl.c glSetMovieSource16). The mask is what P8RGBA_0/1 tested when this widened
      * every pixel to 32 bits first: the whole pixel opaque, its colour bits semi-transparent. */
     extern void glSetMovieSource16(const void *src, int pitch, unsigned short zero);

     ubOpaqueDraw=0;
     glSetMovieSource16(&psxVuw[(1024*xrMovieArea.y0)+xrMovieArea.x0], 1024,
                        DrawSemiTrans ? 0x7fff : 0xffff);
    }

   //xrMovieArea.x1+=b_X;xrMovieArea.y1+=b_Y;
   DefineTextureMovie();
   //xrMovieArea.x1-=b_X;xrMovieArea.y1-=b_Y;
  }
 return gTexName;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

GLuint BlackFake15BitTexture(void)
{
 int pmult;short x1,x2,y1,y2;

 if(PSXDisplay.InterlacedTest) return 0;

 pmult=GlobalTexturePage/16;
 x1=gl_ux[7];
 x2=gl_ux[6]-gl_ux[7];
 y1=gl_ux[5];
 y2=gl_ux[4]-gl_ux[5];

 if(iSpriteTex)
  {
   if(x2<255) x2++;
   if(y2<255) y2++;
  }

 y1+=pmult*256;
 x1+=((GlobalTexturePage-16*pmult)<<6);

 if(   FastCheckAgainstFrontScreen(x1,y1,x2,y2)
    || FastCheckAgainstScreen(x1,y1,x2,y2))
  {
   if(!gTexFrameName)
    {
     glGenTextures(1, &gTexFrameName); glError();
     gTexName=gTexFrameName;
     glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();

     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iClampType); glError();
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iClampType); glError();
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter); glError();
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter); glError();

     {
       unsigned int * ta=(unsigned int *)texturepart;
       for(y1=0;y1<=4;y1++)
        for(x1=0;x1<=4;x1++)
         *ta++=0xff000000;
      }
      #ifdef DISP_DEBUG
//sprintf(txtbuffer, "Fake15BitTexture 4 4\r\n");
//DEBUG_print(txtbuffer, DBG_SPU1);
#endif // DISP_DEBUG
     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texturepart); glError();

    }
   else
    {
     gTexName=gTexFrameName;
     glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();
    }
      //LOGE("BlackFake15BitTexture x:%d y:%d",4,4);
   ubOpaqueDraw=0;

   return (GLuint)gTexName;
  }
 return 0;
}

/////////////////////////////////////////////////////////////////////////////

BOOL bFakeFrontBuffer=FALSE;
BOOL bIgnoreNextTile =FALSE;

int iFTex=512;

GLuint Fake15BitTexture(void)
{

    #ifdef DISP_DEBUG
//sprintf(txtbuffer, "Fake15BitTexture 0\r\n");
//DEBUG_print(txtbuffer, DBG_SPU1);
//writeLogFile(txtbuffer);
#endif // DISP_DEBUG

 int pmult;short x1,x2,y1,y2;int iYAdjust;
 float ScaleX,ScaleY;RECT rSrc;

 if(iFrameTexType==1) return BlackFake15BitTexture();
 if(PSXDisplay.InterlacedTest) return 0;

 pmult=GlobalTexturePage/16;
 x1=gl_ux[7];
 x2=gl_ux[6]-gl_ux[7];
 y1=gl_ux[5];
 y2=gl_ux[4]-gl_ux[5];

 y1+=pmult*256;
 x1+=((GlobalTexturePage-16*pmult)<<6);

 if(iFrameTexType==3)
  {
   if(iFrameReadType==4) return 0;

   if(!FastCheckAgainstFrontScreen(x1,y1,x2,y2) &&
      !FastCheckAgainstScreen(x1,y1,x2,y2))
    return 0;

   if(bFakeFrontBuffer) bIgnoreNextTile=TRUE;
   CheckVRamReadEx(x1,y1,x1+x2,y1+y2);
   return 0;
  }

 /////////////////////////

 if(FastCheckAgainstFrontScreen(x1,y1,x2,y2))
  {
   x1-=PSXDisplay.DisplayPosition.x;
   y1-=PSXDisplay.DisplayPosition.y;
  }
 else
 if(FastCheckAgainstScreen(x1,y1,x2,y2))
  {
   x1-=PreviousPSXDisplay.DisplayPosition.x;
   y1-=PreviousPSXDisplay.DisplayPosition.y;
  }
 else return 0;

 if(!gTexFrameName)
  {
   char * p;

   if(iResX>1280 || iResY>1024) iFTex=2048;
   else
   if(iResX>640  || iResY>480)  iFTex=1024;
   else                         iFTex=512;

   glGenTextures(1, &gTexFrameName); glError();
   gTexName=gTexFrameName;
   glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iClampType); glError();
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iClampType); glError();
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter); glError();
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter); glError();

   p=(char *)_mem2_malloc(iFTex*iFTex*4);
   if(p)
    memset(p,0,iFTex*iFTex*4);
   #ifdef DISP_DEBUG
//sprintf(txtbuffer, "Fake15BitTexture            %d %d\r\n", iFTex, iFTex);
//DEBUG_print(txtbuffer, DBG_SPU1);
#endif // DISP_DEBUG
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iFTex, iFTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, p); glError();
   _mem2_free(p);

   glGetError();
  }
 else
  {
   gTexName=gTexFrameName;
   glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();
  }
      //LOGE("Fake15BitTexture x:%d y:%d",iFTex,iFTex);
 x1+=PreviousPSXDisplay.Range.x0;
 y1+=PreviousPSXDisplay.Range.y0;

 if(PSXDisplay.DisplayMode.x)
      ScaleX=(float)rRatioRect.right/(float)PSXDisplay.DisplayMode.x;
 else ScaleX=1.0f;
 if(PSXDisplay.DisplayMode.y)
      ScaleY=(float)rRatioRect.bottom/(float)PSXDisplay.DisplayMode.y;
 else ScaleY=1.0f;

 rSrc.left  =max(x1*ScaleX,0);
 rSrc.right =min((x1+x2)*ScaleX+0.99f,iResX-1);
 rSrc.top   =max(y1*ScaleY,0);
 rSrc.bottom=min((y1+y2)*ScaleY+0.99f,iResY-1);

 iYAdjust=(y1+y2)-PSXDisplay.DisplayMode.y;
 if(iYAdjust>0)
      iYAdjust=(int)((float)iYAdjust*ScaleY)+1;
 else iYAdjust=0;

 gl_vy[0]=255-gl_vy[0];
 gl_vy[1]=255-gl_vy[1];
 gl_vy[2]=255-gl_vy[2];
 gl_vy[3]=255-gl_vy[3];

 y1=min(gl_vy[0],min(gl_vy[1],min(gl_vy[2],gl_vy[3])));

 gl_vy[0]-=y1;
 gl_vy[1]-=y1;
 gl_vy[2]-=y1;
 gl_vy[3]-=y1;
 gl_ux[0]-=gl_ux[7];
 gl_ux[1]-=gl_ux[7];
 gl_ux[2]-=gl_ux[7];
 gl_ux[3]-=gl_ux[7];

 ScaleX*=256.0f/((float)(iFTex));
 ScaleY*=256.0f/((float)(iFTex));

 y1=((float)gl_vy[0]*ScaleY); if(y1>255) y1=255;
 gl_vy[0]=y1;
 y1=((float)gl_vy[1]*ScaleY); if(y1>255) y1=255;
 gl_vy[1]=y1;
 y1=((float)gl_vy[2]*ScaleY); if(y1>255) y1=255;
 gl_vy[2]=y1;
 y1=((float)gl_vy[3]*ScaleY); if(y1>255) y1=255;
 gl_vy[3]=y1;

 x1=((float)gl_ux[0]*ScaleX); if(x1>255) x1=255;
 gl_ux[0]=x1;
 x1=((float)gl_ux[1]*ScaleX); if(x1>255) x1=255;
 gl_ux[1]=x1;
 x1=((float)gl_ux[2]*ScaleX); if(x1>255) x1=255;
 gl_ux[2]=x1;
 x1=((float)gl_ux[3]*ScaleX); if(x1>255) x1=255;
 gl_ux[3]=x1;

 x1=rSrc.right-rSrc.left;
 if(x1<=0)             x1=1;
 if(x1>iFTex)          x1=iFTex;

 y1=rSrc.bottom-rSrc.top;
 if(y1<=0)             y1=1;
 if(y1+iYAdjust>iFTex) y1=iFTex-iYAdjust;


 #ifdef DISP_DEBUG
//sprintf(txtbuffer, "glCopyTexSubImage2D\r\n");
//DEBUG_print(txtbuffer, DBG_CDR4);
#endif // DISP_DEBUG
 glCopyTexSubImage2D( GL_TEXTURE_2D, 0,
                      0,
                      iYAdjust,
                      rSrc.left+rRatioRect.left,
                      iResY-rSrc.bottom-rRatioRect.top,
                      x1,y1); glError();

// if(glGetError()) // never error
//  {
//   char * p=(char *)_mem2_malloc(iFTex*iFTex*4);
//   memset(p,0,iFTex*iFTex*4);
//   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, iFTex, iFTex,
//                   GL_RGBA, GL_UNSIGNED_BYTE, p); glError();
//   _mem2_free(p);
//  }


 ubOpaqueDraw=0;

 if(iSpriteTex)
  {
   sprtW=gl_ux[1]-gl_ux[0];
   sprtH=-(gl_vy[0]-gl_vy[2]);
  }

 return (GLuint)gTexName;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// load texture part (unpacked)
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

static void LoadSubTexturePageSortBody(int pageid, int mode, short cx, short cy);

/* Timed wrapper: this is the CLUT expansion, the CPU kernel measured as a
 * locked-cache candidate (perf.log "texk:"). gl_ux[4..7] hold the rectangle. */
void LoadSubTexturePageSort(int pageid, int mode, short cx, short cy)
{
 unsigned long long t0 = perf_now_ticks();
 LoadSubTexturePageSortBody(pageid, mode, cx, cy);
 PERF_ADD(ogx_conv_ticks, perf_now_ticks() - t0);
 PERF_INC(ogx_conv_calls);
 PERF_ADD(ogx_conv_texels, (unsigned long long)(gl_ux[6] - gl_ux[7] + 1) * (gl_ux[4] - gl_ux[5] + 1));
}

static void LoadSubTexturePageSortBody(int pageid, int mode, short cx, short cy)
{
 unsigned int  start,row,column,j,sxh,sxm;
 unsigned int   palstart;
 unsigned int  *px,*pa,*ta;
 unsigned char  *cSRCPtr;
 unsigned short *wSRCPtr;
 unsigned int  LineOffset;
 unsigned int  x2a,xalign=0;
 unsigned int  x1=gl_ux[7];
 unsigned int  x2=gl_ux[6];
 unsigned int  y1=gl_ux[5];
 unsigned int  y2=gl_ux[4];
 unsigned int  dx=x2-x1+1;
 unsigned int  dy=y2-y1+1;
 int pmult=pageid/16;
 unsigned int (*LTCOL)(unsigned int);
 unsigned int a,r,g,b,cnt,h;
 unsigned int scol[8];

 LTCOL=TCF[DrawSemiTrans];

 pa=px=(unsigned int *)ubPaletteBuffer;
 ta=(unsigned int *)texturepart;
 palstart=cx+(cy<<10);

 ubOpaqueDraw=0;

 if(YTexS) {ta+=dx;if(XTexS) ta+=2;}
 if(XTexS) {ta+=1;xalign=2;}

 switch(mode)
  {
   //--------------------------------------------------//
   // 4bit texture load ..
   case 0:
    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=4;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      for(TXV=y1;TXV<=y2;TXV++)
       {
        for(TXU=x1;TXU<=x2;TXU++)
         {
  		  n_xi = ( ( TXU >> 2 ) & ~0x3c ) + ( ( TXV << 2 ) & 0x3c );
		  n_yi = ( TXV & ~0xf ) + ( ( TXU >> 4 ) & 0xf );

          //*ta++=*(pa+((*( psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi ) >> ( ( TXU & 0x03 ) << 2 ) ) & 0x0f ));
          *ta++ = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x03 ) << 2 ) ) & 0x0f ));
         }
        ta+=xalign;
       }
      break;
     }

    start=((pageid-16*pmult)<<7)+524288*pmult;
    // convert CLUT to 32bits .. and then use THAT as a lookup table

    wSRCPtr=psxVuw+palstart;

    row=4;do
     {
      *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
      *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
      *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
      *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
      row--;px+=4;wSRCPtr+=4;
     }
    while (row);

    x2a=x2?(x2-1):0;//if(x2) x2a=x2-1; else x2a=0;
    sxm=x1&1;sxh=x1>>1;
    j=sxm?(x1+1):x1;//if(sxm) j=x1+1; else j=x1;
    for(column=y1;column<=y2;column++)
     {
      cSRCPtr = psxVub + start + (column<<11) + sxh;

      if(sxm) *ta++=*(pa+((*cSRCPtr++ >> 4) & 0xF));

      for(row=j;row<x2a;row+=2)
       {
        *ta    =*(pa+(*cSRCPtr & 0xF));
        *(ta+1)=*(pa+((*cSRCPtr >> 4) & 0xF));
        cSRCPtr++;ta+=2;
       }

      if(row<=x2)
       {
        *ta++=*(pa+(*cSRCPtr & 0xF)); row++;
        if(row<=x2) *ta++=*(pa+((*cSRCPtr >> 4) & 0xF));
       }

      ta+=xalign;
     }

    break;
   //--------------------------------------------------//
   // 8bit texture load ..
   case 1:
    if(GlobalTextIL)
     {
      unsigned int TXV,TXU,n_xi,n_yi;

      wSRCPtr=psxVuw+palstart;

      row=64;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      for(TXV=y1;TXV<=y2;TXV++)
       {
        for(TXU=x1;TXU<=x2;TXU++)
         {
  		  n_xi = ( ( TXU >> 1 ) & ~0x78 ) + ( ( TXU << 2 ) & 0x40 ) + ( ( TXV << 3 ) & 0x38 );
		  n_yi = ( TXV & ~0x7 ) + ( ( TXU >> 5 ) & 0x7 );

          //*ta++=*(pa+((*( psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi ) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff));
          *ta++ = *(pa + ((GETLE16((unsigned short *)(psxVuw + ((GlobalTextAddrY + n_yi)*1024) + GlobalTextAddrX + n_xi)) >> ( ( TXU & 0x01 ) << 3 ) ) & 0xff ));
         }
        ta+=xalign;
       }

      break;
     }

    start=((pageid-16*pmult)<<7)+524288*pmult;

    cSRCPtr = psxVub + start + (y1<<11) + x1;
    LineOffset = 2048 - dx;

    if(dy*dx>384)
     {
      wSRCPtr=psxVuw+palstart;

      row=64;do
       {
        *px    =LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
        *(px+1)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 1)));
        *(px+2)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 2)));
        *(px+3)=LTCOL(GETLE16((unsigned short *)(wSRCPtr + 3)));
        row--;px+=4;wSRCPtr+=4;
       }
      while (row);

      column=dy;do
       {
        row=dx;
        do {*ta++=*(pa+(*cSRCPtr++));row--;} while(row);
        ta+=xalign;
        cSRCPtr+=LineOffset;column--;
       }
      while(column);
     }
    else
     {
      wSRCPtr=psxVuw+palstart;

      column=dy;do
       {
        row=dx;
        //do {*ta++=LTCOL(GETLE16(wSRCPtr+*cSRCPtr++));row--;} while(row);
        do {
            *ta++ = LTCOL(GETLE16((unsigned short *)(wSRCPtr + *cSRCPtr++)));
            row--;
        }
        while(row);
        ta+=xalign;
        cSRCPtr+=LineOffset;column--;
       }
      while(column);
     }

    break;
   //--------------------------------------------------//
   // 16bit texture load ..
   case 2:
    start=((pageid-16*pmult)<<6)+262144*pmult;

    wSRCPtr = psxVuw + start + (y1<<10) + x1;
    LineOffset = 1024 - dx;

    column=dy;do
     {
      row=dx;
      //do {*ta++=LTCOL(*wSRCPtr++);row--;} while(row);
      do {
            *ta++ = LTCOL(GETLE16((unsigned short *)(wSRCPtr)));
			wSRCPtr++;
            row--;
      }
      while(row);
      ta+=xalign;
      wSRCPtr+=LineOffset;column--;
     }
    while(column);

    break;
   //--------------------------------------------------//
   // others are not possible !
  }

 x2a=dx+xalign;

 if(YTexS)
  {
   ta=(unsigned int *)texturepart;
   pa=(unsigned int *)texturepart+x2a;
   row=x2a;do {*ta++=*pa++;row--;} while(row);
   pa=(unsigned int *)texturepart+dy*x2a;
   ta=pa+x2a;
   row=x2a;do {*ta++=*pa++;row--;} while(row);
   YTexS--;
   dy+=2;
  }

 if(XTexS)
  {
   ta=(unsigned int *)texturepart;
   pa=ta+1;
   row=dy;do {*ta=*pa;ta+=x2a;pa+=x2a;row--;} while(row);
   pa=(unsigned int *)texturepart+dx;
   ta=pa+1;
   row=dy;do {*ta=*pa;ta+=x2a;pa+=x2a;row--;} while(row);
   XTexS--;
   dx+=2;
  }

 DXTexS=dx;DYTexS=dy;

 if(!iFilterType) {DefineSubTextureSort();return;}
 if(iFilterType!=2 && iFilterType!=4 && iFilterType!=6) {DefineSubTextureSort();return;}
 if((iFilterType==4 || iFilterType==6) && ly0==ly1 && ly2==ly3 && lx0==lx3 && lx1==lx2)
  {DefineSubTextureSort();return;}

// ta=(unsigned int *)texturepart;
// x1=dx-1;
// y1=dy-1;
//
// if(bOpaquePass)
//  {
//{
//     for(column=0;column<dy;column++)
//      {
//       for(row=0;row<dx;row++)
//        {
//         if(*ta==0x50000000)
//          {
//           cnt=0;
//
//           if(           column     && *(ta-dx)  !=0x50000000 && *(ta-dx)>>24!=1) scol[cnt++]=*(ta-dx);
//           if(row                   && *(ta-1)   !=0x50000000 && *(ta-1)>>24!=1) scol[cnt++]=*(ta-1);
//           if(row!=x1               && *(ta+1)   !=0x50000000 && *(ta+1)>>24!=1) scol[cnt++]=*(ta+1);
//           if(           column!=y1 && *(ta+dx)  !=0x50000000 && *(ta+dx)>>24!=1) scol[cnt++]=*(ta+dx);
//
//           if(row     && column     && *(ta-dx-1)!=0x50000000 && *(ta-dx-1)>>24!=1) scol[cnt++]=*(ta-dx-1);
//           if(row!=x1 && column     && *(ta-dx+1)!=0x50000000 && *(ta-dx+1)>>24!=1) scol[cnt++]=*(ta-dx+1);
//           if(row     && column!=y1 && *(ta+dx-1)!=0x50000000 && *(ta+dx-1)>>24!=1) scol[cnt++]=*(ta+dx-1);
//           if(row!=x1 && column!=y1 && *(ta+dx+1)!=0x50000000 && *(ta+dx+1)>>24!=1) scol[cnt++]=*(ta+dx+1);
//
//           if(cnt)
//            {
//             r=g=b=a=0;
//             for(h=0;h<cnt;h++)
//              {
//               a+=(scol[h]>>24);
//               r+=(scol[h]>>16)&0xff;
//               g+=(scol[h]>>8)&0xff;
//               b+=scol[h]&0xff;
//              }
//             r/=cnt;b/=cnt;g/=cnt;
//
//             *ta=(r<<16)|(g<<8)|b;
//             if(a) *ta|=0x50000000;
//             else  *ta|=0x01000000;
//            }
//          }
//         ta++;
//        }
//      }
//    }
//  }
// else
// for(column=0;column<dy;column++)
//  {
//   for(row=0;row<dx;row++)
//    {
//     if(*ta==0x00000000)
//      {
//       cnt=0;
//
//       if(row!=x1               && *(ta+1)   !=0x00000000) scol[cnt++]=*(ta+1);
//       if(           column!=y1 && *(ta+dx)  !=0x00000000) scol[cnt++]=*(ta+dx);
//
//       if(cnt)
//        {
//         r=g=b=0;
//         for(h=0;h<cnt;h++)
//          {
//           r+=(scol[h]>>16)&0xff;
//           g+=(scol[h]>>8)&0xff;
//           b+=scol[h]&0xff;
//          }
//         r/=cnt;b/=cnt;g/=cnt;
//         *ta=(r<<16)|(g<<8)|b;
//        }
//      }
//     ta++;
//    }
//  }
//
// DefineSubTextureSort();
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// load texture part (packed)
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// hires texture funcs
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


#define GET_RESULT(A, B, C, D) ((A != C || A != D) - (B != C || B != D))

////////////////////////////////////////////////////////////////////////

//#define colorMask8     0x00FEFEFE
//#define lowPixelMask8  0x00010101
//#define qcolorMask8    0x00FCFCFC
//#define qlowpixelMask8 0x00030303
//
//
//#define INTERPOLATE8_02(A, B) (((((A & colorMask8) >> 1) + ((B & colorMask8) >> 1) + (A & B & lowPixelMask8))|((((A&0xFF000000)==0x03000000)?0x03000000:(((B&0xFF000000)==0x03000000)?0x03000000:(((A&0xFF000000)==0x00000000)?0x00000000:(((B&0xFF000000)==0x00000000)?0x00000000:0xFF000000)))))))
//
//#define Q_INTERPOLATE8_02(A, B, C, D) (((((A & qcolorMask8) >> 2) + ((B & qcolorMask8) >> 2) + ((C & qcolorMask8) >> 2) + ((D & qcolorMask8) >> 2) + ((((A & qlowpixelMask8) + (B & qlowpixelMask8) + (C & qlowpixelMask8) + (D & qlowpixelMask8)) >> 2) & qlowpixelMask8))|((((A&0xFF000000)==0x03000000)?0x03000000:(((B&0xFF000000)==0x03000000)?0x03000000:(((C&0xFF000000)==0x03000000)?0x03000000:(((D&0xFF000000)==0x03000000)?0x03000000:(((A&0xFF000000)==0x00000000)?0x00000000:(((B&0xFF000000)==0x00000000)?0x00000000:(((C&0xFF000000)==0x00000000)?0x00000000:(((D&0xFF000000)==0x00000000)?0x00000000:0xFF000000)))))))))))
//
//#define INTERPOLATE8(A, B) (((((A & colorMask8) >> 1) + ((B & colorMask8) >> 1) + (A & B & lowPixelMask8))|((((A&0xFF000000)==0x50000000)?0x50000000:(((B&0xFF000000)==0x50000000)?0x50000000:(((A&0xFF000000)==0x00000000)?0x00000000:(((B&0xFF000000)==0x00000000)?0x00000000:0xFF000000)))))))
//
//#define Q_INTERPOLATE8(A, B, C, D) (((((A & qcolorMask8) >> 2) + ((B & qcolorMask8) >> 2) + ((C & qcolorMask8) >> 2) + ((D & qcolorMask8) >> 2) + ((((A & qlowpixelMask8) + (B & qlowpixelMask8) + (C & qlowpixelMask8) + (D & qlowpixelMask8)) >> 2) & qlowpixelMask8))|((((A&0xFF000000)==0x50000000)?0x50000000:(((B&0xFF000000)==0x50000000)?0x50000000:(((C&0xFF000000)==0x50000000)?0x50000000:(((D&0xFF000000)==0x50000000)?0x50000000:(((A&0xFF000000)==0x00000000)?0x00000000:(((B&0xFF000000)==0x00000000)?0x00000000:(((C&0xFF000000)==0x00000000)?0x00000000:(((D&0xFF000000)==0x00000000)?0x00000000:0xFF000000)))))))))))
//
//void Super2xSaI_ex8_Ex(unsigned char *srcPtr, DWORD srcPitch,
//	            unsigned char  *dstBitmap, int width, int height)
//{
// DWORD dstPitch = srcPitch * 2;
// DWORD line;
// DWORD *dP;
// DWORD *bP;
// int   width2 = width*2;
// int iXA,iXB,iXC,iYA,iYB,iYC,finish;
// DWORD color4, color5, color6;
// DWORD color1, color2, color3;
// DWORD colorA0, colorA1, colorA2, colorA3,
//       colorB0, colorB1, colorB2, colorB3,
//       colorS1, colorS2;
// DWORD product1a, product1b,
//       product2a, product2b;
//
// line = 0;
//
//  {
//   for (; height; height-=1)
//	{
//     bP = (DWORD *)srcPtr;
//	 dP = (DWORD *)(dstBitmap + line*dstPitch);
//     for (finish = width; finish; finish -= 1 )
//      {
////---------------------------------------    B1 B2
////                                         4  5  6 S2
////                                         1  2  3 S1
////                                           A1 A2
//       if(finish==width) iXA=0;
//       else              iXA=1;
//       if(finish>4) {iXB=1;iXC=2;}
//       else
//       if(finish>3) {iXB=1;iXC=1;}
//       else         {iXB=0;iXC=0;}
//       if(line==0) iYA=0;
//       else        iYA=width;
//       if(height>4) {iYB=width;iYC=width2;}
//       else
//       if(height>3) {iYB=width;iYC=width;}
//       else         {iYB=0;iYC=0;}
//
//
//       colorB0 = *(bP- iYA - iXA);
//       colorB1 = *(bP- iYA);
//       colorB2 = *(bP- iYA + iXB);
//       colorB3 = *(bP- iYA + iXC);
//
//       color4 = *(bP  - iXA);
//       color5 = *(bP);
//       color6 = *(bP  + iXB);
//       colorS2 = *(bP + iXC);
//
//       color1 = *(bP  + iYB  - iXA);
//       color2 = *(bP  + iYB);
//       color3 = *(bP  + iYB  + iXB);
//       colorS1= *(bP  + iYB  + iXC);
//
//       colorA0 = *(bP + iYC - iXA);
//       colorA1 = *(bP + iYC);
//       colorA2 = *(bP + iYC + iXB);
//       colorA3 = *(bP + iYC + iXC);
//
////--------------------------------------
//       if (color2 == color6 && color5 != color3)
//        {
//         product2b = product1b = color2;
//        }
//       else
//       if (color5 == color3 && color2 != color6)
//        {
//         product2b = product1b = color5;
//        }
//       else
//       if (color5 == color3 && color2 == color6)
//        {
//         register int r = 0;
//
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (color1&0x00ffffff),  (colorA1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (color4&0x00ffffff),  (colorB1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (colorA2&0x00ffffff), (colorS1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (colorB2&0x00ffffff), (colorS2&0x00ffffff));
//
//         if (r > 0)
//          product2b = product1b = color6;
//         else
//         if (r < 0)
//          product2b = product1b = color5;
//         else
//          {
//           product2b = product1b = INTERPOLATE8_02(color5, color6);
//          }
//        }
//       else
//        {
//         if (color6 == color3 && color3 == colorA1 && color2 != colorA2 && color3 != colorA0)
//             product2b = Q_INTERPOLATE8_02 (color3, color3, color3, color2);
//         else
//         if (color5 == color2 && color2 == colorA2 && colorA1 != color3 && color2 != colorA3)
//             product2b = Q_INTERPOLATE8_02 (color2, color2, color2, color3);
//         else
//             product2b = INTERPOLATE8_02 (color2, color3);
//
//         if (color6 == color3 && color6 == colorB1 && color5 != colorB2 && color6 != colorB0)
//             product1b = Q_INTERPOLATE8_02 (color6, color6, color6, color5);
//         else
//         if (color5 == color2 && color5 == colorB2 && colorB1 != color6 && color5 != colorB3)
//             product1b = Q_INTERPOLATE8_02 (color6, color5, color5, color5);
//         else
//             product1b = INTERPOLATE8_02 (color5, color6);
//        }
//
//       if (color5 == color3 && color2 != color6 && color4 == color5 && color5 != colorA2)
//        product2a = INTERPOLATE8_02(color2, color5);
//       else
//       if (color5 == color1 && color6 == color5 && color4 != color2 && color5 != colorA0)
//        product2a = INTERPOLATE8_02(color2, color5);
//       else
//        product2a = color2;
//
//       if (color2 == color6 && color5 != color3 && color1 == color2 && color2 != colorB2)
//        product1a = INTERPOLATE8_02(color2, color5);
//       else
//       if (color4 == color2 && color3 == color2 && color1 != color5 && color2 != colorB0)
//        product1a = INTERPOLATE8_02(color2, color5);
//       else
//        product1a = color5;
//
//       *dP=product1a;
//       *(dP+1)=product1b;
//       *(dP+(width2))=product2a;
//       *(dP+1+(width2))=product2b;
//
//       bP += 1;
//       dP += 2;
//      }//end of for ( finish= width etc..)
//
//     line += 2;
//     srcPtr += srcPitch;
//	}; //endof: for (; height; height--)
//  }
//}
//
//
//void Super2xSaI_ex8(unsigned char *srcPtr, DWORD srcPitch,
//	            unsigned char  *dstBitmap, int width, int height)
//{
// DWORD dstPitch = srcPitch * 2;
// DWORD line;
// DWORD *dP;
// DWORD *bP;
// int   width2 = width*2;
// int iXA,iXB,iXC,iYA,iYB,iYC,finish;
// DWORD color4, color5, color6;
// DWORD color1, color2, color3;
// DWORD colorA0, colorA1, colorA2, colorA3,
//       colorB0, colorB1, colorB2, colorB3,
//       colorS1, colorS2;
// DWORD product1a, product1b,
//       product2a, product2b;
//
// line = 0;
//
//  {
//   for (; height; height-=1)
//	{
//     bP = (DWORD *)srcPtr;
//	 dP = (DWORD *)(dstBitmap + line*dstPitch);
//     for (finish = width; finish; finish -= 1 )
//      {
////---------------------------------------    B1 B2
////                                         4  5  6 S2
////                                         1  2  3 S1
////                                           A1 A2
//       if(finish==width) iXA=0;
//       else              iXA=1;
//       if(finish>4) {iXB=1;iXC=2;}
//       else
//       if(finish>3) {iXB=1;iXC=1;}
//       else         {iXB=0;iXC=0;}
//       if(line==0) iYA=0;
//       else        iYA=width;
//       if(height>4) {iYB=width;iYC=width2;}
//       else
//       if(height>3) {iYB=width;iYC=width;}
//       else         {iYB=0;iYC=0;}
//
//
//       colorB0 = *(bP- iYA - iXA);
//       colorB1 = *(bP- iYA);
//       colorB2 = *(bP- iYA + iXB);
//       colorB3 = *(bP- iYA + iXC);
//
//       color4 = *(bP  - iXA);
//       color5 = *(bP);
//       color6 = *(bP  + iXB);
//       colorS2 = *(bP + iXC);
//
//       color1 = *(bP  + iYB  - iXA);
//       color2 = *(bP  + iYB);
//       color3 = *(bP  + iYB  + iXB);
//       colorS1= *(bP  + iYB  + iXC);
//
//       colorA0 = *(bP + iYC - iXA);
//       colorA1 = *(bP + iYC);
//       colorA2 = *(bP + iYC + iXB);
//       colorA3 = *(bP + iYC + iXC);
//
////--------------------------------------
//       if (color2 == color6 && color5 != color3)
//        {
//         product2b = product1b = color2;
//        }
//       else
//       if (color5 == color3 && color2 != color6)
//        {
//         product2b = product1b = color5;
//        }
//       else
//       if (color5 == color3 && color2 == color6)
//        {
//         register int r = 0;
//
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (color1&0x00ffffff),  (colorA1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (color4&0x00ffffff),  (colorB1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (colorA2&0x00ffffff), (colorS1&0x00ffffff));
//         r += GET_RESULT ((color6&0x00ffffff), (color5&0x00ffffff), (colorB2&0x00ffffff), (colorS2&0x00ffffff));
//
//         if (r > 0)
//          product2b = product1b = color6;
//         else
//         if (r < 0)
//          product2b = product1b = color5;
//         else
//          {
//           product2b = product1b = INTERPOLATE8(color5, color6);
//          }
//        }
//       else
//        {
//         if (color6 == color3 && color3 == colorA1 && color2 != colorA2 && color3 != colorA0)
//             product2b = Q_INTERPOLATE8 (color3, color3, color3, color2);
//         else
//         if (color5 == color2 && color2 == colorA2 && colorA1 != color3 && color2 != colorA3)
//             product2b = Q_INTERPOLATE8 (color2, color2, color2, color3);
//         else
//             product2b = INTERPOLATE8 (color2, color3);
//
//         if (color6 == color3 && color6 == colorB1 && color5 != colorB2 && color6 != colorB0)
//             product1b = Q_INTERPOLATE8 (color6, color6, color6, color5);
//         else
//         if (color5 == color2 && color5 == colorB2 && colorB1 != color6 && color5 != colorB3)
//             product1b = Q_INTERPOLATE8 (color6, color5, color5, color5);
//         else
//             product1b = INTERPOLATE8 (color5, color6);
//        }
//
//       if (color5 == color3 && color2 != color6 && color4 == color5 && color5 != colorA2)
//        product2a = INTERPOLATE8(color2, color5);
//       else
//       if (color5 == color1 && color6 == color5 && color4 != color2 && color5 != colorA0)
//        product2a = INTERPOLATE8(color2, color5);
//       else
//        product2a = color2;
//
//       if (color2 == color6 && color5 != color3 && color1 == color2 && color2 != colorB2)
//        product1a = INTERPOLATE8(color2, color5);
//       else
//       if (color4 == color2 && color3 == color2 && color1 != color5 && color2 != colorB0)
//        product1a = INTERPOLATE8(color2, color5);
//       else
//        product1a = color5;
//
//       *dP=product1a;
//       *(dP+1)=product1b;
//       *(dP+(width2))=product2a;
//       *(dP+1+(width2))=product2b;
//
//       bP += 1;
//       dP += 2;
//      }//end of for ( finish= width etc..)
//
//     line += 2;
//     srcPtr += srcPitch;
//	}; //endof: for (; height; height--)
//  }
//}
///////////////////////////////////////////////////////////////////////////////
//
//#define colorMask4     0x0000EEE0
//#define lowPixelMask4  0x00001110
//#define qcolorMask4    0x0000CCC0
//#define qlowpixelMask4 0x00003330
//
//#define INTERPOLATE4(A, B) ((((A & colorMask4) >> 1) + ((B & colorMask4) >> 1) + (A & B & lowPixelMask4))|((((A&0x0000000F)==0x00000006)?0x00000006:(((B&0x0000000F)==0x00000006)?0x00000006:(((A&0x0000000F)==0x00000000)?0x00000000:(((B&0x0000000F)==0x00000000)?0x00000000:0x0000000F))))))
//
//#define Q_INTERPOLATE4(A, B, C, D) ((((A & qcolorMask4) >> 2) + ((B & qcolorMask4) >> 2) + ((C & qcolorMask4) >> 2) + ((D & qcolorMask4) >> 2) + ((((A & qlowpixelMask4) + (B & qlowpixelMask4) + (C & qlowpixelMask4) + (D & qlowpixelMask4)) >> 2) & qlowpixelMask4))| ((((A&0x0000000F)==0x00000006)?0x00000006:(((B&0x0000000F)==0x00000006)?0x00000006:(((C&0x0000000F)==0x00000006)?0x00000006:(((D&0x0000000F)==0x00000006)?0x00000006:(((A&0x0000000F)==0x00000000)?0x00000000:(((B&0x0000000F)==0x00000000)?0x00000000:(((C&0x0000000F)==0x00000000)?0x00000000:(((D&0x0000000F)==0x00000000)?0x00000000:0x0000000F))))))))))
//
//
//#define colorMask5     0x0000F7BC
//#define lowPixelMask5  0x00000842
//#define qcolorMask5    0x0000E738
//#define qlowpixelMask5 0x000018C6
//
//#define INTERPOLATE5(A, B) ((((A & colorMask5) >> 1) + ((B & colorMask5) >> 1) + (A & B & lowPixelMask5))|((((A&0x00000001)==0x00000000)?0x00000000:(((B&0x00000001)==0x00000000)?0x00000000:0x00000001))))
//
//#define Q_INTERPOLATE5(A, B, C, D) ((((A & qcolorMask5) >> 2) + ((B & qcolorMask5) >> 2) + ((C & qcolorMask5) >> 2) + ((D & qcolorMask5) >> 2) + ((((A & qlowpixelMask5) + (B & qlowpixelMask5) + (C & qlowpixelMask5) + (D & qlowpixelMask5)) >> 2) & qlowpixelMask5))| ((((A&0x00000001)==0x00000000)?0x00000000:(((B&0x00000001)==0x00000000)?0x00000000:(((C&0x00000001)==0x00000000)?0x00000000:(((D&0x00000001)==0x00000000)?0x00000000:0x00000001))))))
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// ogl texture defines
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////

void DefineSubTextureSort(void)
{
    glSetRGB24( 0 );

 if(!gTexName)
  {
   glGenTextures(1, &gTexName); glError();
   glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();

   glInitRGBATextures(256, 256);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iClampType); glError();
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iClampType); glError();

   if(iFilterType)
    {
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glError();
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glError();
    }
   else
    {
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter); glError();
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter); glError();
    }
   //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0,GL_RGBA, GL_UNSIGNED_BYTE, texturepart); glError();
  }
 else
 {
     glBindTextureBef(GL_TEXTURE_2D, gTexName); glError();
  }
  #ifdef DISP_DEBUG
  //int oldWidth, oldHeight;
  //glGetTextureInfo(gTexName, &oldWidth, &oldHeight);
  //sprintf(txtbuffer, "DefineSubTextureSort %d %d %d %d %d %d\r\n", oldWidth, oldHeight, XTexS, YTexS, DXTexS, DYTexS);
  if (logFileEnabled()) {   /* once per upload: see the note in gpuPrim.c on CMD_LOG_2D */
  sprintf(txtbuffer, "DefineSubTextureSort %d %d %d %d\r\n", XTexS, YTexS, DXTexS, DYTexS);
  DEBUG_print(txtbuffer, DBG_CDR2);
  writeLogFile(txtbuffer);
  }
  #endif // DISP_DEBUG

  int textureType;
  PERF_INC(ogx_sub_new);
  textureType = glTexSubImage2D(GL_TEXTURE_2D, 0, XTexS, YTexS,
                 DXTexS, DYTexS,
                 GL_RGBA, GL_UNSIGNED_BYTE, texturepart); glError();
    gl_ux[8] = (GLubyte)(textureType);
}

/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// texture cache garbage collection
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

void DoTexGarbageCollection(void)
{
 static unsigned short LRUCleaned=0;
 PERF_INC(ogx_gc);
 unsigned short iC,iC1,iC2;
 int i,j,iMax;textureSubCacheEntryS * tsb;

 iC=4;//=iSortTexCnt/2,
 LRUCleaned+=iC;                                       // we clean different textures each time
 if((LRUCleaned+iC)>=iSortTexCnt) LRUCleaned=0;        // wrap? wrap!
 iC1=LRUCleaned;                                       // range of textures to clean
 iC2=LRUCleaned+iC;

 for(iC=iC1;iC<iC2;iC++)                               // make some textures available
  {
   pxSsubtexLeft[iC]->l=0;
  }

 for(i=0;i<3;i++)                                      // remove all references to that textures
  for(j=0;j<MAXTPAGES;j++)
   for(iC=0;iC<4;iC++)                                 // loop all texture rect info areas
    {
     tsb=pscSubtexStore[i][j]+(iC*SOFFB);
     iMax=GETLE32(SUBCACHE_COUNT_PTR(tsb));
     if(iMax)
      do
       {
        tsb++;
        if(tsb->cTexID>=iC1 && tsb->cTexID<iC2)        // info uses the cleaned textures? remove info
         tsb->ClutKey=0;
       }
      while(--iMax);
     }

 usLRUTexPage=LRUCleaned;
}

/* The space a sub-texture reserves in a 256x256 cache page, one dimension at a time: its
 * size r (x2 - x1), plus the one-texel border it is uploaded with on each side, rounded
 * up to whole 4x4 GX blocks. The rounding keeps the origin of every free rectangle -- and
 * so of every upload -- on a block boundary, which is what lets OpenGX tile an upload
 * with its block path (_ogx_scramble_4b_sub) instead of one texel at a time. Measured
 * before: 83% of Crash Bash's uploads went texel by texel, and the tiling pass was 6.1%
 * of wall. The cost is at most three texels of page per dimension. A reservation that
 * fills the page (more than 252) is left as it was: nothing follows it to misalign.
 * *adj is cleared when the texture is as wide as the page and has no room for a border. */
#ifndef SUBTEX_ALIGN
#define SUBTEX_ALIGN 1   /* -DSUBTEX_ALIGN=0: the old unaligned placement, to compare */
#endif
static unsigned int SubTexReserve(unsigned int r, unsigned char *adj)
{
 r += 3;
 if (r > 255) { *adj = 0; return 255; }
 if (r > 252 || !SUBTEX_ALIGN) return r;
 return (r + 3) & ~3u;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// search cache for existing (already used) parts
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

#if PERF_PROF_TEXCHECK
/* The staleness oracle: does the cached GX texture still hold what VRAM holds? The
 * expected texel is the converter's own rule: the palette colour (or the 15-bit texel)
 * through TCF[semi], then the tiler's split -- 0 transparent, a semi-transparent draw's
 * texel without bit 15 in the semi copy, the rest in the opaque data with bit 15 set.
 * 64 texels per hit on a grid that moves each hit, so a kept stale entry is found. */
extern int glGetTexture16(unsigned int name, const unsigned short **data,
                          const unsigned short **semi, int *w, int *h);
extern unsigned int frame_counter;
static unsigned short tc_vram(int x, int y) { return GETLE16(&psxVuw[((y) & 511) * 1024 + ((x) & 1023)]); }
static void texcheck_hit(const textureSubCacheEntryS *e, int mode, uint64_t key)
{
 static unsigned roll;
 const unsigned short *data, *semi;
 int w, h, i, j, bad = 0;
 unsigned id = (unsigned)(key & CLUT_KEY_ADDRESS_MASK);
 int ccx = (id << 4) & 0x3F0, ccy = (id >> 6) & CLUTYMASK;
 int st = ClutKeyDrawSemiTrans(key);
 int ex = e->pos.c.x1 - e->posTX, ey = e->pos.c.y1 - e->posTY;
 int W = e->pos.c.x2 - e->pos.c.x1 + 1, H = e->pos.c.y2 - e->pos.c.y1 + 1;
 if (GlobalTextIL || W <= 0 || H <= 0) return;
 if (!glGetTexture16(uiStexturePage[e->cTexID], &data, &semi, &w, &h)) return;
 g_perf.tc_hits++;
 roll++;
 for (j = 0; j < 8; j++)
  for (i = 0; i < 8; i++)
   {
    int u = e->pos.c.x1 + (int)((i * W / 8 + roll) % W);
    int v = e->pos.c.y1 + (int)((j * H / 8 + roll / 8) % H);
    int gx = u - ex, gy = v - ey, off;
    unsigned short c, t, want_o, want_s, got_o, got_s;
    if (gx < 0 || gy < 0 || gx >= w || gy >= h) continue;
    if (mode == 0)      { t = tc_vram(GlobalTextAddrX + (u >> 2), GlobalTextAddrY + v); c = tc_vram(ccx + ((t >> ((u & 3) * 4)) & 0xf), ccy); }
    else if (mode == 1) { t = tc_vram(GlobalTextAddrX + (u >> 1), GlobalTextAddrY + v); c = tc_vram(ccx + ((t >> ((u & 1) * 8)) & 0xff), ccy); }
    else                  c = tc_vram(GlobalTextAddrX + u, GlobalTextAddrY + v);
#ifdef TEXCHECK_SELFTEST   /* the oracle's own test: a changed texel must be reported */
    if (i == 0 && j == 0 && (roll & 63) == 0) c ^= 0x0421;
#endif
    c = (unsigned short)(TCF[st](c) & 0xffff);
    if (c) g_perf.tc_nonzero++;
    want_o = want_s = 0;
    if (c && st && !(c & 0x8000)) want_s = c | 0x8000;
    else if (c)                   want_o = c | 0x8000;
    off = ((gy >> 2) * ((w + 3) >> 2) + (gx >> 2)) * 16 + (gy & 3) * 4 + (gx & 3);
    got_o = data[off];
    got_s = semi ? semi[off] : 0;
    g_perf.tc_texels++;
    if (got_o == want_o && got_s == want_s) continue;
    g_perf.tc_bad++;
    bad = 1;
    if (g_perf.tc_n < 8) {
     unsigned k = g_perf.tc_n++;
     g_perf.tc_s[k].vbl = frame_counter; g_perf.tc_s[k].exp = want_o | want_s; g_perf.tc_s[k].got = got_o | got_s;
     g_perf.tc_s[k].mode = mode; g_perf.tc_s[k].page = GlobalTexturePage; g_perf.tc_s[k].u = u; g_perf.tc_s[k].v = v;
     g_perf.tc_s[k].x1 = e->pos.c.x1; g_perf.tc_s[k].y1 = e->pos.c.y1; g_perf.tc_s[k].x2 = e->pos.c.x2; g_perf.tc_s[k].y2 = e->pos.c.y2;
    }
   }
 g_perf.tc_bad_hits += bad;
}
#endif

textureSubCacheEntryS *CheckTextureInSubSCache(
 int TextureMode,uint64_t clutKey,unsigned short *pCache)
{
 textureSubCacheEntryS * tsx, * tsb, *tsg;//, *tse=NULL;
 int i,iMax;EXLong npos;
 unsigned char cx,cy;
 int iC,j,k;unsigned int rx,ry,mx,my;
 EXLong * ul=0, * uls;
 EXLong rfree;
 unsigned char cXAdj,cYAdj;

 npos.l=*((unsigned int *)&gl_ux[4]);

 //--------------------------------------------------------------//
 // find matching texturepart first... speed up...
 //--------------------------------------------------------------//

 tsg=pscSubtexStore[TextureMode][GlobalTexturePage];
 tsg+=ClutKeyBucket(clutKey)*SOFFB;

 iMax=GETLE32(SUBCACHE_COUNT_PTR(tsg));
 if(iMax)
  {
   i=iMax;
   tsb=tsg+1;
   do
    {
     if(clutKey==tsb->ClutKey &&
        (INCHECK(tsb->pos,npos)))
      {
        {
         cx=tsb->pos.c.x1-tsb->posTX;
         cy=tsb->pos.c.y1-tsb->posTY;

         gl_ux[0]-=cx;
         gl_ux[1]-=cx;
         gl_ux[2]-=cx;
         gl_ux[3]-=cx;
         gl_vy[0]-=cy;
         gl_vy[1]-=cy;
         gl_vy[2]-=cy;
         gl_vy[3]-=cy;

         gl_ux[8]=DrawInfoTextureType(tsb->drawInfo);

         ubOpaqueDraw=DrawInfoOpaque(tsb->drawInfo);
         *pCache=tsb->cTexID;
         PERF_INC(ogx_sub_hit);
#if PERF_PROF_TEXCHECK
         texcheck_hit(tsb, TextureMode, clutKey);
#endif
         return NULL;
        }
      }
     tsb++;
    }
   while(--i);
  }

 //----------------------------------------------------//

 cXAdj=1;cYAdj=1;

 rx=(int)gl_ux[6]-(int)gl_ux[7];
 ry=(int)gl_ux[4]-(int)gl_ux[5];

 tsx=NULL;tsb=tsg+1;
 for(i=0;i<iMax;i++,tsb++)
  {
   if(!tsb->ClutKey) {tsx=tsb;break;}
  }

 if(!tsx)
  {
   iMax++;
   if(iMax>=SOFFB-2)
    {
     if(iTexGarbageCollection)                         // gc mode?
      {
       if(*pCache==0)
        {
         dwTexPageComp|=(1<<GlobalTexturePage);
         *pCache=0xffff;
         return 0;
        }

       iMax--;
       tsb=tsg+1;

       for(i=0;i<iMax;i++,tsb++)                       // 1. search other slots with same cluts, and unite the area
        if(clutKey==tsb->ClutKey)
         {
          if(!tsx) {tsx=tsb;rfree.l=npos.l;}           //
          else      tsb->ClutKey=0;
          rfree.c.x1=min(rfree.c.x1,tsb->pos.c.x1);
          rfree.c.x2=max(rfree.c.x2,tsb->pos.c.x2);
          rfree.c.y1=min(rfree.c.y1,tsb->pos.c.y1);
          rfree.c.y2=max(rfree.c.y2,tsb->pos.c.y2);
          MarkFree(tsb);
         }

       if(tsx)                                         // 3. if one or more found, create a new rect with bigger size
        {
         *((unsigned int *)&gl_ux[4])=npos.l=rfree.l;
         rx=(int)rfree.c.x2-(int)rfree.c.x1;
         ry=(int)rfree.c.y2-(int)rfree.c.y1;
         DoTexGarbageCollection();

         goto ENDLOOP3;
        }
      }

     iMax=1;
    }
   tsx=tsg+iMax;
   PUTLE32(SUBCACHE_COUNT_PTR(tsg),iMax);
  }

 //----------------------------------------------------//
 // now get a free texture space
 //----------------------------------------------------//

 if(iTexGarbageCollection) usLRUTexPage=0;

ENDLOOP3:

 rx=SubTexReserve(rx,&cXAdj);
 ry=SubTexReserve(ry,&cYAdj);

 iC=usLRUTexPage;

 for(k=0;k<iSortTexCnt;k++)
  {
   uls=pxSsubtexLeft[iC];
   iMax=GETLE32((unsigned long *)(uls));ul=uls+1;

   //--------------------------------------------------//
   // first time

   if(!iMax)
    {
     rfree.l=0;

     if(rx>252 && ry>252)
      {PUTLE32((unsigned long *)(uls), 1);ul->l=0xffffffff;ul=0;goto ENDLOOP;}

     if(rx<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=rx;
       ul->c.x2=255-rx;
       ul->c.y1=0;
       ul->c.y2=ry;
       ul++;
      }

     if(ry<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=0;
       ul->c.x2=255;
       ul->c.y1=ry;
       ul->c.y2=255-ry;
      }
     ul=0;
     goto ENDLOOP;
    }

   //--------------------------------------------------//
   for(i=0;i<iMax;i++,ul++)
    {
     if(ul->l!=0xffffffff &&
        ry<=ul->c.y2      &&
        rx<=ul->c.x2)
      {
       rfree=*ul;
       mx=ul->c.x2-2;
       my=ul->c.y2-2;
       if(rx<mx && ry<my)
        {
         ul->c.x1+=rx;
         ul->c.x2-=rx;
         ul->c.y2=ry;

         for(ul=uls+1,j=0;j<iMax;j++,ul++)
          if(ul->l==0xffffffff) break;

         if(j<CSUBSIZE-2)
          {
           if(j==iMax) PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);

           ul->c.x1=rfree.c.x1;
           ul->c.x2=rfree.c.x2;
           ul->c.y1=rfree.c.y1+ry;
           ul->c.y2=rfree.c.y2-ry;
          }
        }
       else if(rx<mx)
        {
         ul->c.x1+=rx;
         ul->c.x2-=rx;
        }
       else if(ry<my)
        {
         ul->c.y1+=ry;
         ul->c.y2-=ry;
        }
       else
        {
         ul->l=0xffffffff;
        }
       ul=0;
       goto ENDLOOP;
      }
    }

   //--------------------------------------------------//

   iC++; if(iC>=iSortTexCnt) iC=0;
  }

 //----------------------------------------------------//
 // check, if free space got
 //----------------------------------------------------//

ENDLOOP:
 if(ul)
  {
   //////////////////////////////////////////////////////

    {
     dwTexPageComp=0;

     for(i=0;i<3;i++)                                    // cleaning up
      for(j=0;j<MAXTPAGES;j++)
       {
        tsb=pscSubtexStore[i][j];
        (tsb+SOFFA)->pos.l=0;
        (tsb+SOFFB)->pos.l=0;
        (tsb+SOFFC)->pos.l=0;
        (tsb+SOFFD)->pos.l=0;
       }
     for(i=0;i<iSortTexCnt;i++)
      {ul=pxSsubtexLeft[i];ul->l=0;}
     usLRUTexPage=0;
    }

   //////////////////////////////////////////////////////
   iC=usLRUTexPage;
   uls=pxSsubtexLeft[usLRUTexPage];
   uls->l=0;ul=uls+1;
   rfree.l=0;

   if(rx>252 && ry>252)
    {PUTLE32((unsigned long *)(uls), 1);ul->l=0xffffffff;}
   else
    {
     if(rx<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=rx;
       ul->c.x2=255-rx;
       ul->c.y1=0;
       ul->c.y2=ry;
       ul++;
      }
     if(ry<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=0;
       ul->c.x2=255;
       ul->c.y1=ry;
       ul->c.y2=255-ry;
      }
    }
   PUTLE32(SUBCACHE_COUNT_PTR(tsg),1);tsx=tsg+1;
  }

 rfree.c.x1+=cXAdj;
 rfree.c.y1+=cYAdj;

 tsx->cTexID   =*pCache=iC;
 tsx->pos      = npos;
 tsx->ClutKey  = clutKey;
 tsx->posTX    = rfree.c.x1;
 tsx->posTY    = rfree.c.y1;

 cx=gl_ux[7]-rfree.c.x1;
 cy=gl_ux[5]-rfree.c.y1;

 gl_ux[0]-=cx;
 gl_ux[1]-=cx;
 gl_ux[2]-=cx;
 gl_ux[3]-=cx;
 gl_vy[0]-=cy;
 gl_vy[1]-=cy;
 gl_vy[2]-=cy;
 gl_vy[3]-=cy;

 XTexS=rfree.c.x1;
 YTexS=rfree.c.y1;

 return tsx;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// search cache for free place (on compress)
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

BOOL GetCompressTexturePlace(textureSubCacheEntryS * tsx)
{
 int i,j,k,iMax,iC;unsigned int rx,ry,mx,my;
 EXLong * ul=0, * uls, rfree;
 unsigned char cXAdj=1,cYAdj=1;

 rx=(int)tsx->pos.c.x2-(int)tsx->pos.c.x1;
 ry=(int)tsx->pos.c.y2-(int)tsx->pos.c.y1;

 rx=SubTexReserve(rx,&cXAdj);
 ry=SubTexReserve(ry,&cYAdj);

 iC=usLRUTexPage;

 for(k=0;k<iSortTexCnt;k++)
  {
   uls=pxSsubtexLeft[iC];
   iMax=GETLE32((unsigned long *)(uls));ul=uls+1;

   //--------------------------------------------------//
   // first time

   if(!iMax)
    {
     rfree.l=0;

     if(rx>252 && ry>252)
      {PUTLE32((unsigned long *)(uls), 1);ul->l=0xffffffff;ul=0;goto TENDLOOP;}

     if(rx<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=rx;
       ul->c.x2=255-rx;
       ul->c.y1=0;
       ul->c.y2=ry;
       ul++;
      }

     if(ry<253)
      {
       PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);
       ul->c.x1=0;
       ul->c.x2=255;
       ul->c.y1=ry;
       ul->c.y2=255-ry;
      }
     ul=0;
     goto TENDLOOP;
    }

   //--------------------------------------------------//
   for(i=0;i<iMax;i++,ul++)
    {
     if(ul->l!=0xffffffff &&
        ry<=ul->c.y2      &&
        rx<=ul->c.x2)
      {
       rfree=*ul;
       mx=ul->c.x2-2;
       my=ul->c.y2-2;

       if(rx<mx && ry<my)
        {
         ul->c.x1+=rx;
         ul->c.x2-=rx;
         ul->c.y2=ry;

         for(ul=uls+1,j=0;j<iMax;j++,ul++)
          if(ul->l==0xffffffff) break;

         if(j<CSUBSIZE-2)
          {
           if(j==iMax) PUTLE32((unsigned long *)(uls), GETLE32((unsigned long *)(uls)) + 1);

           ul->c.x1=rfree.c.x1;
           ul->c.x2=rfree.c.x2;
           ul->c.y1=rfree.c.y1+ry;
           ul->c.y2=rfree.c.y2-ry;
          }
        }
       else if(rx<mx)
        {
         ul->c.x1+=rx;
         ul->c.x2-=rx;
        }
       else if(ry<my)
        {
         ul->c.y1+=ry;
         ul->c.y2-=ry;
        }
       else
        {
         ul->l=0xffffffff;
        }
       ul=0;
       goto TENDLOOP;
      }
    }

   //--------------------------------------------------//

   iC++; if(iC>=iSortTexCnt) iC=0;
  }

 //----------------------------------------------------//
 // check, if free space got
 //----------------------------------------------------//

TENDLOOP:
 if(ul) return FALSE;

 rfree.c.x1+=cXAdj;
 rfree.c.y1+=cYAdj;

 tsx->cTexID   = iC;
 tsx->posTX    = rfree.c.x1;
 tsx->posTY    = rfree.c.y1;

 XTexS=rfree.c.x1;
 YTexS=rfree.c.y1;

 return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// compress texture cache (to make place for new texture part, if needed)
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

void CompressTextureSpace(void)
{
 textureSubCacheEntryS * tsx, * tsg, * tsb;
 int i,j,k,m,n,iMax;EXLong * ul, r,opos;
 short sOldDST=DrawSemiTrans,cx,cy;
 int  lOGTP=GlobalTexturePage;

 opos.l=*((unsigned int *)&gl_ux[4]);

 // 1. mark all textures as free
 for(i=0;i<iSortTexCnt;i++)
  {ul=pxSsubtexLeft[i];ul->l=0;}
 usLRUTexPage=0;

 // 2. compress
 for(j=0;j<3;j++)
  {
   for(k=0;k<MAXTPAGES;k++)
    {
     tsg=pscSubtexStore[j][k];

     if((!(dwTexPageComp&(1<<k))))
      {
       (tsg+SOFFA)->pos.l=0;
       (tsg+SOFFB)->pos.l=0;
       (tsg+SOFFC)->pos.l=0;
       (tsg+SOFFD)->pos.l=0;
       continue;
      }

     for(m=0;m<4;m++,tsg+=SOFFB)
      {
       iMax=GETLE32(SUBCACHE_COUNT_PTR(tsg));

       tsx=tsg+1;
       for(i=0;i<iMax;i++,tsx++)
        {
         if(tsx->ClutKey)
          {
           r.l=tsx->pos.l;
           for(n=i+1,tsb=tsx+1;n<iMax;n++,tsb++)
            {
             if(tsx->ClutKey==tsb->ClutKey)
              {
               r.c.x1=min(r.c.x1,tsb->pos.c.x1);
               r.c.x2=max(r.c.x2,tsb->pos.c.x2);
               r.c.y1=min(r.c.y1,tsb->pos.c.y1);
               r.c.y2=max(r.c.y2,tsb->pos.c.y2);
               tsb->ClutKey=0;
              }
            }

//           if(r.l!=tsx->pos.l)
            {
             cx=(short)((tsx->ClutKey<<4)&0x3F0);
             cy=(short)((tsx->ClutKey>>6)&CLUTYMASK);

             if(j!=2 &&
                BuildClutCacheKey((unsigned int)tsx->ClutKey,j,
                                  ClutKeyDrawSemiTrans(tsx->ClutKey))!=
                tsx->ClutKey)
              {tsx->ClutKey=0;continue;}

             tsx->pos.l=r.l;
             if(!GetCompressTexturePlace(tsx))         // no place?
              {
               for(i=0;i<3;i++)                        // -> clean up everything
                for(j=0;j<MAXTPAGES;j++)
                 {
                  tsb=pscSubtexStore[i][j];
                  (tsb+SOFFA)->pos.l=0;
                  (tsb+SOFFB)->pos.l=0;
                  (tsb+SOFFC)->pos.l=0;
                  (tsb+SOFFD)->pos.l=0;
                 }
               for(i=0;i<iSortTexCnt;i++)
                {ul=pxSsubtexLeft[i];ul->l=0;}
               usLRUTexPage=0;
               DrawSemiTrans=sOldDST;
               GlobalTexturePage=lOGTP;
               *((unsigned int *)&gl_ux[4])=opos.l;
               dwTexPageComp=0;

               return;
              }

             DrawSemiTrans=ClutKeyDrawSemiTrans(tsx->ClutKey);
             *((unsigned int *)&gl_ux[4])=r.l;

             gTexName=uiStexturePage[tsx->cTexID];
             LoadSubTexFn(k,j,cx,cy);
             uiStexturePage[tsx->cTexID]=gTexName;
             tsx->drawInfo=PackDrawInfo(ubOpaqueDraw,gl_ux[8]);
            }
          }
        }

       if(iMax)
        {
         tsx=tsg+iMax;
         while(!tsx->ClutKey && iMax) {tsx--;iMax--;}
         PUTLE32(SUBCACHE_COUNT_PTR(tsg),iMax);
        }

      }
    }
  }

 if(dwTexPageComp==0xffffffff) dwTexPageComp=0;

 *((unsigned int *)&gl_ux[4])=opos.l;
 GlobalTexturePage=lOGTP;
 DrawSemiTrans=sOldDST;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// main entry for searching/creating textures, called from prim.c
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

GLuint SelectSubTextureS(int TextureMode, unsigned int GivenClutId)
{
#if PERF_PROF_GPU
 g_perf.ogx_cur_mode = TextureMode;   /* probe: mode of the texture the next draw samples */
 g_perf.ogx_cur_clut = GivenClutId; g_perf.ogx_cur_page = GlobalTexturePage;
 g_perf.ogx_cur_tx = GlobalTextAddrX; g_perf.ogx_cur_ty = GlobalTextAddrY;
 g_perf.ogx_cur_semi = DrawSemiTrans; g_perf.ogx_cur_twin = bUsingTWin;
 { int qi; for (qi = 0; qi < 4; qi++) { g_perf.ogx_cur_u[qi] = gl_ux[qi]; g_perf.ogx_cur_v[qi] = gl_vy[qi]; } }
#endif
 textureSubCacheEntryS *newEntry;
 uint64_t clutKey;
 unsigned short iCache;short cx,cy;

 // sort sow/tow infos for fast access

 unsigned char ma1,ma2,mi1,mi2;
 if(gl_ux[0]>gl_ux[1]) {mi1=gl_ux[1];ma1=gl_ux[0];}
 else                  {mi1=gl_ux[0];ma1=gl_ux[1];}
 if(gl_ux[2]>gl_ux[3]) {mi2=gl_ux[3];ma2=gl_ux[2];}
 else                  {mi2=gl_ux[2];ma2=gl_ux[3];}
 if(mi1>mi2) gl_ux[7]=mi2;
 else        gl_ux[7]=mi1;
 if(ma1>ma2) gl_ux[6]=ma1;
 else        gl_ux[6]=ma2;

 if(gl_vy[0]>gl_vy[1]) {mi1=gl_vy[1];ma1=gl_vy[0];}
 else                  {mi1=gl_vy[0];ma1=gl_vy[1];}
 if(gl_vy[2]>gl_vy[3]) {mi2=gl_vy[3];ma2=gl_vy[2];}
 else                  {mi2=gl_vy[2];ma2=gl_vy[3];}
 if(mi1>mi2) gl_ux[5]=mi2;
 else        gl_ux[5]=mi1;
 if(ma1>ma2) gl_ux[4]=ma1;
 else        gl_ux[4]=ma2;

 // get clut infos in one 32 bit val

 if(TextureMode==2)                                    // no clut here
  {
   cx=cy=0;

   if(iFrameTexType && Fake15BitTexture())
   {

       return (GLuint)gTexName;
   }
  }
 else
  {
   cx=((GivenClutId << 4) & 0x3F0);                    // but here
   cy=((GivenClutId >> 6) & CLUTYMASK);
  }
 clutKey=BuildClutCacheKey(GivenClutId,TextureMode,DrawSemiTrans);

 // search cache
 iCache=0;
 newEntry=CheckTextureInSubSCache(TextureMode,clutKey,&iCache);

 // cache full? compress and try again
 if(iCache==0xffff)
  {
   CompressTextureSpace();
   newEntry=CheckTextureInSubSCache(TextureMode,clutKey,&iCache);
  }

 // found? fine
 usLRUTexPage=iCache;
 #ifdef DISP_DEBUG
// sprintf(txtbuffer, "SelectSubTextureS 1 %d %d\r\n", *OPtr, iCache);
// DEBUG_print(txtbuffer,  DBG_CDR3);
//writeLogFile(txtbuffer);
 #endif // DISP_DEBUG
  if(!newEntry) return uiStexturePage[iCache];

  texChgType = 3;
  //GX_Flush();
  //GX_SetDrawDone();

 // not found? upload texture and store infos in cache
 gTexName=uiStexturePage[iCache];
 #ifdef DISP_DEBUG
// sprintf(txtbuffer, "SelectSubTextureS 2 %d %d\r\n", iCache, gTexName);
// DEBUG_print(txtbuffer,  DBG_CDR3);
 //writeLogFile(txtbuffer);
 #endif // DISP_DEBUG
 LoadSubTexFn(GlobalTexturePage,TextureMode,cx,cy);
 uiStexturePage[iCache]=gTexName;
 newEntry->drawInfo=PackDrawInfo(ubOpaqueDraw,gl_ux[8]);
 #ifdef DISP_DEBUG
// sprintf(txtbuffer, "SelectSubTextureS 3 %d\r\n", gTexName);
// DEBUG_print(txtbuffer,  DBG_CDR3);
 //writeLogFile(txtbuffer);
 #endif // DISP_DEBUG
 return (GLuint) gTexName;
}

#endif // _IN_GPU_LIB

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
