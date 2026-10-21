# Parked bugs

Bugs that were investigated and then stopped on purpose. Each entry says what is known,
how to reproduce it, what was tried, and what to do next, so that work can start again
without doing the investigation a second time.

---

## FF8 opening and FF7's SCEA screen black: interlaced loads lost to the EFB sync (OpenGX) -- FIXED 2026-10-21

**Symptom.** Final Fantasy VIII disc 1 under OpenGX: "Published by Square Electronic Arts"
showed only faintly during its fades, then black for the Squaresoft logo, the staff credits and
the opening FMV's first frames; Soft showed them all. Found with it: FF7's "Sony Computer
Entertainment America Presents" (the screen before the logos) was black on main too, unnoticed
because `ff7_green` only counts green frames.

**Numbers that pointed at it.** The display is 640x480 interlaced (`uplst1` InterlacedTest 2),
the GPU gets image loads only (single-line `primLoadImage16 0 438 640 1`), 601 uploads asked
and done, VRAM correct at the end of the run (vram_01.png = Soft's) -- yet every upload was
preceded by `vramio C1 0,39 640x402 1000 0` / `C2 ... 97 1`: the EFB sync found 1000 tiles
GX-newer and copied 97% of the rect from the EFB into psxVuw. With no primitive in the trace,
`offsoft: inside=468` (one 20h-7Fh command a present reached the dispatcher) and Soft's
`gpucmd: 62=.../596` named it: one semi-transparent rectangle (62h) a frame over the picture.

**Cause.** Under an interlaced display a CPU load over the screen is not put into the EFB at
once: `CheckWriteUpdateRect` (gpuPrim.c) only widens `xrUploadAreaIL`, and `updateDisplayGl`
uploads it at the vblank. FF8 loads its lines and then draws the 62h rectangle over them.
`EfbBeforeGxCommand` stamps the rectangle's tiles GX-newer than the load (efbSync.inc), but GX
drew it on an EFB that never got the load. The vblank's `UploadScreen` syncs first: the tiles
are GX's, so the EFB (black from the start) is copied over the freshly loaded lines, and the
black is uploaded and presented. Every frame repeated it; the picture never reached the EFB.
Only lines loaded after the rectangle survived, which is why fades showed a little of it.
Soft blends the rectangle with VRAM and has no second copy to go stale.

**Fix.** `EfbBeforeGxCommand` (gpuPlugin.c): before a GX primitive whose bounds overlap a
pending interlaced upload (`bNeedInterlaceUpdate`, not 24-bit), that upload is done first
(`UploadScreen(TRUE)` of `xrUploadAreaIL`, `xrUploadArea` kept), as a non-interlaced load is
uploaded at once. Counter `uplret: il_early=` (deep builds). The upload count does not grow:
the early upload takes the vblank's place (FF8 596 vs 601 calls, FF7 394 vs 394).

**Proof (Dolphin, deep debug DOLs, main e10175af vs try/ff8-interlace-upload).**
- FF8 (`scripts/chains/ff8_boot.txt`, run .runs/ff8il_fix1): publisher, Squaresoft, credits and
  FMV shown; C1/C2 gone from vramio; `il_early=591`; OpenGX frames match their best Soft frame
  outside the overlay (mean abs diff 0.11-0.48 a channel, the rest fade levels on other presents).
- FF7 (`ff7_green.txt`): 0 green frames on both; frames 109-355 differ (the SCEA screen, now
  shown, matching Soft run .runs/ff8il_ff7_soft), every other frame identical; `il_early=253`.
- `stills_check.txt`, `audit_tiger.txt`, `audit_spyro.txt`, Tobal 2 (640x480i,
  `tobal2_boot.txt`): `il_early=0`, `wsx.sh frames` 0 differing of 581 / 2423 / 1633 / 855.

**Bench Wii (deep debug, `scripts/chains/ff8il_hw.txt`: FF8 then FF7, runs .runs/ff8il_hw_main / _fix1).**
The same signature on hardware: main syncs `C1 0,39 640x402 1000` before every FF8 upload; the fix
never does. FF8: speed 0.88x -> 0.98x (wall 45.3 -> 40.9 s for 2400 vblanks), upload 8.63 -> 3.74 s,
sync 5.69 -> 0.02 s, EFB captures 590 -> 0, `il_early=591`. FF7: 1.00x both, 394 uploads both,
upload 0.95 -> 0.43 s, sync 0.56 -> 0.005 s, `il_early=253`. Both runs back to HBC, no crash.

**Reproduce.** `wsx.sh chain NAME scripts/chains/ff8_boot.txt --dol <deep DOL> --fast --secs 900
--env FRAMES_DUMP=True --env KEEP=100000` (OpenGX then Soft, 2400 vblanks each).

---

## CTR Tiger Temple rain: every drop reads the frame back through the CPU (OpenGX) -- PARKED 2026-10-20

**What it is.** The rain drops are opaque 15-bit textured primitives whose texture lies in the
frame being drawn (1-4 pixels under each drop, drawn back to front). Each read is a partial EFB
capture (`efb_capture_rect`: RGB565 + Z8 copy, `GX_DrawDone`), a merge into psxVuw, a texture
rebuild: ~47 a vblank in rain, 185,256 in a 12,300-vblank run. All are needed: `efbexact:`
found something drawn over the read pixels since the last sync in all but 2.
**Where it stands.** After cb5c672f (texture-cache free list) and the scaled-merge rewrite, the
rain stretches run at 17.2 ms a vblank on the bench Wii (debug build, budget 16.7): the rain
adds ~2 ms of render-mode time and ~3 ms of GPU work a vblank, most of it out of limiter slack.
Captures cost 3.5 s a race, of which DrawDone 2.8 s (`efbminitime:`).
**Plan (needs the bench Wii to measure; Dolphin does not model DrawDone or copy cost).** A
GPU-only path for a 15-bit read wholly inside live: `GX_CopyTex` the rect to a small texture,
`GX_PixModeSync()` (libogc gx.h: "after using GX_CopyTex() and before a primitive that uses
the copied texture") and `GX_InvalidateTexAll()`, then draw the drop from that texture with its
UVs mapped to EFB scale -- no CPU wait. PS1 rules to keep: texel 0x0000 is transparent (13.6%
of the sampled texels: colour 0 AND mask 0, the mask is in the Z copy), the texel's bit 15 goes
to the drawn pixel's mask (86% have it); the drops are never semi-transparent. psxVuw stays
stale (tiles stay GX-newer), so a later CPU read still syncs. Proof bar: frames vs the current
build (rain colours may legitimately move, see AGENTS.md), `texcheck`, and one bench race.
**Probes:** `efbmini:`, `efbminimode:`, `efbtexrd:`, `efbexact:`, `efbminitime:` (deep builds).

---

## Gex 3: the LOADING picture after the Eidos logo is black (OpenGX) -- FIXED 2026-10-13

Parked 2026-10-12, fixed 2026-10-13. Found by the per-game sweep (Soft vs OpenGX, the TV
picture at the same vblanks: `.runs/sweep_xfb.sh`, sheets in `.runs/xs_sheets/`). Soft shows
the snowman / Gex LOADING picture at vblanks 750 and 900; OpenGX showed black.

**Cause.** The picture is not drawn: it is two CPU image loads and nothing else for 300
vblanks. The game double-buffers 0,0 / 0,256 until vblank 699, stops with the display at 0,0
(so `PreviousPSXDisplay` = 0,256) and its drawing area in the displayed buffer: the plugin
draws into the front buffer (`bDrawFrontBuffer`, gpuDraw.c), the EFB holds 0,0. At vblank 744
it loads the picture at 0,0 512x256 and at 0,240 512x256 (two copies, VRAM rows 0-239 and
240-479). `CheckWriteUpdateRect` (gpuPrim.c) still assumed the EFB holds the previous buffer:
the 0,0 load only set `bNeedUploadAfter`, which waits for a flip that never came; the 0,240
load was uploaded at the previous display's offset (VRAM rows 256-495 into the EFB, the
picture 16 rows off) and `UploadWentToDisplay` is false for a 256-row jump, so no present was
asked for. vramio showed it in one line: `745-1039 x295 F1 0,0 0x0 2 8` (iDrawnSomething 2 =
upload only, 8 = drawing the displayed buffer, never 4 = presented). The trace at vblank 745
in the parked entry was of the gameplay after 1040: a scheduled trace waits for the next
present, and there was none in between.

**Fix.** While `bDrawFrontBuffer`, `CheckWriteUpdateRect` uploads a load over the displayed
buffer with `UploadScreen(TRUE)` and sets `needFlipEGL`, so the vblank presents it. A load
over the other buffer is not put into the EFB (it holds the wrong buffer); it is kept
(`hiddenLoad`) and uploaded when the EFB becomes that buffer: at a GP1 05 flip to it
(`FrontModeHiddenLoadFlip`, from `updateDisplayGl`) or when the drawing area moves there
(`FrontModeHiddenLoadToEfb`, from `SetOGLDisplaySettings`), before anything is drawn over it.

**Reproduce / check.** `printf 'sd:/wiistation/isos/Gex 3 - Deep Cover Gecko\nGex 3 - Deep
Cover Gecko [NTSC-U] [SLUS-00806].ccd\n' > ab.txt`; `wsx.sh run g3 --dol debug --secs 75 --fast
--autoboot ab.txt --input xfb8.txt --nodump --env XFB_RAM=1` with `xfb 750` / `xfb 900`
lines. After the fix the picture at 750 and 900 differs from Soft's in 178 of 168000 pixels
(the overlay text); before, in 162325.

**Sweep (17 library games, OpenGX, the 8 xfb8 pictures vs the 5.12.1 references).** No new
black or garbage picture; all pictures within 231 changed pixels (the overlay) except Gex 3 at
750/900 (now the picture), Crash Bash at 300 (the SCEA "Presents" logo, black before this fix
and on 81263ce, now as Soft) and Spyro at 150-450 (its logos, already fixed on main by
d9af5fa). Drawing into the front buffer is common (vramio F1 b&8 in 11 of the 17 games), so
the sweep covers the new path.

---

## FF7: one green frame at the start of the opening video (OpenGX) -- FIXED 2026-09-29

Parked 2026-09-23, fixed 2026-09-29. The reproduction below now shows no green frame.

**Came back in 5.12.2 (d9af5fa4, found 2026-10-21 by `git bisect --first-parent` with
`scripts/green_frames.py`), fixed again the same day.** 5.12.2 presented a skipped 24-bit
picture at the next vblank (`iSkipTwo`, for Spyro's logos); in this opening that put out the
half-switched first video frame. Removed: Spyro's logos come through the GP1 03 path, now
deferred like a GP1 05 flip (`displayOnPending`), and Gex 3's LOADING screen through
`bDrawFrontBuffer`. Check: `scripts/chains/ff7_green.txt` (green_frames.py: 0) and
`scripts/chains/stills_check.txt` (Spyro and Gex 3, frames identical to the build before).

**Cause.** Two OpenGX present bugs. The game timing was not the cause.
1. `UploadWentToDisplay` (gpuPrim.c) took FF7's two video buffers for one buffer: they
   start at y 0 and y 232 and are 240 lines high, so they share 8 rows. At vblanks 1989-1990
   the game decodes the first 24-bit frame into the hidden buffer, and each MDEC slice upload
   set `needFlipEGL`. `GL_GPUupdateLace` then presented that buffer in 15-bit mode. Now it is
   the same buffer only if the display start moved by less than half the display (CTR's Sony
   screen moves it by 2 lines).
2. A flip by GP1 05 presented at once, before the GP1 08 = 24-bit that `PutDispEnv` writes
   next. The 24-bit present from the 08 was then skipped, because the first copy to the XFB
   was still in flight. Now GP1 05 marks the present as pending (`flip05Pending`), and the
   next GPU access other than GP1 06-08 presents it.

**Reproduce.** The user's recording `scripts/autoinput/final_fantasy_vii_1_play.txt`, 2400
vblanks (`.runs/ff7_green.txt`), every frame dumped:
`bash scripts/wsx.sh chain NAME FILE --secs 500 --env FRAMES_DUMP=True --env KEEP=100000`,
then look for a frame where green covers more than 1% of the picture (g > 100, g > r + 60,
g > b + 60, below the 80-pixel overlay). Before the fix: dump frame ~666.

**Found on the way, not changed.** With OpenGX the game never saw the GPU busy during a
block DMA upload. psxdma.c sets the core's nBUSY bit ("idle") at the end of each DMA chain
and clears it only at the next chain, and psxHwReadGpuSR can only OR "idle" in. With gpulib
that bit stays 0 and busy comes only from `psxRegs.gpuIdleAfter`. Removing the two writes
(psxdma.c, the `gpuPtr != &newSoftGpu` blocks) made FF7 write GP1 05 and 08 = 24-bit in the
same vblank, as with gpulib. It is now the GpuTiming setting (2ed5587, Docs/GPU_CPU_PLAN.md G7). It moves the timing of every game:
it needs an eleven-game A/B and the user's recordings before it goes in. The per-command
cost from `GL_GPUdmaChain` (gpulib's `gpu_timing.h`) alone changed nothing visible.

**Tools that found it.** A GP1 05/08 log per vblank (a temporary probe in psxhw.c), one run
per plugin, diffed; then `wsx.sh build debug all` and `trace <vblank>` in the input script.
An episode is 16 presents: the run must go on long enough to finish it, or the report
prints the previous episode (its `vblank` then looks wrong).

## Old PPC dynarec (Core = 2): jumps to address 0 at the start of a game -- PARKED 2026-09-30

**Symptom.** A chain with `Core=2` on Crash 3 never reaches its first chain end; Dolphin logs
`ISI exception at 0x00000000` (a branch to 0). Found while moving the dynarec's 10 MB into the
MEM2 heap (598aaa3), and **not caused by it**: the build before that change (the same tree
with the fixed RECMEM2 region) fails the same way. Lightrec (the default) and the interpreter
are not affected.

**Where to start.** `wsx.sh chain NAME FILE` with a `Core=2` chain line and `--dol` a debug
build; the old core's recError path and 3778ed3's opcode propagation changes in
ppc/pR3000A.c are the recent edits there. Bisect by transplant (ppc/ only) if needed.
