# Parked bugs

Bugs that were investigated and then stopped on purpose. Each entry says what is known,
how to reproduce it, what was tried, and what to do next, so that work can start again
without doing the investigation a second time.

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
