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
#include <ogc/lwp_heap.h>

#include "IPLFont.h"
#include "../MEM2.h"
#include "../perf_prof.h"

#include "gui2/gettext.h"
#include "../wiiSXconfig.h"
extern "C" {
#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-libfat.h"
#include "../../gpu.h"
}

extern GXTexRegion texCacheRegionS[8];

namespace menu {

#define CH_FONT_HEIGHT 24
#define CH_FONT_WIDTH 24

#define CHAR_IMG_SIZE 1152

static std::map<wchar_t, int> charCodeMap;
static std::map<wchar_t, u8*> charPngBufMap;
heap_cntrl* GXtexCache;

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

    GXtexCache = (heap_cntrl*)malloc(sizeof(heap_cntrl));
    __lwp_heap_init(GXtexCache, CN_FONT_LO, CN_FONT_SIZE, 32);

    if (charPngFile != NULL)
    {
        fseek(charPngFile, 0, SEEK_END);
        fontSize = (int)ftell(charPngFile);
        searchLen = (int)(fontSize / (CHAR_IMG_SIZE + 4));
        fontBuffer = (u8*) RECMEM2_LO;

        fseek(charPngFile, 0, SEEK_SET);
        fread(fontBuffer, 1, fontSize, charPngFile);
        fclose(charPngFile);
        charPngFile = NULL;
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
    if (charPngFile != NULL)
    {
        //__lwp_heap_free(GXtexCache, fontBuffer);
    }
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
    if (atlas != NULL)
    {
        __lwp_heap_free(GXtexCache, atlas);
        atlas = NULL;
    }
    charCodeMap.clear();
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
        snprintf(fontPathBuf, sizeof(fontPathBuf), "%s:/wiisxrx/fonts/%s.dat", sdUsb, menuFont);
        f = fopen(fontPathBuf, "rb");
        if (f) return f;
    }
    switch(lang)
    {
        case SIMP_CHINESE:
        case TRAD_CHINESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Chs.dat");
            break;

        case KOREAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Kr.dat");
            break;

        case SPANISH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Es.dat");
            break;

        case PORTUGUESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Pt.dat");
            break;

        case ITALIAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/It.dat");
            break;

        case GERMAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/De.dat");
            break;

        case JAPANESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Ja.dat");
            break;

        case FRENCH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Fr.dat");
            break;

        case BRAZILIAN_PORTUGUESE:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Br.dat");
            break;

        case CATALAN:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Ca.dat");
            break;

        case TURKISH:
            sprintf(fontPathBuf, "%s%s", sdUsb, ":/wiisxrx/fonts/Tu.dat");
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
    if(screenMode && menuActive)
        guOrtho(GXprojection2D, 0, 479, -104, 743, 0, 700);
    else if(screenMode == SCREENMODE_16x9_PILLARBOX)
        guOrtho(GXprojection2D, 0, 479, -104, 743, 0, 700);
    else
        guOrtho(GXprojection2D, 0, 479, 0, 639, 0, 700);
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

void IplFont::drawString(int x, int y, char *string, float scale, bool centered)
{
    if(centered)
    {
        int strHeight = this->getStringHeight(string, scale);
        int strWidth = this->getStringWidth(string, scale);

        x = (int) x - strWidth/2;
        y = (int) y - strHeight/2;
    }

    //GX_InvalidateTexAll();
    GX_InvalidateTexRegion(&texCacheRegionS[0]);
    PERF_INC(menu_strings);

    /* The glyph quad and the advance both follow `scale`; at 1.0 this is the 24x24 tile
     * the font file holds, which is what every menu control asks for. */
    const int gw = (int)(CH_FONT_WIDTH * scale);
    const int gh = (int)(CH_FONT_HEIGHT * scale);

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
        if (n > 0)
        {
            PERF_ADD(menu_glyphs, n);
            PERF_INC(menu_texloads);
            GX_LoadTexObjPreloaded(&atlasTexObj, &texCacheRegionS[0], GX_TEXMAP0);
            GX_Begin(GX_QUADS, GX_VTXFMT1, 4 * n);
            for (int k = 0; k < n; k++)
            {
                int slot = utf8Txt[k] < ATLAS_FIRST ? 0 : utf8Txt[k] - ATLAS_FIRST;
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

                x += (int)((this->getCharCode(utf8Txt[k]) + 1) * scale); // x + charWidth
            }
            GX_End();
        }
        delete[] tmpPtr;
        return;
    }

    while (*utf8Txt) {
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

        x += (int)((this->getCharCode(*utf8Txt) + 1) * scale); // x + charWidth
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

void IplFont_loadFontFile(FILE* fontFile)
{
    menu::IplFont::getInstance().loadFontFile(fontFile);
}

FILE* IplFont_getFontFile(char* sdUsb)
{
    return menu::IplFont::getInstance().getFontFile(sdUsb);
}
}
