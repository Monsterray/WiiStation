/***************************************************************************
 * acc.c -- the Accurate and Hi-Fi pixel pipelines of the soft GPU plugins
 * (setting SoftGpuMode: 0 Accurate, 1 Hi-Fi, 2 Fast = the P.E.Op.S. code in soft.c).
 *
 * Included by gpulib_if.c after soft.c and prim.c (one unit). Both soft plugins use it:
 * Soft Fast (gpulib/oldGpu.c) and Soft Timed (gpulib/gpulib.c) dispatch their GP0 commands
 * through primTableAcc instead of primTableJ when the mode is not Fast.
 *
 * The drawing rules -- triangle setup and edge walking, colour and UV interpolation in fixed
 * point, the dither matrix, semi-transparency, mask set/check, sprites with flip, lines, the
 * GP0 02 fill, the GP0 80 copy, the CLUT cache and the texture cache -- are those of the
 * Mednafen PS1 GPU (src/psx/gpu_polygon.cpp, gpu_sprite.cpp, gpu_line.cpp, gpu_common.inc,
 * gpu.cpp; Copyright (C) 2011-2017 Mednafen Team; GNU GPL version 2 or later), rewritten in
 * C for WiiStation's VRAM (psxVuw, PS1 byte order on a big-endian Wii). Not ported: the 480i
 * field skip (LineSkipTest: both fields are drawn, as the Fast pipeline does).
 *
 * Draw time: every command adds GPU ticks to acc_ticks, which the core charges as GPU busy
 * time when GpuTiming is Accurate (psxhw.c gpu_charge_draw, psxdma.c). The structure is
 * Mednafen's DrawTimeAvail; the constants are fitted to ps1-tests gpu/bandwidth on a real
 * PS1 (one tick = 0.5293 CPU cycles; fill as Mednafen; a drawn pixel 1, semi-transparent or
 * mask-checked untextured +0.5, gouraud or textured polygon 2, textured sprite 1, a texture
 * cache line load 16, a VRAM copy 2.5 per pixel).
 *
 * Hi-Fi keeps every rule of Accurate (geometry, mask, which pixels blend) and changes only
 * the colour arithmetic: no dither, colours rounded to the nearest 5-bit step instead of
 * truncated, and semi-transparent blending done on the 8-bit colour before it is rounded.
 ***************************************************************************/

#ifdef _IN_GPU

extern char softGpuMode;   /* Gamecube/GamecubeMain.cpp, setting SoftGpuMode */
#define SOFTGPU_ACCURATE 0
#define SOFTGPU_HIFI     1
#define SOFTGPU_FAST     2

typedef struct { int32_t x, y; int32_t u, v; int32_t r, g, b; } acc_vtx;

static struct
{
 int32_t  cx0, cy0, cx1, cy1;      /* drawing area, inclusive */
 int32_t  offx, offy;
 uint16_t mask_or, mask_and;       /* E6.0 -> 8000h OR, E6.1 -> 8000h test */
 uint8_t  dtd, abr, texmode;       /* texmode 0..3 as in E1 */
 uint8_t  tex_disable, allow_tex_disable;
 uint32_t tpx, tpy;
 uint32_t sprite_flip;
 uint32_t tww, twh, twx, twy;
 uint32_t twx_and, twx_add, twy_and, twy_add;
 uint16_t clut[256];
 uint32_t clut_vb;                 /* the CLUT word the cache holds, ~0 = none */
 uint8_t  clut_8;                  /* it holds 256 entries (loaded for 8-bit), not 16 */
 struct { uint32_t tag; uint16_t data[4]; } tc[256];
 uint8_t  dlut[4][4][512];        /* index = colour * modulation, 8.0 scale, up to 511 */
 uint8_t  hlut[512];              /* Hi-Fi: rounded, no dither */
 int      hifi;
 int      ready;
} A;

static inline int32_t acc_sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }

static uint32_t acc_ticks;   /* draw time not yet charged by the core (acc_take_ticks) */

/* psxhw.c / psxdma.c: the GPU ticks drawn since the last call */
uint32_t acc_take_ticks(void)
{
 uint32_t t = acc_ticks;
 acc_ticks = 0;
 return t;
}

#define ACC_PX(x, y) (&psxVuw[((uint32_t)((y) & 511) << 10) | ((x) & 1023)])

static void acc_tc_invalidate(void)
{
 int i;
 for (i = 0; i < 256; i++) A.tc[i].tag = ~0U;
}

static void acc_cache_invalidate(void)
{
 A.clut_vb = ~0U;
 acc_tc_invalidate();
}

static void acc_tw_recalc(void)
{
 uint32_t tm = A.texmode > 2 ? 2 : A.texmode;
 A.twx_and = ~(A.tww << 3);
 A.twx_add = ((A.twx & A.tww) << 3) + (A.tpx << (2 - tm));
 A.twy_and = ~(A.twh << 3);
 A.twy_add = ((A.twy & A.twh) << 3) + A.tpy;
}

/* GP1(00) and power-on: the state of Mednafen's SoftReset */
void acc_reset(void)
{
 int x, y, v;
 static const int dt[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
 if (!A.ready)
  {
   for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
     for (v = 0; v < 512; v++)
      {
       int d = (v + dt[y][x]) >> 3;
       A.dlut[y][x][v] = d < 0 ? 0 : (d > 31 ? 31 : d);
      }
   for (v = 0; v < 512; v++)
    A.hlut[v] = (v + 4) >> 3 > 31 ? 31 : (v + 4) >> 3;
   A.ready = 1;
  }
 A.cx0 = A.cy0 = A.cx1 = A.cy1 = 0;
 A.offx = A.offy = 0;
 A.mask_or = A.mask_and = 0;
 A.dtd = A.abr = A.texmode = 0;
 A.tex_disable = A.allow_tex_disable = 0;
 A.tpx = A.tpy = 0;
 A.sprite_flip = 0;
 A.tww = A.twh = A.twx = A.twy = 0;
 acc_tw_recalc();
 acc_cache_invalidate();
}

static inline int acc_on(void)
{
 if (!A.ready) acc_reset();
 A.hifi = softGpuMode == SOFTGPU_HIFI;
 return softGpuMode != SOFTGPU_FAST;
}

/* GPUSTAT bit 15, the texture disable bit (written through E1.11 only after GP1(09h) allows it) */
static inline uint32_t acc_status15(void) { return (uint32_t)A.tex_disable << 15; }

static void acc_set_tpage(uint32_t w)
{
 uint32_t ntpx = (w & 0xF) * 64, ntpy = (w & 0x10) * 16, ntm = (w >> 7) & 3;
 A.abr = (w >> 5) & 3;
 if (!ntm != !A.texmode || ntpx != A.tpx || ntpy != A.tpy)
  acc_tc_invalidate();
 {
  /* ps1-tests gpu/gp0-e1: the bit follows E1.11 while GP1(09h) allows it, and a write while
   * it is not allowed clears it (it reads back unchanged until the next write) */
  uint8_t nd = A.allow_tex_disable ? (w >> 11) & 1 : 0;
  if (nd != A.tex_disable) acc_tc_invalidate();
  A.tex_disable = nd;
 }
 A.tpx = ntpx; A.tpy = ntpy; A.texmode = ntm;
 acc_tw_recalc();
}

/* E1..E6 as words (host order); the P.E.Op.S. handlers have already run for the status bits */
static void acc_ecmd(uint32_t w)
{
 switch (w >> 24)
  {
   case 0xE1:
    acc_set_tpage(w);
    A.sprite_flip = w & 0x3000;
    A.dtd = (w >> 9) & 1;
    lGPUstatusRet = (lGPUstatusRet & ~0x8000) | acc_status15();
    break;
   case 0xE2:
    A.tww = w & 0x1F; A.twh = (w >> 5) & 0x1F; A.twx = (w >> 10) & 0x1F; A.twy = (w >> 15) & 0x1F;
    acc_tw_recalc();
    break;
   case 0xE3: A.cx0 = w & 1023; A.cy0 = (w >> 10) & 1023; break;
   case 0xE4: A.cx1 = w & 1023; A.cy1 = (w >> 10) & 1023; break;
   case 0xE5: A.offx = acc_sext11(w & 2047); A.offy = acc_sext11((w >> 11) & 2047); break;
   case 0xE6: A.mask_or = (w & 1) ? 0x8000 : 0; A.mask_and = (w & 2) ? 0x8000 : 0; break;
  }
}

/* After a state load or a plugin switch: the E1..E6 words the front-end kept */
void acc_sync_ecmds(const uint32_t *e)
{
 int i;
 if (!A.ready) acc_reset();
 for (i = 1; i <= 6; i++)
  acc_ecmd((0xE0u + i) << 24 | (e[i] & 0xFFFFFF));
 acc_cache_invalidate();
}

/* GP1(09h): allow E1.11 to set the texture disable bit */
void acc_gp1_09(uint32_t w) { A.allow_tex_disable = w & 1; }

//////////////////////////////////////////////////////////////////////////
// pixels
//////////////////////////////////////////////////////////////////////////

#define ACC_INLINE static inline __attribute__((always_inline))

/* blargg's 15-bit maths, as Mednafen; fg has bit 15 set */
ACC_INLINE uint16_t acc_blend15(uint32_t bg, uint32_t fg, const int bm)
{
 switch (bm)
  {
   case 0:
    bg |= 0x8000;
    return ((fg + bg) - ((fg ^ bg) & 0x0421)) >> 1;
   case 1:
    {
     uint32_t sum, carry;
     bg &= ~0x8000;
     sum = fg + bg;
     carry = (sum - ((fg ^ bg) & 0x8421)) & 0x8420;
     return (sum - carry) | (carry - (carry >> 5));
    }
   case 2:
    {
     uint32_t diff, borrow;
     bg |= 0x8000;
     fg &= ~0x8000;
     diff = bg - fg + 0x108420;
     borrow = (diff - ((bg ^ fg) & 0x108420)) & 0x108420;
     return (diff - borrow) & (borrow - (borrow >> 5));
    }
   default:
    {
     uint32_t sum, carry;
     bg &= ~0x8000;
     fg = ((fg >> 2) & 0x1CE7) | 0x8000;
     sum = fg + bg;
     carry = (sum - ((fg ^ bg) & 0x8421)) & 0x8420;
     return (sum - carry) | (carry - (carry >> 5));
    }
  }
}

/* One pixel at p (PS1 byte order): fore = 15-bit colour | bit 15; bm = -1 opaque, 0..3 the
 * semi-transparency of a pixel whose bit 15 is set; mand/mor = E6 check and set */
ACC_INLINE void acc_px(uint16_t *p, uint16_t fore, const int bm, const int textured,
                       const uint16_t mand, const uint16_t mor)
{
 uint16_t old = GETLE16(p), pix = fore;
 if (old & mand)
  return;
 if (bm >= 0 && (fore & 0x8000))
  pix = acc_blend15(old, fore, bm);
 PUTLE16(p, (textured ? pix : (pix & 0x7FFF)) | mor);
}

/* Hi-Fi: one channel blended at 8 bits, rounded to 5 */
ACC_INLINE uint32_t acc_hblend(uint32_t b5, uint32_t f, const int bm)
{
 int32_t b = (b5 << 3) | (b5 >> 2), r;
 if (f > 255) f = 255;
 switch (bm)
  {
   case 0:  r = (b + (int32_t)f) >> 1; break;
   case 1:  r = b + (int32_t)f; break;
   case 2:  r = b - (int32_t)f; break;
   default: r = b + (int32_t)(f >> 2); break;
  }
 if (r < 0) r = 0;
 return A.hlut[r > 511 ? 511 : r];
}

/* A pixel from 8-bit-scale channels (r, g, b index l, up to 511) and its bit 15. l is the
 * dither LUT row of this pixel, or hlut in Hi-Fi, where a blended pixel mixes at 8 bits. */
ACC_INLINE void acc_px8(uint16_t *p, uint32_t r, uint32_t g, uint32_t b, uint16_t stp, const int bm,
                        const int textured, const uint8_t *l, const int hifi,
                        const uint16_t mand, const uint16_t mor)
{
 if (hifi && bm >= 0 && stp)
  {
   uint16_t old = GETLE16(p);
   if (old & mand)
    return;
   PUTLE16(p, (textured ? 0x8000 : 0) | acc_hblend(old & 31, r, bm) |
              (acc_hblend((old >> 5) & 31, g, bm) << 5) |
              (acc_hblend((old >> 10) & 31, b, bm) << 10) | mor);
   return;
  }
 acc_px(p, stp | l[r] | (l[g] << 5) | (l[b] << 10), bm, textured, mand, mor);
}

/* The same by coordinates, for lines (x, y inside the drawing area) */
static void acc_plot(int32_t x, int32_t y, uint16_t fore, int bm, int textured)
{
 acc_px(ACC_PX(x, y), fore, bm, textured, A.mask_and, A.mask_or);
}

static void acc_plot8(int32_t x, int32_t y, uint32_t r, uint32_t g, uint32_t b,
                      uint16_t stp, int bm, int textured, int dx, int dy)
{
 acc_px8(ACC_PX(x, y), r, g, b, stp, bm, textured, A.hifi ? A.hlut : A.dlut[dy][dx], A.hifi,
         A.mask_and, A.mask_or);
}

/* The CLUT cache (ps1-tests gpu/clut-cache, real-PS1 VRAM): a draw with a 4- or 8-bit texture
 * loads it when the CLUT address differs from the cached one, or when it is 8-bit and the
 * cache holds only the 16 entries of a 4-bit load. GP0 01 empties it. Mode changes (E1) and
 * VRAM writes over the CLUT do not reload it. The row wraps at x = 1024. */
static void acc_clut_update(uint32_t raw, uint32_t tm)
{
 if (tm < 2)
  {
   uint32_t vb = raw & 0x7FFF;
   if (A.clut_vb != vb || (tm == 1 && !A.clut_8))
    {
     uint32_t row = (raw >> 6) & 0x1FF, cxo = (raw & 0x3F) << 4, n = tm ? 256 : 16, i;
     for (i = 0; i < n; i++)
      A.clut[i] = GETLE16(&psxVuw[(row << 10) | ((cxo + i) & 0x3FF)]);
     A.clut_vb = vb;
     A.clut_8 = tm == 1;
    }
  }
}

static inline uint16_t acc_texel(uint32_t u, uint32_t v, uint32_t tm)
{
 uint32_t ux = (u & A.twx_and) + A.twx_add;
 uint32_t fx = (ux >> (2 - tm)) & 1023;
 uint32_t fy = (v & A.twy_and) + A.twy_add;
 uint32_t gro = fy * 1024U + fx, ci;
 uint16_t w;
 if (tm == 0) ci = ((gro >> 2) & 0x3) | ((gro >> 8) & 0xFC);
 else         ci = ((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8);
 if (A.tc[ci].tag != (gro & ~3U))
  {
   acc_ticks += 16;   /* a cache line load */
   const uint16_t *s = &psxVuw[(gro & ~3U) & 0x7FFFF];
   A.tc[ci].data[0] = GETLE16(&s[0]); A.tc[ci].data[1] = GETLE16(&s[1]);
   A.tc[ci].data[2] = GETLE16(&s[2]); A.tc[ci].data[3] = GETLE16(&s[3]);
   A.tc[ci].tag = gro & ~3U;
  }
 w = A.tc[ci].data[gro & 3];
 if (tm == 0) return A.clut[(w >> ((ux & 3) * 4)) & 0xF];
 if (tm == 1) return A.clut[(w >> ((ux & 1) * 8)) & 0xFF];
 return w;
}

//////////////////////////////////////////////////////////////////////////
// triangles (Mednafen gpu_polygon.cpp)
//////////////////////////////////////////////////////////////////////////

#define AFBS 12
#define APAD 12

typedef struct { uint32_t u, v, r, g, b; } acc_ig;
typedef struct { uint32_t du_dx, dv_dx, dr_dx, dg_dx, db_dx, du_dy, dv_dy, dr_dy, dg_dy, db_dy; } acc_idl;

typedef struct
{
 int gouraud, textured, bm, texmult, tm;
 void (*span)(int32_t, int32_t, int32_t, acc_ig, const acc_idl *);
} acc_pmode;

static inline int64_t acc_xfp(int32_t x) { return (int64_t)(((uint64_t)(uint32_t)x << 32) + ((1ULL << 32) - (1 << 11))); }

static inline int64_t acc_xfp_step(int32_t dx, int32_t dy)
{
 int64_t dxe = (int64_t)((uint64_t)(int64_t)dx << 32);
 if (dxe < 0) dxe -= dy - 1;
 if (dxe > 0) dxe += dy - 1;
 return dxe / dy;
}

static inline int32_t acc_calcis(const acc_vtx *A_, const acc_vtx *B, const acc_vtx *C, int ax, int ay)
{
 /* CALCIS(x, y) = (B.x - A.x) * (C.y - B.y) - (C.x - B.x) * (B.y - A.y), int32 wrap */
 const int32_t *a = &A_->x, *b = &B->x, *c = &C->x;
 return (int32_t)((uint32_t)(b[ax] - a[ax]) * (uint32_t)(c[ay] - b[ay]) -
                  (uint32_t)(c[ax] - b[ax]) * (uint32_t)(b[ay] - a[ay]));
}

/* field indexes in acc_vtx (all int32): x 0, y 1, u 2, v 3, r 4, g 5, b 6 */
static inline uint32_t acc_delta(const acc_vtx *a, const acc_vtx *b, const acc_vtx *c, int f1, int f2, int32_t denom)
{
 int32_t n = (int32_t)((uint32_t)acc_calcis(a, b, c, f1, f2) * (1u << AFBS));
 return (uint32_t)(n / denom) << APAD;
}

static inline void acc_add_dx(acc_ig *ig, const acc_idl *d, uint32_t n, const acc_pmode *m)
{
 if (m->textured) { ig->u += d->du_dx * n; ig->v += d->dv_dx * n; }
 if (m->gouraud) { ig->r += d->dr_dx * n; ig->g += d->dg_dx * n; ig->b += d->db_dx * n; }
}

static inline void acc_add_dy(acc_ig *ig, const acc_idl *d, uint32_t n, const acc_pmode *m)
{
 if (m->textured) { ig->u += d->du_dy * n; ig->v += d->dv_dy * n; }
 if (m->gouraud) { ig->r += d->dr_dy * n; ig->g += d->dg_dy * n; ig->b += d->db_dy * n; }
}

/* One span of a triangle, specialised by the constant arguments (acc_spans[] below) */
ACC_INLINE void acc_span_k(int32_t y, int32_t x_start, int32_t x_bound, acc_ig ig, const acc_idl *d,
                           const int gouraud, const int textured, const int texmult, const int bm, const int tm)
{
 int32_t xa = x_start, w = x_bound - x_start, x = acc_sext11((uint32_t)x_start);
 const uint16_t mand = A.mask_and, mor = A.mask_or;
 const int hifi = A.hifi, dtd = A.dtd;
 uint32_t u = ig.u, v = ig.v, r = ig.r, g = ig.g, b = ig.b;
 uint16_t *row;
 if (x < A.cx0)
  {
   int32_t delta = A.cx0 - x;
   xa += delta; x += delta; w -= delta;
  }
 if (x + w > A.cx1 + 1)
  w = A.cx1 + 1 - x;
 if (w <= 0)
  return;
 if (gouraud || textured) acc_ticks += w * 2;
 else if (bm >= 0 || mand) acc_ticks += w + ((w + 1) >> 1);
 else acc_ticks += w;
 if (textured)
  {
   u += d->du_dx * (uint32_t)xa; v += d->dv_dx * (uint32_t)xa;
   u += d->du_dy * (uint32_t)y;  v += d->dv_dy * (uint32_t)y;
  }
 if (gouraud)
  {
   r += d->dr_dx * (uint32_t)xa; g += d->dg_dx * (uint32_t)xa; b += d->db_dx * (uint32_t)xa;
   r += d->dr_dy * (uint32_t)y;  g += d->dg_dy * (uint32_t)y;  b += d->db_dy * (uint32_t)y;
  }
 row = &psxVuw[(uint32_t)(y & 511) << 10];
 do
  {
   uint16_t *p = row + x;
   uint32_t r8 = r >> (AFBS + APAD), g8 = g >> (AFBS + APAD), b8 = b >> (AFBS + APAD);
   if (textured)
    {
     uint16_t t = acc_texel(u >> (AFBS + APAD), v >> (AFBS + APAD), tm);
     if (t)
      {
       if (texmult)
        acc_px8(p, ((t & 0x1F) * r8) >> 4, ((t & 0x3E0) * g8) >> 9, ((t & 0x7C00) * b8) >> 14,
                t & 0x8000, bm, 1, hifi ? A.hlut : dtd ? A.dlut[y & 3][x & 3] : A.dlut[2][3], hifi, mand, mor);
       else if (hifi && bm >= 0 && (t & 0x8000))
        acc_px8(p, (t & 0x1F) << 3, ((t >> 5) & 0x1F) << 3, ((t >> 10) & 0x1F) << 3, 0x8000, bm, 1,
                A.hlut, 1, mand, mor);
       else
        acc_px(p, t, bm, 1, mand, mor);
      }
    }
   else if (hifi)
    acc_px8(p, r8, g8, b8, 0x8000, bm, 0, A.hlut, 1, mand, mor);
   else if (gouraud && dtd)
    acc_px8(p, r8, g8, b8, 0x8000, bm, 0, A.dlut[y & 3][x & 3], 0, mand, mor);
   else
    acc_px(p, 0x8000 | (r8 >> 3) | ((g8 >> 3) << 5) | ((b8 >> 3) << 10), bm, 0, mand, mor);
   x++;
   if (textured) { u += d->du_dx; v += d->dv_dx; }
   if (gouraud) { r += d->dr_dx; g += d->dg_dx; b += d->db_dx; }
  } while (--w > 0);
}

typedef void (*acc_span_fn)(int32_t, int32_t, int32_t, acc_ig, const acc_idl *);

static void acc_span_00000(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 0, 0, -1, 0); }
static void acc_span_00010(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 0, 0, 0, 0); }
static void acc_span_00020(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 0, 0, 1, 0); }
static void acc_span_00030(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 0, 0, 2, 0); }
static void acc_span_00040(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 0, 0, 3, 0); }
static void acc_span_01000(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, -1, 0); }
static void acc_span_01001(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, -1, 1); }
static void acc_span_01002(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, -1, 2); }
static void acc_span_01010(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 0, 0); }
static void acc_span_01011(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 0, 1); }
static void acc_span_01012(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 0, 2); }
static void acc_span_01020(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 1, 0); }
static void acc_span_01021(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 1, 1); }
static void acc_span_01022(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 1, 2); }
static void acc_span_01030(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 2, 0); }
static void acc_span_01031(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 2, 1); }
static void acc_span_01032(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 2, 2); }
static void acc_span_01040(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 3, 0); }
static void acc_span_01041(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 3, 1); }
static void acc_span_01042(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 0, 3, 2); }
static void acc_span_01100(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, -1, 0); }
static void acc_span_01101(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, -1, 1); }
static void acc_span_01102(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, -1, 2); }
static void acc_span_01110(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 0, 0); }
static void acc_span_01111(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 0, 1); }
static void acc_span_01112(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 0, 2); }
static void acc_span_01120(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 1, 0); }
static void acc_span_01121(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 1, 1); }
static void acc_span_01122(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 1, 2); }
static void acc_span_01130(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 2, 0); }
static void acc_span_01131(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 2, 1); }
static void acc_span_01132(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 2, 2); }
static void acc_span_01140(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 3, 0); }
static void acc_span_01141(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 3, 1); }
static void acc_span_01142(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 0, 1, 1, 3, 2); }
static void acc_span_10000(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 0, 0, -1, 0); }
static void acc_span_10010(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 0, 0, 0, 0); }
static void acc_span_10020(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 0, 0, 1, 0); }
static void acc_span_10030(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 0, 0, 2, 0); }
static void acc_span_10040(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 0, 0, 3, 0); }
static void acc_span_11000(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, -1, 0); }
static void acc_span_11001(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, -1, 1); }
static void acc_span_11002(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, -1, 2); }
static void acc_span_11010(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 0, 0); }
static void acc_span_11011(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 0, 1); }
static void acc_span_11012(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 0, 2); }
static void acc_span_11020(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 1, 0); }
static void acc_span_11021(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 1, 1); }
static void acc_span_11022(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 1, 2); }
static void acc_span_11030(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 2, 0); }
static void acc_span_11031(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 2, 1); }
static void acc_span_11032(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 2, 2); }
static void acc_span_11040(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 3, 0); }
static void acc_span_11041(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 3, 1); }
static void acc_span_11042(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 0, 3, 2); }
static void acc_span_11100(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, -1, 0); }
static void acc_span_11101(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, -1, 1); }
static void acc_span_11102(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, -1, 2); }
static void acc_span_11110(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 0, 0); }
static void acc_span_11111(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 0, 1); }
static void acc_span_11112(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 0, 2); }
static void acc_span_11120(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 1, 0); }
static void acc_span_11121(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 1, 1); }
static void acc_span_11122(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 1, 2); }
static void acc_span_11130(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 2, 0); }
static void acc_span_11131(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 2, 1); }
static void acc_span_11132(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 2, 2); }
static void acc_span_11140(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 3, 0); }
static void acc_span_11141(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 3, 1); }
static void acc_span_11142(int32_t y, int32_t xs, int32_t xb, acc_ig ig, const acc_idl *d) { acc_span_k(y, xs, xb, ig, d, 1, 1, 1, 3, 2); }

/* [gouraud][textured][texmult][bm + 1][texture mode 0..2] */
static const acc_span_fn acc_spans[2][2][2][5][3] =
{
  acc_span_00000, acc_span_00000, acc_span_00000,
  acc_span_00010, acc_span_00010, acc_span_00010,
  acc_span_00020, acc_span_00020, acc_span_00020,
  acc_span_00030, acc_span_00030, acc_span_00030,
  acc_span_00040, acc_span_00040, acc_span_00040,
  acc_span_00000, acc_span_00000, acc_span_00000,
  acc_span_00010, acc_span_00010, acc_span_00010,
  acc_span_00020, acc_span_00020, acc_span_00020,
  acc_span_00030, acc_span_00030, acc_span_00030,
  acc_span_00040, acc_span_00040, acc_span_00040,
  acc_span_01000, acc_span_01001, acc_span_01002,
  acc_span_01010, acc_span_01011, acc_span_01012,
  acc_span_01020, acc_span_01021, acc_span_01022,
  acc_span_01030, acc_span_01031, acc_span_01032,
  acc_span_01040, acc_span_01041, acc_span_01042,
  acc_span_01100, acc_span_01101, acc_span_01102,
  acc_span_01110, acc_span_01111, acc_span_01112,
  acc_span_01120, acc_span_01121, acc_span_01122,
  acc_span_01130, acc_span_01131, acc_span_01132,
  acc_span_01140, acc_span_01141, acc_span_01142,
  acc_span_10000, acc_span_10000, acc_span_10000,
  acc_span_10010, acc_span_10010, acc_span_10010,
  acc_span_10020, acc_span_10020, acc_span_10020,
  acc_span_10030, acc_span_10030, acc_span_10030,
  acc_span_10040, acc_span_10040, acc_span_10040,
  acc_span_10000, acc_span_10000, acc_span_10000,
  acc_span_10010, acc_span_10010, acc_span_10010,
  acc_span_10020, acc_span_10020, acc_span_10020,
  acc_span_10030, acc_span_10030, acc_span_10030,
  acc_span_10040, acc_span_10040, acc_span_10040,
  acc_span_11000, acc_span_11001, acc_span_11002,
  acc_span_11010, acc_span_11011, acc_span_11012,
  acc_span_11020, acc_span_11021, acc_span_11022,
  acc_span_11030, acc_span_11031, acc_span_11032,
  acc_span_11040, acc_span_11041, acc_span_11042,
  acc_span_11100, acc_span_11101, acc_span_11102,
  acc_span_11110, acc_span_11111, acc_span_11112,
  acc_span_11120, acc_span_11121, acc_span_11122,
  acc_span_11130, acc_span_11131, acc_span_11132,
  acc_span_11140, acc_span_11141, acc_span_11142
};

static void acc_triangle(acc_vtx *v, const acc_pmode *m)
{
 acc_idl d;
 acc_ig ig;
 unsigned core, cvt = 0, vo = 0, vp = 0, i;
 int64_t base_coord, base_step, bound_us, bound_ls;
 int right;
 int32_t denom;
 acc_vtx t;
 struct { uint64_t xc[2], xs[2]; int32_t y, yb; int dec; } tp[2];

 if (v[1].x <= v[0].x)
  cvt = (v[2].x <= v[1].x) ? (1 << 2) : (1 << 1);
 else if (v[2].x < v[0].x)
  cvt = 1 << 2;
 else
  cvt = 1 << 0;
 if (v[2].y < v[1].y) { t = v[2]; v[2] = v[1]; v[1] = t; cvt = ((cvt >> 1) & 0x2) | ((cvt << 1) & 0x4) | (cvt & 0x1); }
 if (v[1].y < v[0].y) { t = v[1]; v[1] = v[0]; v[0] = t; cvt = ((cvt >> 1) & 0x1) | ((cvt << 1) & 0x2) | (cvt & 0x4); }
 if (v[2].y < v[1].y) { t = v[2]; v[2] = v[1]; v[1] = t; cvt = ((cvt >> 1) & 0x2) | ((cvt << 1) & 0x4) | (cvt & 0x1); }
 core = cvt >> 1;

 if (v[0].y == v[2].y) return;
 if (v[2].y - v[0].y >= 512) return;
 if (abs(v[2].x - v[0].x) >= 1024 || abs(v[2].x - v[1].x) >= 1024 || abs(v[1].x - v[0].x) >= 1024) return;

 denom = acc_calcis(&v[0], &v[1], &v[2], 0, 1);
 if (!denom) return;
 memset(&d, 0, sizeof d);
 if (m->gouraud)
  {
   d.dr_dx = acc_delta(&v[0], &v[1], &v[2], 4, 1, denom); d.dr_dy = acc_delta(&v[0], &v[1], &v[2], 0, 4, denom);
   d.dg_dx = acc_delta(&v[0], &v[1], &v[2], 5, 1, denom); d.dg_dy = acc_delta(&v[0], &v[1], &v[2], 0, 5, denom);
   d.db_dx = acc_delta(&v[0], &v[1], &v[2], 6, 1, denom); d.db_dy = acc_delta(&v[0], &v[1], &v[2], 0, 6, denom);
  }
 if (m->textured)
  {
   d.du_dx = acc_delta(&v[0], &v[1], &v[2], 2, 1, denom); d.du_dy = acc_delta(&v[0], &v[1], &v[2], 0, 2, denom);
   d.dv_dx = acc_delta(&v[0], &v[1], &v[2], 3, 1, denom); d.dv_dy = acc_delta(&v[0], &v[1], &v[2], 0, 3, denom);
  }

 ig.u = ig.v = 0;
 if (m->textured)
  {
   ig.u = (((uint32_t)v[core].u << AFBS) + (1 << (AFBS - 1))) << APAD;
   ig.v = (((uint32_t)v[core].v << AFBS) + (1 << (AFBS - 1))) << APAD;
  }
 ig.r = (((uint32_t)v[core].r << AFBS) + (1 << (AFBS - 1))) << APAD;
 ig.g = (((uint32_t)v[core].g << AFBS) + (1 << (AFBS - 1))) << APAD;
 ig.b = (((uint32_t)v[core].b << AFBS) + (1 << (AFBS - 1))) << APAD;
 acc_add_dx(&ig, &d, (uint32_t)-v[core].x, m);
 acc_add_dy(&ig, &d, (uint32_t)-v[core].y, m);

 base_coord = acc_xfp(v[0].x);
 base_step = acc_xfp_step(v[2].x - v[0].x, v[2].y - v[0].y);
 if (v[1].y == v[0].y)
  {
   bound_us = 0;
   right = v[1].x > v[0].x;
  }
 else
  {
   bound_us = acc_xfp_step(v[1].x - v[0].x, v[1].y - v[0].y);
   right = bound_us > base_step;
  }
 bound_ls = (v[2].y == v[1].y) ? 0 : acc_xfp_step(v[2].x - v[1].x, v[2].y - v[1].y);

 if (core) vo = 1;
 if (core == 2) vp = 3;
 {
  int k = vo;
  tp[k].y = v[0 ^ vo].y;
  tp[k].yb = v[1 ^ vo].y;
  tp[k].xc[right] = (uint64_t)acc_xfp(v[0 ^ vo].x);
  tp[k].xs[right] = (uint64_t)bound_us;
  tp[k].xc[!right] = (uint64_t)(base_coord + (int64_t)(v[vo].y - v[0].y) * base_step);
  tp[k].xs[!right] = (uint64_t)base_step;
  tp[k].dec = vo;
 }
 {
  int k = vo ^ 1;
  tp[k].y = v[1 ^ vp].y;
  tp[k].yb = v[2 ^ vp].y;
  tp[k].xc[right] = (uint64_t)acc_xfp(v[1 ^ vp].x);
  tp[k].xs[right] = (uint64_t)bound_ls;
  tp[k].xc[!right] = (uint64_t)(base_coord + (int64_t)(v[1 ^ vp].y - v[0].y) * base_step);
  tp[k].xs[!right] = (uint64_t)base_step;
  tp[k].dec = vp;
 }

 for (i = 0; i < 2; i++)
  {
   int32_t yi = tp[i].y, yb = tp[i].yb;
   uint64_t lc = tp[i].xc[0], ls = tp[i].xs[0], rc = tp[i].xc[1], rs = tp[i].xs[1];
   if (tp[i].dec)
    {
     while (yi > yb)
      {
       int32_t y;
       yi--; lc -= ls; rc -= rs;
       y = acc_sext11((uint32_t)yi);
       if (y < A.cy0) break;
       if (y > A.cy1) { acc_ticks += 2; continue; }
       m->span(yi, (int32_t)((int64_t)lc >> 32), (int32_t)((int64_t)rc >> 32), ig, &d);
      }
    }
   else
    {
     while (yi < yb)
      {
       int32_t y = acc_sext11((uint32_t)yi);
       if (y > A.cy1) break;
       if (y >= A.cy0)
        m->span(yi, (int32_t)((int64_t)lc >> 32), (int32_t)((int64_t)rc >> 32), ig, &d);
       else
        acc_ticks += 2;
       yi++; lc += ls; rc += rs;
      }
    }
  }
}

/* GP0 20..3F */
static void accPoly(unsigned char *baseAddr)
{
 const uint32_t *cb = (const uint32_t *)baseAddr;
 uint32_t c0 = GETLE32(cb), cc = c0 >> 24;
 int nv = (cc & 0x08) ? 4 : 3, i, k = 0;
 acc_pmode m;
 acc_vtx v[4];

 m.gouraud = (cc >> 4) & 1;
 m.textured = (cc >> 2) & 1;
 m.bm = (cc & 2) ? -2 : -1;
 m.texmult = m.textured && !(cc & 1);
 if (m.textured)
  {
   uint32_t tpage = GETLE32(&cb[4 + ((cc >> 4) & 1)]) >> 16;
   acc_set_tpage(tpage);
   UpdateGlobalTP((unsigned short)tpage);          /* P.E.Op.S. state and GPUSTAT 0..8 */
   lGPUstatusRet = (lGPUstatusRet & ~0x8000) | acc_status15();
  }
 if (m.bm == -2) m.bm = A.abr;
 m.tm = A.texmode > 2 ? 2 : A.texmode;
 m.span = acc_spans[m.gouraud][m.textured][m.texmult][m.bm + 1][m.tm];

 for (i = 0; i < nv; i++)
  {
   uint32_t xy;
   if (i == 0 || m.gouraud)
    {
     uint32_t col = GETLE32(&cb[k++]) & 0xFFFFFF;
     v[i].r = col & 0xFF; v[i].g = (col >> 8) & 0xFF; v[i].b = (col >> 16) & 0xFF;
    }
   else
    { v[i].r = v[0].r; v[i].g = v[0].g; v[i].b = v[0].b; }
   xy = GETLE32(&cb[k++]);
   v[i].x = acc_sext11((int16_t)(xy & 0xFFFF)) + A.offx;
   v[i].y = acc_sext11((int16_t)(xy >> 16)) + A.offy;
   v[i].u = v[i].v = 0;
   if (m.textured)
    {
     uint32_t uv = GETLE32(&cb[k++]);
     v[i].u = uv & 0xFF; v[i].v = (uv >> 8) & 0xFF;
     if (i == 0) acc_clut_update((uv >> 16) & 0xFFFF, m.tm);
    }
  }
 {
  acc_vtx t[3];
  uint32_t setup = m.gouraud && m.textured ? 450 : m.gouraud ? 288 : m.textured ? 180 : 0;
  t[0] = v[0]; t[1] = v[1]; t[2] = v[2];
  acc_ticks += 2 + 64 + 18 + setup;
  acc_triangle(t, &m);
  if (nv == 4)
   {
    t[0] = v[1]; t[1] = v[2]; t[2] = v[3];
    acc_ticks += 28 + 18 + setup;
    acc_triangle(t, &m);
   }
 }
 bDoVSyncUpdate = TRUE;
}

//////////////////////////////////////////////////////////////////////////
// sprites (Mednafen gpu_sprite.cpp)
//////////////////////////////////////////////////////////////////////////

/* The rows of a sprite, specialised by the constant arguments (acc_sprites[] below) */
ACC_INLINE void acc_sprite_k(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv,
                             int u_inc, int v_inc, int32_t r, int32_t g, int32_t b,
                             const int textured, const int texmult, const int bm, const int tm)
{
 const uint16_t mand = A.mask_and, mor = A.mask_or;
 const int hifi = A.hifi;
 const uint8_t *l = hifi ? A.hlut : A.dlut[2][3];
 const uint16_t fill = 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
 int32_t x, y;
 for (y = ys; y < yb; y++)
  {
   uint8_t ur = u;
   uint16_t *row = &psxVuw[(uint32_t)(y & 511) << 10];
   if (xb > xs)
    acc_ticks += (xb - xs) + (!textured && (bm >= 0 || mand) ? (((xb + 1) & ~1) - (xs & ~1)) >> 1 : 0);
   for (x = xs; x < xb; x++)
    {
     uint16_t *p = row + x;
     if (textured)
      {
       uint16_t t = acc_texel(ur, vv, tm);
       if (t)
        {
         if (texmult)
          acc_px8(p, ((t & 0x1F) * r) >> 4, ((t & 0x3E0) * g) >> 9, ((t & 0x7C00) * b) >> 14,
                  t & 0x8000, bm, 1, l, hifi, mand, mor);
         else if (hifi && bm >= 0 && (t & 0x8000))
          acc_px8(p, (t & 0x1F) << 3, ((t >> 5) & 0x1F) << 3, ((t >> 10) & 0x1F) << 3, 0x8000, bm, 1,
                  A.hlut, 1, mand, mor);
         else
          acc_px(p, t, bm, 1, mand, mor);
        }
       ur += u_inc;
      }
     else if (hifi)
      acc_px8(p, r, g, b, 0x8000, bm, 0, A.hlut, 1, mand, mor);
     else
      acc_px(p, fill, bm, 0, mand, mor);
    }
   if (textured)
    vv += v_inc;
  }
}

typedef void (*acc_sprite_fn)(int32_t, int32_t, int32_t, int32_t, uint8_t, uint8_t, int, int, int32_t, int32_t, int32_t);

static void acc_sprite_0000(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 0, 0, -1, 0); }
static void acc_sprite_0010(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 0, 0, 0, 0); }
static void acc_sprite_0020(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 0, 0, 1, 0); }
static void acc_sprite_0030(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 0, 0, 2, 0); }
static void acc_sprite_0040(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 0, 0, 3, 0); }
static void acc_sprite_1000(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, -1, 0); }
static void acc_sprite_1001(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, -1, 1); }
static void acc_sprite_1002(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, -1, 2); }
static void acc_sprite_1010(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 0, 0); }
static void acc_sprite_1011(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 0, 1); }
static void acc_sprite_1012(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 0, 2); }
static void acc_sprite_1020(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 1, 0); }
static void acc_sprite_1021(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 1, 1); }
static void acc_sprite_1022(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 1, 2); }
static void acc_sprite_1030(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 2, 0); }
static void acc_sprite_1031(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 2, 1); }
static void acc_sprite_1032(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 2, 2); }
static void acc_sprite_1040(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 3, 0); }
static void acc_sprite_1041(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 3, 1); }
static void acc_sprite_1042(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 0, 3, 2); }
static void acc_sprite_1100(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, -1, 0); }
static void acc_sprite_1101(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, -1, 1); }
static void acc_sprite_1102(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, -1, 2); }
static void acc_sprite_1110(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 0, 0); }
static void acc_sprite_1111(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 0, 1); }
static void acc_sprite_1112(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 0, 2); }
static void acc_sprite_1120(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 1, 0); }
static void acc_sprite_1121(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 1, 1); }
static void acc_sprite_1122(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 1, 2); }
static void acc_sprite_1130(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 2, 0); }
static void acc_sprite_1131(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 2, 1); }
static void acc_sprite_1132(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 2, 2); }
static void acc_sprite_1140(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 3, 0); }
static void acc_sprite_1141(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 3, 1); }
static void acc_sprite_1142(int32_t xs, int32_t xb, int32_t ys, int32_t yb, uint8_t u, uint8_t vv, int ui, int vi, int32_t r, int32_t g, int32_t b) { acc_sprite_k(xs, xb, ys, yb, u, vv, ui, vi, r, g, b, 1, 1, 3, 2); }

/* [textured][texmult][bm + 1][texture mode 0..2] */
static const acc_sprite_fn acc_sprites[2][2][5][3] =
{
  acc_sprite_0000, acc_sprite_0000, acc_sprite_0000,
  acc_sprite_0010, acc_sprite_0010, acc_sprite_0010,
  acc_sprite_0020, acc_sprite_0020, acc_sprite_0020,
  acc_sprite_0030, acc_sprite_0030, acc_sprite_0030,
  acc_sprite_0040, acc_sprite_0040, acc_sprite_0040,
  acc_sprite_0000, acc_sprite_0000, acc_sprite_0000,
  acc_sprite_0010, acc_sprite_0010, acc_sprite_0010,
  acc_sprite_0020, acc_sprite_0020, acc_sprite_0020,
  acc_sprite_0030, acc_sprite_0030, acc_sprite_0030,
  acc_sprite_0040, acc_sprite_0040, acc_sprite_0040,
  acc_sprite_1000, acc_sprite_1001, acc_sprite_1002,
  acc_sprite_1010, acc_sprite_1011, acc_sprite_1012,
  acc_sprite_1020, acc_sprite_1021, acc_sprite_1022,
  acc_sprite_1030, acc_sprite_1031, acc_sprite_1032,
  acc_sprite_1040, acc_sprite_1041, acc_sprite_1042,
  acc_sprite_1100, acc_sprite_1101, acc_sprite_1102,
  acc_sprite_1110, acc_sprite_1111, acc_sprite_1112,
  acc_sprite_1120, acc_sprite_1121, acc_sprite_1122,
  acc_sprite_1130, acc_sprite_1131, acc_sprite_1132,
  acc_sprite_1140, acc_sprite_1141, acc_sprite_1142
};

static void accSprite(unsigned char *baseAddr)
{
 const uint32_t *cb = (const uint32_t *)baseAddr;
 uint32_t c0 = GETLE32(cb), cc = c0 >> 24, color = c0 & 0xFFFFFF, xy;
 int textured = (cc >> 2) & 1, bm = (cc & 2) ? A.abr : -1, texmult = textured && !(cc & 1);
 int32_t x, y, w, h, xs, xb, ys, yb, k = 1, r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
 uint32_t tm = A.texmode > 2 ? 2 : A.texmode;
 uint8_t u = 0, vv = 0;
 int u_inc = 1, v_inc = 1, flipx = (A.sprite_flip & 0x1000) != 0, flipy = (A.sprite_flip & 0x2000) != 0;

 xy = GETLE32(&cb[k++]);
 x = acc_sext11(xy & 0xFFFF);
 y = acc_sext11(xy >> 16);
 if (textured)
  {
   uint32_t uv = GETLE32(&cb[k++]);
   u = uv & 0xFF; vv = (uv >> 8) & 0xFF;
   acc_clut_update((uv >> 16) & 0xFFFF, tm);
  }
 switch ((cc >> 3) & 3)
  {
   case 0: { uint32_t wh = GETLE32(&cb[k++]); w = wh & 0x3FF; h = (wh >> 16) & 0x1FF; } break;
   case 1: w = h = 1; break;
   case 2: w = h = 8; break;
   default: w = h = 16; break;
  }
 x = acc_sext11((uint32_t)(x + A.offx));
 y = acc_sext11((uint32_t)(y + A.offy));
 if (texmult && color == 0x808080)
  texmult = 0;

 xs = x; xb = x + w; ys = y; yb = y + h;
 if (textured)
  {
   if (flipx) { u_inc = -1; u |= 1; }
   if (flipy) v_inc = -1;
  }
 if (xs < A.cx0) { if (textured) u += (A.cx0 - xs) * u_inc; xs = A.cx0; }
 if (ys < A.cy0) { if (textured) vv += (A.cy0 - ys) * v_inc; ys = A.cy0; }
 if (xb > A.cx1 + 1) xb = A.cx1 + 1;
 if (yb > A.cy1 + 1) yb = A.cy1 + 1;

 acc_ticks += 2 + 16;
 acc_sprites[textured][texmult][bm + 1][tm](xs, xb, ys, yb, u, vv, u_inc, v_inc, r, g, b);
 bDoVSyncUpdate = TRUE;
}

//////////////////////////////////////////////////////////////////////////
// lines (Mednafen gpu_line.cpp)
//////////////////////////////////////////////////////////////////////////

typedef struct { int32_t x, y; uint8_t r, g, b; } acc_lpt;

static inline int64_t acc_ldiv(int64_t delta, int32_t dk)
{
 delta = (int64_t)((uint64_t)delta << 32);
 if (delta < 0) delta -= dk - 1;
 if (delta > 0) delta += dk - 1;
 return delta / dk;
}

static void acc_line(acc_lpt *p, int gouraud, int bm)
{
 int32_t idx = abs(p[1].x - p[0].x), idy = abs(p[1].y - p[0].y), k = idx > idy ? idx : idy, i;
 int64_t dx = 0, dy = 0;
 int32_t dr = 0, dg = 0, db = 0;
 uint64_t cx, cy;
 uint32_t cr, cg, cbl;

 acc_ticks += 16;
 if (idx >= 1024 || idy >= 512)
  return;
 acc_ticks += k * 2;
 if (p[0].x >= p[1].x && k)
  { acc_lpt t = p[1]; p[1] = p[0]; p[0] = t; }
 if (k)
  {
   dx = acc_ldiv(p[1].x - p[0].x, k);
   dy = acc_ldiv(p[1].y - p[0].y, k);
   if (gouraud)
    {
     dr = (int32_t)((uint32_t)(p[1].r - p[0].r) << 12) / k;
     dg = (int32_t)((uint32_t)(p[1].g - p[0].g) << 12) / k;
     db = (int32_t)((uint32_t)(p[1].b - p[0].b) << 12) / k;
    }
  }
 cx = ((uint64_t)(uint32_t)p[0].x << 32) | (1ULL << 31);
 cy = ((uint64_t)(uint32_t)p[0].y << 32) | (1ULL << 31);
 cx -= 1024;
 if (dy < 0) cy -= 1024;
 cr = ((uint32_t)p[0].r << 12) | (1 << 11);
 cg = ((uint32_t)p[0].g << 12) | (1 << 11);
 cbl = ((uint32_t)p[0].b << 12) | (1 << 11);

 for (i = 0; i <= k; i++)
  {
   int32_t x = (cx >> 32) & 2047, y = (cy >> 32) & 2047;
   uint32_t r, g, b;
   if (gouraud) { r = (cr >> 12) & 0xFF; g = (cg >> 12) & 0xFF; b = (cbl >> 12) & 0xFF; }
   else { r = p[0].r; g = p[0].g; b = p[0].b; }
   if (x >= A.cx0 && x <= A.cx1 && y >= A.cy0 && y <= A.cy1)
    {
     if (A.dtd || A.hifi)
      acc_plot8(x, y, r, g, b, 0x8000, bm, 0, x & 3, y & 3);
     else
      acc_plot(x, y, 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10), bm, 0);
    }
   cx += (uint64_t)dx; cy += (uint64_t)dy;
   if (gouraud) { cr += dr; cg += dg; cbl += db; }
  }
}

static inline int acc_lterm(uint32_t w) { return (w & 0xF000F000) == 0x50005000; }

/* GP0 40..5F: single lines and polylines (the front-ends pass a polyline whole, ended by
 * a 5xxx5xxx word) */
static void accLine(unsigned char *baseAddr)
{
 const uint32_t *cb = (const uint32_t *)baseAddr;
 uint32_t c0 = GETLE32(cb), cc = c0 >> 24, w;
 int gouraud = (cc >> 4) & 1, poly = (cc >> 3) & 1, bm = (cc & 2) ? A.abr : -1, k = 1, n;
 acc_lpt p[2];

 p[0].r = c0 & 0xFF; p[0].g = (c0 >> 8) & 0xFF; p[0].b = (c0 >> 16) & 0xFF;
 w = GETLE32(&cb[k++]);
 p[0].x = acc_sext11(w & 0xFFFF) + A.offx;
 p[0].y = acc_sext11(w >> 16) + A.offy;
 for (n = 0; n < 255; n++)
  {
   acc_lpt q[2];
   if (n > 0)
    {
     if (!poly || k >= 255 || acc_lterm(GETLE32(&cb[k])))
      break;
    }
   if (gouraud)
    {
     w = GETLE32(&cb[k++]);
     p[1].r = w & 0xFF; p[1].g = (w >> 8) & 0xFF; p[1].b = (w >> 16) & 0xFF;
    }
   else
    { p[1].r = p[0].r; p[1].g = p[0].g; p[1].b = p[0].b; }
   w = GETLE32(&cb[k++]);
   p[1].x = acc_sext11(w & 0xFFFF) + A.offx;
   p[1].y = acc_sext11(w >> 16) + A.offy;
   q[0] = p[0]; q[1] = p[1];
   acc_line(q, gouraud, bm);
   p[0] = p[1];
  }
 bDoVSyncUpdate = TRUE;
}

//////////////////////////////////////////////////////////////////////////
// fill, copy, transfers, E-commands
//////////////////////////////////////////////////////////////////////////

/* GP0 02: ignores the drawing area, the offset and both mask bits; writes bit 15 = 0 */
static void accFill(unsigned char *baseAddr)
{
 if (chain_fill_wait) chain_fill_hook();   /* ChainDump: the dump before the next frame's clear */
 const uint32_t *cb = (const uint32_t *)baseAddr;
 uint32_t c = GETLE32(&cb[0]), p = GETLE32(&cb[1]), s = GETLE32(&cb[2]);
 uint16_t val = ((c & 0xFF) >> 3) | ((((c >> 8) & 0xFF) >> 3) << 5) | ((((c >> 16) & 0xFF) >> 3) << 10);
 int32_t dx = p & 0x3F0, dy = (p >> 16) & 0x3FF;
 int32_t w = ((s & 0x3FF) + 0xF) & ~0xF, h = (s >> 16) & 0x1FF, x, y;
 acc_ticks += 2 + 46 + h * ((w >> 3) + 9);
 for (y = 0; y < h; y++)
  {
   uint16_t *row = &psxVuw[((y + dy) & 511) << 10];
   for (x = 0; x < w; x++)
    PUTLE16(&row[(x + dx) & 1023], val);
  }
 bDoVSyncUpdate = TRUE;
}

/* GP0 80: in 128-pixel chunks (source read before the chunk is written), mask honoured */
void acc_copy(uint32_t src, uint32_t dst, uint32_t size)
{
 int32_t sx = src & 0x3FF, sy = (src >> 16) & 0x3FF, dx = dst & 0x3FF, dy = (dst >> 16) & 0x3FF;
 int32_t w = size & 0x3FF, h = (size >> 16) & 0x1FF, x, y, c;
 uint16_t tmp[128];
 if (!w) w = 0x400;
 if (!h) h = 0x200;
 acc_tc_invalidate();
 acc_ticks += (uint32_t)(w * h * 5) >> 1;
 for (y = 0; y < h; y++)
  for (x = 0; x < w; x += 128)
   {
    int32_t n = w - x < 128 ? w - x : 128;
    const uint16_t *srow = &psxVuw[((y + sy) & 511) << 10];
    uint16_t *drow = &psxVuw[((y + dy) & 511) << 10];
    for (c = 0; c < n; c++)
     tmp[c] = GETLE16(&srow[(x + c + sx) & 1023]);
    for (c = 0; c < n; c++)
     {
      uint16_t *d = &drow[(x + c + dx) & 1023];
      if (!(GETLE16(d) & A.mask_and))
       PUTLE16(d, tmp[c] | A.mask_or);
     }
   }
}

static void accMove(unsigned char *baseAddr)
{
 const uint32_t *cb = (const uint32_t *)baseAddr;
 acc_copy(GETLE32(&cb[1]), GETLE32(&cb[2]), GETLE32(&cb[3]));
 bDoVSyncUpdate = TRUE;
}

/* One pixel of a GP0 A0 load: the mask rules of a draw */
void acc_put(unsigned short *p, uint16_t v)
{
 if (!(GETLE16(p) & A.mask_and))
  PUTLE16(p, v | A.mask_or);
}

static void accLoadImage(unsigned char *baseAddr)
{
 acc_tc_invalidate();
 primLoadImage(baseAddr);
 /* PS1 sizes: 0 is 1024 x 512, larger values wrap ((n - 1) & mask) + 1 */
 VRAMWrite.Width  = ((VRAMWrite.Width - 1) & 0x3FF) + 1;
 VRAMWrite.Height = ((VRAMWrite.Height - 1) & 0x1FF) + 1;
 VRAMWrite.RowsRemaining = VRAMWrite.Width;
 VRAMWrite.ColsRemaining = VRAMWrite.Height;
}

static void accStoreImage(unsigned char *baseAddr)
{
 acc_tc_invalidate();
 primStoreImage(baseAddr);
}

static void accClearCache(unsigned char *baseAddr)
{
 acc_cache_invalidate();   /* GP0 01: CLUT and texture caches (gpu/clut-cache on a PS1) */
}

static void accE1(unsigned char *b) { cmdTexturePage(b); acc_ecmd(GETLE32(b)); }
static void accE2(unsigned char *b) { cmdTextureWindow(b); acc_ecmd(GETLE32(b)); }
static void accE3(unsigned char *b) { cmdDrawAreaStart(b); acc_ecmd(GETLE32(b)); }
static void accE4(unsigned char *b) { cmdDrawAreaEnd(b); acc_ecmd(GETLE32(b)); }
static void accE5(unsigned char *b) { cmdDrawOffset(b); acc_ecmd(GETLE32(b)); }
static void accE6(unsigned char *b) { cmdSTP(b); acc_ecmd(GETLE32(b)); }

void (*primTableAcc[256])(unsigned char *);

static void acc_table_init(void)
{
 int i;
 for (i = 0; i < 256; i++)
  {
   void (*f)(unsigned char *) = primTableJ[i];
   if (i == 0x01) f = accClearCache;
   else if (i == 0x02) f = accFill;
   else if (i >= 0x20 && i < 0x40) f = accPoly;
   else if (i >= 0x40 && i < 0x60) f = accLine;
   else if (i >= 0x60 && i < 0x80) f = accSprite;
   else if (i >= 0x80 && i < 0xA0) f = accMove;
   else if (i >= 0xA0 && i < 0xC0) f = accLoadImage;
   else if (i >= 0xC0 && i < 0xE0) f = accStoreImage;
   else if (i == 0xE1) f = accE1;
   else if (i == 0xE2) f = accE2;
   else if (i == 0xE3) f = accE3;
   else if (i == 0xE4) f = accE4;
   else if (i == 0xE5) f = accE5;
   else if (i == 0xE6) f = accE6;
   primTableAcc[i] = f;
  }
}

/* Fast -> Accurate/Hi-Fi while a game runs: take the E1..E6 state from the P.E.Op.S. copies
 * (GPUSTAT and lGPUInfoVals, which both front-ends keep; the sprite flip bits are not kept) */
static void acc_sync_peops(void)
{
 uint32_t e[7];
 e[1] = lGPUstatusRet & 0x7FF;
 e[2] = lGPUInfoVals[INFO_TW];
 e[3] = lGPUInfoVals[INFO_DRAWSTART];
 e[4] = lGPUInfoVals[INFO_DRAWEND];
 e[5] = lGPUInfoVals[INFO_DRAWOFF];
 e[6] = (lGPUstatusRet >> 11) & 3;
 acc_sync_ecmds(e);
}

/* The table a front-end dispatches GP0 commands through: primTableJ in Fast mode */
void (**acc_prim_table(void))(unsigned char *)
{
 static int was_fast = 1;
 if (!acc_on())
  {
   was_fast = 1;
   return primTableJ;
  }
 if (was_fast)
  {
   was_fast = 0;
   acc_sync_peops();
  }
 if (!primTableAcc[0])
  acc_table_init();
 return primTableAcc;
}

/* Command lengths (words after the first) where the Fast tables leave a command out:
 * 0x01 cache clear, 0x6C..0x6F textured 1x1 sprites, the 0x81.. aliases of 80/A0/C0. */
const unsigned char primTableCXAcc[256] =
{
 0,1,3,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
 4,4,4,4,7,7,7,7, 5,5,5,5,9,9,9,9, 6,6,6,6,9,9,9,9, 8,8,8,8,12,12,12,12,
 3,3,3,3,3,3,3,3, 254,254,254,254,254,254,254,254, 4,4,4,4,4,4,4,4, 255,255,255,255,255,255,255,255,
 3,3,3,3,4,4,4,4, 2,2,2,2,3,3,3,3, 2,2,2,2,3,3,3,3, 2,2,2,2,3,3,3,3,
 4,4,4,4,4,4,4,4, 4,4,4,4,4,4,4,4, 4,4,4,4,4,4,4,4, 4,4,4,4,4,4,4,4,
 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3,
 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3,
 0,1,1,1,1,1,1,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

/* For gpulib.c (Soft Timed), which handles 01, 80, A0 and C0 itself */
int acc_active(void) { return acc_on(); }
void acc_invalidate_tex(void) { acc_tc_invalidate(); }
void acc_invalidate_all(void) { acc_cache_invalidate(); }   /* GP0 01 */
uint32_t acc_gpustat15(void) { return acc_status15(); }

#endif
