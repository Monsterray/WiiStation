/**
 * Wii64 - IPLFont.cpp
 * Copyright (C) 2009 sepp256
 *
 * Wii64 homepage: http://www.emulatemii.com
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

#include <map>
#include <string.h>
#include <ogc/lwp_heap.h>

#include "IPLFont.h"
#include "../MEM2.h"
extern "C" {
#include "../../mem2_manager.h"
}
#include "../perf_prof.h"

#include "gui2/gettext.h"
#include "../wiiSXconfig.h"
extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../../gpu.h"
}

extern GXTexRegion texCacheRegionS[8];
extern "C" GXRModeObj *g_tv_mode;   /* GraphicsGX.cpp: the video mode on the TV (NULL before the first mode switch) */
extern GXRModeObj *vmode;           /* GamecubeMain.cpp: the menu's mode (the class's own vmode is never set) */
/* the ortho drawInit loaded: the picture's left and right edge in the menu's 640-wide units */
static float pjL = 0, pjR = 639;

namespace menu {

#define CH_FONT_HEIGHT 24
#define CH_FONT_WIDTH 24

#define CHAR_IMG_SIZE 1152

static std::map<wchar_t, int> charCodeMap;
static std::map<wchar_t, u8*> charPngBufMap;
static std::map<wchar_t, std::pair<int, int> > inkBearing;   /* first / one past the last inked column per glyph (inkSpan) */
heap_cntrl* GXtexCache;
/* The glyph heap's block of the general MEM2 heap, sized to the loaded font (MEM2.h). */
static void *fontHeapMem;
unsigned int fontHeapBytes;   /* OptionsFrame.cpp's Memory page */

IplFont::IplFont()
        : frameWidth(640), atlas(NULL)
{
    FILE* charPngFile = getFontFile("sd");
    if (charPngFile == NULL)
    {
        charPngFile = getFontFile("usb");
    }

    loadFontFile(charPngFile);
}

IplFont::~IplFont()
{
    releaseFontMem();
}

void IplFont::loadFontFile(FILE* charPngFile)
{
    releaseFontMem();

    int bufIndex = 0;
    int skipSetp = (CHAR_IMG_SIZE + 4) / 2;
    int fontSize = 0;
    int searchLen;
    u8 *fontBuffer;

    u8 *fileBuffer = NULL;   /* a font file is read into a temporary MEM2 block */
    if (charPngFile != NULL)
    {
        fseek(charPngFile, 0, SEEK_END);
        fontSize = (int)ftell(charPngFile);
        if (fontSize > 0 && fontSize <= CN_FONT_SIZE)
            fileBuffer = (u8*)_mem2_malloc(fontSize);
        if (fileBuffer)
        {
            fseek(charPngFile, 0, SEEK_SET);
            if ((int)fread(fileBuffer, 1, fontSize, charPngFile) != fontSize)
            {
                _mem2_free(fileBuffer);
                fileBuffer = NULL;
            }
        }
        fclose(charPngFile);
        charPngFile = NULL;
    }
    if (fileBuffer)
    {
        fontBuffer = fileBuffer;
        searchLen = (int)(fontSize / (CHAR_IMG_SIZE + 4));
    }
    else
    {
        // font file is not exists, reload lanaguage
        lang = ENGLISH;

        // load inline english font
        fontBuffer = (u8*)En_dat;
        fontSize = En_dat_size;
        searchLen = (int)(fontSize / (CHAR_IMG_SIZE + 4));
    }

    /* The heap: every glyph's tile plus the heap's own per-block header and 32-byte
     * rounding (64 bytes is ample), the 256 KB atlas, and slack. Taken from the general
     * heap, so what a Latin font does not need stays there for textures. */
    fontHeapBytes = (unsigned int)searchLen * (CHAR_IMG_SIZE + 64) + 512 * 256 * 2 + 64 * 1024;
    fontHeapMem = _mem2_memalign(32, fontHeapBytes);
    if (!fontHeapMem && fileBuffer)
    {
        /* no room for this font: the built-in English one needs a fraction of it */
        _mem2_free(fileBuffer);
        fileBuffer = NULL;
        lang = ENGLISH;
        fontBuffer = (u8*)En_dat;
        fontSize = En_dat_size;
        searchLen = (int)(fontSize / (CHAR_IMG_SIZE + 4));
        fontHeapBytes = (unsigned int)searchLen * (CHAR_IMG_SIZE + 64) + 512 * 256 * 2 + 64 * 1024;
        fontHeapMem = _mem2_memalign(32, fontHeapBytes);
    }
    if (!fontHeapMem)
    {
        fontHeapBytes = 0;
        return;   /* no menu font at all: text draws nothing, nothing is written anywhere */
    }
    GXtexCache = (heap_cntrl*)malloc(sizeof(heap_cntrl));
    __lwp_heap_init(GXtexCache, fontHeapMem, fontHeapBytes, 32);

    blankChar = charToWideChar(" ");

    uint16_t *zhFontBufTemp = (uint16_t *)fontBuffer;
    while (bufIndex < searchLen)
    {
        charCodeMap.insert(std::pair<wchar_t, int>(*zhFontBufTemp, *((u8*)(zhFontBufTemp + 1) + 1)));
        u8 * tmpPngBuf = (u8*) __lwp_heap_allocate(GXtexCache, CHAR_IMG_SIZE);
        /* One 1152-byte tile per glyph out of the fixed CN_FONT_SIZE region, so a font with
         * more glyphs than the region holds runs it out. __lwp_heap_allocate then returns
         * NULL and the memcpy below would write to address 0; stop loading instead and keep
         * the glyphs read so far, which is enough to render the menu and say so. */
        if (tmpPngBuf == NULL)
        {
            charCodeMap.erase(*zhFontBufTemp);
            break;
        }
        memcpy(tmpPngBuf, (u8*)(zhFontBufTemp + 2), CHAR_IMG_SIZE);
        charPngBufMap.insert(std::pair<wchar_t, u8*>(*zhFontBufTemp, tmpPngBuf));

        zhFontBufTemp += skipSetp;
        bufIndex++;
    }
    if (fileBuffer)
        _mem2_free(fileBuffer);   /* the glyphs are copied into the heap */
    buildAtlas();
}

/* The atlas: 16 x 8 cells of 32x32, IA8, 512x256 (256 KB, from the font heap). Each 24x24
 * glyph sits at +4,+4 in its cell, and the 4-texel gutter around it repeats its edge
 * texels: that is what GX_CLAMP gives a glyph drawn from its own texture, so scaled text,
 * whose bilinear samples reach past a glyph's edge, samples the same values here. Every
 * texture coordinate is a multiple of 1/128 or 1/64, exact in floating point. */
#define ATLAS_COLS 16
#define ATLAS_ROWS 8
#define ATLAS_CELL 32
#define ATLAS_PAD ((ATLAS_CELL - CH_FONT_WIDTH) / 2)
#define ATLAS_W (ATLAS_COLS * ATLAS_CELL)
#define ATLAS_H (ATLAS_ROWS * ATLAS_CELL)
#define ATLAS_FIRST 32
#define ATLAS_LAST 126

/* the 2 bytes of texel (x, y) in a GX IA8 texture w texels wide: 4x4 blocks of 32 bytes */
static inline int ia8_offset(int w, int x, int y)
{
    return (((y >> 2) * (w >> 2) + (x >> 2)) << 5) + (((y & 3) * 4 + (x & 3)) << 1);
}

void IplFont::buildAtlas(void)
{
    int c, x, y;

    atlas = (u8*)__lwp_heap_allocate(GXtexCache, ATLAS_W * ATLAS_H * 2);
    if (atlas == NULL)
        return;                                           /* drawString keeps the glyph path */
    memset(atlas, 0, ATLAS_W * ATLAS_H * 2);
    for (c = ATLAS_FIRST; c <= ATLAS_LAST; c++)
    {
        const u8* glyph = getCharPngBuf((wchar_t)c);      /* the blank glyph if the font lacks it */
        int slot = c - ATLAS_FIRST;
        int x0 = (slot % ATLAS_COLS) * ATLAS_CELL, y0 = (slot / ATLAS_COLS) * ATLAS_CELL;
        for (y = 0; y < ATLAS_CELL; y++)
            for (x = 0; x < ATLAS_CELL; x++)
            {
                int gx = x - ATLAS_PAD, gy = y - ATLAS_PAD;   /* clamped into the glyph */
                gx = gx < 0 ? 0 : gx >= CH_FONT_WIDTH ? CH_FONT_WIDTH - 1 : gx;
                gy = gy < 0 ? 0 : gy >= CH_FONT_HEIGHT ? CH_FONT_HEIGHT - 1 : gy;
                memcpy(atlas + ia8_offset(ATLAS_W, x0 + x, y0 + y),
                       glyph + ia8_offset(CH_FONT_WIDTH, gx, gy), 2);
            }
    }
    DCFlushRange(atlas, ATLAS_W * ATLAS_H * 2);
    GX_InitTexObj(&atlasTexObj, atlas, ATLAS_W, ATLAS_H, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
}

void IplFont::releaseFontMem(void)
{
    freeSmall();
    if (atlas != NULL)
    {
        __lwp_heap_free(GXtexCache, atlas);
        atlas = NULL;
    }
    charCodeMap.clear();
    inkBearing.clear();
    if (charPngBufMap.size() > 0)
    {
         std::map<wchar_t, u8*>::iterator it = charPngBufMap.begin();
         while (it != charPngBufMap.end())
         {
             u8* tmpBuf = it->second;
             __lwp_heap_free(GXtexCache, tmpBuf);
             ++it;
         }

         charPngBufMap.clear();
    }
    if (GXtexCache)
    {
        free(GXtexCache);
        GXtexCache = NULL;
    }
    if (fontHeapMem)
    {
        _mem2_free(fontHeapMem);
        fontHeapMem = NULL;
    }
}

void IplFont::setVmode(GXRModeObj *rmode)
{
    vmode = rmode;
}

extern "C" char menuActive;
extern "C" char menuFont[];     /* GamecubeMain.cpp, global namespace */

FILE* IplFont::getFontFile(char* sdUsb)
{
    int (*configFile_init)(fileBrowser_file*) = fileBrowser_libfat_init;
    fileBrowser_file* configFile_file = (sdUsb[0] == 's' ? &saveDir_libfat_Default : &saveDir_libfat_USB);
    configFile_init(configFile_file);

    char fontPathBuf[256];
    /* A font chosen in the settings file (MenuFont = "Name") replaces the
     * language's default file for every script the .dat files can hold
     * (Latin); the CJK and Korean languages keep their own glyph sets. */
    if (menuFont[0] && lang != SIMP_CHINESE && lang != TRAD_CHINESE &&
        lang != KOREAN && lang != JAPANESE)
    {
        FILE* f;
        /* The 5.x names (Microsoft faces) now mean the open-source faces that
         * replaced them; a file of the old name on the card is the fallback. */
        const char* now = !strcmp(menuFont, "Segoe") ? "Sans" :
                          !strcmp(menuFont, "CalibriBold") ? "SansBold" :
                          !strcmp(menuFont, "Times") ? "Serif" : menuFont;
        snprintf(fontPathBuf, sizeof(fontPathBuf), "%s:/wiistation/fonts/%s.dat", sdUsb, now);
        f = fopen(fontPathBuf, "rb");
        if (f) return f;
        if (now != menuFont)
        {
            snprintf(fontPathBuf, sizeof(fontPathBuf), "%s:/wiistation/fonts/%s.dat", sdUsb, menuFont);
            f = fopen(fontPathBuf, "rb");
            if (f) return f;
        }
    }
    switch(lang)
    {
        case SIMP_CHINESE:
        case TRAD_CHINESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Chs.dat");
            break;

        case KOREAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Kr.dat");
            break;

        case SPANISH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Es.dat");
            break;

        case PORTUGUESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Pt.dat");
            break;

        case ITALIAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/It.dat");
            break;

        case GERMAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/De.dat");
            break;

        case JAPANESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Ja.dat");
            break;

        case FRENCH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Fr.dat");
            break;

        case BRAZILIAN_PORTUGUESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Br.dat");
            break;

        case CATALAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Ca.dat");
            break;

        case TURKISH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiistation/fonts/Tu.dat");
            break;

        default:
            return NULL;
    }
    return fopen(fontPathBuf, "rb");
}

void IplFont::drawInit(GXColor fontColor)
{
    setColor(fontColor);

    //FixMe: vmode access
    Mtx44 GXprojection2D;
    Mtx GXmodelView2D;

    // Reset various parameters from gfx plugin
    GX_SetCoPlanar(GX_DISABLE);
    GX_SetClipMode(GX_CLIP_ENABLE);
//    GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

    guMtxIdentity(GXmodelView2D);
    GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
//    guMtxTransApply (GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -5.0F);
    GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
    if((screenMode && menuActive) || screenMode == SCREENMODE_16x9_PILLARBOX)
    {
        pjL = -104; pjR = 743;
    }
    else
    {
        pjL = 0; pjR = 639;
    }
    guOrtho(GXprojection2D, 0, 479, pjL, pjR, 0, 700);
    GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);
//    GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);

    GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_TRUE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_PNMTX0);
    GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
//    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
//    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
//    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
//    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
//    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
//    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    //set vertex attribute formats here
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XY, GX_S16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
//    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_U16, 7);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    //enable textures
    GX_SetNumChans (1);
//    GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
    GX_SetNumTexGens (1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    //GX_InvalidateTexAll();
    //GX_InitTexObj(&fontTexObj, ZhBufFont_dat, fontPngWidth, CH_FONT_HEIGHT, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    //GX_InitTexObj(&fontTexObj, ZhBufFont_dat, CH_FONT_WIDTH, fontPngHeight, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    //GX_LoadTexObj(&fontTexObj, GX_TEXMAP0);

    GX_SetNumTevStages (1);
    GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0); // change to (u8) tile later
    GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_C1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
    GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
    GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);

    //set blend mode
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
    GX_SetColorUpdate(GX_ENABLE);
//    GX_SetAlphaUpdate(GX_ENABLE);
//    GX_SetDstAlpha(GX_DISABLE, 0xFF);
    //set cull mode
    GX_SetCullMode (GX_CULL_NONE);

}

void IplFont::setColor(GXColor fontColour)
{
    GX_SetTevColor(GX_TEVREG1, fontColour);
//    GX_SetTevKColor(GX_KCOLOR0, fontColour);
    fontColor.r = fontColour.r;
    fontColor.g = fontColour.g;
    fontColor.b = fontColour.b;
    fontColor.a = fontColour.a;
}

void IplFont::setColor(GXColor* fontColorPtr)
{
    GX_SetTevColor(GX_TEVREG1, *fontColorPtr);
//    GX_SetTevKColor(GX_KCOLOR0, *fontColorPtr);
    fontColor.r = fontColorPtr->r;
    fontColor.g = fontColorPtr->g;
    fontColor.b = fontColorPtr->b;
    fontColor.a = fontColorPtr->a;
}

__inline wchar_t* IplFont::charToWideChar(char* strChar) {
    wchar_t *strWChar = new wchar_t[strlen(strChar) + 1];

    size_t bt = mbstowcs(strWChar, strChar, strlen(strChar));
    if (bt != (size_t)-1) {
        strWChar[bt] = (wchar_t)'\0';
        return strWChar;
    }

    wchar_t *tempDest = strWChar;
    while((*tempDest++ = *strChar++));

    return strWChar;
}

__inline wchar_t* IplFont::charToWideChar(const char* strChar) {
    return charToWideChar((char*)strChar);
}

__inline u8* IplFont::getCharPngBuf(const wchar_t wChar) {
    std::map<wchar_t, u8*>::iterator iter = charPngBufMap.find(wChar);
    if (iter != charPngBufMap.end())
    {
        return (u8*)(charPngBufMap[wChar]);
    }
    else
    {
        return (u8*)(charPngBufMap[blankChar[0]]);
    }
}

__inline int IplFont::getCharCode(const wchar_t wChar) {
    std::map<wchar_t, int>::iterator iter = charCodeMap.find(wChar);
    if (iter != charCodeMap.end())
    {
        return charCodeMap[wChar];
    }
    else
    {
        return charCodeMap[blankChar[0]];
    }
}

/* ---- small text ----
 *
 * The 24 px glyphs drawn at 0.4 to 0.8 of their size and sampled bilinearly from the 24 px atlas
 * take every second or third texel and skip the rest: strokes break into dots and gaps (it looks
 * dithered). And where a vertex lands between pixels, as most do in the 640 x 479 ortho (a unit
 * is 640/639 of a pixel), the rest blurs. So a string below scale 1.0 is drawn from glyphs
 * reduced once, by area, to the pixels it will cover (the EFB's: Force 16:9 squeezes them to 3/4
 * of the width, the 240p modes halve their height), one texel for one pixel, each at a whole
 * pixel and sampled nearest. The reduced sets are kept per size, up to SMALL_SETS. */
#define SMALL_SETS 8
#define SMALL_MAX 30     /* the reduced glyph's largest side: the cell stays within the 32 of the full atlas */

struct IplFont::SmallAtlas
{
    int pw, ph, cell;
    u32 used;
    u8 *tex;
    GXTexObj obj;
};
static IplFont::SmallAtlas smallSets[SMALL_SETS];
static u32 smallTick;

IplFont::SmallAtlas* IplFont::smallAtlas(int pw, int ph)
{
    SmallAtlas *a = NULL;
    int i, c, x, y;
    const int cell = ((pw > ph ? pw : ph) + 2 + 3) & ~3;   /* a pixel of margin; whole 4 x 4 texel blocks */
    const int w = ATLAS_COLS * cell, h = ((ATLAS_LAST - ATLAS_FIRST) / ATLAS_COLS + 1) * cell;

    for (i = 0; i < SMALL_SETS; i++)
        if (smallSets[i].tex && smallSets[i].pw == pw && smallSets[i].ph == ph)
        {
            smallSets[i].used = ++smallTick;
            return &smallSets[i];
        }
    for (i = 0; i < SMALL_SETS; i++)   /* a free slot, else the one used longest ago */
    {
        if (!smallSets[i].tex) { a = &smallSets[i]; break; }
        if (!a || smallSets[i].used < a->used) a = &smallSets[i];
    }
    if (a->tex)
    {
        _mem2_free(a->tex);
        a->tex = NULL;
    }
    a->tex = (u8*)_mem2_memalign(32, w * h * 2);
    if (!a->tex)
        return NULL;
    memset(a->tex, 0, w * h * 2);
    for (c = ATLAS_FIRST; c <= ATLAS_LAST; c++)
    {
        const u8 *glyph = getCharPngBuf((wchar_t)c);
        const int slot = c - ATLAS_FIRST, x0 = (slot % ATLAS_COLS) * cell + 1, y0 = (slot / ATLAS_COLS) * cell + 1;
        for (y = 0; y < ph; y++)
            for (x = 0; x < pw; x++)
            {
                /* this pixel's source rectangle in 1/4096 of a source texel; each texel counts by its overlap */
                const int sx0 = x * CH_FONT_WIDTH * 4096 / pw, sx1 = (x + 1) * CH_FONT_WIDTH * 4096 / pw;
                const int sy0 = y * CH_FONT_HEIGHT * 4096 / ph, sy1 = (y + 1) * CH_FONT_HEIGHT * 4096 / ph;
                unsigned long long sum[2] = { 0, 0 }, area = 0;
                int tx, ty;
                for (ty = sy0 >> 12; ty < CH_FONT_HEIGHT && ty * 4096 < sy1; ty++)
                {
                    const int oy = (sy1 < ty * 4096 + 4096 ? sy1 : ty * 4096 + 4096) - (sy0 > ty * 4096 ? sy0 : ty * 4096);
                    for (tx = sx0 >> 12; tx < CH_FONT_WIDTH && tx * 4096 < sx1; tx++)
                    {
                        const int ox = (sx1 < tx * 4096 + 4096 ? sx1 : tx * 4096 + 4096) - (sx0 > tx * 4096 ? sx0 : tx * 4096);
                        const u8 *t = glyph + ia8_offset(CH_FONT_WIDTH, tx, ty);
                        const unsigned long long wgt = (unsigned long long)ox * oy;
                        sum[0] += t[0] * wgt;
                        sum[1] += t[1] * wgt;
                        area += wgt;
                    }
                }
                if (area)
                {
                    u8 *d = a->tex + ia8_offset(w, x0 + x, y0 + y);
                    const int cov = (int)((sum[0] + area / 2) / area);
                    /* a stroke two texels wide is one pixel or less here, and a pixel half covered
                     * shows as a pale one: coverage c becomes 1 - (1 - c)^2 (0.5 -> 0.75) so the
                     * strokes read solid, the edges still soft */
                    d[0] = (u8)(cov + (255 - cov) * cov / 255);
                    d[1] = (u8)((sum[1] + area / 2) / area);
                }
            }
    }
    DCFlushRange(a->tex, w * h * 2);
    a->pw = pw;
    a->ph = ph;
    a->cell = cell;
    a->used = ++smallTick;
    GX_InitTexObj(&a->obj, a->tex, w, h, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&a->obj, GX_NEAR, GX_NEAR);
    return a;
}

void IplFont::freeSmall(void)
{
    for (int i = 0; i < SMALL_SETS; i++)
        if (smallSets[i].tex)
        {
            _mem2_free(smallSets[i].tex);
            smallSets[i].tex = NULL;
        }
}

/* The n printable characters of txt at (x, y), laid out as drawString does, from glyphs made
 * for the pixels. False when the size does not suit (no atlas, a glyph past SMALL_MAX): the
 * caller draws as before. */
bool IplFont::drawSmall(int x, int y, const wchar_t *txt, int n, float scale)
{
    GXRModeObj *m = g_tv_mode ? g_tv_mode : ::vmode;
    int pw, ph, k;
    float kx, ky;
    SmallAtlas *a;

    if (!m || n <= 0 || !(scale > 0.0f) || scale >= 1.0f)
        return false;
    kx = m->fbWidth / (pjR - pjL);          /* EFB pixels per unit across, and down (y is 0..479) */
    ky = m->efbHeight / 479.0f;
    pw = (int)(CH_FONT_WIDTH * scale * kx + 0.5f);
    ph = (int)(CH_FONT_HEIGHT * scale * ky + 0.5f);
    if (pw < 3 || ph < 3 || pw > SMALL_MAX || ph > SMALL_MAX)
        return false;
    a = smallAtlas(pw, ph);
    if (!a)
        return false;

    const int cell = a->cell, aw = ATLAS_COLS * cell, ah = ((ATLAS_LAST - ATLAS_FIRST) / ATLAS_COLS + 1) * cell;
    const float pxTop = (float)(int)(y * ky + 0.5f);
    const float py = pxTop / ky, py1 = (pxTop + ph) / ky;

    float pen = (float)x;   /* the pen runs in fractions: the full path's whole-unit steps drop up to a unit a glyph */
    PERF_ADD(menu_glyphs, n);
    PERF_INC(menu_texloads);
    GX_LoadTexObjPreloaded(&a->obj, &texCacheRegionS[0], GX_TEXMAP0);   /* outside Begin/End */
    /* the vertex format the achievement toast's shapes use: float positions, so a vertex can sit
     * on a pixel */
    GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT7, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_Begin(GX_QUADS, GX_VTXFMT7, 4 * n);
    for (k = 0; k < n; k++)
    {
        const int slot = txt[k] < ATLAS_FIRST ? 0 : txt[k] - ATLAS_FIRST;
        const float pxLeft = (float)(int)((pen - pjL) * kx + 0.5f);
        const float vx0 = pxLeft / kx + pjL, vx1 = (pxLeft + pw) / kx + pjL;
        const float u0 = (float)((slot % ATLAS_COLS) * cell + 1) / aw, u1 = u0 + (float)pw / aw;
        const float v0 = (float)((slot / ATLAS_COLS) * cell + 1) / ah, v1 = v0 + (float)ph / ah;

        GX_Position2f32(vx0, py);
        GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
        GX_TexCoord2f32(u0, v0);

        GX_Position2f32(vx1, py);
        GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
        GX_TexCoord2f32(u1, v0);

        GX_Position2f32(vx1, py1);
        GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
        GX_TexCoord2f32(u1, v1);

        GX_Position2f32(vx0, py1);
        GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
        GX_TexCoord2f32(u0, v1);

        pen += (this->getCharCode(txt[k]) + 1) * scale;
    }
    GX_End();
    return true;
}

/* Where the ink of txt lies, in pixels from the pen's start, laid out as drawString lays it out
 * (a glyph's bearings are measured once from its tile). False when nothing is inked. */
bool IplFont::inkSpan(const wchar_t *txt, float scale, float &left, float &right)
{
    std::map<wchar_t, std::pair<int, int> > &bearing = inkBearing;
    float pen = 0.0f;
    bool any = false;

    for (; *txt; txt++)
    {
        std::map<wchar_t, std::pair<int, int> >::iterator it = bearing.find(*txt);
        if (it == bearing.end())
        {
            const u8 *g = getCharPngBuf(*txt);
            int c0 = CH_FONT_WIDTH, c1 = 0;
            for (int y = 0; y < CH_FONT_HEIGHT; y++)
                for (int x = 0; x < CH_FONT_WIDTH; x++)
                    if (g[ia8_offset(CH_FONT_WIDTH, x, y)] >= 64)
                    {
                        if (x < c0) c0 = x;
                        if (x + 1 > c1) c1 = x + 1;
                    }
            it = bearing.insert(std::make_pair(*txt, std::make_pair(c0, c1))).first;
        }
        if (it->second.first < it->second.second)
        {
            const float l = (int)(pen + 0.5f) + it->second.first * scale, r = (int)(pen + 0.5f) + it->second.second * scale;
            if (!any || l < left) left = l;
            if (!any || r > right) right = r;
            any = true;
        }
        pen += (getCharCode(*txt) + 1) * scale;
    }
    return any;
}

void IplFont::drawString(int x, int y, char *string, float scale, bool centered)
{
    if(centered)
    {
        int strHeight = this->getStringHeight(string, scale);
        int strWidth = this->getStringWidth(string, scale);
        float inkL, inkR;
        wchar_t *w = charToWideChar(gettext(string));

        /* Centre the ink, not the advance box: the width above counts 5 units of slack and the
         * spacing after the last letter, and each glyph sits in its cell with a left bearing, so
         * the ink of a label centred on x landed 1.5 to 3 px left of x (tests/menu_text_center.py) */
        if (inkSpan(w, scale, inkL, inkR))
            x = (int)((float)x - (inkL + inkR) / 2.0f + 0.5f);
        else
            x = (int) x - strWidth/2;
        delete[] w;
        y = (int) y - strHeight/2;
    }

    //GX_InvalidateTexAll();
    GX_InvalidateTexRegion(&texCacheRegionS[0]);
    PERF_INC(menu_strings);

    /* The glyph quad and the advance both follow `scale`; at 1.0 this is the 24x24 tile
     * the font file holds, which is what every menu control asks for. */
    const int gw = (int)(CH_FONT_WIDTH * scale + 0.5f);
    const int gh = (int)(CH_FONT_HEIGHT * scale + 0.5f);
    /* The pen runs in fractions and each glyph is placed at the nearest pixel: truncating the
     * advance of every glyph dropped up to a pixel each (at 1.3 an "a" moved 16 for 16.9), so
     * the gaps between letters differed from pair to pair and some looked like spaces. */
    float pen = (float)x;

    wchar_t *utf8Txt = charToWideChar(gettext(string));
    wchar_t *tmpPtr = utf8Txt;

    /* All printable ASCII: one texture load, then one GX_Begin for the whole string (the
     * load stays outside Begin/End: AGENTS.md). Anything else, such as CJK text, takes the
     * glyph-by-glyph path below. */
    /* control characters (the debug overlay's lines end in 
) are not in the font: the
     * glyph path draws them as the blank glyph, and so does the atlas, from the space cell */
    int n = 0;
    while (atlas != NULL && utf8Txt[n] &&
           ((utf8Txt[n] >= ATLAS_FIRST && utf8Txt[n] <= ATLAS_LAST) ||
            (utf8Txt[n] < ATLAS_FIRST && charPngBufMap.find(utf8Txt[n]) == charPngBufMap.end())))
        n++;
    if (atlas != NULL && utf8Txt[n] == 0)
    {
        if (n > 0 && drawSmall(x, y, utf8Txt, n, scale))
        {
            PERF_INC(menu_small);
            delete[] tmpPtr;
            return;
        }
        if (n > 0 && scale < 1.0f)
            PERF_INC(menu_small_no);
        if (n > 0)
        {
            PERF_ADD(menu_glyphs, n);
            PERF_INC(menu_texloads);
            GX_LoadTexObjPreloaded(&atlasTexObj, &texCacheRegionS[0], GX_TEXMAP0);
            GX_Begin(GX_QUADS, GX_VTXFMT1, 4 * n);
            for (int k = 0; k < n; k++)
            {
                int slot = utf8Txt[k] < ATLAS_FIRST ? 0 : utf8Txt[k] - ATLAS_FIRST;
                const int x = (int)(pen + 0.5f);
                float u0 = (float)((slot % ATLAS_COLS) * ATLAS_CELL + ATLAS_PAD) / ATLAS_W;
                float v0 = (float)((slot / ATLAS_COLS) * ATLAS_CELL + ATLAS_PAD) / ATLAS_H;
                float u1 = u0 + (float)CH_FONT_WIDTH / ATLAS_W, v1 = v0 + (float)CH_FONT_HEIGHT / ATLAS_H;

                GX_Position2s16(x, y);
                GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
                GX_TexCoord2f32(u0, v0);

                GX_Position2s16(gw + x, y);
                GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
                GX_TexCoord2f32(u1, v0);

                GX_Position2s16(gw + x, gh + y);
                GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
                GX_TexCoord2f32(u1, v1);

                GX_Position2s16(x, gh + y);
                GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
                GX_TexCoord2f32(u0, v1);

                pen += (this->getCharCode(utf8Txt[k]) + 1) * scale; // x + charWidth
            }
            GX_End();
        }
        delete[] tmpPtr;
        return;
    }

    while (*utf8Txt) {
        const int x = (int)(pen + 0.5f);
        PERF_INC(menu_glyphs);
        PERF_INC(menu_texloads);

        GX_InitTexObj(&fontTexObj, this->getCharPngBuf(*utf8Txt), CH_FONT_WIDTH, CH_FONT_HEIGHT, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_LoadTexObjPreloaded(&fontTexObj, &texCacheRegionS[0], GX_TEXMAP0);

        GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
            GX_Position2s16(x, y);
            GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
            GX_TexCoord2f32(0.0f, 0.0f);

            GX_Position2s16(gw + x, y);
            GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
            GX_TexCoord2f32(1.0f, 0.0f);

            GX_Position2s16(gw + x, gh + y);
            GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
            GX_TexCoord2f32(1.0f, 1.0f);

            GX_Position2s16(x, gh + y);
            GX_Color4u8(fontColor.r, fontColor.g, fontColor.b, fontColor.a);
            GX_TexCoord2f32(0.0f, 1.0f);

        GX_End();

        pen += (this->getCharCode(*utf8Txt) + 1) * scale; // x + charWidth
        utf8Txt++;
    }

    delete[] tmpPtr;
}

int IplFont::drawStringWrap(int x, int y, char *string, float scale, bool centered, int maxWidth, int lineSpacing)
{
    int numLines = 0;
    int stringWidth = 0;
    int tokenWidth = 0;
    int numTokens = 0;
    char* utf8Txt = (char *)gettext(string);
    char* lineStart = utf8Txt;
    char* lineStop = utf8Txt;
    char* stringWork = utf8Txt;
    char* stringDraw = NULL;
    lineSpacing = CH_FONT_HEIGHT;

    while(1)
    {
        if(*stringWork == 0) //end of string
        {
            if((stringWidth + tokenWidth <= maxWidth) || (numTokens == 0))
            {
                if (stringWidth + tokenWidth > 0)
                {
                    drawString( x, y+numLines*lineSpacing, lineStart, scale, centered);
                    numLines++;
                }
                break;
            }
            else
            {
                stringDraw = (char*)malloc(lineStop - lineStart + 1);
                for (int i = 0; i < lineStop-lineStart; i++)
                    stringDraw[i] = lineStart[i];
                stringDraw[lineStop-lineStart] = 0;
                drawString( x, y+numLines*lineSpacing, stringDraw, scale, centered);
                free(stringDraw);
                numTokens = 0; // Reset for next part
                lineStart = lineStop+1;
                stringWork = lineStart; // Reset work pointer
                stringWidth = 0;
                tokenWidth = 0;
                // We don't break; we continue to process the rest of the string
                continue;
            }
        }

        if((*stringWork == ' ')) //end of token
        {
            if(stringWidth + tokenWidth <= maxWidth)
            {
                stringWidth += tokenWidth;
                numTokens++;
                tokenWidth = 0;
                lineStop = stringWork;
            }
            else
            {
                if (numTokens == 0)    //if the word is wider than maxWidth, just print it
                    lineStop = stringWork;

                stringDraw = (char*)malloc(lineStop - lineStart + 1);
                for (int i = 0; i < lineStop-lineStart; i++)
                    stringDraw[i] = lineStart[i];
                stringDraw[lineStop-lineStart] = 0;
                drawString( x, y+numLines*lineSpacing, stringDraw, scale, centered);
                free(stringDraw);
                numTokens = 0; // Reset
                lineStart = lineStop+1;
                lineStop = lineStart;
                stringWork = lineStart;
                stringWidth = 0;
                tokenWidth = 0;
                continue;
            }
        }
        tokenWidth += (int) CH_FONT_WIDTH * scale;

        stringWork++;
    }

    return numLines;
}

void IplFont::drawStringAtOrigin(char *string, float scale)
{
    int x0, y0, x = 0;
    wchar_t *utf8Txt = charToWideChar(gettext(string));
    wchar_t *tmpPtr = utf8Txt;
    while (*utf8Txt)
    {
        x += this->getCharCode(*utf8Txt);
        utf8Txt++;
    }
    x0 = (int) -x / 2;
    y0 = (int) -CH_FONT_HEIGHT / 2;
    delete[] tmpPtr;

    drawString(x0, y0, string, scale, false);
}

/* Must match drawString's advance, or centred text lands off centre. */
int IplFont::getStringWidth(char *string, float scale)
{
    int strWidth = 0;
    wchar_t *utf8Txt = charToWideChar(gettext(string));
    wchar_t *tmpPtr = utf8Txt;
    while(*utf8Txt)
    {
        strWidth += this->getCharCode(*utf8Txt) + 1;
        utf8Txt++;
    }
    delete[] tmpPtr;

    return (int)((strWidth + 5) * scale);
}

int IplFont::getStringHeight(char *string, float scale)
{
    return (int)(CH_FONT_HEIGHT * scale);
}

} //namespace menu

extern "C" {
void IplFont_drawInit(GXColor fontColor)
{
    menu::IplFont::getInstance().drawInit(fontColor);
}

void IplFont_drawString(int x, int y, char *string, float scale, bool centered)
{
    menu::IplFont::getInstance().drawString(x, y, string, scale, centered);
}

int IplFont_getStringWidth(char *string, float scale)
{
    return menu::IplFont::getInstance().getStringWidth(string, scale);
}

void IplFont_loadFontFile(FILE* fontFile)
{
    menu::IplFont::getInstance().loadFontFile(fontFile);
}

FILE* IplFont_getFontFile(char* sdUsb)
{
    return menu::IplFont::getInstance().getFontFile(sdUsb);
}
}
