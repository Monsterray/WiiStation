# GX picture options: mipmaps, anisotropy, anti-aliasing, dither

2026-10-19, branch `try/gx-quality` (from 5.14.0, 59f1348). The question: can the OpenGX
renderer use the Wii GPU's mipmapping, LOD bias, anisotropic filtering, anti-aliasing and
dithering, and would a game look better or run faster? An earlier session skipped all of
them; each reason is checked here.

Everything here is about the **OpenGX** renderer (`gpuPlugin = 2`). The default renderer is
Soft Fast (`gpuPlugin = 0`); the soft renderers draw with the CPU at the PS1's resolution and
already dither exactly like a PS1 (`SoftGPU/soft.c`, `dither5.h`).

## Verdict

| Feature | Possible? | Cost | Look | Speed | Status |
|---|---|---|---|---|---|
| Mipmaps + trilinear / LOD bias | Yes, with work: the sub-texture pages allow 2 levels | +33% texture MEM2 (CTR: +5.3 MB); a box filter per upload on the CPU | Less shimmer on 8-12% of the textured area (480 lines), 27% in 240p; distant textures blur; seams of 1-2 texels | None: costs CPU, saves nothing measurable | Not built. Measured where it would act (probe) |
| Anisotropic filter, linear minification | Yes, one call per texture object | Free on the CPU; GPU cycles only where texels are minified (bench) | Distant ground smoother, a little blurred; no mipmaps, so LOD stays 0 | Possibly slower on GPU-bound scenes (Tiger Temple): bench | Prototype `GxTexMin` |
| Anti-aliasing (GX multisample) | Only in 240p mode (EFB 640x240 <= 264 lines); the 480-line EFB would need two passes | One pass, RGB565 | Smoother polygon edges; 5/6/5 colour (PS1 is 5/5/5) | Unknown: bench | Prototype `GxColor=2` |
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

## Prototypes (off by default, not in the menu)

| Setting | Values | Where |
|---|---|---|
| `GxTexMin` | 0 off; 1 minified texels linear (`GX_LINEAR`), magnified keep the `BilinearFilter` choice; 2 / 3 `GX_LIN_MIP_LIN` + edge LOD + `GX_ANISO_2` / `GX_ANISO_4` | `gc_gl.c ogx_texq()`, on the sub-texture and window textures (not FMV/screen uploads) and in `glChgTextureFilter` |
| `GxColor` | 0 RGB8; 1 RGBA6 + GX dither; 2 RGB565 multisampled + GX dither in 240p at <= 264 lines (else as 0) | `GraphicsGX.cpp gxApplyEfbFormat()` and `switchToTVMode` (AA sample pattern of libogc's AA modes, the 240p mode's own no-filter taps = mean of a pixel's 3 samples); `MainFrame.cpp` applies it before a game resumes; `gpuPrim.c GxDitherPrim()` turns GX dither on per primitive where a PS1 dithers (Gouraud or texture-blended polygons and lines, never rectangles) and the `Dithering` setting allows (Default = the game's E1 bit 9) |

Also changed: `efbSync.inc` copies its snapshots with the TV mode's AA state
(`g_tv_mode`), not the menu's `vmode`, so a snapshot of a multisampled EFB is resolved.
`PadWiiSX.c`/`perf_prof.c` take 32 `xfb` pictures per boot (was 16; a chain adds each game's),
`dolphin_run.sh` extracts 32, and `wii_lab.py` fetches `xfb_01..32.bin` from the Wii.

Known limits of the prototypes: `GxColor=1` in the 480-line mode is applied at game start
(`MainFrame`) only; the BIOS autoboot path (`MenuContext.cpp`) does not apply it.
`GxColor=2` needs NTSC-height 240p (PAL 288-line displays do not fit).

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
  chain `gx_dol_ctr240.txt`), so it is not from this branch. Filed as a separate task. It
  also means 240p pictures of CTR cannot show the AA yet.

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

1. Run `gx_quality_ab.txt` on the bench Wii (needs the queue; nothing here touched a Wii).
2. Fix the 240p XFB height (separate task), then re-run row 6 for AA pictures.
3. Decide from the pictures: keep `GxTexMin` if aniso does something on hardware at an
   acceptable GPU cost (a menu entry "Distant Textures"?); keep `GxColor=2` as a 240p
   "Anti-Aliasing" option if edges improve and Tiger Temple does not slow down.
4. `Dithering` on OpenGX: either make it do something (GX dither via `GxColor`, or the
   exact TEV dither in 240p) or say in the menu that it applies to the soft renderers only.
5. Mipmaps only if 3 shows that minified textures still shimmer and the MEM2 pool has room.
