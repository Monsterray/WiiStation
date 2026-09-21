# Save state thumbnails — what it would take

The slot picker on the Current ROM screen says "Slot 3" and, since the last round, "Slot 3*"
when that slot holds something. It cannot say *what* it holds. Ten slots per game and no way
to tell them apart is the complaint this would fix.

The file format already has room. Every state file reserves 36,864 bytes right after the
header for a 128×96 RGB picture, and `LoadState` seeks over it. Today those bytes are zeros.
So nothing about the format has to change — this is purely about filling them in and drawing
them.

## Why it isn't a small job

The obvious call, `gpuPtr->getScreenPic(pMem)`, is commented out in `SaveState`. It has been
commented out for a long time, and it would not compile if you uncommented it: `gpu_t` in
`gpu.h` has no `getScreenPic` member. The only implementations anywhere in the tree are
`GPU__getScreenPic` in `plugins.c`, which returns −1, and `GPUgetScreenPic` in PeopsSoftGPU,
whose whole body is `// Unsupported`.

There is also no single place to read the picture from. Each of the three renderers keeps its
own copy of the display geometry — `PSXDisplay` is declared separately in `SoftGPU/gpulib_if.c`
and `PeopsSoftGPU/gpu.c`, and GlesGpu has its own. Without that struct you do not know where
in the 1 MB of emulated VRAM the visible picture starts, or how big it is.

## The three pieces

**1. A way to ask the renderer for the picture.**

Add to `gpu_t` in `gpu.h`:

    void (*getScreenPic)(unsigned char *rgb);   /* 128 x 96, 3 bytes per pixel */

Implement it in all three renderers. The body is the same shape each time: take
`PSXDisplay.DisplayPosition` and `PSXDisplay.DisplayMode`, walk that rectangle of
`psxVuw` (the emulated VRAM, 16-bit BGR555), box-average down to 128×96, write RGB.
Roughly 40 lines per renderer, and the only thing that differs is which `PSXDisplay` it
reads.

**Check this first**: under OpenGX the drawing goes through the Wii's GX hardware, and the
emulated VRAM is only brought up to date when something reads it back. If the VRAM is stale
at save time, the thumbnail will be a frame or two old, or blank. Write the capture, save a
state in a game, pull the 36,864 bytes out of the file with a script, and look at them as a
PNG on the PC. If it is blank under OpenGX and right under the software renderers, that is
the answer, and the fix is to force a readback before capturing.

**2. Call it.** In `SaveState`, where the zero buffer is written, call
`gpuPtr->getScreenPic()` into a 36,864-byte buffer and write that instead. A renderer that
has nothing to give leaves it zeroed, which is what the file holds today, so nothing breaks.

**3. Draw it.** This is the fiddly part, because GX does not take a flat RGB buffer.

- Read the 36,864 bytes from the state file. They start at offset 33: 32 bytes of header,
  then one byte of `Config.HLE`. `StateExists` in `misc.c` already opens a slot's file and
  reads the header; extend it, or add a sibling next to it.
- Convert 128×96 RGB888 to RGB5A3 in GX's 4×4 tile order. There is already an example of
  this in the tree — `glTexSubImage2D` in `deps/opengx/gc_gl.c` does the same tiling for
  the game's textures.
- Hand the result to `menu::Image` (`Gamecube/libgui/Image.h`), which wraps a `GXTexObj`,
  and draw it with `Graphics::drawImage` from `CurrentRomFrame::drawChildren`.

The Current ROM screen has room at roughly x 520–650, y 240–340, to the right of the Load
State and Save State buttons. `scripts/menu_text_width.py` will tell you if that runs into
the spinning logo.

## Cost and order

Piece 1 is the risky one and the cheapest to test on its own, so do it first and prove the
picture is real before writing any menu code. Pieces 2 and 3 are straightforward after that.
Call it a day's work if the OpenGX readback behaves, two if it does not.

## Worth knowing

- `menu::Image` has to keep the converted buffer alive for as long as the texture is used,
  and it must be 32-byte aligned and flushed from the CPU cache before GX reads it.
- Only one thumbnail needs to exist at a time — the one for the selected slot. Rebuild it
  when the slot changes, the same way the `Slot 3*` marker is rebuilt now.
- A state written by an older build has zeros there. Drawing a black rectangle is fine;
  do not treat it as an error.
