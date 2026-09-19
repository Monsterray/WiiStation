# PSX GPU and plugin architecture facts (established by measurement)

## Plugins and shared state

- Three GPU plugins: Old Soft (`gpulib/oldGpu.c`, `gpuPlugin = 0`, ground truth), New Soft
  (`SoftGPU/gpulib_if.c` + `soft.c`/`prim.c`, `gpuPlugin = 1`), OpenGX (`GlesGpu/gpuPlugin.c`,
  a unity build that `#include`s `gpuDraw.c`, `gpuTexture.c`, `gpuVramReadback.inc`,
  `gpuPrim.c`; renders through `deps/opengx/gc_gl.c`).
- `psxVuw`/`psxVub` (the 1024x512 16-bit VRAM), `PSXDisplay`, `PreviousPSXDisplay`,
  `lGPUstatusRet` are single globals defined in `SoftGPU/gpulib_if.c` and used by the GX plugin
  through externs. The software rasterizer therefore draws into the GX plugin's VRAM when
  called while OpenGX is active — that is how off-screen primitives are handled.
- `do_cmd_list(list, n, &cycles, &last, &cmd)` (declared in `gpulib/gpu.h`) executes raw GP0
  words with the New Soft rasterizer, including E1..E6 state words; `cmd_lengths[]` gives
  word counts.

## Display buffers and the GX draw origin

- GP1 05h (display start) makes P.E.Op.S. copy the current `PSXDisplay.DisplayPosition` into
  `PreviousPSXDisplay` and take the new one. The GX plugin then draws relative to
  `PreviousPSXDisplay.DisplayPosition` (`SetOGLDisplaySettings`, `GDrawOffset`), i.e. the
  buffer a double-buffered game is drawing INTO, so between two presents the EFB holds the
  back buffer. A present (`flipEGL`) happens at the display change and clears the EFB.
- The readback machinery's `g_activeMap` is the DISPLAYED buffer, so a game reading the buffer
  it just drew classifies as `MAPPING_PREVIOUS`. `TryCapturePreviousReadRect` accepts that read
  when the drawing area (E3/E4) overlaps the previous display. EFB tile coverage cannot gate
  it: a GP0 02 fill covering the next screen is never drawn (`primBlkFill`,
  `else if (!clearNext)`), it becomes the clear-on-swap colour, and
  `EfbDiscardedAfterPresent` zeroes the tile arrays.
- Drawing-area clipping (E3/E4) is applied through `GX_SetScissor` in top-origin coordinates,
  lifted around fills (GP0 02 ignores the drawing area), re-armed on E3/E4. Without it,
  polygons spill into the 8 rows above/below a 224-line drawing area inside a 240-line display.

## VRAM readback (GlesGpu/gpuVramReadback.inc)

Gated per game by `dwActFixes & AUTO_FIX_VRAM_READBACK` (`database.c`
`special_game_hack_vram_readback`: Dino Crisis 2, Vagrant Story, Spyro). Snapshots of the EFB
are taken with `GX_CopyTex` into MEM2 (async, draw-sync token, `GX_DrawDone` +
`DCInvalidateRange` before a read), tracked as 16x16 tiles keyed by VRAM coordinates, and
merged into psxVuw on a GP0 C0 read (`MergeReadbackToPsxVuw`, `BestOwningSnapshotForPixel`).
Under Dolphin the copy only reaches RAM with EFB-to-texture-only OFF.

## Off-screen primitives

`OffscreenSoftDraw()` in `gpuPlugin.c`: polygon/rect commands (0x20-0x3F, 0x60-0x7F) whose
bounding box (raw words + DrawOffset) misses both display rects, with a display set up and the
box inside VRAM, are prefixed with E1 (from `lGPUstatusRet`), E2 (last captured word), E3/E4/E5
(from `PSXDisplay`), E6 (from status bits 11-12) and sent to `do_cmd_list`, then
`InvalidateTextureArea` + `MarkCpuVramWrite`. Lines stay with GX. The PSX BIOS shell renders its
spheres and button splashes this way at x 640..830 and paints them as 15-bit textured quads.

## Textures

- Converter/cache/placement were verified texel-exact against VRAM (`ogxeq mism=0`); GX
  texels are the PSX 1555 word with bit 15 set, texel 0 transparent.
- Spyro uses flat-CLUT textured polygons for Gouraud surfaces: a uniform-texture draw is not a
  bug by itself.
- P.E.Op.S. "framebuffer textures" (`iFrameTexType` 2/3) do not work in this port: opengx has
  no `glCopyTexSubImage2D`, `CheckVRamReadEx` is a stub. The off-screen rasterizer and the
  readback machinery are the replacements.

## Input path

- Pad plugin bound in `Gamecube/GamecubePlugins.h` is SSSPSX (`PadSSSPSX.c`); `PadWiiSX.c`
  holds the scripted-input parser (`autoinput_load/mask/active`) that both use.
- `sio.c` answers the pad-select byte from the plugin only when `padType[0] != 0`
  (`PadType1` setting); otherwise 0xFF, no SIO interrupt, and the title sees no controller.
  `auto_assign_controllers()` (`Gamecube/PlugPAD.c`, at boot and every 15 menu frames while
  `PadAutoAssign = 1`) sets `padType` to NONE for ports with no host controller.
- The BIOS shell polls ~2x per vblank; with a pad present `sio: start` grows accordingly.

## GP0/GP1 cheat sheet for traces

| Word | Meaning |
|---|---|
| 01 | clear cache; 02 fill rect (ignores drawing area and mask) |
| 20-3F | polygons: +08 quad, +10 gouraud, +04 textured, +02 semi-transparent, +01 raw texture |
| 40-5F | lines (48-4F / 58-5F polylines terminated by 5000 5000) |
| 60-7F | rectangles: 60-67 variable size, 68 1x1, 70 8x8, 78 16x16; +04 textured, +02 semi |
| 80 | VRAM→VRAM move; A0 CPU→VRAM; C0 VRAM→CPU (readback) |
| E1 | texpage/ABR/dither (mirrors status bits 0-10); E2 texture window; E3/E4 drawing area; E5 draw offset; E6 mask bits |
| ABR (semi-transparency) | 0 = B/2+F/2, 1 = B+F, 2 = B-F, 3 = B+F/4 |
| GP1 05 | display start (x, y) — the swap; F5 in the trace; F8 display mode |

Trace geometry: `(x0,y0)-(x1,y1)` are VRAM coordinates after the draw offset.
