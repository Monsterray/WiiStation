# Parked bugs

Bugs that were investigated and then stopped on purpose. Each entry says what is known,
how to reproduce it, what was tried, and what to do next, so that work can start again
without doing the investigation a second time.

---

## CTR after Spyro in one Official chain: Lightrec hangs or crashes in the race -- FIXED 2026-10-24

**Cause: MEM1 ran out.** libogc v3's malloc does not spill into MEM2 (arena2 stays at 0), so a
full MEM1 returns NULL, and GNU Lightning writes through what `jit_alloc` returns without a check:
the hang in `_jit_optimize` and the DSI were Lightning working in memory it never got. On the Wii,
CTR after Spyro had malloc at 6.9 MB of a ~7.25 MB heap. Lightrec's blocks, IR and MIPS copies,
jitq's pending jobs (up to 565) and Lightning's compile state all grew in MEM1 with the game.
**Fix (bounded memory):**
- Lightrec and Lightning allocate from a 4 MB TLSF pool in MEM2 taken once (`lightrec.c`,
  `lr_pool_*`; a full pool falls back to the MEM2 heap, counted in perf.log `lrmem: fallbacks=`).
  Not the general MEM2 heap: it is a first-fit list, and Lightrec's thousands of small blocks
  slowed every texture allocation (Tiger Temple GPU 20.9% -> 34.6%; with the pool 22.3%, 0.98x).
- Lightrec's own part is capped at 3 MB (`LIGHTREC_DYN_MAX`, `lightrec_bound_memory()`: frees
  outdated blocks by age, as a full code buffer does).
- rcheevos, curl, Mbed TLS and the badges allocate in MEM2 (3cf3dcf6, a5846dda); the XA ring
  (375 KB), the chain list (75 KB) and the overlay's EFB save buffer (1.4 MB) moved there too.
  The overlay buffer in MEM2 costs nothing measurable (Tiger Temple GPU 22.2% vs 22.3% in MEM1).
**Check:** perf.log `gameclose:` after every game; `chain_table.py` prints a `memory:` line and
warns when malloc grows between games, Lightrec keeps memory after a close, or MEM1 goes under
512 KB. Bench Wii: the Official Spyro -> CTR chain finishes at 0.98x (lowest MEM1 1600 KB,
`final2_official`); the six-game `chain_memory.txt` closes every game at malloc 5904 KB with
Lightrec at 0 (lowest MEM1 1600 KB, `final_chainmem`).
Dolphin: a game run twice in one chain matches itself in every vsig field at every vblank.
The record of the hunt:

`scripts/chains/ach_official_bench.txt` (Spyro 2400 vblanks, then CTR's recorded Tiger Temple race,
Achievements=3, spectator) on the bench Wii. Spyro is fine and CTR's set loads (117 achievements,
58 boards, achdump at CTR vblank 1500), then:
- 30d2dd2c: hang, main thread in GNU Lightning's `_jit_optimize` (from `jq_compile_now`) at CTR
  vblank 3717; an earlier build hung at 1265 with the compile thread in `_jit_realize`.
- 3cf3dcf6 (rcheevos and TLS in MEM2; MEM1 free after Spyro 416 -> 772 KB): DSI in Lightrec
  code (pc 80b01690, dar 0844c780, lr in lightrec_execute) at CTR vblank 4204.
CTR alone finishes the same race (tt_spots_with/without, 2026-10-24), and a 3000-vblank CTR after
Spyro passes. In Dolphin the same chain shows wild memsets (pointers holding text) while CTR loads,
in some builds only. Suspects: what the chain carries from Spyro to CTR in Lightrec (the kept state,
jitq), and MEM1 left for Lightning. Next: the same chain with Achievements=0 on the Wii (separates
Official from the chain), then JitDelay=0 / no jitq.

## 5.17.0 debug build: start-up stalls in the first SD init on the bench Wii -- FIXED 2026-10-24

**Symptom.** The debug build of eb50ad7a and later never reaches its first log line on the
bench Wii; Dolphin (also MMU=1 CACHE=1) starts it fine. The boot guard (ac0fdbdc) caught it:
`hbc.py crash` -- "boot hang at settings (SD/USB mount)", main thread blocked in
`__ipc_syncrequest` <- `IOS_Ioctlv` <- `__sd0_initio` (libogc wiisd.c): IOS never answers the
first /dev/sdio ioctl. HID's thread (prio 70) sleeps in HIDRun beside it. The guard returned
the Wii to HBC itself every time (code 0x85).
**Bisect (all guarded, `scripts/chains/lab_check.txt`, 2026-10-24):**

| build | start-up |
|---|---|
| ef64c4bd + guard, no HTTPS (worktree path with a space drops it) | passes |
| the same + 1.3 MB of unused read-only data | passes |
| main (afc07c50) without the HTTPS stack | passes |
| main with HTTPS, the clock-learning change (ra_transport.c, tests/achievement_clock.c/.h) put back to ef64c4bd | passes |
| the same + 4 KB of never-run code (.text end past the bad builds') | passes |
| main with HTTPS and the clock change (5 builds, 7 runs) | stalls in SD init |

None of the clock code runs at start-up; the libogc SD/IPC buffers have the same alignment in
good and bad builds; no global symbol is replaced. With LTO the change recompiles about 20
unrelated functions differently (VI I2C, wiiuse LEDs, DSP callbacks, DVD start-up, zstd).
**Cause: LTO and -Wl,--wrap.** Two more guarded runs: main built without LTO starts; main with
the SD card mounted before HIDInit still stalls (at that earlier mount, no HID thread), so HID is
not it. Main with LTO but the four files that define `__wrap_` functions (hb_probe.c: every
IOS_* call; wpad_leds.c: wiiuse_set_leds; ogc_compat.c: VIDEO_*; ws_crash.c) compiled
`-fno-lto` starts, as does the clean build with that rule in both Makefile_Wii files
(lab_check_fixed). GCC's LTO does not handle --wrap reliably; which builds broke depended on how
LTO partitioned unrelated code. Keep any new `__wrap_` definition in a file on that -fno-lto list.

## CD audio from PBP, CHD and CBN starts 24 ms late (audio output) -- PARKED 2026-10-21, to fix

**Status (2026-11-03): still open.** No commit since the 2026-10-21 park touches the CD-DA start offset (only CD-image work: 29bb8fd0 `.mds` and read-ahead, 46119536 CHD decode-ahead; neither mentions it). Not re-measured today.

**Symptom.** Frogger's CD-DA music (track 2, from 6.17 s into a 90 s idle run) plays 1152 output
samples (24.0 ms at 48 kHz) later from a .pbp, .chd or .cbn than from the .cue. The user wants it
fixed. It is not audible as a gap (see the mechanism), only as music 24 ms late against the picture.

**Reproduce (Dolphin, debug build, `--fast`, so SoundRateControl=0 SoundTempo=0).**
`scripts/chains/pbp_frogger_cue.txt`, `pbp_frogger_pbp.txt`, `pbp_frogger_chd.txt`,
`pbp_frogger_cbn.txt` (5400 vblanks, `none.txt` input, `--env AUDIO_DUMP=1`), each as its own run;
`wsx.sh audio pbp_frogger_cue pbp_frogger_pbp` -> "first difference at frame 295945 (6.166 s)".
Runs kept: `.runs/pbp_frogger_cue`, `_cue2`, `_pbp`, `_pbp2`, `_chd`, `_cbn`.

**What is measured.**
- Deterministic: PBP twice gives the same 7,463,227 differing samples; CUE twice is identical.
  CHD and CBN differ too (other counts: their shift is similar, not measured exactly).
- The data is not the cause: the PBP's track 2 bytes, read the way ISOreadCDDA reads them
  (image sector 94444 = INDEX 01, start_offset = index0 x 2352 with the PBP's absolute MSF), are
  byte-identical to the CUE's `Track 2.bin` from INDEX 01 (offset 150 x 2352). Every PBP/CHD sector
  decodes to the original (Docs/TEST_REGIMEN.md, lossless).
- The guest is identical: vsig ram/cycle/pc every vblank, and since 29bb8fd0 the CD hash too.
- The SPU side is identical: `atrace.log` without the wall-clock field differs only in the S
  heartbeats' `f` (output driver busy) bit at 5 points (c=23592971, 30971037, 32440326, 41287695,
  78899987).
- The PBP output is the CUE output shifted: B[t + 1152] == A[t] with zero error at 6.3, 12, 19, 30,
  45, 57, 70 and 85 s, and the whole shift is extra silence before the music: both dumps are silent
  from 3.0 s, the CUE for 151,945 samples, the PBP for 153,097 (+1152). Nothing after the music
  starts is cut or doubled.
- `rate:` lines: ppm=0 (rate control off in --fast), prefill 5760 for CUE/PBP/CHD, 6912 for CBN.
  `limitwait:` and `limit: debt_max` are the same within noise.

**Mechanism (most likely; confirm first).** The compressed formats cost Wii CPU time on the
emulator thread when the CD audio starts (the first CD-DA reads decode a block/hunk the decode-ahead
did not have: a new zone of the disc). With rate control off the output stage cannot pull latency
back, so a moment when the output found nothing to play (an underrun during the silent load) adds
silence once and every later sample is that much later. With SoundRateControl=1 (the default for
players) the rate control should pull most of it back over a few seconds, so in normal play the
offset may already shrink: measure that too (two normal-speed runs disagree sample by sample,
so compare the music onset only).

**Next steps.**
1. Prove the underrun: a probe counting output-driver underruns (samples of silence the driver
   inserted) and when, per run; it should read 1152 more for the PBP, at ~6.1 s.
2. Find which read stalls: `cd:`/`chd:` per-read times around vblank 360-370 (6.1 s); `vtl_view.py
   --around 365` on both runs; is it the first CD-DA read (ISOreadCDDA -> cdread_compressed /
   cdread_chd on the emulator thread, a decode-ahead miss on a new zone)?
3. Fixes to weigh: kick the decode-ahead at the CD-DA start position when the game issues CdlPlay
   (the seek target is known before the first read); or decode CD-DA blocks on the thread with the
   play position as the want; or have the output stage not insert silence for an underrun while
   the SPU has produced nothing (pure silence period).
4. Proof bar: `wsx.sh audio` bit-identical between the CUE and the PBP/CHD/CBN runs (with
   SoundRateControl=0 SoundTempo=0), and the same on the bench Wii's music onset.

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

**Status (2026-11-03): still open.** The last rain work is the scaled-merge rewrite 99e58363 (measured 2026-10-24, below); the GPU-only `GX_CopyTex` path in the plan is not built (no commit adds it). Not re-measured today.

**What it is.** The rain drops are opaque 15-bit textured primitives whose texture lies in the
frame being drawn (1-4 pixels under each drop, drawn back to front). Each read is a partial EFB
capture (`efb_capture_rect`: RGB565 + Z8 copy, `GX_DrawDone`), a merge into psxVuw, a texture
rebuild: ~47 a vblank in rain, 185,256 in a 12,300-vblank run. All are needed: `efbexact:`
found something drawn over the read pixels since the last sync in all but 2.
**Where it stands.** After cb5c672f (texture-cache free list) and the scaled-merge rewrite, the
rain stretches run at 17.2 ms a vblank on the bench Wii (debug build, budget 16.7): the rain
adds ~2 ms of render-mode time and ~3 ms of GPU work a vblank, most of it out of limiter slack.
Captures cost 3.5 s a race, of which DrawDone 2.8 s (`efbminitime:`).
**The scaled-merge rewrite (99e58363), measured 2026-10-24** on the bench Wii: one race each,
`debug deep`, main with and without the commit (`.runs/tt_spots_with`, `tt_spots_without`,
chain `tiger_temple_diag.txt`, `perf_windows.py --slow 11717-11764,14037-14097`). Vblanks
14037-14097: 31.9 -> 27.3 ms a vblank (render mode 11.2 -> 7.4 ms); 11717-11764: 20.6 -> 20.5
(no change: that spot is not the merge). Whole race: load 72.5% -> 70.5%, GPU 23.4% -> 20.9%,
speed 0.98x both. Both spots are still over 16.7 ms.
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
same vblank, as with gpulib. It is now the GpuTiming setting (2ed5587, Docs/PERFORMANCE_PLAN.md Part A, G7). It moves the timing of every game:
it needs an eleven-game A/B and the user's recordings before it goes in. The per-command
cost from `GL_GPUdmaChain` (gpulib's `gpu_timing.h`) alone changed nothing visible.

**Tools that found it.** A GP1 05/08 log per vblank (a temporary probe in psxhw.c), one run
per plugin, diffed; then `wsx.sh build debug all` and `trace <vblank>` in the input script.
An episode is 16 presents: the run must go on long enough to finish it, or the report
prints the previous episode (its `vblank` then looks wrong).

## Old PPC dynarec (Core = 2): jumps to address 0 at the start of a game -- PARKED 2026-09-30

**Status (2026-11-03): still open (as far as the log shows).** No commit since 2026-09-30 names this fault. The only `ppc/` change is 485efc54 (2026-10-10), a stop-flag check in `iJump`/`iBranch` (not a jump-to-0 fix); no `Core=2` chain exists in `scripts/chains/`, so it was not re-run. Unverified.

**Symptom.** A chain with `Core=2` on Crash 3 never reaches its first chain end; Dolphin logs
`ISI exception at 0x00000000` (a branch to 0). Found while moving the dynarec's 10 MB into the
MEM2 heap (598aaa3), and **not caused by it**: the build before that change (the same tree
with the fixed RECMEM2 region) fails the same way. Lightrec (the default) and the interpreter
are not affected.

**Where to start.** `wsx.sh chain NAME FILE` with a `Core=2` chain line and `--dol` a debug
build; the old core's recError path and 3778ed3's opcode propagation changes in
ppc/pR3000A.c are the recent edits there. Bisect by transplant (ppc/ only) if needed.
