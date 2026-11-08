# GX picture options: mipmaps, anisotropy, anti-aliasing, dither

2026-10-19, branch `try/gx-quality` (from 5.14.0, 59f1348). The question: can the OpenGX
renderer use the Wii GPU's mipmapping, LOD bias, anisotropic filtering, anti-aliasing and
dithering, and would a game look better or run faster? An earlier session skipped all of
them; each reason is checked here.

Everything here is about the **OpenGX** renderer (`gpuPlugin = 2`). The default renderer is
Soft Fast (`gpuPlugin = 0`); the soft renderers draw with the CPU at the PS1's resolution and
already dither exactly like a PS1 (`SoftGPU/soft.c`, `dither5.h`).

## Status (2026-11-03)
The study text below was written 2026-10-19 and some lines ("not built", "not measured", "nothing
touched a Wii") predate later work. Since then:
- AA (`GxColor=2`) ran on the bench Wii on 2026-10-19 (`hw_gx_aa*`, 08c486c7, 14e560c8): right
  pictures, same presents and wall as plain 240p; selectable again. Not measured on Tiger Temple.
- Menu entries exist: Far Textures and GX Color on Advanced > GPU (65a53ede), and in the Filter
  Tour (44011389).
- Anisotropy: `GxTexMin` 2/3 (Far Textures Aniso 2x/4x) went in a prototype, was removed 2026-10-29
  (Ape Escape hand, 1a6a593c), restored 2026-11-01 for minified polygons only (1de16535, c12246a7)
  and capped to 2/4 texels per pixel on 2026-11-02 after coloured dashes on far grass on the Wii
  (254fcc46). Mipmaps are still not built.
- Open: run `gx_quality_ab.txt` on the bench Wii; decide `GxTexMin` / `GxColor` as shipped options;
  the `Dithering` setting does nothing on OpenGX at `GxColor=0` (see step 4 below); mipmaps for far
  ground (planned later).

## Verdict

| Feature | Possible? | Cost | Look | Speed | Status |
|---|---|---|---|---|---|
| Mipmaps + trilinear / LOD bias | Yes, with work: the sub-texture pages allow 2 levels | +33% texture MEM2 (CTR: +5.3 MB); a box filter per upload on the CPU | Less shimmer on 8-12% of the textured area (480 lines), 27% in 240p; distant textures blur; seams of 1-2 texels | None: costs CPU, saves nothing measurable | Not built. Measured where it would act (probe) |
| Anisotropic filter, linear minification | Yes, one call per texture object | Free on the CPU; GPU cycles only where texels are minified (bench) | Distant ground smoother, a little blurred; no mipmaps, so LOD stays 0 | Possibly slower on GPU-bound scenes (Tiger Temple): bench | Prototype `GxTexMin` |
| Anti-aliasing (GX multisample) | Only in 240p mode (EFB 640x240 <= 264 lines); the 480-line EFB would need two passes | One pass, RGB565 | Smoother polygon edges; 5/6/5 colour (PS1 is 5/5/5) | Works on the Wii (bench, 2026-10-19 afternoon): AA on polygon edges, same presents and wall as plain 240p; the morning's black TV did not come back ("AA on the Wii" below) | `GxColor=2` (menu: GX Color "AA (240p)") |
| GX dither | Only in an RGBA6 or RGB565 EFB | One BP write when it changes | GX's own pattern at EFB resolution: not the PS1 look | None | Prototype `GxColor=1` (and in `GxColor=2`) |
| PS1-exact dither on OpenGX | Only with RGB565 (240p + AA): a TEV stage + 4x4 offset texture | One TEV stage per dithered draw | The PS1's own pattern | Small GPU cost | Design only (below) |
| Higher internal resolution | Already 2x for 320-wide games (EFB 640x480) | More needs a tiled multi-pass EFB | -- | -- | Out of scope |

Findings on the earlier reasons:

1. *"OpenGX textures have no mipmaps, so LOD stays at level 0"* -- true today
   (`GX_InitTexObj(..., GX_FALSE)` everywhere in `deps/opengx/gc_gl.c`, the
   `GX_InitTexObjLOD` line there is commented out). LOD bias therefore does nothing. It is
   not a law of nature: the textures could have mipmaps (below). Anisotropy is a separate
   question: GX iterates its filter along the axis of anisotropy (libogc `gx.h`,
   `GX_InitTexObjMaxAniso`), and whether the hardware does that at LOD 0 is unknown; Dolphin
   replaces it with the PC GPU's anisotropy and cannot answer it.
2. *"Copy AA needs a 16-bit, 640x264 EFB plus two-pass rendering"* -- true for the default
   480-line EFB. False in 240p mode (`TVMode = 1`): its EFB is 640x240 (`TVModeResolution`,
   `switchToTVMode`), which fits. The 16-bit format is also the only GX format with 5-bit red
   and blue, the PS1's own depth.
3. *"GX dither does nothing with the RGB8 EFB"* -- true (libogc `gx.h`: dither is valid only
   with `GX_PF_RGBA6_Z24` or `GX_PF_RGB565_Z16`). Missed: OpenGX does not dither at all
   today, whatever the `Dithering` setting says (below).

## How OpenGX draws today (facts the verdicts rest on)

- **EFB size.** 640x480 by default for any display of 288 lines or fewer
  (`gpuPlugin.c TVModeResolution`), so a 320x240 game is drawn at twice its resolution in
  both directions. In 240p mode the EFB is 640 (or the display width, if wider) x 240.
- **EFB format.** `GX_PF_RGB8_Z24` (`switchToTVMode`, `GraphicsGX.cpp`). Colours and
  blends are 8 bits per channel: smoother than a PS1, which writes 5 bits per channel.
- **Textures.** The sub-texture cache (`gpuTexture.c`) packs the PS1 texture areas a game
  uses, per page and CLUT, into 256x256 RGB5A3 GX textures (`glInitRGBATextures(256,256)`)
  and updates them with `glTexSubImage2D`. Each area is reserved with a border of up to 3
  texels, rounded up to whole 4x4 GX blocks (`SubTexReserve`, `SUBTEX_ALIGN`). Texel 0 is
  transparent; semi-transparent texels go to a second texture (`semiTransData`).
- **Dither.** The `Dithering` setting reaches the soft renderers only
  (`SoftGPU/drawGX.c plugin_call_rearmed_cbs` -> `iUseDither`). In GlesGpu, `bDrawDither`
  is never assigned, `cmdTexturePage` (E1) does not read bit 9 for drawing, and
  `glEnable(GL_DITHER)` is ignored by OpenGX (no `GL_DITHER` case in `gc_gl.c`). OpenGX
  draws 24-bit colour with no dither: the setting does nothing there.
- **Pixel formats.** libogc's `GX_SetPixelFmt` turns multisampling on whenever the format is
  `GX_PF_RGB565_Z16` (`libogc/gx.c`, `ms_en`): on GX the 16-bit format *is* the
  anti-aliased format, limited to 640x264. A non-AA, 528-line 16-bit EFB does not exist;
  RGBA6 (6 bits per channel, 24-bit Z) is the only dithered non-AA format.

## Where textures are minified: the `lod:` probe

`gpuPrim.c lod_probe()` (debug builds with `PERF_PROF_GPUSPLIT`, i.e. `build debug deep`):
for every textured polygon (sprites left out) it takes the affine map from screen to
texture, works out texels per EFB pixel along x and y (rho; GX's LOD is log2 rho), and adds
the polygon's EFB area to one of four bins. perf.log line `lod: mag= l0= l1= l2= aniso2=`.
Run `gx_ctr` and `gx_spyro` (below), whole run (intro, menus and play):

| Scene | EFB | textured area | magnified (LOD<0) | LOD 0-1 | LOD 1-2 | LOD >=2 | footprint ratio >= 2 |
|---|---|---|---|---|---|---|---|
| CTR, Time Trial to the start line | 640x480 | 659 M px | 68.0% | 20.6% | 7.5% | 3.9% | 19.0% |
| CTR, same, 240p | 512x216 | 263 M px | 32.6% | 40.7% | 16.5% | 10.2% | 36.3% |
| Spyro, the user's play (Artisans) | 640x480 | 1095 M px | 72.6% | 15.0% | 8.5% | 3.9% | 19.4% |

Reading: at OpenGX's normal resolution two thirds of the textured area is magnified, and
mipmaps would change only the 11-12% at LOD 1 or more (distant road and grass, the far
walls). In 240p mode the share more than doubles (27%). Linear minification (`GxTexMin=1`)
acts on the LOD 0-1 bin as well.

## Mipmaps: how they could be built

- **Levels.** Levels 1 and 2 (2x2 and 4x4 averages) stay inside each reserved 4-aligned
  area, so they do not mix two cached textures; level 3 would. Max LOD 2.
- **Generation.** After each sub-upload (both paths of `glTexSubImage2D_body`, after the
  locked-cache tiler's DMA), re-average the uploaded rectangle's blocks into levels 1 and 2,
  for the opaque and the semi-transparent texture. Transparency: a level-1 texel is opaque
  when at least two of its four texels are, averaging only those (keeps cut-outs). With
  `GX_NEAR_MIP_NEAR` the Near look stays for magnified texels.
- **Cost.** Memory: +33% of the texture pool -- CTR held 8192 KB data + 8192 KB semi
  (`texmem:`), so about +5.3 MB of MEM2, a pool that has already run full once (FF7 then
  Crash 3, `ogx_tex_alloc`). CPU: about a third more texel writes than the tiler, per
  upload; games that re-upload often (Crash Bash, FMV-heavy games) pay it every frame.
- **Look.** PS1 games never had mipmaps; with them, distant textures turn into averaged
  colour and seams of 1-2 texels can show where a PS1 tile meets its neighbour in the same
  area. The 2x internal resolution already halves the PS1's shimmer.
- **Recommendation:** not now. Measure `GxTexMin` on the Wii first: if anisotropy helps on
  hardware at LOD 0, it gives most of the gain without the memory.

## Prototypes (off by default)

| Setting | Values | Where |
|---|---|---|
| `GxTexMin` | 0 off; 1 minified texels linear (`GX_LINEAR`), magnified keep the `BilinearFilter` choice; 2 / 3 `GX_LIN_MIP_LIN` + edge LOD + `GX_ANISO_2` / `GX_ANISO_4` | `gc_gl.c ogx_texq()` (plain) / `ogx_texq_aniso()` (2, 3, only for polygon draws that minify: `ogx_texq_pick()`), on the sub-texture and window textures (not FMV/screen uploads) and in `glChgTextureFilter` |
| `GxColor` | 0 RGB8; 1 RGBA6 + GX dither; 2 RGB565 multisampled + GX dither in 240p at <= 264 lines (else as 0) | `GraphicsGX.cpp gxApplyEfbFormat()` and `switchToTVMode` (AA sample pattern of libogc's AA modes, the 240p mode's own no-filter taps = mean of a pixel's 3 samples); `MainFrame.cpp` applies it before a game resumes; `gpuPrim.c GxDitherPrim()` turns GX dither on per primitive where a PS1 dithers (Gouraud or texture-blended polygons and lines, never rectangles) and the `Dithering` setting allows (Default = the game's E1 bit 9) |

Also changed: `efbSync.inc` copies its snapshots with the TV mode's AA state
(`g_tv_mode`), not the menu's `vmode`, so a snapshot of a multisampled EFB is resolved.
`PadWiiSX.c`/`perf_prof.c` take 32 `xfb` pictures per boot (was 16; a chain adds each game's),
`dolphin_run.sh` extracts 32, and `wii_lab.py` fetches `xfb_01..32.bin` from the Wii.

Known limits of the prototypes: `GxColor=1` in the 480-line mode is applied at game start
(`MainFrame`) only; the BIOS autoboot path (`MenuContext.cpp`) does not apply it.
`GxColor=2` needs NTSC-height 240p (PAL 288-line displays do not fit).

The Filter Tour (Advanced Graphics, `GraphicsGX.cpp filterTour*`) changes both while a game
runs. `GxTexMin` goes on every OpenGX texture at once through `glChgTextureFilter` (each one
loaded again at its next bind; Off now also takes the anisotropy and edge LOD back out in
`ogx_texq`; the anisotropic variant is chosen per draw). `GxColor` waits for the next copy to the XFB (`gx_vout_render` calls
`filterTourCopied`): a copy that kept the EFB is made again with the clear, the EFB mirror ends
as after a clearing present, then `gxSetAa`, `gxApplyEfbFormat(1)` and `applyCopyFilter`. In
Dolphin (Spyro, 240p and 480i) no frame around a switch is black or garbled. GX dither's
per-primitive cache (`gpuPrim.c gx_dither_on`) is not reset by a tour switch, so dither can
stay off until the next primitive that does not want it.

**First bench result (2026-10-19 morning, `hw_gx_quality`, pmc build of 48bd936):** CTR with
`TVMode=1 GxColor=2` kept running (0.99x, 554 presents in 2016 vblanks) but the user saw a
black TV until he pressed RESET. Two readings of that run were wrong: its `ogxeq` line
(82294 mismatches of 86499) is the texture-compare probe, and every row of that chain has the
same ratio (row 1: 221479 of 242622), so it says nothing about AA; and its 240p `xfb`
pictures were 54 lines high (libogc v3.1.0 `GX_SetDispCopyYScale` return value, see
`switchToTVMode`), and the AA game wrote none, so no picture of it existed. Same run:
`GxTexMin=1/3` and `GxColor=1` ran CTR and Spyro at 1.00x with the GPU share unchanged (CTR
9.6 -> 8.7-8.8%, Spyro 14.0-14.2%). The afternoon's runs (next section) show the AA path
working.

## AA on the Wii: the hardware loop and what it showed (2026-10-19)

### The loop: `scripts/wii_probe/gxaa/` (one queue job, about a minute)

`gxaa.c` is a small DOL (devkitPPC r50, libogc v3, the HBC agent linked so `hbc.py screen`
and `exit` work) that sets up WiiStation's 240p mode (`TVMODE_240p`, NTSC/EuRGB60
double-strike), switches the EFB the way `switchToTVMode`/`gxApplyEfbFormat` do, and draws a
fixed pattern with GX: a blue-red gradient, a grey ramp (565 banding shows there), red,
green, blue and white squares, a pinwheel of thin slivers and two near-horizontal and
near-vertical wedges (AA shows on every edge), a depth pair (green near, drawn first; red
far, over it: green must win) and an EFB-to-texture readback drawn back as an inset, as
`efbSync.inc` does. Each wiiload argument is one variant (`key=value,...`; the keys are in
the file's header: `fmt`, `aa`, `pat`, `vf`, `w`/`h`, `ys`, `fm`, `sync`, `z`, `tex`,
`clamp`, `clr`, `zfmt`, `dither`, `scis`, `vp`; the default is WiiStation's `GxColor=2`).
The DOL shows each for 150 frames in turn and loops until `hbc.py exit` (or 150 s). After
each copy the CPU stamps the variant's number and the readback texel (RGB565 of the red
square; `f800` is right) into the XFB's top-left 16 lines, so every screenshot says which
variant it is, even a black one.

```bash
bash scripts/wii_probe/gxaa/build.sh              # Git Bash; builds deps/hbc_agent too
python "$WII_BENCH" add --name gxaa --timeout 300 \
  --cwd <repo> -- python scripts/wii_probe/gxaa/run.py NAME [VARIANT ...]
python "$WII_BENCH" wait ID
```

`run.py` sends the DOL with the variants (none: its default set of 8), takes `hbc.py
screen` pictures until each variant was seen twice, exits to HBC and writes
`.runs/NAME/vNN.png`, `sheet.png` (all variants, labelled) and `result.txt` (captures,
luma, readback texel). 8 variants take 60-75 s. `hbc.py screen` reads the XFB the VI scans
(RAM), not the TV signal: a VI-only fault would not show, which is what the `vireg:` line
below is for.

### What the variants showed (bench Wii, `.runs/gxaa_r1..r3/sheet.png`)

| Variant | Picture |
|---|---|
| `fmt=8,aa=0` (RGB8, today's 240p) | right, hard staircase edges, smooth ramp |
| default (RGB565 + AA + the RGB565 readback) | right, **anti-aliased edges**, readback `f800` (right) |
| `tex=0` (565 + AA) | right, AA edges, the ramp in 565 steps (`gxaa_r1/crop_aa.png`: RGB8, AA, `aa=0`, centred samples) |
| `aa=0` (565 EFB, copy filter without AA) | right but aliased: the copy takes one sample |
| `pat=c` (all samples at the centre) | right but aliased |
| `fmt=6,aa=0` (RGBA6) | right |
| `w=512,h=216` (CTR's 240p size) | right |
| `sync=0` (format switched behind a queued RGB8 frame) | right |
| `tex=8` (RGBA8 readback, as `OverlaySaveEfb`) | right, readback right |
| `vf=df` (interlace deflicker taps) | right, vertically softer |
| `fm=1`, `dither=1`, `zfmt=3`, `h=264` | right (dither: GX's pattern shows in the ramp) |
| `fmt=8,aa=1` (AA copy filter on a non-AA EFB) | wrong: stretched vertically, bottom cut (`gxaa_r2/v04.png`): what a filter/format mismatch looks like; never black |
| `h=288` (over the AA limit of 264 lines) | garbage below line ~264, as documented |

Side finding: after a `zfmt=3` (`GX_ZC_FAR`) variant, the next `GX_ZC_LINEAR` variants lost
their depth pair (r2; r3 without `zfmt=3` never did). WiiStation always uses `GX_ZC_LINEAR`.

So every suspect of the first report is fine on the hardware: the sample pattern and taps
WiiStation uses, `GX_SetDispCopyYScale`, the RGB565 and RGBA8 readbacks, and a pixel format
switch with a frame in flight.

### WiiStation itself (debug builds of this branch, `scripts/wii_lab.py`)

| Run | Chain | Result |
|---|---|---|
| `hw_gx_aa1` | the AA game first in the boot | 4 TV pictures (vblank 400-2300), all right |
| `hw_gx_aa2` | a 240p game, then the AA game (the morning's rows 5 -> 6) | all right |
| `hw_gx_aa3` (`gx_aa_ctr3.txt`) | `GxTexMin=3`, `GxColor=1` (480i), 240p, then AA (the morning's order) | all right |
| `hw_gx_aa_ab`, `hw_gx_aa_ab_pmc` (`gx_aa_ab.txt`) | 240p base vs AA, same intro moment, light and pmc builds | both right; 446 presents each, wall 30.32 / 30.20 s; 1744 of 110592 pixels differ by more than 8 levels, all on polygon edges (`diffmap.png`, `crop_ab_edge.png`) |
| `hw_gx_aa_vi` | the same, with the `vireg:` probe | VI registers identical in both games except the beam position (0x2C/0x2E); the VI scans `xfb[2]`, the buffer the `xfb` dump reads |

`vireg:` (debug builds, `perf_prof.c`, in each report): the framebuffer the VI scans
(`VIDEO_GetCurrentFramebuffer`), `xfb[2]`, and the first 32 VI registers. `VIDEO_Configure`
does not read `GXRModeObj.aa` (libogc v3 `video.c`; only `VIDEO_GetFrameBufferSize` does), so
with an identical VI and a right XFB the TV shows the picture.

**Root cause of the morning's black TV: not found, and not in the AA path.** Five boots of
this code put a right, anti-aliased picture in the XFB the VI scans, with the VI set up
exactly as for plain 240p. The morning's build differs from this one only in the ANALOG note
text and the dump's line count. Left to check by eye: run `gx_aa_ab.txt` (or the gxaa loop)
while watching the bench TV.

### Trying it in person

`GxColor = 2` and `TVMode = 1` in `sd:/wiistation/settings.ini` (or a game's own settings),
with `gpuPlugin = 2` (OpenGX). In the menu: Video > 240p On, and General > Advanced > GPU > GX
Colour "AA (240p)". It applies at the next game start or resume. Only NTSC/EuRGB60 240p at
264 lines or fewer (PAL 288-line 240p stays RGB8). Look at polygon edges (CTR's karts and
track borders, the Naughty Dog crate); textures and 2D are unchanged. Colours are 5/6/5
with GX dither where the game dithers (`Dithering`).

Known limit: GX dither stays as the last primitive left it for draws that are not PS1
primitives (FMV and screen uploads, the overlay restore), so in `GxColor` 1 and 2 those can
be dithered too.

## Evidence (Dolphin, debug deep build, worktree `.runs/`)

```bash
bash scripts/wsx.sh build debug deep
bash scripts/wsx.sh chain gx_ctr   scripts/chains/gx_dol_ctr.txt   --dol debug --fast --secs 900 --env XFB_RAM=1
bash scripts/wsx.sh chain gx_spyro scripts/chains/gx_dol_spyro.txt --dol debug --fast --secs 900 --env XFB_RAM=1
python scripts/xfb2png.py .runs/gx_ctr/xfb_01.bin .runs/gx_ctr/xfb_01.png
```

Pictures (xfb = what the TV shows; same guest moment in every row, the game state is
identical between settings):

- `.runs/gx_ctr/crop_road.png` -- CTR start line, distant road and wall, x2: base,
  `GxTexMin=3`, `GxColor=1`. `.runs/gx_ctr/sheet_base_texmin3_rgba6.png` full frames.
- `.runs/gx_spyro/crop_ground.png` -- Spyro, distant grass, path and wall, x2, same rows.
- `.runs/gx_ctr/xfb_07..10.png` -- 240p base vs `GxColor=2` (see the 240p XFB bug below).

Numbers:

- Pixel differences against the base (overlay text excluded by eye; mean absolute
  difference over the frame): `GxTexMin=3` changes 16-18 k pixels by more than 8 levels in
  CTR, 12-15 k in Spyro (mean 1.6-2.0): the distant textures, smoother. `GxColor=1` changes
  four pixels in five by 1-4 levels (mean 1.0-1.35): Dolphin's 6-bit quantization and its 2x2
  dither -- invisible at normal size. `GxColor=2` in Dolphin: at most 10 levels, mean 2.2:
  Dolphin emulates neither GX AA nor the 565 quantization (`FramebufferManager.cpp`: one
  RGBA8 EFB; `PixelShaderGen.cpp`: dither and quantization only for RGBA6), so its picture
  says nothing about the Wii's.
- CPU: the chain tables give the same wall, speed (1.00x) and GPU-plugin share for every
  variant (CTR gpu 8.5-8.7%, Spyro 13.7-13.8%): no CPU cost that Dolphin can see. The GX
  cost of anisotropy, AA and dither is GPU time, which only the Wii shows.
- AA survives the EFB sync in Dolphin: CTR's 240p AA game took 183 EFB snapshots
  (`gpupres: efbcap=183`) with no texture or allocation failure. The real AA EFB layout is
  hardware-only.
- **Side finding, 240p XFB:** CTR in 240p (display 512x216) writes a 512x54 XFB: the whole
  frame squashed into 54 lines. The same with main's debug DOL (`.runs/gx_ctr240_main`,
  chain `gx_dol_ctr240.txt`), so it is not from this branch. **Resolved (08c486c7):** only
  the dump was wrong. libogc v3.1.0's `GX_SetDispCopyYScale` reads the copy height at bit
  12 instead of 10, so it returns `((h-1)>>2)+1` lines (216 -> 54, 240 -> 60); the picture
  on the TV was always right, and the 54 lines were its top quarter, not a squashed frame.
  switchToTVMode now takes `rmode->xfbHeight`: CTR dumps 512x216 and Spyro 512x240, matching
  Dolphin's frame dumps (`.runs/xfb_ctr240_frames`).

## Dolphin is the wrong witness for all four

| Feature | What Dolphin does | Source |
|---|---|---|
| GX anisotropy | replaced by the PC GPU's anisotropy, even with no mipmaps | `VideoCommon/RenderState.cpp` (max_aniso), `TextureCacheBase.cpp GetSamplerState` |
| LOD / mipmaps | honest (when a texture has levels) | `RenderState.cpp` |
| RGB565 + AA | one RGBA8 EFB; AA only as the user's MSAA; no 565 quantization | `FramebufferManager.cpp GetEFBColorFormat`, `PixelShaderGen.cpp` |
| GX dither | 2x2 Bayer at 6 bits, RGBA6 only; libogc's docs say 4x4 Bayer and RGB565 too | `PixelShaderGen.cpp`, `Software/SWEfbInterface.cpp Dither` |

## Bench Wii: what to run and compare

`scripts/chains/gx_quality_ab.txt` (CTR start line x6, Spyro play x3), `build debug pmc`,
queued through wii-bench with `wii_lab.py` (command in the file). Compare:

1. **GPU cost.** Rows 2 (`GxTexMin=1`), 3 (`=3`), 4 (`GxColor=1`) against row 1; row 6
   (240p AA) against row 5; rows 8-9 against 7: speed, gpu %, presents, pmc cycles. A
   GPU-bound scene shows the GX cost as fewer presents; for a harder test add
   `GxTexMin=3` to `ctr_tiger_attract.txt` (Tiger Temple, ~12 fps on the bench).
2. **Pictures** (xfb_NN, two per game in chain order): `xfb2png.py`, then the distant road
   and grass side by side as above. What only the Wii can answer: does `GX_ANISO_4` change
   anything with no mipmaps; is GX's dither 2x2 or 4x4, and does it apply to RGB565; how
   AA edges and 565 colour look (after the 240p XFB bug is fixed).

## PS1-exact dither on OpenGX (design, not built)

A PS1 adds a 4x4 offset (-4..+3, `SoftGPU/dither5.h`) to the 8-bit colour, then truncates
to 5 bits, per PS1 pixel. On GX: a second TEV stage adds a signed 4x4 I8 texture whose
coordinates come from the vertex position (`GX_TG_POS` through a matrix that scales EFB
pixels back to PS1 pixels, so one offset covers a 2x2 EFB block at 2x resolution), and the
5-bit truncation comes from the RGB565 EFB -- which exists only with AA at 264 lines or
fewer, i.e. in 240p. In the 480-line RGB8 EFB nothing truncates, and the offsets would be
noise on a 24-bit picture. So: exact PS1 dither on OpenGX = 240p + `GxColor=2` + this
stage; the cost is one TEV stage and one texgen on dithered draws.

## Recommended next steps

1. Run `gx_quality_ab.txt` on the bench Wii (needs the queue). Status (2026-11-03): still open as a
   whole; the Wii has since run only the AA loops (`hw_gx_aa*`, 2026-10-19) and the anisotropy
   checks (2026-11-02, below), not this chain's GPU-cost rows.
2. Done (2026-10-19): the 240p XFB height (`switchToTVMode`) and the AA pictures (above).
3. Decide from the pictures: keep `GxTexMin` if aniso does something on hardware at an
   acceptable GPU cost (a menu entry "Distant Textures"?); keep `GxColor=2` as a 240p
   "Anti-Aliasing" option: edges improve and the CTR intro costs nothing; Tiger Temple is not
   measured yet (status 2026-11-03: still open; decide `GxTexMin` and `GxColor` after step 1).
4. `Dithering` on OpenGX: either make it do something (GX dither via `GxColor`, or the
   exact TEV dither in 240p) or say in the menu that it applies to the soft renderers only.
   Status (2026-11-03): at `GxColor` 0 (default) it still does nothing on OpenGX (`bDrawDither` is
   never assigned; `GL_DITHER` is ignored). At `GxColor` 1 and 2 `GxDitherPrim()` (`GlesGpu/gpuPrim.c`,
   c49d60be) reads `useDithering` and switches GX dither per primitive. The exact PS1 TEV
   dither is not built; the menu note is not written.
5. Status (2026-11-03): open; mipmaps for far ground are planned later (see "The Wii's anisotropy and the texture cache"). Mipmaps only if 3 shows that minified textures still shimmer and the MEM2 pool has room.

## Anisotropy only where it helps (2026-11-01)

`GxTexMin` 2 and 3 (anisotropic 2x / 4x) were removed on 2026-10-29 because Ape Escape's hand
cursor (title screen, pause menus) lost its look: a blurred outline with gray seams where the
white inside should be. They are back, and the hand is right, because a draw gets the anisotropic
filter only when it can use it.

Cause, measured in Dolphin (`scripts/chains/ape_hand_aniso.txt`, user's settings, `BilinearFilter`
Near; the hand is a 32x32 sprite, `cmd=65`, drawn about 2x magnified):
- OpenGX splits every texture by its texels' STP bit into two passes (`GxMask` Accurate): the
  hand's white texels (bit 15 clear) are in the semi copy, its blue shading and outline (bit 15
  set) in the opaque one, each with transparent texels (alpha 0, color 0) where the other class is.
  Any filter wider than nearest blends across that border: the color is pulled toward black and
  the two passes' alphas (0.5 and 0.5 at the border) do not add up, so a seam of the background
  shows. Plain bilinear on the same sprite (`BilinearFilter=2`, `GxTexMin=0` or 1, no anisotropy)
  gives the same picture; `GxMask=1` (Fast, no split) with bilinear is clean; no Z write in the
  passes (test) changes nothing; drawing only the semi pass or only the opaque pass shows each
  half is right on its own.
- With anisotropy on, Dolphin's host sampler is `D3D12_FILTER_ANISOTROPIC` (linear in every
  direction), so the sprite is filtered linear even though its mag filter is Near. Anisotropy
  was also set on every texture, whatever was drawn with it. The earlier measurement (aniso 2 or 4
  broke it with a linear or mip filter, edge LOD on or off, either layer alone; a mip filter with
  aniso 1 did not) is the same fact.
- Anisotropic taps cannot improve a sprite or a magnified polygon (nothing is minified), so
  there is nothing to lose by keeping them off those draws.

Fix: `GlesGpu/gpuPrim.c aniso_mark()` sets `ogx_aniso_ok` for each textured draw: a polygon (not a
sprite or tile) whose affine map has one texel or more per EFB pixel along x or y (the footprint
`lod_probe` already measured, now `tex_footprint()`). `deps/opengx/gc_gl.c`: every texture object
is made with the plain filter (`ogx_texq`: linear minification, magnified texels as the
`BilinearFilter` setting); `ogx_texq_pick()`, called from `checkLoadTextureObj` for each draw,
switches the object to `ogx_texq_aniso` (`GX_LIN_MIP_LIN`, edge LOD, `GX_ANISO_2` / `_4`) and
writes it to its TMEM slot again when the draw takes it, and back when it does not (about 2% of
the draws toggle in CTR). The TMEM region is not reloaded. The movie/screen-upload texture is
left alone (`ANI_MGD`).

Checked in Dolphin (it replaces GX anisotropy with the PC's, so this proves the selection, not the
Wii's filter): hand box identical to `GxTexMin=1` in every frame of the title screen with
`GxTexMin` 2 and 3 (was 2700-4200 differing pixels); Ape's 3D intro (magnified polygons) identical
to Linear (was 22-31 k pixels blurred); the lab scene's minified floor is smoothed as before while
the magnified coat and shoes stay crisp (Linear: noisy floor; old anisotropy: everything blurred);
CTR's HUD identical to Linear (old: 19 k pixels changed). Not checked: the Wii (the hand was seen
broken there with `GxTexMin=3`, 2026-10-29; the fix keeps anisotropy off sprites and magnified
polygons, which is all the hardware can have disliked, but the hardware's own anisotropy was not
re-run), and minified cut-out polygons (fences, leaves) still show the class seam when filtered
anisotropically: a texture that is mostly transparent texels of one class would need the colors
of the other class in its transparent texels to hide it.

## The Wii's anisotropy and the texture cache (2026-11-02)

On the bench Wii, Aniso 2x/4x drew rows of coloured dashes on Ape Escape's far grass (title demo,
`scripts/chains/ape_attract_texmin.txt`, run `ape_texmin_wii`); Off and Linear were clean, Dolphin
never showed it. The textures share one cache texture and have no smaller mip level, so the
hardware spreads its taps over the whole footprint, tens of texels at the horizon, into the
neighbouring textures. `aniso_mark()` (gpuPrim.c) now gives a polygon the taps only up to 2 (2x)
or 4 (4x) texels per pixel; farther ground keeps the linear filter (`ape_anicap_wii`: clean at
the horizon and in the middle distance, all three modes). Taps for the far ground need mipmaps
built per cached texture ("Mipmaps: how they could be built" above): the planned next step.
