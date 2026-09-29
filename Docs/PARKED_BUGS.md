# Parked bugs

Bugs that were investigated and then stopped on purpose. Each entry says what is known,
how to reproduce it, what was tried, and what to do next, so that work can start again
without doing the investigation a second time.

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
same vblank, as with gpulib. It is more correct, but it moves the timing of every game:
it needs an eleven-game A/B and the user's recordings before it goes in. The per-command
cost from `GL_GPUdmaChain` (gpulib's `gpu_timing.h`) alone changed nothing visible.

**Tools that found it.** A GP1 05/08 log per vblank (a temporary probe in psxhw.c), one run
per plugin, diffed; then `wsx.sh build debug all` and `trace <vblank>` in the input script.
An episode is 16 presents: the run must go on long enough to finish it, or the report
prints the previous episode (its `vblank` then looks wrong).
