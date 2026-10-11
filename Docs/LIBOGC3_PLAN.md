# Moving to devkitPro libogc 3 -- plan (for review, 2026-10-11)

Today: Extrems' libogc2 2.1.0 + devkitPPC r41-2 (GCC 12.1, newlib 4.2). Target: devkitPro libogc
3.1.0 (tuxedo kernel) + devkitPPC r50-1 (GCC 16.1, newlib 4.6), both already installed beside the
old ones. Order agreed (Monty, 2026-10-11): this first, then the menu framework, then cheats.
Research 2026-10-11 by five read-only agents; probe script and logs in
`%TEMP%\claude\ogc3probe\` (re-runnable).

## How far away it is (measured: compile-only probe of all 114 sources)
49 files compile as they are; 65 fail with 92 errors, nearly all mechanical:
| Kind | Count | Fix |
|---|---|---|
| `wupc/wupc.h`, `wiidrc/wiidrc.h`, `SDL/SDL.h` missing (fatal; hide later errors) | 32 | the libraries (below) |
| `TRUE`/`FALSE`/`BOOL` gone from gccore's chain | 35 | include `gcbool.h` in one compat header |
| `frame_context`, `EX_DSI`, `__exception_sethandler`, `DSISR`/`DAR` macro clash (vm.c, ws_crash.c, hprof.c) | ~10 | tuxedo exception API (PPCContext, PPCExcpt*) -- the real work |
| `ogc/lwp_objmgr.h` (hprof.c, perf_prof.c: debug probes) | 2 | tuxedo thread listing |
| `POLL*`, `if_config` 5 args (lab_net.c, SMB) | 7 | small edits |
| `WPAD_LED_1`, `CARD_WORKAREA`, `syswd_t` | 4 | renamed constants/types |
About 80% of the API we use is unchanged by name (threads, IOS, DI, net, AESND, VIDEO/GX, PAD/WPAD,
CONF, LC). A compat header lets one tree build on both libraries while the work goes on.

## Libraries
| Library | Plan |
|---|---|
| libfat, SDL 1.2, aesnd, iso9660 | pacman's libogc3 builds (libfat-ogc, wii-sdl); keep our patched iso9660.c |
| Lightrec, GNU Lightning, libsmb2, libchdr (+lzma/zstd/zlib), OpenGX, HBC agent | ours, rebuilt with GCC 16 (agent: `AGENT_TUXEDO`, which it already has) |
| libwiidrc (Wii U GamePad) | build from source (vbagx / usbloadergx carry `wiidrc.c`) |
| **libwupc (Wii U Pro Controller)** | **no source here, no libogc3 build** -- decision 1 below |
| dfsound's AESND driver (`cube.c`) | written against libogc2's AESND (~330 lines differ): re-verify with `wsx.sh audio` and the DSP_LLE run |

## What could cost resources -- needs your call before the switch
1. **Thread switches (CPU, unknown size).** Tuxedo saves a 704-byte context with all FP and
   paired-single registers; libogc2 saves FP lazily. WiiStation switches threads constantly
   (audio, CD read-ahead, SMB pump, HID). Could be negligible or a few %. Measured in step 4.
2. **Filesystem (SD/USB/CD-image read speed, unknown).** libogc3's libfat is FatFs under libdvm,
   not libogc2's own code. Same cache size by default. CHD/ISO streaming and save states depend
   on it. Measured in step 4.
3. **Lightrec's fast memory map (CPU, potentially large).** vm.c programs the MMU directly
   (SDR1, segment registers, its own DSI handler) so Lightrec can map PS1 RAM; tuxedo may own the
   MMU. If it cannot be kept, Lightrec falls back to checking addresses in its code: a measurable
   slowdown in every game. This is the go/no-go item; step 2 answers it first.
4. **Baselines.** GCC 16 may round floating point differently in places: frame/audio comparisons
   against old runs may not be byte-exact, and recordings may need a re-check (not a resource
   cost; a test cost).
Memory: no regression found (same 144 KB stacks, same GX FIFO, text ~3% smaller; DOL size
unmeasured).

## Steps
All in a worktree on `try/libogc3`; main keeps shipping on libogc2 until step 5 passes.
1. **Compat header + build switch.** `Gamecube/ogc_compat.h` (gcbool, renamed constants,
   POLL*, if_config); `scripts/build.sh` gets `LIBOGC=2|3` choosing toolchain, libogc and libs.
   Main still builds byte-identical on libogc2.
2. **The MMU question first (go/no-go).** Port vm.c's DSI handler and segment setup to tuxedo; a
   bench run of one game with Lightrec's map. If tuxedo will not allow it, stop and bring the
   numbers to Monty.
3. **The rest of the port.** Exceptions in ws_crash.c/hprof.c, debug probes, libraries (wiidrc
   built, wupc per decision), rebuilt deps, then a full link.
4. **Measure, both builds, same bench Wii:** chains (fps, %speed, hprof per game), audio
   (DSP and CPU paths), SD/USB/SMB read speed, save/load state time, menu, Co-Op/HID/multitap,
   DVD; ps1-tests; replay3/replay_rest; statecheck; Quit (the open freeze). A table old vs new.
5. **Decide and switch.** If nothing regresses beyond noise (or Monty accepts what does): default
   `LIBOGC=3`, update AGENTS.md, setup-devkitpro-windows.sh, Docs/BACKENDS.md; libogc2 kept
   building for one release, then removed.

## Decided (Monty, 2026-10-11)
1. libwupc: downloaded at build time by a script (source not kept in the repo), then built.
2. If the MMU map cannot stay: stop, show the results and the ways to soften the cost.
3. Re-baselining: decided after seeing how large the differences are.

## Decisions for Monty (as first asked)
1. **Wii U Pro Controller (libwupc):** (a) download its source (FIX94's libwupc, GPL) and build it,
   (b) keep the old prebuilt `libwupc.a` (may not link against libogc3), or (c) drop Wii U Pro
   support. Recommendation: (a).
2. If step 2 shows the MMU map cannot stay: stop the move, or accept the Lightrec slowdown?
   (Numbers first.)
3. OK to re-baseline recordings/frame dumps on the new toolchain where only FP rounding differs?

## Results
**2026-10-11, bench Wii, scripts/chains/three.txt (60 s each), release then debug builds.**
Both builds ran all three games and went back to HBC; no crash. Load = % of the frame budget
used (debug perf block); A = libogc2, B = libogc3:
| Game | load A | load B | B-A | notable |
|---|---|---|---|---|
| Spyro the Dragon | 55.04 | 54.43 | -0.61 | psx +0.27, gpu -0.21 |
| Crash Bash | 61.70 | 61.19 | -0.50 | gpu -0.33, cd -0.28 |
| Micro Machines V3 | 51.54 | 51.64 | +0.10 | gpu +0.53, cd -0.15 |
No slowdown beyond noise; the CD (file reading) column is a little lower on libogc3. DOL
+139 KB (3,564,800 vs 3,426,240 release).
Still to check (step 4): DSP audio path, SMB/USB reading, save states, Co-Op/HID/Wii U pads,
DVD, ps1-tests, replay3/replay_rest, statecheck, Quit, Wiimote LEDs (no WPAD_ControlLed in v3).
**dsp_ab (CPU and DSP audio paths), bench, debug builds:** 8/8 games on both, no crash.
Load B-A: Ape Escape CPU -0.57, DSP -1.20; FF7 CPU -0.23, DSP -0.55. The AESND (DSP) path works on
libogc3's AESND. Audio dropout counters not yet compared (run_summary on both runs).
**ps1-tests in Dolphin, debug builds (dbgab_ps1_A/B):** tty.log byte-identical and all 10 per-test
VRAM dumps identical between libogc2 and libogc3 (the magenta/noise screens some tests draw are
the tests' own output on both). Same known gaps against a real PS1 on both.
**Dolphin, debug pair:** replay3 guest counters identical at every report vblank (both games);
statecheck3 all 3 games match with identical fingerprints on both. No floating-point difference
seen anywhere so far, so no re-baselining is needed yet (decision 3). Note: replay_check.py found
no reports in these debug logs (no gpupres: line in that build); compared by wall: vblanks instead.
