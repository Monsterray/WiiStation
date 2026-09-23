# Parked bugs

Bugs that were investigated and then stopped on purpose. Each entry says what is known,
how to reproduce it, what was tried, and what to do next, so that work can start again
without doing the investigation a second time.

---

## FF7: one green frame at the start of the opening video (OpenGX)

Parked 2026-09-23. Commits of the investigation: `3c8a6dc` (probes).

**Symptom.** New Game in Final Fantasy VII (Disc 1): one frame of green blotches at the start
of the opening star video. It is the video's first 24-bit picture displayed in 15-bit mode.

**Reproduce.** The user's recording `scripts/autoinput/final_fantasy_vii_1_play.txt` (commit
it first if it is still untracked). A chain of 2400 vblanks with every frame dumped:

```
CHAIN
2400 sd:/wiisxrx/final_fantasy_vii_1_play.txt PadAutoAssign=1
sd:/wiisxrx/isos/Final Fantasy VII/Final Fantasy VII 1
Final Fantasy VII [U] [Disc 1] [SLUS-94163].cue
```

`bash scripts/wsx.sh chain NAME FILE --secs 500 --env FRAMES_DUMP=True --env KEEP=100000`,
then look for a frame where green covers more than 1% of the picture (g > 100, g > r + 60,
g > b + 60, below the 80-pixel overlay). It is dump frame 958 (present ~859, vblank 1991).

**What is known.**
- Not caused by the 24-bit video colour change: `FmvColour=0` shows it too.
- The display commands are timed correctly. All GP1 05/08 writes land in the vblank
  (trace field x1 = 240), and the present after them shows the field a TV scans next.
- The game itself shows the frame. At vblank 1990 (OpenGX) it writes GP1 08 = 0x01 (15-bit)
  and GP1 05 = display start (0,0), where the top buffer already holds 24-bit data; GP1 08 =
  0x11 (24-bit) comes three vblanks later. PsyQ `PutDispEnv` writes both together, so the
  display environment the game flips to still has `isrgb24 = 0`.
- The software renderer (`gpuPlugin=1`, gpulib) does not show it: there the game writes
  GP1 05 (0,0) and GP1 08 = 0x11 in the same vblank.
- The difference is GPU timing seen by the game. With gpulib the game reads GPUSTAT busy
  (0x50......) and then idle (0x54......) around each drawing DMA; with OpenGX it almost
  never sees busy. `GL_GPUdmaChain` returns the list length in words; `LIB_GPUdmaChain`
  returns a cost in CPU cycles (`gpulib/gpu_timing.h`, charged per command in
  `SoftGPU/gpulib_if.c` `do_cmd_list`), and psxdma.c sets `psxRegs.gpuIdleAfter` from it.
  Block DMA (mem2vram) is timed the same for both plugins.

**Tried.** OpenGX returning gpulib's per-command cost from `GL_GPUdmaChain` (a `GpuTiming`
setting, reverted): the green frame moved by one dump frame (958 to 959) but stayed.

**Next.**
1. Match gpulib's timing completely: also its slow list walking (`progress_addr`, break
   after 512 cycles, `gpuInterrupt()` continues the chain -- gated to `newSoftGpu` in
   psxdma.c today) and the GPU busy bits. Then check the frame, and run all eleven games
   (`scripts/chains/all.txt`) against a baseline: a timing change moves every game, and can
   put the user's recordings out of step.
2. Or hide it in OpenGX: detect a displayed buffer that holds 24-bit data in 15-bit mode
   (MDEC slices 24 halfwords wide, not 16) and repeat the previous frame.

**Tools that found it.** `wsx.sh build debug all` (primitive trace); `trace <vblank>` in
the input script (an episode is 16 presents: arm it early enough to finish before the run
ends); `dump <vblank>` for `vram.bin`. The trace needs `perf_present_tick()` per present,
which only OpenGX calls; to trace gpulib add a call in `LIB_GPUupdateLace`.
