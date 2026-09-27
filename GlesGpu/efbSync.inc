/*
 * efbSync.inc -- keeping PlayStation VRAM (psxVuw) right where the GX renderer drew.
 *
 * Included by gpuPlugin.c after gpuTexture.c and before gpuPrim.c: one translation unit.
 *
 * THE PROBLEM. This plugin draws with GX into the EFB, not into psxVuw. A PlayStation game
 * can read what it drew: GP0 C0 (VRAM to CPU), GP0 80 (VRAM to VRAM), a texture page over a
 * frame it drew, and this plugin itself re-uploads VRAM into the EFB after a CPU image load
 * over the screen. For those, the drawn pixels must first be in psxVuw.
 *
 * THE MODEL (replaces gpuVramReadback.inc, 2026-09-27; see Docs/EFB_SYNC.md).
 *  1. Who wrote each 16x16 VRAM tile last: efb_cpu[][] (image loads, fills, the software
 *     rasterizer, VRAM moves, merges) and efb_gx[][] (GX draws), stamped from one counter.
 *     gx > cpu means psxVuw is stale there and the pixels are in the EFB or a snapshot.
 *  2. The EFB holds one VRAM rectangle, "live": origin PSXDisplay.GDrawOffset -- the buffer
 *     being DRAWN, which is where P.E.Op.S. puts the EFB's origin (gpuDraw.c
 *     SetOGLDisplaySettings) -- and the display's size, through the viewport rRatioRect.
 *     The old tracker tied the EFB to the DISPLAYED buffer; a double-buffered game draws
 *     the other one, so its frames were filed under the wrong buffer (Ape Escape: pause
 *     screen blue, fades cut to black).
 *  3. Each GX primitive stamps the tiles of its own bounds (from its GP0 words, clipped to
 *     the drawing area and to live), so a game that uploads its pictures with the CPU and
 *     draws a little over them keeps the rest CPU-owned: stamping the whole drawing area
 *     made FF7's intro copy and convert the whole EFB on every one of its uploads.
 *  4. Snapshots keep EFB content that is about to be lost: at a present (the XFB copy may
 *     clear the EFB), before the drawing moves to another buffer, before the display
 *     geometry changes, and on demand for a read of live. An async GX copy to RAM, in GX
 *     command order, so a copy sees the EFB as it was when queued.
 *  5. efb_sync(rect) before any read of VRAM: for each tile with gx > cpu, take the pixels
 *     from the newest snapshot captured after that GX write (capturing live now if the
 *     tile is live's), then mark the tile in sync and invalidate the texture cache there.
 *
 * POLICY (efb_mode): the tile stamps are always kept (they cost a few stores per event).
 * Snapshots at presents cost a GX copy a frame, so they run when the game needs them:
 *  - database.c game fixes: FIX_EFB_SYNC_ON (Spyro, Ape Escape, Dino Crisis 2, Vagrant
 *    Story) or FIX_EFB_SYNC_OFF;
 *  - otherwise "auto": on from the first read that found GX pixels it could not recover
 *    (reads of live work without any history);
 *  - the setting EfbSync (0 auto, 1 always, 2 never) overrides both, for testing.
 */
#ifndef EFB_SYNC_INC
#define EFB_SYNC_INC

#include <string.h>

extern void RestoreDispCopyInfo(void);   /* gpuPlugin.c */

#define EFB_TILE   16
#define EFB_TX     (1024 / EFB_TILE)
#define EFB_TY     (512 / EFB_TILE)
#define EFB_SNAPS  4

/* A VRAM rectangle and where it lies in the EFB */
typedef struct {
    int x, y, w, h;          /* VRAM, halfwords */
    int vx, vy, vw, vh;      /* EFB viewport, pixels */
} EfbGeom;

typedef struct {
    void *pixels;            /* RGB565 texture of the whole EFB, MEM2 */
    int size, texW, texH;
    EfbGeom g;
    uint64_t seq;            /* stamp at capture: holds every GX write stamped before it */
    int valid;               /* complete and readable */
    int pending;             /* a GX copy into it is in flight */
} EfbSnap;

enum { EFB_MODE_AUTO = 0, EFB_MODE_ON = 1, EFB_MODE_OFF = 2 };

static uint64_t efb_seq;
static uint64_t efb_cpu[EFB_TY][EFB_TX];
static uint64_t efb_gx[EFB_TY][EFB_TX];
static EfbGeom  efb_live;
static int      efb_live_set;
static uint64_t efb_live_gx;          /* newest GX stamp inside live */
static uint64_t efb_live_saved;       /* stamp of the newest snapshot of live */
static int      efb_seq_is_gx;        /* the current stamp is a GX draw's: the next may share it */
static EfbSnap  efb_snap[EFB_SNAPS];
static EfbSnap *efb_inflight;
static volatile int efb_inflight_done;
static u16      efb_token = 0x5100;
static int      efb_learned;          /* auto: a read needed history it did not have */
static unsigned efb_misses;           /* GX tiles a read could not recover */
/* Texture pages (64 halfwords by 256 lines: 16 columns, 2 rows) holding a tile stamped GX
 * and not synced since: a textured primitive on a clean page costs one test */
static unsigned efb_page_dirty[2];

extern char efbSyncSetting;           /* settings: EfbSync 0 auto, 1 always, 2 never */
static int efb_last_merged;           /* pixels the last efb_sync() wrote, for the logs */

/* ---- policy --------------------------------------------------------------------------- */

static int efb_mode(void)
{
    if (efbSyncSetting == EFB_MODE_ON || efbSyncSetting == EFB_MODE_OFF)
        return efbSyncSetting;
    /* the game fixes (database.c), read each time: they are set when the game is identified */
    if (dwActFixes & AUTO_FIX_EFB_SYNC_OFF)
        return EFB_MODE_OFF;
    if (dwActFixes & AUTO_FIX_VRAM_READBACK)
        return EFB_MODE_ON;
    return EFB_MODE_AUTO;
}

/* snapshots at presents and at buffer switches */
static int efb_history_on(void)
{
    int m = efb_mode();
    return m == EFB_MODE_ON || (m == EFB_MODE_AUTO && efb_learned);
}

/* ---- geometry ------------------------------------------------------------------------- */

static void efb_geom_now(EfbGeom *g)
{
    g->x = PSXDisplay.GDrawOffset.x;
    g->y = PSXDisplay.GDrawOffset.y;
    g->w = PSXDisplay.DisplayEnd.x - PSXDisplay.DisplayPosition.x;
    g->h = PSXDisplay.DisplayEnd.y - PSXDisplay.DisplayPosition.y;
    g->vx = rRatioRect.left;
    g->vy = iResY - (rRatioRect.top + rRatioRect.bottom);
    g->vw = rRatioRect.right;
    g->vh = rRatioRect.bottom;
}

static int efb_geom_ok(const EfbGeom *g)
{
    return g->w > 0 && g->h > 0 && g->vw > 0 && g->vh > 0;
}

static int efb_geom_eq(const EfbGeom *a, const EfbGeom *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
           a->vx == b->vx && a->vy == b->vy && a->vw == b->vw && a->vh == b->vh;
}

/* Tile range of a VRAM rect clipped to VRAM (no wrap: every caller clips first) */
static void efb_tiles(int x0, int y0, int x1, int y1, int *tx0, int *ty0, int *tx1, int *ty1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 1024) x1 = 1024;
    if (y1 > 512) y1 = 512;
    *tx0 = x0 / EFB_TILE; *ty0 = y0 / EFB_TILE;
    *tx1 = (x1 + EFB_TILE - 1) / EFB_TILE; *ty1 = (y1 + EFB_TILE - 1) / EFB_TILE;
}

/* ---- snapshots ------------------------------------------------------------------------ */

static void efb_copy_done(u16 token)
{
    if (efb_inflight && token == efb_token)
        efb_inflight_done = 1;
}

/* Publish a finished copy. wait: finish it now (a read needs it) */
static void efb_publish(int wait)
{
    EfbSnap *s = efb_inflight;

    if (!s)
        return;
    if (!efb_inflight_done)
    {
        if (!wait)
            return;
        GX_DrawDone();        /* the copy is before this in the command stream */
        PERF_INC(gx_drawdone);
    }
    DCInvalidateRange(s->pixels, s->size);
    s->pending = 0;
    s->valid = 1;
    efb_inflight = NULL;
    efb_inflight_done = 0;
}

/* The slot for a snapshot of g: the one already holding g, else the oldest */
static EfbSnap *efb_slot_for(const EfbGeom *g)
{
    EfbSnap *best = &efb_snap[0];
    int i;

    for (i = 0; i < EFB_SNAPS; i++)
        if (efb_snap[i].valid && efb_geom_eq(&efb_snap[i].g, g))
            return &efb_snap[i];
    for (i = 0; i < EFB_SNAPS; i++)
    {
        if (!efb_snap[i].valid && !efb_snap[i].pending)
            return &efb_snap[i];
        if (efb_snap[i].seq < best->seq)
            best = &efb_snap[i];
    }
    return best;
}

/* Copy the EFB, which holds live, to a snapshot of live. wait: complete before returning */
static int efb_capture_live(int wait)
{
    int copyW = (iResX + 15) & ~15;
    int copyH = (iResY + 1) & ~1;
    /* RGB565: every pixel at full colour. RGB5A3 stores a pixel whose EFB alpha is not
     * full with 4-bit colour, and this EFB's alpha varies per pixel: frames read back
     * came out speckled in the wrong hues (Ape Escape's pause) */
    int size = GX_GetTexBufferSize(copyW, copyH, GX_TF_RGB565, 0, GX_FALSE);
    EfbSnap *s;

    if (!efb_live_set || !efb_geom_ok(&efb_live))
        return 0;
    if (STATUSREG & GPUSTATUS_RGB24)      /* 24-bit frames are CPU uploads: psxVuw has them */
        return 0;
    efb_publish(1);                       /* one copy in flight at a time */

    s = efb_slot_for(&efb_live);
    if (!s->pixels || s->size < size)
    {
        void *buf = _mem2_malloc(size);
        if (!buf)
            return 0;
        if (s->pixels)
            _mem2_free(s->pixels);
        s->pixels = buf;
        s->size = size;
    }
    s->valid = 0;
    s->pending = 1;
    s->g = efb_live;
    s->seq = ++efb_seq;
    efb_seq_is_gx = 0;
    s->texW = copyW;
    s->texH = copyH;
    efb_live_saved = s->seq;

    efb_inflight = s;
    efb_inflight_done = 0;
    if (++efb_token == 0)
        ++efb_token;

    /* no deflicker or vertical filter: they would blur the pixels a game reads back */
    GX_SetCopyFilter(vmode ? vmode->aa : GX_FALSE,
                     (vmode && vmode->aa) ? vmode->sample_pattern : NULL, GX_FALSE, NULL);
    GX_SetTexCopySrc(0, 0, iResX, iResY);
    GX_SetTexCopyDst(copyW, copyH, GX_TF_RGB565, GX_FALSE);
    GX_CopyTex(MEM_K0_TO_K1(s->pixels), GX_FALSE);
    GX_PixModeSync();
    GX_SetDrawSyncCallback(efb_copy_done);
    GX_SetDrawSync(efb_token);
    GX_Flush();
    RestoreDispCopyInfo();

    if (wait)
        efb_publish(1);
    return 1;
}

/* One PSX pixel from a snapshot; 0 if outside it */
static int efb_snap_pixel(const EfbSnap *s, int px, int py, uint16_t *out)
{
    int nx = px - s->g.x, ny = py - s->g.y, tx, ty;

    if (nx < 0 || ny < 0 || nx >= s->g.w || ny >= s->g.h)
        return 0;
    tx = s->g.vx + (nx * s->g.vw) / s->g.w;
    ty = s->g.vy + (ny * s->g.vh) / s->g.h;
    if (tx < 0 || tx >= s->texW || ty < 0 || ty >= s->texH)
        return 0;
    {
        /* 16-bit texels in 4x4 blocks, as RGB5A3; RGB565 is RRRRRGGGGGGBBBBB and the PSX
         * pixel is BBBBBGGGGGRRRRR (red in the low bits) */
        unsigned short c = ReadGXRGB5A3PixelRaw(s->pixels, s->texW, tx, ty);
        *out = (uint16_t)((c >> 11) | (((c >> 6) & 0x1F) << 5) | ((c & 0x1F) << 10));
    }
    return 1;
}

/* ---- stamps --------------------------------------------------------------------------- */

/* Tile columns per page: 64 / EFB_TILE = 4; tile rows per page: 256 / EFB_TILE = 16 */
static void efb_pages_mark(int tx0, int ty0, int tx1, int ty1)
{
    int px, py;
    for (py = ty0 / 16; py <= (ty1 - 1) / 16 && py < 2; py++)
        for (px = tx0 / 4; px <= (tx1 - 1) / 4 && px < 16; px++)
            efb_page_dirty[py] |= 1u << px;
}

/* After a sync: clear the pages of this tile range that have no stale tile left */
static void efb_pages_recheck(int tx0, int ty0, int tx1, int ty1)
{
    int px, py, tx, ty;
    for (py = ty0 / 16; py <= (ty1 - 1) / 16 && py < 2; py++)
        for (px = tx0 / 4; px <= (tx1 - 1) / 4 && px < 16; px++)
        {
            int stale = 0;
            if (!(efb_page_dirty[py] & (1u << px)))
                continue;
            for (ty = py * 16; ty < py * 16 + 16 && !stale; ty++)
                for (tx = px * 4; tx < px * 4 + 4; tx++)
                    if (efb_gx[ty][tx] > efb_cpu[ty][tx]) { stale = 1; break; }
            if (!stale)
                efb_page_dirty[py] &= ~(1u << px);
        }
}


/* A CPU-side write of psxVuw: an image load, a fill, the software rasterizer, a move */
static void efb_cpu_write(int x, int y, int w, int h)
{
    int tx0, ty0, tx1, ty1, tx, ty;

    if (w <= 0 || h <= 0)
        return;
    ++efb_seq;
    efb_seq_is_gx = 0;
    /* VRAM wraps: split at the right and bottom edges */
    if (x + w > 1024) { efb_cpu_write(0, y, x + w - 1024, h); w = 1024 - x; }
    if (y + h > 512)  { efb_cpu_write(x, 0, w, y + h - 512); h = 512 - y; }
    efb_tiles(x, y, x + w, y + h, &tx0, &ty0, &tx1, &ty1);
    for (ty = ty0; ty < ty1; ty++)
        for (tx = tx0; tx < tx1; tx++)
            efb_cpu[ty][tx] = efb_seq;
}

/* Before live changes (another buffer, another viewport): keep what the EFB holds */
static void efb_save_live_if_needed(void)
{
    if (efb_live_set && efb_live_gx > efb_live_saved && efb_history_on())
        efb_capture_live(0);
}

/* A GX primitive from the display list (gpuPlugin.c dispatcher), before it is drawn, with
 * its VRAM bounds (x1, y1 exclusive; the drawing area when unknown) */
static void efb_gx_draw(int x0, int y0, int x1, int y1)
{
    EfbGeom g;
    int tx0, ty0, tx1, ty1, tx, ty;

    efb_geom_now(&g);
    if (!efb_geom_ok(&g))
        return;
    if (!efb_live_set || !efb_geom_eq(&g, &efb_live))
    {
        efb_save_live_if_needed();
        efb_live = g;
        efb_live_set = 1;
        efb_live_gx = 0;
        efb_live_saved = 0;
    }
    /* within the drawing area (inclusive end) and within live */
    if (x0 < PSXDisplay.DrawArea.x0) x0 = PSXDisplay.DrawArea.x0;
    if (y0 < PSXDisplay.DrawArea.y0) y0 = PSXDisplay.DrawArea.y0;
    if (x1 > PSXDisplay.DrawArea.x1 + 1) x1 = PSXDisplay.DrawArea.x1 + 1;
    if (y1 > PSXDisplay.DrawArea.y1 + 1) y1 = PSXDisplay.DrawArea.y1 + 1;
    if (x0 < g.x) x0 = g.x;
    if (y0 < g.y) y0 = g.y;
    if (x1 > g.x + g.w) x1 = g.x + g.w;
    if (y1 > g.y + g.h) y1 = g.y + g.h;
    if (x1 <= x0 || y1 <= y0)
        return;
    if (!efb_seq_is_gx)
    {
        ++efb_seq;
        efb_seq_is_gx = 1;
    }
    efb_tiles(x0, y0, x1, y1, &tx0, &ty0, &tx1, &ty1);
    for (ty = ty0; ty < ty1; ty++)
        for (tx = tx0; tx < tx1; tx++)
            efb_gx[ty][tx] = efb_seq;
    efb_live_gx = efb_seq;
    efb_pages_mark(tx0, ty0, tx1, ty1);
}

/* ---- the events the plugin reports ---------------------------------------------------- */

/* A present, before the copy to the XFB (which may clear the EFB) */
static void efb_present(void)
{
    efb_publish(0);
    if (efb_live_set && efb_live_gx > efb_live_saved && efb_history_on())
        efb_capture_live(0);
    /* vramio C4: a present; a = history kept (snapshots), b = a read has missed pixels */
    perf_vram_event(0xC4, efb_live.x, efb_live.y, efb_live.w, efb_live.h,
                    efb_history_on(), efb_misses != 0);
}

/* The display's size or position mapping is about to change (GP1 05-08, resolution) */
static void efb_before_geometry_change(void)
{
    efb_save_live_if_needed();
}

/* Before a read of VRAM rect (x,y,w,h): put the GX-drawn pixels there into psxVuw.
 * Returns the pixels written. */
static int efb_sync(int x, int y, int w, int h)
{
    int tx0, ty0, tx1, ty1, tx, ty, merged = 0, stale = 0, need_live = 0, missed = 0;
    int ix0 = 1024, iy0 = 512, ix1 = 0, iy1 = 0;   /* what was merged, for the texture cache */

    efb_last_merged = 0;
    if (w <= 0 || h <= 0 || efb_mode() == EFB_MODE_OFF)
        return 0;
    if (x + w > 1024 || y + h > 512)   /* a wrapped read: each part on its own */
    {
        int a = x + w > 1024 ? 1024 - x : w, b = y + h > 512 ? 512 - y : h;
        merged = efb_sync(x, y, a, b);
        if (x + w > 1024) merged += efb_sync(0, y, x + w - 1024, b);
        if (y + h > 512) merged += efb_sync(x, 0, a, y + h - 512);
        if (x + w > 1024 && y + h > 512) merged += efb_sync(0, 0, x + w - 1024, y + h - 512);
        efb_last_merged = merged;
        return merged;
    }
    efb_publish(0);
    efb_tiles(x, y, x + w, y + h, &tx0, &ty0, &tx1, &ty1);

    /* what is stale, and whether live's EFB must be copied for it */
    for (ty = ty0; ty < ty1; ty++)
        for (tx = tx0; tx < tx1; tx++)
            if (efb_gx[ty][tx] > efb_cpu[ty][tx])
            {
                stale++;
                if (efb_live_set && efb_gx[ty][tx] > efb_live_saved &&
                    tx * EFB_TILE < efb_live.x + efb_live.w && (tx + 1) * EFB_TILE > efb_live.x &&
                    ty * EFB_TILE < efb_live.y + efb_live.h && (ty + 1) * EFB_TILE > efb_live.y)
                    need_live = 1;
            }
    if (!stale)
        return 0;
    if (need_live)
        efb_capture_live(1);
    else
        efb_publish(1);

    for (ty = ty0; ty < ty1; ty++)
        for (tx = tx0; tx < tx1; tx++)
        {
            const EfbSnap *best = NULL;
            int i, px, py, px0, py0, px1, py1;

            if (efb_gx[ty][tx] <= efb_cpu[ty][tx])
                continue;
            for (i = 0; i < EFB_SNAPS; i++)
                if (efb_snap[i].valid && efb_snap[i].seq >= efb_gx[ty][tx] &&
                    (!best || efb_snap[i].seq > best->seq) &&
                    tx * EFB_TILE < efb_snap[i].g.x + efb_snap[i].g.w &&
                    (tx + 1) * EFB_TILE > efb_snap[i].g.x &&
                    ty * EFB_TILE < efb_snap[i].g.y + efb_snap[i].g.h &&
                    (ty + 1) * EFB_TILE > efb_snap[i].g.y)
                    best = &efb_snap[i];
            if (!best)
            {
                missed++;
                continue;
            }
            /* The whole tile, then it is in sync. Pixels outside the snapshot stay: GX draws are
             * clipped to live, which is what snapshots hold, so those were never GX's (the
             * right edge of a 368-wide display leaves most of its last tile outside) */
            px0 = tx * EFB_TILE; py0 = ty * EFB_TILE; px1 = px0 + EFB_TILE; py1 = py0 + EFB_TILE;
            for (py = py0; py < py1; py++)
                for (px = px0; px < px1; px++)
                {
                    uint16_t c;
                    unsigned short *d = &psxVuw[(py << 10) + px];
                    if (!efb_snap_pixel(best, px, py, &c))
                        continue;
                    /* psxVuw is in PlayStation byte order (little-endian) on this big-endian
                     * CPU; keep the pixel's mask bit */
                    PUTLE16(d, (unsigned short)((c & 0x7FFF) | (GETLE16(d) & 0x8000)));
                    if (px >= x && px < x + w && py >= y && py < y + h)
                        merged++;
                }
            efb_cpu[ty][tx] = efb_gx[ty][tx];
            if (px0 < ix0) ix0 = px0;
            if (py0 < iy0) iy0 = py0;
            if (px1 > ix1) ix1 = px1;
            if (py1 > iy1) iy1 = py1;
        }
    if (ix1 > ix0)
        InvalidateTextureArea(ix0, iy0, ix1 - ix0, iy1 - iy0);
    efb_pages_recheck(tx0, ty0, tx1, ty1);
    if (missed)
    {
        efb_misses += missed;
        if (efb_mode() == EFB_MODE_AUTO && !efb_learned)
            efb_learned = 1;   /* from now on, keep the history this read did not have */
    }
    /* vramio C1: a = tiles that were stale, b = tiles no snapshot could fill;
     * C2: a = percent of the read rect written, b = live had to be copied for it */
    perf_vram_event(0xC1, x, y, w, h, stale, missed);
    perf_vram_event(0xC2, x, y, w, h, w * h ? (int)(100u * (unsigned)merged / (unsigned)(w * h)) : 0, need_live);
    efb_last_merged = merged;
    return merged;
}

/* A textured primitive is about to sample (SetRenderMode, gpuPrim.c): if the texels it
 * reads lie where GX drew -- a game using one of its frames as a texture -- sync them.
 * The rect is the primitive's UV bounds in VRAM halfwords (4-bit texels are a quarter of
 * one, 8-bit a half), or the whole page under a texture window. */
static void efb_sync_texture(void)
{
    int shift = GlobalTextTP == 0 ? 2 : (GlobalTextTP == 1 ? 1 : 0);
    int x0 = GlobalTextAddrX, y0 = GlobalTextAddrY, x1, y1, i, px, py = y0 >> 8;
    unsigned cols;

    if (py > 1)
        return;
    /* the page columns this texture page spans: 1 (4-bit), 2 (8-bit), 4 (15-bit) */
    px = x0 >> 6;
    cols = ((1u << (4 >> shift)) - 1u) << px;
    if (!(efb_page_dirty[py] & cols))
        return;
    if (bUsingTWin)
    {
        x1 = x0 + (256 >> shift);
        y1 = y0 + 256;
    }
    else
    {
        int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
        for (i = 0; i < 4; i++)
        {
            if (gl_ux[i] < u0) u0 = gl_ux[i];
            if (gl_ux[i] > u1) u1 = gl_ux[i];
            if (gl_vy[i] < v0) v0 = gl_vy[i];
            if (gl_vy[i] > v1) v1 = gl_vy[i];
        }
        x1 = x0 + (u1 >> shift) + 1;
        y1 = y0 + v1 + 1;
        x0 += u0 >> shift;
        y0 += v0;
    }
    efb_sync(x0, y0, x1 - x0, y1 - y0);
}

static void efb_reset(void)
{
    int i;

    efb_publish(1);
    for (i = 0; i < EFB_SNAPS; i++)
    {
        efb_snap[i].valid = 0;
        efb_snap[i].pending = 0;
        efb_snap[i].seq = 0;
    }
    memset(efb_cpu, 0, sizeof(efb_cpu));
    memset(efb_gx, 0, sizeof(efb_gx));
    efb_seq = 0;
    efb_live_set = 0;
    efb_live_gx = efb_live_saved = 0;
    efb_seq_is_gx = 0;
    efb_learned = 0;
    efb_misses = 0;
    efb_page_dirty[0] = efb_page_dirty[1] = 0;
}

#endif /* EFB_SYNC_INC */
