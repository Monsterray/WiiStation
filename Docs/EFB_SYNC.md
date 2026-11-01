# EFB sync: PlayStation VRAM where the GX renderer drew

`GlesGpu/efbSync.inc` (2026-09-27) replaces `gpuVramReadback.inc`. It is the one place that
keeps the emulated VRAM (`psxVuw`) right for pixels the OpenGX plugin drew with GX into the
EFB instead of into VRAM.

## Why it exists

A game can read back what it drew:

| Access | Where it is served |
|---|---|
| GP0 C0, VRAM to CPU | `primStoreImage` (gpuPrim.c) |
| GP0 80, VRAM to VRAM (the source) | `primMoveImage` |
| a texture over a frame it drew | `SetRenderMode`, `efb_sync_texture()` |
| the plugin's own re-upload of VRAM into the EFB | `UploadScreen` |
| a primitive drawn in software over GX pixels | `OffscreenSoftDraw` |

Each calls `efb_sync(rect)` first.

## The model

1. **Who wrote each 16x16 VRAM tile last.** `efb_cpu[][]`: image loads, fills, the software
   rasterizer, VRAM moves, merges (`efb_cpu_write()`). `efb_gx[][]`: GX primitives
   (`efb_gx_draw()`, called by the display-list dispatcher with each primitive's bounds from
   its GP0 words). One counter stamps both. `gx > cpu` = VRAM is stale there.
2. **The EFB holds one VRAM rectangle, "live":** origin `PSXDisplay.GDrawOffset` (the buffer
   being drawn: P.E.Op.S. puts the EFB's origin there), the display's size, and the viewport
   `rRatioRect`. The old tracker tied the EFB to the displayed buffer, so a double-buffered
   game's frames were filed under the other buffer.
3. **GX draws only live.** A primitive that lies outside live (off-screen, or in the displayed
   buffer while the game draws the other one) is drawn in software into VRAM
   (`OffscreenSoftDraw`), after the pixels under it are synced.
4. **Snapshots** (4 slots, keyed by rectangle; async `GX_CopyTex` of the EFB, RGB565) are taken
   when EFB content is about to be lost: at a present, before the drawing moves to another
   buffer, before a display-geometry change (GP1 05-08), and on demand for a read of live.
   GX runs its command stream in order, so a copy sees the EFB as it was when queued.
5. **`efb_sync(rect)`**: each stale tile is filled from the newest snapshot taken after its
   last GX write (live is copied now if the tile is live's), then marked in sync; the
   texture cache is invalidated over what was written.

Snapshots are RGB565, not RGB5A3: RGB5A3 keeps only 4 bits of colour for a pixel whose EFB
alpha is not full, and this EFB's alpha varies (Ape Escape's pause came out speckled).
`psxVuw` is little-endian on this big-endian CPU: write it with `PUTLE16`.

Bit 15 (STP / mask) of a synced pixel: with `GxMask` 0 (default) OpenGX keeps it in the EFB
depth (`deps/opengx/gc_gl.c` `OGX_ZC_SET`), each snapshot gets a Z8 copy beside the RGB565 one,
and `efb_sync` takes the bit per pixel from it (Docs/STP_BIT15.md sections 11-12). With
`GxMask` 1 each tile keeps the bit the last GX primitive over it would have written on a PS1
(`efb_stp`): set by a set-mask (E6) draw or a textured one (the texel's STP, assumed set),
clear for an untextured one. CTR draws its last frame back semi-transparently for its intro
blur; a PS1 blends only texels with bit 15, and with the stale 0 the blur came out opaque and
fed back into stripes (2026-10-18).

## Policy

Tile stamps are always kept. Snapshots at presents (one GX copy per frame) are kept when:

- the game has `AUTO_FIX_VRAM_READBACK` in `database.c` `gpu_game_fixes[]` (Spyro, Ape
  Escape, Dino Crisis 2, Vagrant Story);
- or, by default ("auto"), from the first read that needed a snapshot it did not have. A
  read of the live buffer never needs one.

`AUTO_FIX_EFB_SYNC_OFF` turns the sync off for a game. The setting `EfbSync` (0 auto,
1 always, 2 never) overrides both, for testing: an A/B of `EfbSync=2` against `0` in one
chain measures the cost.

## Adding a per-game fix

A line in `database.c` `gpu_game_fixes[]` with the game's IDs and the flags, and a flag in
`database.h` if none fits. The GPU plugins read `dwActFixes`.

## Measured (2026-09-27, Dolphin, debug build)

- Ape Escape pause: the frozen game (was the blue clear). vramio `C1 0,0 384x240 345 0`:
  345 GX tiles, none missed; `C2 ... 95 0`: 95% of the rect written (the rest is past the
  368-pixel display).
- Spyro pause: live EFB copied (`C2 ... 1`), the tinted frame as before.
- FF7's opening (uploads the screen with the CPU and draws over it): 0.64 against 0.69 with
  `EfbSync=2`. Its re-uploads now keep the GX pixels they used to overwrite with stale VRAM.
- Crash 3, Gex, MediEvil (headstone text, off-screen path): unchanged.
- Not verified: Dino Crisis 2 and Vagrant Story (not on the test card). Their old special
  cases (the DC2 move strips, the "rebuild from snapshot" upload path) are replaced by the
  general model.

## Logs

`vramio.log`: `C1` a = tiles GX drew in the rect, b = tiles no snapshot could fill; `C2`
a = % of the rect written, b = 1 if live was copied; `C4` per present, a = snapshots on,
b = a read has missed. See `.claude/skills/wiistation-diagnostics/references/probes.md`.

## When a frame is presented (non-interlaced)

A non-interlaced frame goes to the TV at a GP1 05 display move (`updateDisplayGl`, only
when `iDrawnSomething` is not 0), or at a vblank after a full-screen upload. Two holes,
both found on Ape Escape (2026-09-27) by comparing with the soft plugin's frame dumps:

1. **Tile-only frames.** The tile primitives (60-7F) did not set `iDrawnSomething`, so a
   flip after a frame of tiles only was not presented: Ape's fades (white tiles over a
   frozen frame copied with GP0 80) froze, then cut to black. Tiles now set 0x10.
2. **Drawing into the displayed buffer.** P.E.Op.S. puts the EFB at the previous display
   position (the buffer not on screen). Ape's CRT turn-off and its WARNING / "Capture N
   monkeys" screen stop flipping and draw into the buffer on screen: every primitive was
   outside live, drawn into VRAM in software, and never shown. `SetOGLDisplaySettings` now
   sets `bDrawFrontBuffer` when the drawing area starts in the displayed buffer and not in
   the other one, and puts the EFB there; `GL_GPUupdateLace` then presents each vblank that
   drew (0x11), as a CRT shows VRAM.

vramio `F1` (per vblank) and `F5` (per GP1 05) show the decision: display, draw offset,
`iDrawnSomething`, and why a present happened or did not.

## Open

- Re-uploads of the live buffer copy its GX pixels out and back in (FF7's cost above); the
  upload could skip tiles the EFB already holds.

## Partial captures (2026-10-19)

A read that needs pixels GX drew since the last capture used to copy the whole EFB (640x480 RGB565, plus a Z8 copy with GxMask exact): about 1 ms of GP time on a Wii. CTR's Tiger Temple samples 1-3 pixel spots of the frame it is drawing as textures, about 50 times a frame: 62,000 full copies in 7000 vblanks, 61 s of a 143 s run, 0.63x on the bench Wii. `efb_capture_rect` now copies only the stale tiles' EFB area (8x4 aligned, any VRAM-to-EFB scale: it keeps live's mapping moved to the copy's corner) into a buffer of its own; the merge takes those tiles from it. `efb_live_saved` is not advanced, so what it did not copy is still captured when read. A read wider than a quarter of live keeps the full capture. Also: live is captured only when the read rect itself meets live in a stale tile (GX draws are clipped to live). Bench Wii, Tiger Temple first 7000 vblanks, deep build: GxMask Accurate 0.63x -> 0.92x (GPU 49% -> 24%), Fast 0.74x -> 0.92x; guest identical (vsig), VRAM differs in 271 px by 1-2 colour steps (pixels taken at the read instead of an earlier copy); bit15_oracle unchanged. Debug perf.log: `efbcapby:` (full captures per caller, `mini=` partial ones).

## Entering front-buffer mode (2026-10-29)

A third hole in the same present logic, found on Ape Escape's CRT turn-on (the lab, the
"Capture N monkeys" screen): the game draws the new scene into the hidden buffer
(`vramio F1 0,0 0x256`, display (0,0), draw offset (0,256)), then draws thin textured strips of
it, 4 lines high and wider every frame, into the DISPLAYED buffer (soft: a line, then the
picture growing). The strips sample the hidden buffer as a texture (`C1 0,256 193x256`). The EFB
held the hidden buffer's picture and `iDrawnSomething` (0x15) still said "drawn", so the first
vblank in front-buffer mode presented the whole scene (`F1 0,0 0x0 21 12`: 4 = presented) before
the first strip finished its frame.

`FrontModeEnter` (gpuPrim.c) fixes it at that vblank (gpuPlugin.c, before the present test).
Three conditions, each measured against a game that has the first two and not the third:
- `SetOGLDisplaySettings` saw `bDrawFrontBuffer` turn on with the display and previous display
  apart and `iDrawnSomething & 0x11` (`bFrontEnterPending`). Alone: Bomberman, 300 times in 4000
  vblanks (it draws into the displayed buffer, then flips).
- A texture a third of the display or more was taken from the hidden buffer since the last
  present (`bHiddenRead`, `efb_sync_texture`): the picture is source material, not a frame.
  Without it: Spyro 3's cinematics (a 256x40 strip of the hidden buffer), where Soft is nearer the
  picture from before the fix than after it.
- The drawing area is still in the displayed buffer at the vblank (FF7 had moved it back).
Then it keeps the hidden buffer's GX pixels (`efb_before_geometry_change`), clears
`iDrawnSomething` and uploads the displayed buffer from VRAM into the EFB. Probes: `uplret:
front_enter=`, vramio `F9` (x,y display; w,h previous display; a = iDrawnSomething; b = drawing
area y0). Shown from 5.12 at least (every older DOL tried had the full picture at once).
Same pictures as before in Spyro, Crash 3, FF7, CTR, Crash Bash, Gex 2, Bomberman, Vagrant Story,
Spyro 3 and MediEvil (`front_enter=0`; `scripts/chains/ape_front_reg.txt`, `ape_front_reg2.txt`);
in Ape Escape 4 events: the lab and the briefing turn-ons (10 frames each) and two single frames.

### The rectangle before the line (2026-11-01)

The first strip of the turn-on is a line of no width (Ape's lab: two quads at x 192, 4 lines
high; the plugin's bounds add one pixel each way, `F6`: 1x5 at (192,382)). It is drawn at the
vblank BEFORE `FrontModeEnter`, when `SetOGLDisplaySettings` has already moved live to the
displayed buffer but the EFB still holds the hidden buffer's picture. `efb_gx_draw` stamped the two
tiles it touches (16x32), and `FrontModeEnter`'s `efb_before_geometry_change` captured the EFB as a
snapshot filed under the DISPLAYED buffer's rectangle: the upload's `efb_sync` then filled those
two tiles of VRAM from it (vramio `F7`, probe: tiles (192,368) and (192,384) from slot 1, g.y 256)
and sent 16x32 pixels of the lab scene to the TV, a 36x60 rectangle in a frame dump, 3 vblanks
before the line. `efb_live_foreign` (set in `efb_gx_draw` when live changes under
`bFrontEnterPending`) and `efb_front_enter_drop` (called first in `FrontModeEnter`) drop the stamps
made since, and live's unsaved mark: VRAM, which the upload sends next, is the truth for the
displayed buffer. The hidden buffer's own GX pixels were already captured, under their own
rectangle, when live changed. A strip drawn in that window is lost from the EFB either way (the
upload replaces it); each strip covers the one before it.
