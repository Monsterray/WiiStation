# Bit 15 (STP / mask) of pixels that GX draws

Notes for a proper fix (written 2026-10-19, main 59f1348 = 5.14.0). The 5.14.0 code is a
heuristic. This file holds what is known, the PS1 rules, where WiiStation loses the bit, the
candidate designs with their cost, and the tests that a fix must pass.

Related: `Docs/EFB_SYNC.md` (the readback model), `GlesGpu/efbSync.inc`, `GlesGpu/gpuPlugin.c`
`EfbBeforeGxCommand()`, skill `references/case-studies.md` section 16, commit 173d3dc (merge
7d3c793).

## 1. Summary

- On a PS1, every VRAM pixel has bit 15. It is the STP bit of a texel and the mask bit of a
  drawn pixel. A game can read it back: it decides which texels blend when the game uses
  its own frame as a texture, and which pixels a check-mask draw must not change.
- The OpenGX plugin draws with GX into the EFB. The EFB of a game is `GX_PF_RGB8_Z24`: it
  has no alpha, so no per-pixel place for bit 15. The GX textures do not carry the texel's
  STP either.
- `efb_sync()` copies GX pixels back into `psxVuw` from RGB565 snapshots. Until 5.14.0 it
  kept the stale bit 15 that `psxVuw` held before the draw (usually 0).
- 5.14.0 keeps one bit-15 value per 16x16 VRAM tile (`efb_stp[][]`), taken from the last
  GX primitive over the tile. This fixed CTR's intro transitions. It is not per pixel.
- Recommended proper fix (section 7): put the EFB in `GX_PF_RGBA6_Z24`, write bit 15 into
  the EFB alpha with `GX_SetDstAlpha` per draw pass, copy an A8 snapshot with each RGB565
  snapshot, and let `efb_sync()` take bit 15 per pixel from the A8 copy.

## 2. The PS1 rules for bit 15

Source: psx-spx, "Graphics Processing Unit (GPU)"
(https://psx-spx.consoledev.net/graphicsprocessingunitgpu/), sections GP0(02h), GP0(E6h),
"Texture Colors" and "Semi Transparency". `E6.0` = "set mask while drawing", `E6.1` = "check
mask before draw".

| Write path | Bit 15 written | Check mask (E6.1) applies |
|---|---|---|
| Textured polygon or rectangle, opaque (20-3F/60-7F, +04, no +02) | texel bit 15 OR E6.0 | yes |
| Textured, semi-transparent (+04 +02) | texel bit 15 OR E6.0. Only texels with bit 15 = 1 blend. Texels with bit 15 = 0 are drawn opaque. | yes, also for blended pixels |
| Texel 0000h (any textured draw) | nothing: the pixel is not written | - |
| Texel 8000h | drawn: black, opaque or semi-transparent by the rule above | yes |
| Raw (+01) or modulated texture | same rule, no difference | yes |
| Untextured polygon, rectangle or line, opaque or semi-transparent | E6.0 only (psx-spx: "for untextured polygons, bit15 is set to zero") | yes |
| GP0 02 fill | always 0. The fill ignores E6.0, E6.1 and the drawing area. | no |
| GP0 A0, CPU to VRAM | data bit 15 OR E6.0 | yes |
| GP0 80, VRAM to VRAM | source bit 15 OR E6.0 | yes |
| GP0 C0, VRAM to CPU | returns the stored bit | - |
| Dithering | does not change bit 15 | - |
| Display (15-bit mode) | bit 15 is not shown | - |

Check mask: a write to a destination pixel whose bit 15 is 1 does not occur. This applies
per pixel, for every source of bit 15 (texel STP, E6.0, an image load).

## 3. The bug found on 2026-10-18 (CTR intro transitions)

- Symptom: in CTR's attract intro, the zoom / motion-blur transition between the character
  clips showed blocky vertical stripes of old picture under OpenGX. Soft Fast showed a blur.
  Reproduction: `scripts/chains/ctr_demo.txt` (no input), intro blur near vblank 9200
  (debug build, Dolphin).
- Trace (`trace 9206`, `trace 9300`): each present ends with two `cmd=2f` quads (textured,
  semi-transparent, raw 15-bit, ABR 0). They cover the drawn buffer and use the OTHER buffer
  as the texture: the last frame is drawn back at 50% over the new one.
- `vramio.log`: `C1 6,300 250x209 224 0` and `C2 ... 100 0` x83. `efb_sync()` filled the
  texture rectangle from snapshots and missed no tile. The colour was correct.
- Decisive measurement, on the `vram.bin` of both plugins: display-buffer pixels with bit 15
  set = Soft 92.2%, OpenGX 0.0%. A PS1 blends a textured pixel only if the texel's bit 15 is
  1. The OpenGX texel split agrees with that rule (`deps/opengx/gc_gl.c`: bit 15 set goes to
  the blended pass, TEX_TYPE_2).
- Cause: `efb_sync()` wrote the snapshot colour and kept the old bit 15 of `psxVuw`, which
  was 0 after the game's GP0 02 clears. The blur quad drew opaque, zoomed, every frame, and
  the error fed back into the next frame.

## 4. The 5.14.0 heuristic and its limits

Code (commit 173d3dc):

- `GlesGpu/efbSync.inc`: `static unsigned char efb_stp[EFB_TY][EFB_TX]`, one byte per 16x16
  tile. `efb_gx_draw(x0, y0, x1, y1, stp)` writes `stp` into every tile of the primitive's
  bounding box. `efb_sync()` writes `(colour & 0x7FFF) | (efb_stp ? 0x8000 : 0)`.
  `efb_reset()` clears the array.
- `GlesGpu/gpuPlugin.c` `EfbBeforeGxCommand()`: `stp = sSetMask != 0 || ((cmd < 0x40 || cmd
  >= 0x60) && (cmd & 0x04))`. Set-mask draws and textured polygons / rectangles count as
  set. Untextured draws and lines count as clear.

Measured with it: CTR's transition matches Soft Fast (bit 15 = 100% in the synced buffer).
Ape Escape's pause: bit 15 share 49% against Soft 44%, picture unchanged. Spyro, Crash 3
and FF7 replays: guest RAM identical for 10800 vblanks.

Known limits (each one gives a wrong bit 15 on some pixels):

1. A textured texel with STP = 0 is marked set. Most opaque CLUT entries have STP = 0, so
   opaque textured scenery gets bit 15 = 1 where a PS1 writes 0.
2. The unit is a 16x16 tile, and the area is the primitive's bounding box, not its
   coverage. A small sprite marks whole tiles. The last primitive in command order wins for
   the whole tile.
3. A tile that an untextured primitive drew last loses the bit of textured pixels under it,
   and the opposite.
4. Only `efb_sync()` uses it. The GX side itself (mask check, see section 5) does not.

When a wrong bit can show: (a) the game uses a GX-drawn frame as a semi-transparent texture
(CTR), (b) a check-mask draw, image load or VRAM move goes over synced pixels in VRAM,
(c) game code reads bit 15 through GP0 C0 (rare).

## 5. Where WiiStation loses bit 15 today

| Path | What happens to bit 15 | Exact? |
|---|---|---|
| Soft plugins (Soft Fast / Soft Timed, `SoftGPU/soft.c`) | PS1 rules, in `psxVuw` | yes: the reference |
| OpenGX `OffscreenSoftDraw()` (primitives outside the live buffer) | soft rasterizer into `psxVuw` | yes |
| OpenGX image load / VRAM move / fill in `psxVuw` | CPU code, E6.0 applied in `primMoveImage` | yes in `psxVuw` |
| OpenGX GX draw into the EFB | the EFB is `GX_PF_RGB8_Z24` (`Gamecube/libgui/GraphicsGX.cpp`, the TV-mode set-up and `GX_Init` path): no alpha bits | lost |
| GX textures | `_ogx_scramble_*` (`gc_gl.c`) write the PS1 word with bit 15 forced to 1 (RGB5A3 "opaque RGB555"), 0000h = alpha 0. The texel STP only selects the pass (TEX_TYPE_1 / TEX_TYPE_2), and only for semi-transparent draws. | lost in the texture |
| Snapshots (`efb_capture_live`) | `GX_TF_RGB565` copy | lost |
| `efb_sync()` | 5.14.0 tile heuristic (section 4) | approximate |
| `UploadScreen` (VRAM into the EFB) | texture with bit 15 forced to 1 | lost in the EFB (VRAM keeps it) |
| Mask check under GX (`gpuPrim.c` `SetZMask*`, `cmdSTP`) | P.E.Op.S. depth emulation: set-mask draws, and semi-transparent textured draws, get depth 0.95. Other draws get an increasing `gl_z` (reset to 0 at each swap and each fill). E6.1 sets the depth test to LESS. | approximate: texel STP of opaque draws is not seen |
| Anti-aliased video mode (`vmode->aa`) | the menu init path sets `GX_PF_RGB565_Z16` | lost |

The commented-out block in `gc_gl.c` `_ogx_apply_state()` says "It appears that the
GX_SetDstAlpha function is not functioning". The cause is the EFB format: an RGB8_Z24 EFB
has no alpha to write. Dolphin disables the alpha write for that format (section 6).

## 6. What Dolphin shows (C:/projects/dolphin-src, tag 2609)

- `Source/Core/VideoCommon/RenderState.cpp` `BlendingState::Generate()`: `target_has_alpha`
  is true only for `RGBA6_Z24`. `alpha_update` and `dst_alpha` (GX_SetDstAlpha) are off for
  any other format. With `dst_alpha`, the alpha blend factors become One / Zero: the
  constant alpha is written whatever the colour blend does.
- `TextureConverterShaderGen.cpp` (EFB to texture) and `TextureConversionShader.cpp` (EFB to
  RAM, `RGBA8ToRGB8`): for an EFB without alpha, every copied pixel has alpha 255.
- `PixelShaderGen.cpp`: `rgba6_format` quantizes colour to 6 bits per channel, and
  `dither` applies, only when `ForceTrueColor` is off. `GraphicsSettings.cpp`:
  `GFX_ENHANCE_FORCE_TRUE_COLOR` defaults to **true**, and the WiiStation profiles do not
  set it. So a default Dolphin run does not show the 6-bit banding that real hardware shows
  with RGBA6. Set `[Enhancements] ForceTrueColor = False` in GFX.ini for a run that must show
  the hardware picture.
- `BPFunctions.cpp`: a pixel-format change makes Dolphin convert the EFB content
  (`ReinterpretPixelData`, RGB8ToRGBA6 and back). Real hardware does not convert: it
  reinterprets the bits. Change the format only where the EFB is cleared next.
- Open question: `efbSync.inc` says the EFB alpha "varies per pixel" and that RGB5A3
  snapshots of Ape Escape's pause came out speckled. With RGB8_Z24, Dolphin's copies return
  alpha 255. The real cause of that speckle is not known. Find it before any design relies
  on EFB alpha.

## 7. Candidate designs

### A. EFB alpha holds bit 15, written with GX_SetDstAlpha (recommended)

How:

1. Set `GX_PF_RGBA6_Z24` while a game runs (gpuPlugin init / `switchToTVMode`), and
   `GX_SetAlphaUpdate(GX_TRUE)`. Restore RGB8_Z24 for the menu. Change it only before a
   clear.
2. For each GX draw pass, `GX_SetDstAlpha(GX_ENABLE, stp ? 0xFF : 0x00)`:
   - untextured polygon, rectangle, line: `stp = E6.0`.
   - textured: the existing two-pass split by texel STP (TEX_TYPE_1 = STP 0, TEX_TYPE_2 =
     STP 1), with `stp = 0 | E6.0` on the first pass and `1` on the second. Today the split
     exists only for semi-transparent draws. Extend it to opaque textured draws. A texture
     with one STP class still draws in one pass (the other pass returns early, as
     `ogx_skip_b1` / `ogx_skip_b2` count today).
   - GP0 02 fill (the `glClear` quad and `TitleFillArea`): `stp = 0`.
   - `UploadScreen`: split the upload by bit 15 in the same way, or draw it in two passes.
3. Snapshot: with each RGB565 copy, also copy the alpha (`GX_SetTexCopyDst(..., GX_CTF_A8,
   ...)`, 1 byte per pixel). `efb_sync()` takes bit 15 = (A8 >= 0x80) per pixel. Remove
   `efb_stp[][]`.

Why `GX_SetDstAlpha` and not the TEV alpha output: the TEV alpha already carries texture
transparency. The alpha compare (`GX_SetAlphaCompare(GX_GREATER, 0, ...)`) discards texel
0000h with it, and the opaque pass blends with SRCALPHA / INVSRCALPHA. A TEV alpha of 0 for
STP 0 would discard those texels. The constant destination alpha is independent of the TEV
and of blending.

Cost on the Wii:

- Fill rate: none. RGB8_Z24 and RGBA6_Z24 both use 24 bits per EFB pixel.
- Colour depth: 6 bits per channel instead of 8. A PS1 stores 5 bits, so readback stays
  exact. Gouraud gradients and blends lose precision against today's picture (still finer
  than a PS1). `GX_SetDither(GX_ENABLE)` works in RGBA6. This is a visible change: the user
  must look at it on the bench Wii (Dolphin hides it, section 6).
- Second pass: only for textured primitives whose texture holds both STP classes. Count
  them before building it (a probe on the upload texel scan: textures with both classes,
  and draws of them, per second).
- Texture memory: the split keeps a second texel buffer (`semiTransData`) for every texture
  with mixed STP, also for opaque draws. The MEM2 texture pool was full once already (Crash
  3 after FF7). Measure the pool with the chains in section 9.
- Snapshot: one more `GX_CopyTex` per snapshot, iResX x iResY bytes (50% of the RGB565
  copy). Snapshots run only when the policy has them on (EFB_SYNC.md "Policy").
- `GX_SetDstAlpha` per pass: one BP register write.

Interaction:

- efb_sync / readback: per-pixel bit 15 from the A8 snapshot. Exact for every GX path that
  sets the constant correctly.
- Semi-transparency: `GX_SetDstAlpha` writes the constant alpha whatever the colour blend
  does. That is the PS1 rule: blended texels write 1 (their STP is 1), untextured blends
  write E6.0.
- Mask check: keep the depth emulation for now. A later step can make it exact: write
  depth 0.95 for every pass with `stp = 1` (not only for set-mask and semi-transparent
  draws), so E6.1 blocks every pixel with bit 15, as on a PS1. DuckStation's hardware
  renderers use the same depth idea (`UpdateDepthBufferFromMaskBit()`, a per-draw depth
  counter, commit a08c398 "Provide depth in vertex rather than computing").
- Anti-aliased mode (RGB565_Z16) has no alpha: keep the tile heuristic as the fallback there.

### B. A second render pass into an alpha-only target

GX has one EFB and no second render target. Each primitive would be drawn a second time
into another EFB region, or into the same region after a copy. That doubles vertex and fill
cost for every primitive, and the EFB (640x528) has no free region next to a 640x480
picture. Reject.

### C. A CPU-side bit-15 shadow from the command stream

Keep a 1-bit 1024x512 bitmap (64 KB). Rasterize each GX primitive's coverage on the CPU
and write its bit 15.

- Coverage with a per-primitive bit (no texel STP): fixes limits 2 and 3 of section 4, not
  limit 1. It needs a triangle / rectangle rasterizer per primitive on the CPU, which is
  already the Wii's bottleneck.
- With texel STP: that is the soft rasterizer run a second time, about the cost of Soft
  Fast. Too slow for the default.
- Useful as a debug oracle: a debug build can compare it with design A per pixel.

### D. Read bit 15 from the depth buffer

The mask emulation already writes depth 0.95 for set-mask draws (and semi-transparent
textured draws). A Z copy (`GX_TF_Z8` / `GX_TF_Z16`) at each snapshot could give bit 15 =
(depth near 0.95). It costs no format change. It does not know texel STP of opaque draws,
and it marks semi-transparent textured pixels as set whatever their texels are. Only a
partial fix. It becomes exact only together with design A's per-pass `stp`, and then A's
alpha is the simpler carrier.

### E. RGBA8 textures or a second STP texture

Carry the texel STP into the TEV with a 32-bit texture or an extra I4 texture map, and
write it through a TEV alpha level (for example 0 = discard, 0x80 = STP 0, 0xFF = STP 1).
It doubles texture memory (RGBA8) or adds a texture map and a TEV stage per draw, and it
changes the alpha compare and the opaque blend mode. More cost than design A's split for
the same result. Reject unless the split's second pass turns out expensive.

## 8. Recommended order of work

1. Find the cause of the "EFB alpha varies" speckle (section 6). Expected: it does not
   apply to RGB8_Z24, but it must be known.
2. RGBA6_Z24 alone, `GX_SetDstAlpha` off. Look at gradients on the bench Wii (FF7 opening,
   Spyro sky, Crash 3 title) against the current build. Decide on dither with the user.
3. `GX_SetDstAlpha` for untextured draws, lines and fills. A8 snapshots. `efb_sync()` per
   pixel for those pixels, tile heuristic kept for textured ones.
4. Textured draws through the split, opaque and semi-transparent. Remove `efb_stp[][]`.
   Measure the second-pass count and the MEM2 pool.
5. `UploadScreen` with bit 15.
6. Optional: exact mask check through depth (design A, "Mask check").

After each step: the tests in section 9. Under Dolphin, use `ForceTrueColor = False` for
picture comparisons, as the hardware quantizes.

## 9. Games that depend on bit 15 after a GPU draw

| Game | ID | In the library | What reads bit 15 | Status |
|---|---|---|---|---|
| Crash Team Racing | SCUS-94426 | yes | intro / attract transitions: the last frame drawn back as a semi-transparent texture | fixed by the 5.14.0 heuristic |
| Ape Escape | SCUS-94423 | yes | pause: GP0 C0 readback of the frame, uploads, subtractive sprites. scene fades | share 49% vs Soft 44%, picture correct |
| Spyro the Dragon | SCUS-94228 | yes | pause: GP0 C0 readback, frame redrawn tinted | unchanged by 5.14.0, guest RAM identical |
| Gex 2 - Enter the Gecko | SLUS-00598 | yes | quest menu cursor moved with GP0 80 from GX pixels. Related: re-uploads treat 0000h as a hole except for CPU-written pixels (commit 6cd8062) | fixed, recording exists |
| Dino Crisis 2 | SLUS-01279 | yes | readback scenes (AUTO_FIX_VRAM_READBACK) | readback scenes never reached in a run |
| Vagrant Story | SLUS-01040 | no | readback (AUTO_FIX_VRAM_READBACK) | not tested |
| Silent Hill | SLUS-00707 | no | mask bit (fog, tree boxes): DuckStation commits f9a298f and 9446587 | not tested |
| Ghost in the Shell | SCPS-10043 (JP). US serial: look up | no | menu effect, mask bit (DuckStation 9446587) | not tested |
| Dragon Quest VII | SLPM-86500 (disc 1, look up) | no | menu text colours, mask bit (DuckStation 9446587) | not tested |
| TwinBee RPG | SCPS-45208 | no | fade effect, mask bit (DuckStation 9446587) | not tested |
| Duke Nukem: Land of the Babes | look up | no | water, mask bit (DuckStation 9446587) | not tested |
| Ultraman: Fighting Evolution | SLPS-01248 | no | shadows, mask bit (DuckStation 9446587) | not tested |

The DuckStation list comes from its commit messages. It says which games use the mask bit,
not that they fail in WiiStation. `Docs/GAMES_TO_GET.md` has the full list of games to
obtain.

## 10. Tests for a proper fix

Existing chains and recordings (all games in the library):

| Test | File | What to compare |
|---|---|---|
| CTR intro blur | `scripts/chains/ctr_demo.txt` (9000 vblanks, no input) | frames near 9200 vs Soft Fast, bit-15 share of the display buffers (`dump <vblank>`) |
| CTR attract demos, frame rate | `scripts/chains/ctr_tiger_attract.txt`, `ctr_tiger_attract_skip.txt` | bench Wii timeline 27000-31000: the cost of the second pass and A8 copies |
| CTR pause in Time Trial | `scripts/autoinput/ctr_tt_pause.txt` | xfb pictures 5140-5900, `dump 5380` |
| Ape Escape pause and fades | `scripts/autoinput/ape_escape_play.txt` (in `scripts/chains/replay_rest.txt`) | pause at ~3305, fade at ~3929: picture vs Soft, bit-15 share (Soft 44%) |
| Spyro pause, Crash 3 | `scripts/chains/replay3.txt` | `scripts/replay_check.py --all`, `scripts/vsig_cmp.py`: guest RAM must stay identical |
| FF7, Crash Bash | `scripts/chains/replay_rest.txt` | same |
| Gex 2 quest cursor | `scripts/autoinput/gex2_quest_pointer_play.txt` | one cursor, rows 1-2-3-1, as Soft |
| All library games | `scripts/chains/library20.txt` | frames before / after (`wsx.sh frames`), vsig per vblank |

Bit-15 share of a dump (the measurement that found the bug), for `vram.bin` from `dump`:

```python
import numpy as np
v = np.fromfile("vram.bin", dtype="<u2").reshape(512, 1024)
d = v[y:y + h, x:x + w]          # the display buffer
print((d & 0x8000).astype(bool).mean())
```

Real-PS1 tests to add to the library (`C:/tools/ps1-tests/bin/gpu/`, not staged yet):

- `mask-bit/mask-bit.exe`, real-PS1 log `psx.log`: testWriteAndRead, testSetBit,
  testCheckMaskBit, testCheckIsMaskBitStickyManually, testCheckIsMaskBitStickySetBit. Run it
  through `ps1tests_check.py` (procedure: skill `references/test-roms.md`).
- `transparency/transparency.exe`, reference `vram.png` (a VRAM picture, compare bit 15 and
  colour).
- Note: these tests may draw outside the live buffer. Then `OffscreenSoftDraw()` draws them
  in software and the GX path is not tested. Check `off_soft_*` counters in perf.log.

Pass bar for each step: CTR and Ape Escape pictures as Soft, the bit-15 share within a few
points of Soft on every dump, replays with identical guest RAM, `library20` frames identical
outside the overlay except where the change is intended (RGBA6 gradients, reviewed by the
user), mask-bit.exe 5/5 pass.
