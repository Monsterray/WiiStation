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

## Open

- Ape Escape's scene fades cut to black and its WARNING screen does not show: the frames are
  drawn but not presented (the vblank present decision, `GL_GPUupdateLace`, and the
  front-buffer rendering this port disabled). Presenting on every drawn vblank changed the
  game's timing (the present path feeds back into emulation), so it needs its own study.
- Re-uploads of the live buffer copy its GX pixels out and back in (FF7's cost above); the
  upload could skip tiles the EFB already holds.
