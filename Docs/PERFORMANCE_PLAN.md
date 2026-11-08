# Performance plan: GPU, recompiled code and the locked cache

Scope: where WiiStation's time goes and what to do about it. Part A is the GPU and
recompiled-code (Lightrec) plan, written from the eleven-game chained run. Part B is the
locked-cache plan: what belongs in the 16 KB L1 scratchpad, and in what order. Section
numbers, step numbers and item names (G1 ... G7, C1 ... C3, Step 0 ... 5) are per part; a
reference to "section 3" means that section of the part it is in. What is built and measured
for the locked cache is in `Docs/LOCKED_CACHE.md` (the reference, not a plan). Compile-time
(JIT) work, which neither part covers, is in `Docs/BACKENDS.md` (Lightning 339dc53b, jitstall
ac3681e7, jq_unlink 14a1870b). This file was `GPU_CPU_PLAN.md` and `LOCKED_CACHE_PLAN.md`
until 2026-11-04.

## Status (2026-11-04): what is open

Taken from the "Status 2026-11-03 (HEAD e857a9c2)" blocks of each part, which stay below.

Part A (GPU and recompiled code):
- C2, the GTE 32-bit fast paths: open. Probes and the exact divide table exist; the fast paths
  in `gte.c` do not (the upstream GTE accuracy port went in as fe798371).
- C3, the PSX scratchpad in the locked cache: open, hardware only.
- G5, texture-cache invalidation: written and merged; its one open gate is the bench-Wii deep
  run (MediEvil `texsel` 13.2% expected to fall to about 2-3%).
- Section 5 cleanups, all three open: `glparamstate.dirty` (cleared, never read), the unguarded
  `DISP_DEBUG` `sprintf` blocks in OpenGX, `psxNULL` in the `psxCP2[]` table.
- Section 6: input scripts or recordings for CTR, MediEvil, Gex, Frogger, Micro Machines and
  Point Blank (recordings exist for five games); two counters are missing (a code high-water
  mark, bytes flushed).
- Closed since 2026-10-17: G2's fix (fcb53905), G4 display-list reading (measured, no win,
  dropped: 5559205e), the hardware session (section 6).

Part B (locked cache):
- Step 0 (hardware baseline): partly done; whether `PERF_PROF_GPUSPLIT` was on in the
  bench-Wii PMC runs is unverified.
- Step 2 (`tex-tile` region): in, off by default; no gain on the Wii yet.
- Step 4 (SoftGPU frame conversion): not done; judged not worth it on 2026-10-18.
- Step 5 (SPU hot buffers): the gaussian table is in (`spu-gauss`); the channel state is not.
- Closed: Step 1 (b11b30d4); Step 3 (MDEC) merged (187ae768), then taken out (3ecfa64d,
  2026-10-21): it bought nothing on the bench Wii.

## Contents

- Part A: GPU and recompiled code (sections 1 to 7: where the time goes, GPU items G1 to G7,
  recompiled code C1 to C3, locked-cache candidates, cleanup, future work, order of work)
- Part B: Locked cache (sections 1 to 4: what it is, what was measured, the order of work with
  Steps 0 to 5, the verification checklist)

---

# Part A: GPU and recompiled code

Status, end of 2026-09-22 (commits b11b30d and after). Eleven-game means before -> after:
HLE 4.7% -> 3.3% of wall, load 50.0% -> 48.7%.

| item | state | measured |
|---|---|---|
| C1 HLE handlers | **DONE** (b11b30d; interpreter follow-up a77f6ea) | 81-97% of recompiler entries were single blocks; HLE share halved in the 3D games |
| G2 FMV present | **DONE**: split, hash guard, one-pass upload (fcb5390, -33%); 24-bit FMV shown at 24 bits (7cb1671) | Micro Machines 14.4% -> 4.2%. The rest waits for the LC; 24-bit FMV is shown at 15-bit colour -- a fidelity question for the user |
| G1 tiling | **1 and 2 DONE** (block-aligned placement, flush only the rows written); 3 not needed yet | unaligned uploads 83% -> 0; Crash Bash 6.1% -> 4.0% |
| G3 GX state cache | **DONE** (b11b30d) | skips 96% of GX state calls; 0.6-0.7% of wall in Dolphin, more GP traffic saved on a Wii |
| LC | **DONE** (805659f, `Docs/LOCKED_CACHE.md`): `spu-gauss`, `tex-tile`, off by default | frames and audio proven identical; the gain needs a Wii |

**Checked against the code 2026-09-28 (HEAD bc014aa).** Items marked **DONE** are in main;
**PARTIAL** says what is left; unmarked items are open. Next to work on, in the plan's order:
the G4 items,
C2, then the hardware session (the bench Wii and `scripts/wii_lab.py` are ready).

**Status corrections 2026-10-17 (checked in code and runs, after this text was written).**
- G5 and G6 are **written and merged**: G5 = `8245e58` (TEXCHECK oracle) + `dcac40b` (exact
  invalidation incl. the window cache and the 4/8-bit end-texel rule, host
  `tests/texinval_test.c`); G6 = 0fbe257. The G5/G6 sections below are the plan as it was, kept
  for reasoning. G5's evidence since the fix: `.runs/fr3500_texcheck_after` — the 640x480 fill
  no longer drops anything (`fill=491/0/0`), `texk: conv` 3709 -> 590 at vblank 3450; TEXCHECK
  stale=0 at 135980 hits. The one open G5 gate is its bench-Wii deep run (MediEvil `texsel`
  13.2% -> expected ~2-3%; every pre-fix hardware run in `baselines/` predates the fix).
- Of G4, the only open item is display-list reading (`GlesGpu/gpuPlugin.c:2457-2461`,
  0.6-1.5%); the FF7 0.86x and readback items are done.
- C2 (GTE 32-bit fast paths) is open; probes exist (`PERF_PROF_GTE`, `gte: calls=... us=...`),
  the exact divide table exists (`gte_divider.h`, bit-exact, host-tested) — the fast paths in
  `gte.c` do not.
- OPTIMIZATION_PLAN's Phase-1 counter list has since been filled: `jit: pre_blocks= blocks=
  gen_us=`, `jitparts:`, `jitcode:`, `cd_idle_skips`/`gpu_idle_skips`
  (8de5fa1/8610af3, after bc014aa) exist. Genuinely missing: a code high-water mark
  (only cumulative `lightrec_code_bytes`) and bytes-flushed (only texture bytes today).

**Status 2026-11-03 (HEAD e857a9c2).** Open: C2 (GTE fast paths; the upstream GTE accuracy port
went in as fe798371, the fast paths did not), C3 (scratchpad in the locked cache, hardware only)
and the three section-5 cleanups (checked in the code today). Done since 2026-10-17: G2's fix
(fcb53905), G4 display-list reading measured with no win and dropped (5559205e, merged with
7ebdb79a), the hardware session (section 6). Compile time, which this plan did not cover, is in
`Docs/BACKENDS.md` (Lightning 339dc53b, jitstall ac3681e7, jq_unlink 14a1870b).

The sections below are the plan as written, kept for its reasoning.
Evidence: the eleven-game chained run `baselines/chain_all_20260922/` (debug build, every
probe on, Dolphin) and the three-game runs `.runs/three_flip`, `.runs/three_nolog`.

## 1. Where the time goes, eleven games

One boot, 3600 vblanks (one emulated minute) each. Percent of wall. `load` is wall minus the
frame limiter's idle: 100 means the emulator cannot keep up. Every game ran at real speed.

| game | load | psx | gte | gpu | poly | hle | spu | mdec | on screen at the end |
|---|---|---|---|---|---|---|---|---|---|
| Spyro the Dragon | 44.3 | 21.8 | 4.1 | 13.7 | 8.7 | 4.7 | 1.8 | 0 | 3D gameplay (input script) |
| Crash Bandicoot 3 | 59.5 | 27.4 | 4.4 | 14.9 | 9.3 | 7.1 | 10.8 | 0 | 3D attract |
| Crash Team Racing | 44.1 | 20.9 | 2.0 | 6.9 | 3.7 | 6.9 | 6.4 | 0 | title |
| Crash Bash | 60.9 | 25.4 | 3.0 | 16.5 | 12.2 | 8.6 | 8.5 | 0 | sprite-heavy attract |
| Ape Escape | 31.5 | 17.1 | 0.7 | 3.7 | 1.5 | 9.3 | 2.8 | 0 | title |
| Medievil | 53.4 | 20.6 | 0 | 15.8 | 0 | 3.5 | 1.6 | 4.4 | intro, logo screen |
| Final Fantasy VII | 41.6 | 18.2 | 0 | 16.9 | 3.6 | 2.6 | 4.0 | 0 | intro, 2D |
| Gex | 46.0 | 13.3 | 0 | 15.1 | 0 | 1.4 | 1.7 | 9.9 | FMV |
| Frogger | 50.9 | 17.2 | 1.4 | 17.1 | 3.7 | 2.8 | 1.7 | 6.3 | FMV, then title |
| Micro Machines V3 | 62.7 | 16.5 | 0 | 28.3 | 0 | 3.3 | 3.8 | 8.6 | FMV |
| Point Blank | 55.4 | 12.6 | 0 | 22.9 | 0 | 0.8 | 1.7 | 11.2 | FMV |
| **mean** | **50.0** | **19.2** | **1.4** | **15.6** | **3.9** | **4.7** | **4.1** | **3.7** | |

`psx` is recompiled PlayStation code including the GTE; `gpu` is everything behind the GP0,
GP1 and DMA2 registers. Only Spyro has an input script, so most rows are titles, attract
modes and FMV intros. Treat the table as the shape of the problem, not as gameplay figures.

Three cautions:

1. **Dolphin charges instructions, not stalls.** It models no cache misses and its GPU
   finishes instantly in guest time. Items marked *hardware-only* below cost nothing here
   and may cost much on a Wii. `PERF_PROF_PMC` is the instrument for them.
2. **Chains are not exactly repeatable between different chain files.** Crash Bash's HLE
   share was 8.6% as the 4th game of `all.txt` and 4.6% as the 2nd of `three.txt`, with the
   same build. Compare a change only against the same chain file.
3. The debug build links `lightrecWithLog` (asserts on) and the release build
   `lightrecNoLog`. Measured: no difference above 0.1% in any column. The recompiler numbers
   need no correction. (Contrast the two probe defects fixed today, which did need one:
   `c0e9c07`, `b910ed0`.)

## 2. GPU

The GPU split probes (`PERF_PROF_GPUSPLIT`: perf.log `gpusplit:`, `gpuprim:`, `gpudraw:`,
`gpuregs:`, `gpuflip:`) divide the 15.6% into these items, largest first.

### G1. Texture upload: OpenGX's tiling pass
- **Evidence.** `glTexSubImage2D` is 6.1% of wall in Crash Bash, 4.5% in Micro Machines,
  2.9% in FF7, 2.1% in Frogger: about 200 ns per texel. 83% of Crash Bash's uploads take the
  unaligned per-pixel path. The CLUT expansion before it is only 0.2-0.9%.
- **Cause of the volume.** Not cache capacity (`gc=0`). Crash Bash writes animated sprites
  into VRAM 117,000 times a minute, and each write drops the textures it overlaps: 24,919 new
  textures against 24,839 invalidations. The conversions are real work; their cost per
  texel is the lever.
- **Optimise, in this order:**
  1. Place sub-textures on 4x4-block boundaries in `GetCompressTexturePlace`
     (`GlesGpu/gpuTexture.c`) so uploads take `_ogx_scramble_4b_sub`, the block path. The
     position inside the texture page is the plugin's choice, not the game's, so every upload
     can be aligned. This is step 1 of Part B (the locked-cache plan, below); it needs no locked cache.
     **DONE** (b11b30d, `SubTexReserve` in gpuTexture.c)
  2. Flush only the blocks written. The upload calls `DCFlushRange` on the whole texture
     (up to 128 KB) for a rectangle of about 700 texels. The written blocks are one
     contiguous range per row of blocks, so this is one flush per block row. *Hardware-only
     gain.* **DONE** (`flush_block_rows` in gc_gl.c)
  3. Only if the tiling pass is still above 1% of wall after 1 and 2: expand the CLUT straight
     into RGB5A3 tiles. Today the expansion writes 32-bit texels to `texturepart` and the
     tiling pass reads them back. The saving is that re-read, which no probe has isolated yet;
     the CLUT expansion itself is only 0.2-0.9%.
     **Hardware (2026-09-28) changed the picture.** `conv` includes `tile`; the expansion alone
     is 3-40 ns a texel. The tiling had a fixed ~150 us an upload: glTexSubImage2D zeroed 128 KB
     of the semi-transparent scratch before every upload into a texture without a semi copy,
     opaque ones included. **DONE** (0b2876e: only when the upload has semiFlg). Crash Bash on
     the Wii: tiling 473 -> 74 ms a minute, XFB identical. What is left is per texel (MediEvil,
     ~39 ns: whole 35k-texel pages converted 4084 times a minute because VRAM writes invalidate
     them); step 3 or finer invalidation is the lever there.
     **PARKED (2026-10-19).** The 128 KB/RGBA8 premise is stale: the GX texture is already
     `GX_TF_RGB5A3` at 2 bytes a texel (`glInitRGBATextures` allocates w*h*2), and the tiler
     writes 16-bit tiles. The only 4-byte copy left is the CPU-side `texturepart` staging. A
     16-bit staging variant was implemented exactly (low halfword of `LTCOL`, `GL_UNSIGNED_SHORT`,
     2-byte tiler path and LC variant) and frame-diffed clean: Spyro+CTR 279 frames, FF7 489,
     MediEvil 1592, 0 differing pixels. It does not show in the counters: Spyro 600 vb
     `conv_us` 17474 -> 17750 and `tile_us` 13056 -> 13373 (noise), wall 10.1 s both,
     `tex_hit`/`miss`/`resets` unchanged. A 1800-vb survey puts `tile` at 0.12% of wall in Spyro,
     0.09% Crash 3, 0.05% CTR, 0.01% Crash Bash, 0% Dino Crisis 2 and RE3, below G1's 1% gate.
     Reverted; the diff is at `.runs/ci4_staging16.patch` (local, not committed). CI4/CI8+TLUT was
     not attempted: the gate is not met and it needs palette/TLUT handling of its own.
- **Fix (hardware-only).** Every sub-upload starts with `GX_DrawDone()` (`perf_drawdone()`),
  so the CPU waits until the GPU has drawn everything queued: 415 times a second in Crash
  Bash. It is there for correctness (the GPU may still read the texture). Wait only when the
  texture is in flight: a draw sync token per texture, or two copies of a texture that
  changes often. **DONE** (9077042, a simpler rule than tokens: a texture records the
  `GX_DrawDone` epoch of its last draw, and a write waits only if that is the current one;
  the full-slot-table wait is gone. `GX_DrawDone` 12884 -> 7017 in the six-game chain)
- **Verify.** `texk:` tile_us and `ogx:` unaligned fall; the `ogxeq:` texel detector
  (`PERF_PROF_GPU=1`) shows no new mismatches; frame dumps of Crash Bash and FF7 match.
- **Locked cache.** Yes: tile into the LC and DMA the finished blocks out (step 2 of the LC
  plan). Crash Bash is now the case that plan lacked. **DONE** (805659f, `tile_via_lc`,
  region `tex-tile`, off until a Wii shows it helps)

### G2. Presenting a 24-bit (FMV) frame
- **Evidence.** In Micro Machines, GP1 writes are 15.1% of wall, and 99% of that is
  `updateDisplayGl()` started by GP1 05 (display start): 5.6 ms per present, against 185 us
  in Spyro. Point Blank, Gex and Frogger are the same kind of load (`gpu` 15-23% with no
  polygons).
- **Next step.** Split `updateDisplayGl` for a 24-bit display (`UploadScreen`, the 24 -> 16
  conversion, the texture upload, the draw). One more `PERF_TIME` split; it decides whether
  the fix is the conversion loop or the upload. **PARTIAL**: `gpupres:` splits upload, prep,
  capture and vout (b11b30d); the 24 -> 16 conversion and the texture upload inside
  `UploadScreen` are still one number.
- **Optimise (likely).** One pass from 24-bit VRAM to GX tiles; upload only the display
  rectangle; no `GX_DrawDone` in the path. **DONE** (fcb5390: one pass, upload -33% in Micro Machines,
  XFB identical; the upload was already the display rectangle; its `GX_DrawDone` is now the
  conditional one of 9077042)
- **Locked cache.** Yes, the same pattern as G1: convert into the LC, DMA out.
- **Accuracy.** The conversion must stay exact. 24-bit FMV is the fidelity case the user
  named; no shortcut that drops colour depth. **DONE** (7cb1671: an RGBA8 path shows 24-bit
  FMV at 24 bits, setting `FmvColour`)

### G3. Per-draw GX state
- **Evidence.** `gpudraw:` state 0.2-2.8% of wall, vertex format 0.1-1.2%; one OpenGX draw is
  about 1.7 us. Every draw re-sends Z mode, alpha test, blend, texture object and the whole
  TEV set-up, and a semi-transparent draw does it twice. `glparamstate.dirty` exists but the
  code only clears it and never tests it.
- **Optimise.** Keep the last applied key per group (blend, TEV, Z, alpha, texture object)
  and skip unchanged groups. Set the vertex format only when `texen`/`color_enabled` change.
  **DONE** (b11b30d: `ogx_same`, per `GX_Set*` call; vertex format by `ogx_vtx_key`)
- **Risk.** The menu and OSD call raw `GX_Set*`. Invalidate the cache at every entry to PSX
  rendering and at every frame; do not trust it across `GX_DrawDone`, EFB copies or a menu
  visit. Two panel models agreed on this; they disagreed on the size of the gain, so measure.
  **DONE** (`ogx_state_invalidate` at every flip and in `glResetCacheRegion`)
- **Verify.** `gpudraw:` state_us falls; frame dumps identical; a menu visit mid-game and back.
- **Locked cache.** No.

### G4. Smaller GPU items
- **Readback bookkeeping on a flip** (`gpuflip:` will_us): 0.7% in Spyro. Check it after G3.
  The bookkeeping was replaced (75f2796, `efbSync.inc`); its `will_us` probe is now dead and
  should be removed or pointed at `efb_before_geometry_change`. **DONE** (1faf97e: now
  `gpuflip: geom_us`, the time in `efb_before_geometry_change`)
- **Off-screen test** (`OffscreenSoftDraw`), 0.3-1.1%: it computes bounds for every primitive
  to find the few that land off screen. Skip it while the display and draw areas coincide.
  **DONE** (1faf97e: skipped while the drawing area is inside the GX buffer; Spyro -67%,
  Crash 3 -60%, CTR -63%; Crash Bash keeps it, it really draws off screen)
- **Display-list reading**, 0.6-1.5%: word-by-word with a byte swap per word. Low value.
  **MEASURED, no win (2026-10-18, `try/dma-read` @ 84cb091):** staged the DR_NORMAL loop in
  `GL_GPUwriteDataMem` onto C locals (same PUTLE32-clobber pattern as 0bc58e3) and A/B'd
  window-free builds in Dolphin over ff7_dump450 (500 vbl): parse bucket 49.1 -> 63.3 ms
  (**+14 ms, a regression**), every other counter and vsig identical. Spyro parses moved
  the same direction. Re-solving with the read-loop out gives 30-40 ns/word (Spyro 44, FF7
  a bit more) -- close to the plan's 20-40 estimate, and too small a bucket for staging
  locals to win; GCC's -Ofast/LTO schedule was already fine, likely because G3/G4
  store-buffer forwarding makes the PUTLE32 "memory"-clobber reloads nearly free (0bc58e3
  won only because the VRAM loop is a 24-28 M-words ~10% bucket, not this 0.2-0.4% one).
  A dl `perf` window probe was also built (`gpudl: dl_us`): its own cost (~0.3 us per
  window, tens of thousands of windows per run) swamps the loop it times, so a per-window
  probe is unfit for this path; only window-free parse deltas were trusted. Nothing kept.
- **FF7's `FinishedVRAMWrite`**, 5.0% in the first chain: texture-cache invalidation for
  42.6 M words of background uploads. Look at `InvalidateSubSTextureArea` after G1.
  **DONE**, and the cause was not the invalidation (0.2 s, 0 cache entries scanned): the
  debug build formatted two log lines per transfer, and FF7 sends its logos one row per
  transfer (c763530, `gpudeep: cwu_us`). Release builds never paid it.
- **FF7 at 0.86x on the bench Wii** (the only one of eleven games below full speed). Its
  logos are 640x480 16-bit frames: a full-screen upload of 20 ms each on hardware, more
  than a frame. **DONE** in three steps, each exact (XFB identical, on the Wii too):
  one-pass 16-bit upload from VRAM (34413f9), no zeroing and half the flush (dfd5be6), the
  CPU->VRAM loop on locals instead of globals under PUTLE16's memory clobber (0bc58e3).
  Hardware: upload 10.9 -> 6.5 s, VRAM loop 3.7 -> 2.9 s, FF7 0.86 -> 0.93x
  (`baselines/hw_ff7_*`).

### G5. Texture-cache invalidation: exact rectangles (MediEvil, next).
### **DONE** — 8245e58 (TEXCHECK oracle) + dcac40b (exact invalidation, 2026-09-29). See the
### status corrections above for the post-fix evidence and the one open gate (hardware deep run).
- **Evidence (bench Wii).** MediEvil is the costliest game on hardware: GPU 23.6% of wall,
  `texsel` 13.2% (`baselines/hw_chain_all2_20260928`, deep run hw_all_deep). It converts 4084
  sub-textures a minute, ~35k texels each.
- **Cause, measured (Phase 0, be91650: `texinv:`/`texinvr:` in deep builds).** Not CPU
  uploads (they drop 2 entries; 28% of them change no pixel at all). The per-frame
  640x480 clear at (0,0) drops a 224x192 15-bit texture whose page starts at x = 640: 2907
  drops and 125 M texels reconverted by vblank 3450, about 97% of all dropped texels.
  - `InvalidateTextureArea(x, y, W, H)` takes **width-1, height-1** (P.E.Op.S.):
    `CheckWriteUpdate`, `primMoveImage`, `MoveImageWrapped` pass `w-1`. The fill paths
    (`BlkFillArea`, `TitleFillArea`), `efb_sync` and `OffscreenSoftDraw` pass the full
    width: they invalidate one column and one row too many. Column 640 is page 10.
  - **Second, opposite bug (found in review).** `InvalidateSubSTextureArea` turns the written
    halfwords into texels as `x = (hw - page) << (2-k)` for both ends, so for 4-bit
    (k=0) the last halfword's texels +1..+3 and for 8-bit its texel +1 are not tested. A
    cached 4/8-bit sub-texture that starts there stays **stale** after a CPU upload or move
    that ends mid-texture. The fills' extra column hid this for fills only: fixing the
    fills alone would expose it there too.
  - The filtering border of a sub-texture is copied from its own converted texels
    (`LoadSubTexturePageSortBody`, `XTexS`/`YTexS`), so an entry depends on the texels
    of its rectangle only: an exact invalidation is safe.
  - A 1x1 write is `W = H = 0` under the width-1 convention, and `InvalidateTextureArea`
    returns early on it: a 1-pixel write drops nothing (latent).
- **Plan.**
  1. **One exact API.** `InvalidateTextureArea(x, y, w, h)` with real sizes (w, h >= 1; <= 0
     is nothing), used by every caller; the -1 arithmetic moves inside.
     - It splits at the VRAM edges itself (x = 1024, y = iGPUHeight), as `efb_cpu_write`
       does. Today a CPU write that wraps at x = 1024 (the write loop wraps) is clamped, and
       its part at x = 0 is never invalidated (latent, found in review).
     - Per page, after clipping the halfword range to that page: first texel
       `(hw0 - page) << (2-k)`, last texel `((hw1 - page + 1) << (2-k)) - 1`.
     - The page and texel arithmetic goes into a small pure header (like
       `SoftGPU/dither5.h`), shared by the Wii code and the host test.
  2. **Host test** `tests/texinval_test.c`: for every depth, page and halfword range, the
     entries the function drops equal a brute-force "shares a texel" check, over all
     entry rectangles on a grid. It must fail on today's code (the 4/8-bit end) and pass
     on the new one.
  3. **Texture-window cache** (`InvalidateWndTextureArea`, per page): the same size fix,
     and `px1 = X >> 6` has no widening, so an 8/15-bit window whose page starts left of
     the write survives it (latent). Not the sub cache's `-3` (a bound, not a rule): an
     entry of depth k on page p covers halfwords `[64p, 64p + (64 << k) - 1]`; drop it
     when that span meets the write. The same header, in the same change.
  - **A staleness oracle first** (`TEXCHECK`, a debug preset): on every sub-cache hit,
    convert the entry's rectangle from VRAM again into a scratch and compare it with the
    cached GX texels (untiled); count mismatches and keep the first few (page, rect,
    source of the last write). It is the positive test the XFB and `ogxeq` checks are not,
    and it serves any later texture-cache change. Run it before step 1 on the 11 games
    too: it shows whether the latent bugs above already produce stale texels.
  4. Nothing MediEvil-specific: with exact rectangles the 640-wide clear no longer
     reaches page 10.
  - **Every caller** (code graph `trace_path InvalidateTextureArea` + grep, 11 sites):
    w-1 today: `CheckWriteUpdate`, `primMoveImage`, `MoveImageWrapped` (4),
    `PrepareFullScreenUpload`; full size today: `BlkFillArea`, `TitleFillArea`,
    `OffscreenSoftDraw`, `efb_sync`. `InvalidateTextureAreaEx` (sxmin..sxmax, the
    primitives' off-screen path) calls the caches directly: check its convention too.
- **Verify.**
  - Host test passes (and failed before), including wrapped writes and the page ends.
  - `TEXCHECK` build, 11 games: 0 mismatches after the change (and the count before it,
    if the latent bugs show).
  - Dolphin, MediEvil to vblank 3450: `texinv: fill` drops 2907 -> ~0, `texk: conv` 3709 ->
    ~800; XFB at 3400 identical (`medievil_dump.txt`).
  - Dolphin, 11-game chain: cycles, primitives, VRAM at the end of every game identical;
    `texinv` drops may only fall for fill/efb/soft (exact sizes) and rise slightly for
    load/move (the stale 4/8-bit texels). XFB dumps of Crash Bash and FF7 identical, or
    different only where a stale texel was fixed (then look at it).
  - `PERF_PROF_GPU` build (`build debug all`): the `ogxeq:` texel detector shows no new
    mismatches in the 11 games. It compares the centre texel of each opaque, non-window
    draw in the GX texture with VRAM: it catches a stale entry that is drawn, not every
    stale texel.
  - Bench Wii: MediEvil deep run, `texsel` 13% -> ~2-3%; XFB via wii_lab.py identical.
- **Expected.** Most of MediEvil's 13% `texsel` on hardware, from a two-line cause. Games
  that clear the frame each frame next to a cached texture page get the same. The 11-game
  probe (1800 vblanks each, `.runs/g5_all`) finds the same pattern, a clear at x = 0 of
  width w dropping an entry at texel 0 of the page that starts at x = w, in three more:
  Micro Machines (640 wide, 214 drops, 8.2 M texels), Spyro (512 wide, 498 drops, 1.3 M),
  FF7 (320 wide, 45 drops, 1.7 M). Every fill drop in the chain is this one.
- **Lead for later.** 95% of FF7's 135k CPU->VRAM loads change no pixel (`texinv:
  same_loads`). They cost the VRAM loop and FinishedVRAMWrite, not invalidation.
- **If not enough.** Only then the partial reconversion idea (re-convert just the written
  rectangle of a kept entry): a texture-cache design change, not needed if the above works.

### G6. FF7's logos: screen uploads (2026-09-29)
- **Measured** (texcheck build `uplcheck:`, trace at vblank 500). Each frame the game writes
  240 rows of 640 pixels, one interlaced field, then the frame is uploaded whole (six
  256x256 parts). No upload is redundant (the logo fades), but 95% of the rows change no
  pixel and the changed pixels fill 4.4% of the screen.
- **DONE** (0fbe257): the copy loop keeps each transfer's changed-pixel range; textures are
  dropped only under it, and while the EFB mirrors psxVuw (`ogx_efb_mirror`) only it is
  uploaded. Exact: FF7, MediEvil, Gex, CTR every distinct frame identical; TEXCHECK 0 stale.
  FF7 900 vblanks in Dolphin: upload 8.66 -> 5.72 s. What is left is the first phase, where
  every frame also draws with GX (the mirror must be off there: `mirror: off=` 252 by GX).
- **Bench Wii, FF7 first minute:** 0.96x (c377529) -> 0.98x (14e7cf1, the fade band keeps the
  mirror) -> 0.99x (e6238c0, the EFB sync merges a same-scale snapshot row by row: upload
  3.70 -> 2.34 -> 0.89 s, frames over budget 260 -> 257 -> 25). FF7 is now as fast as the
  other ten games on the Wii.
- **Next, if needed:** a mirror per region (tiles) so a GX draw ends it only where it drew;
  the 16-bit upload tiler still writes the unused semi-transparent scratch for every texel.
- **Found on the way:** Frogger's 24-bit FMV one flip late then black, CTR's Sony screen never
  presented (both fixed, dcac40b).

### G7. GPU busy time: the GpuTiming setting (2026-09-29, 2ed5587)
- **What was wrong.** With OpenGX a game almost never saw the GPU busy. psxdma.c set the
  core's nBUSY at the end of each DMA list and cleared it only at the next one, so the GPU
  read idle during block uploads (video frames, textures) too; and GL_GPUdmaChain returned
  the list's word count, so each list ended far too early. gpulib (Soft Timed) has neither.
- **Now a setting,** GpuTiming Fast (default, as before) or Accurate (gpulib's per-command
  cost, and busy only from gpuIdleAfter). General > Plugins > "GPU Timing"; SETTINGS.md.
- **Measured in Dolphin.** Fast: the eleven games frame-for-frame as before. Accurate: 59 small
  frame differences, no breakage; load +0.2 to +3.8 points from longer GPU poll loops, which
  lightrec.c gpu_poll_idle() mostly takes back (MediEvil +3.8 -> +0.9). Crash Bash (+2.5)
  and Ape Escape (+1.9) keep theirs: their extra time is not in GPUSTAT/DMA2 polls.
- **The user's recordings on Accurate:** FF7 100% the same end VRAM, Ape Escape 99.5%, Crash
  Bash 91%, Crash 3 and Spyro out of step (another place at the end): they were recorded
  under Fast timing, and pad input by vblank lands at other moments. To make Accurate the
  default: re-record those two on Accurate, and a bench Wii run of all eleven.
- **Dead code found:** OpenGX's AUTO_FIX_GPU_BUSY (iFakePrimBusy) toggles the plugin's busy
  bit, which gpuSyncPluginSR discards (the core keeps its own timing bits): it does nothing.
- **CPU (d0d97be): the CpuTiming setting.** Fast (default) charges every instruction the same.
  Accurate adds the cycles a PS1 waits for its GTE (MFC2/CFC2/SWC2/next command while one
  runs) and its mult/div unit (MFHI/MFLO): Lightrec's last optimizer pass,
  lightrec_flag_stalls(), works them out per block and keeps them in 8 free opcode-flag bits.
  Within one block, once through, so it can miss a wait but never adds one. Lightrec only
  (psxRegs.gteBusyCycle and muldivBusyCycle are still unused by the Interpreter core).
  Eleven games: 7 small frame differences, identical presents, Crash 3 -0.6 points of host
  time. The games here have headroom, so the waits change little they show.

## 3. Recompiled code (Lightrec)

### C1. HLE exception handlers run one block per recompiler entry
- **Evidence.** In the 3D games **81-97% of all recompiler entries** are these one-block
  steps (`nested:` against `cpu: slices`). The post-slice path that carries them is 3.6-9.4%
  of wall; Ape Escape also spends 5.4% in the scheduler on them.
- **Cause.** `softCallInException()` (`psxbios.c`) sets `ra = 0x80001000` and loops
  `ExecuteBlock()` until `pc == 0x80001000`, because that address holds ordinary RAM: the
  recompiler must not run into it.
- **Optimise.** Return to an HLE trap in **ROM** instead. A game cannot write ROM, so there
  is no self-modifying-code or data risk. Then run the handler with a normal budget; the
  recompiler exits when it reaches the trap. **DONE** (b11b30d: `SOFTCALL_END` 0xbfc01000, a ROM trap,
  `hleop_softcall_end`; the interpreter honours it since a77f6ea). Two rules:
  - Do **not** plant the trap at 0x1000 in RAM (the panel caught that).
  - Do **not** reuse `rom32[0]`'s `HLEOP(hleop_dummy)` (psxbios.c:4035): `hleDummy()` sets
    `pc = ra` and adds 1000 cycles, so each interrupt would gain 1000 cycles and not stop.
    Add a new `hle_op` that only ends the soft call, at an unused ROM word.

  Check first how upstream PCSX-ReARMed does this.
- **Verify.** Behaviour must not change: same `cpu:` cycles, same per-source interrupt counts
  and exception count as a run of the same chain before, and the frame dumps. `nested:` n
  falls by roughly 35x in Spyro.
- **Expected.** Most of the post-slice time: up to about 8% of wall in Crash Bash and Ape
  Escape. It is the largest software-only item in this plan.
- **Locked cache.** No.

### C2. The GTE call-out
- **Evidence.** 1.4% of wall on average, 4.1-4.4% in Spyro and Crash 3; RTPS is half of all
  calls. Every GTE instruction leaves the recompiled code for a C call.
- **Optimise.** Use 32-bit arithmetic where the operands cannot overflow and keep the 64-bit
  flag checks for the rest. Small; do it after C1.
- **Locked cache.** Possible for the 256-byte CP2 register block, but it lives inside
  Lightrec's register struct. Low value.

### C3. The rest of recompiled code
- **Evidence.** 12.6-27.4% of wall is the guest's own code. The recompiler maps RAM directly,
  so loads and stores need no lookup, and `jit_full=0`: the code buffer never fills.
- **Options.** Lightrec's unsafe optimisation flags (`Config.hacks.lightrec_hacks`), a newer
  upstream Lightrec, and the `try/phase-2-on-main` trial branch. All three need hardware runs
  to judge.
- **Locked cache: the PSX scratchpad.** 1 KB that games use for their hottest data (Spyro
  keeps GTE work there). Lightrec reaches it through one map pointer
  (`lightrec_map[PSX_MAP_SCRATCH_PAD].address = psxH`), so the recompiled side is a one-line
  change; the work is the C-side accessors that reach it through `psxH`. A PlayStation cannot
  DMA into its scratchpad or execute code from it, so neither DMA coherency nor
  self-modifying code applies. *Hardware-only gain.* It is the best CPU-side LC candidate.

## 4. Locked-cache candidates found

All *hardware-only*; none can be judged in Dolphin. The LC is 16 KB.

| candidate | size | why | where it stands |
|---|---|---|---|
| Tiling output staging (G1) | a few KB per band | 6.1% of wall in Crash Bash | **DONE**: region `tex-tile` (off by default) |
| FMV presentation staging (G2) | a few KB per band | 15% in Micro Machines | after the G2 split |
| PSX scratchpad (C3) | 1 KB | hottest guest data | accessors to audit |
| SPU gaussian table + channel state | ~8 KB | the SPU is 1.8-10.9% | **PARTIAL**: region `spu-gauss` holds the 2 KB table; the channel state is not in it |
| CP2 register block (C2) | 256 B | 8.8 M GTE calls a minute | inside Lightrec; low value |

The first two and the scratchpad do not fit together with the SPU state; choose by the
hardware numbers.

## 5. Cleanup and fixes found

Done today: the debug logging charged to the GPU (`c0e9c07`); the nested BIOS timer
double count (`101b1cd`); OpenGX's texel probes ran ungated and compared against zeros
(`b910ed0`); the GTE probe's own cost (`77c807a`).

Still to do (fix, not delete):
- `glparamstate.dirty` is cleared and never read. G3 either uses it or removes the pretence.
  Status 2026-11-03: open (set in many places in `gc_gl.c`, cleared once, read nowhere).
- `glDrawCommon` sets the vertex format on every draw. G3. **DONE** (b11b30d)
- OpenGX has `#ifdef DISP_DEBUG` blocks that `sprintf` per draw (`_ogx_apply_state`,
  `chkTex`). They are compiled out today only because the library is built without
  `DISP_DEBUG`. Guard them with `logFileEnabled()` like the plugin's, so a debug OpenGX
  cannot bring the trap back. Status 2026-11-03: open (`DISP_DEBUG` still commented out;
  its `sprintf` blocks are not guarded).
- The whole-texture `DCFlushRange` per sub-upload (G1.2). **DONE** (only the first
  semi-transparent copy still flushes it all, which is correct)
- `psxinterpreter.c`'s `psxCP2[]` table holds `psxNULL` (no parameters) in a table of
  `void (*)(psxCP2Regs *)`. Harmless on PowerPC, but undefined behaviour; give it a matching
  no-op. Status 2026-11-03: open (`psxNULL` still in `psxCP2[]`).

## 6. Future work

- Input scripts for the other ten games, so a chain measures gameplay and not attract
  modes. The `dump`/`trace` lines of `scripts/autoinput/crash3_title.txt` show the form.
  **PARTIAL**: recordings (`scripts/movie_capture.sh`) exist for Spyro, Crash 3, Crash Bash,
  Ape Escape and FF7; CTR, MediEvil, Gex, Frogger, Micro Machines and Point Blank have none.
- The hardware session: `scripts/chains/all.txt` as `autoboot.txt`, a build with
  `PERF_PROF_PMC=1`, then `chain_table.py`. One boot, one card transfer. Not run yet; the tools
  are ready: the PMC probes (8214a6d) and the bench-Wii loop (`scripts/wii_lab.py` through the
  `$WII_BENCH` queue), which needs no card transfer. **DONE** (status 2026-11-03): run
  on the bench Wii on 2026-09-28 (`baselines/hw_chain_all_20260928`, `hw_chain_all2_20260928`
  with the PMC build c48bb7d, and the LC A/B `hw_chain_lc_ab_20260928`); every version since is
  benchmarked with `library20.txt` (`scripts/benchdb.py`).
- Why the same game measures differently in two chain files (caution 2). **DONE**: a game
  inherited the previous game's timers, SPU, SIO and pad (372a470) and display state
  (627af35); both fixed.

## 7. Order of work

1. C1 (HLE trap in ROM): up to about 8% of wall, measurable in Dolphin, in every game. **DONE**
2. G2 split, then its fix: the largest single item (15.1% in Micro Machines), and a 5.6 ms
   present is a frame-time spike a player sees. The split is one probe. **PARTIAL**: the
   split is done, the fix is open. **DONE** (fcb53905, 2026-09-28).
3. G1.1 (block-aligned placement) and G1.2 (flush the written blocks): both small. **DONE**
4. G3 (state cache), with its invalidation rules. **DONE**
5. The hardware session, then the LC items in the order the hardware numbers give.

The local model panel reviewed this plan. It moved G2 ahead of G1 (all three models agreed)
and made G1.3 conditional. Its other objections were checked against the code and rejected:
block placement is the plugin's choice, a partial flush is one range per block row, and a
PlayStation cannot DMA into or execute from its scratchpad.

---

# Part B: Locked cache: what belongs in the 16 KB L1 scratchpad, and in what order

Written 2026-09-21 from measurements taken that day. Numbers come from the debug build's
`perf.log` (`texk:` line, added for this) under Dolphin; read section 2 before believing any
of them, then section 3 for the order of work.

## 1. What the locked cache is, in this codebase's terms

Broadway can lock half of its 32 KB L1 data cache: 16 KB at `0xE0000000` stops being a cache
and becomes directly addressed memory with its own DMA engine to and from main memory
(libogc2 `ogc/cache.h`: `LCEnable`, `LCAlloc`, `LCLoadBlocks`, `LCStoreBlocks`, `LCQueueLength`,
`LCQueueWait`, `LCFlushQueue`). It pays off for two kinds of code:

- **Streaming kernels**: data flows in, is transformed, flows out. The transform runs from
  the scratchpad with no cache misses, and the DMA for block N+1 (in) and N-1 (out) overlaps
  the work on block N.
- **Hot tables** that a streaming loop keeps evicting from L1 (`LCAlloc` a table into the
  locked half; no DMA involved).

Rules that the source and a three-model review (`llm_court`, 2026-09-21) agree on:

| Rule | Why |
|---|---|
| Both addresses 32-byte aligned; lengths in 32-byte blocks; at most 128 blocks (4 KB) per request | DMAU/DMAL encoding (`LCLoadBlocks` in `cache_asm.S`) |
| LC DMA is **not coherent** with L1 or L2 | it moves memory <-> LC only. Source rows that the CPU wrote recently must be `DCStoreRange`d first, or read them through the normal cache instead; a destination the CPU will read later must be `DCInvalidateRange`d after the store |
| Drain the queue (`LCQueueWait(0)`) before the GP is told to fetch what was stored | the GP reads memory; the DMA is asynchronous |
| Never fill the queue; poll `HID2.DMAQL` (4-bit field, `LCQueueLength()`) | the panel disagreed on whether a full queue stalls or drops; do not find out |
| Issue DMA from one thread; wrap `mtdmau`/`mtdmal` pairs in `_CPU_ISR_Disable` | two-register kick; no interrupt handler here uses the LC, so this is belt and braces |
| Plain stores write the LC; `dcbz_l` is only for establishing lines | `__LCEnable` zeroes all 512 blocks with `dcbz_l` once |
| Dolphin implements the DMA as a **synchronous copy** (`MMU.cpp` `DMA_MemoryToLC`/`DMA_LCToMemory`) and models no cache misses | LC code *works* in Dolphin; nothing about its speed is visible there. With `CACHE=1` (AccurateCPUCache) Dolphin does model the data cache, so a coherency mistake (stale line after an LC store) shows up in that mode and only that mode |

Threads that exist while a game runs (`LWP_CreateThread`): the SD-removal poller, the network
init thread, the USB HID thread. None touch the LC. Keep all LC use on the emulator thread.

## 2. What was measured, and what it can and cannot say

> **Correction, 2026-09-22. Every `hw_gpu_us` number below is wrong, and so is every share
> of wall beside it.** The debug build these runs used had `CMD_LOG_3D`, `CMD_LOG_2D`,
> `CMD_LOG_LINE` and `CMD_LOG_GT4FT4` defined unconditionally in `GlesGpu/gpuPrim.c`, so it
> ran a `sprintf` for every primitive a game drew and threw the string away, plus a hash
> over every pixel of every VRAM image transfer and screen fill in `DebugLogVramHalf()`.
> All of it was inside the GP0 and DMA2 handlers, so all of it was charged to `hw_gpu_us`.
>
> With those made opt-in, the same 1800 frames of Spyro's title: wall 89.56 s -> 63.95 s,
> `hw_gpu_us` 57.99 s -> 9.98 s, **64.8% of wall -> 15.6%**, emulated speed 0.618x ->
> 0.936x, and the run drew the identical 1,626,167 primitives either way. The release
> build never had any of this.
>
> So the GPU is not two thirds of anything, step 0 below is no longer about confirming a
> 66% figure, and the ranking of every other kernel against wall time in this section is
> understated by roughly the same 40%. `PERF_PROF_GPUSPLIT` now divides `hw_gpu_us` into
> its parts; use it and re-take these numbers before acting on them.

Debug build with the `texk:` probes (`LoadSubTexturePageSort` = the CLUT expansion into the
staging buffer; opengx `glTexSubImage2D` = the RGB5A3 4x4 tiling pass; `psxDma1` = MDEC decode
per DMA), 110 s runs at full speed, no frame dump (scratchpad `run92/`):

| | Spyro gameplay (autoinput script) | Medievil idling through its intro FMV |
|---|---|---|
| guest time (`wall_us`) | 87.9 s | 57.6 s |
| limiter idle (`limit_us`) | 12.0 s (14%) | 16.9 s (29%) |
| `hw_gpu_us` (GPU register/DMA path: the GX plugin building GX commands) | **57.7 s (66%)** | 21.4 s (37%) |
| SPU incl. resampler (`spu_us`) | 0.83 s (0.9%) | 0.67 s (1.2%) |
| CLUT expansion (`conv_us`) | 0.42 s, 1706 uploads, 3.9 M texels (avg 48x48) | 0.013 s, 32 uploads |
| tiling pass (`tile_us`) | 0.37 s | 0.009 s |
| MDEC decode (`mdec_us`) | 0 | **2.52 s (4.4%; ~6% while the video plays)**, 9406 DMAs |
| sub-texture cache hit rate | 546924 hits / 1706 new = 99.7% | 99.99% |
| uploads on the per-pixel *unaligned* path | 1242 of 1706 (73%) | 21 of 32 |

Two more titles, plain boot-and-idle runs the same day (`run93/`), Dolphin accounting:

| | Crash 3 (title/attract, 75.2 s) | FF7 (title screen, 83.9 s) |
|---|---|---|
| limiter idle | 20% | 41% |
| `hw_gpu_us` | 43% | 14% |
| SPU | 7.8% (reverb on by default now) | 2.3% |
| CLUT expansion + tiling | 0.28% | **4.4%** (1986 uploads, 42.6 M texels, avg 146x146; 26% sub-texture cache misses) |
| MDEC | 0 | 0 |

FF7 is the texture-streaming case this plan needed: 2D backgrounds re-uploaded as the cache
misses, and the CLUT path there is nearly five times Spyro's share. Step 2's payoff is games
like this, not Spyro.

Four cautions, in order of importance:

0. **The GPU rows are measuring the build's own logging** -- see the correction above.
   They are left here because the shape of the other rows against each other is still
   informative, not because the numbers are usable.
1. **These are Dolphin's instruction-count estimates, not time.** Dolphin charges ~80 cycles
   per texel to a loop that is a load, a table lookup and a store, and it models no cache
   misses at all. The locked cache's whole benefit is the removal of misses and of a 128 KB
   `DCFlushRange` per upload, so its gain is invisible here by construction. The same debug
   build writes the same `perf.log` on a real Wii (`sd:/wiistation/perf.log`); one hardware run of
   each game is the actual baseline. Earlier hardware notes (OpenGX at 98% speed with 62% wall
   idle) contradicted the 66% `hw_gpu` figure, and were right to: the figure was an artefact,
   and the hardware notes were the evidence that should have been believed first.
2. **The CLUT path is 0.9% of guest time in Spyro gameplay** because the cache hits 99.7% of
   the time. It matters during level loads and in games that stream textures (2D games with
   large per-frame sprite uploads). Sizing it needs such a game on hardware, not Spyro.
3. **MDEC is the largest streaming kernel measured** and it is CPU-bound in a different way:
   integer IDCT math over 64-entry blocks. The locked cache helps it only by keeping its tables
   and block resident; the arithmetic itself is a paired-single or integer-optimization
   question.

## 3. The order of work

**Status 2026-11-03 (HEAD e857a9c2).**
- Step 0: partly done. The bench Wii ran the PMC build (`baselines/hw_chain_all2_20260928`, build
  c48bb7d) and an LC A/B (`baselines/hw_chain_lc_ab_20260928`: `LockedCache=3` vs 0, no wall
  gain). Whether `PERF_PROF_GPUSPLIT` was on in those runs is unverified.
- Step 1: **DONE** (b11b30d4).
- Step 2: in as the `tex-tile` region (`tile_via_lc`, 805659f4), off by default; no gain on the
  Wii yet.
- Step 3: merged (187ae768), then **taken out** (3ecfa64d, 2026-10-21): it bought nothing on the
  bench Wii (measurement in `Docs/LOCKED_CACHE.md`).
- Step 4: not done; judged not worth it on 2026-10-18 (`Docs/LOCKED_CACHE.md`).
- Step 5: the gaussian table is in (`spu-gauss` region); the SPU channel state is not.

Hardware numbers for these choices (same bench Wii, measured by WiiXplorer NG, 2026-10):
`Docs/HARDWARE_RESEARCH.md`. In short: LC DMA MEM1->LC 1349, LC->MEM1/MEM2 ~1465, MEM2->LC
only 440 MiB/s; a pass past the 256 KiB L2 runs 2-3x slower, which is what step 2 removes.

### Step 0. Hardware baseline (no code)
Take the Dolphin numbers again first, with the logging fix and `PERF_PROF_GPUSPLIT`: what is
written below was ordered by figures that no longer hold.
Run `Gamecube/WiiSXRX_debug.dol` (this build, with the `texk:` probes) on the Wii for Spyro,
Medievil's intro, and one texture-streaming 2D title; copy `sd:/wiistation/perf.log` back. Read
`hw_gpu_us`, `texk:` and `spu_us` as fractions of `wall_us`. Everything below is ordered by the
Dolphin numbers and should be reordered by these.

### Step 1. Block-aligned sub-texture placement (no LC yet; a prerequisite and a win on its own)
`GetCompressTexturePlace` (GlesGpu/gpuTexture.c) carves free rectangles with a 1-texel guard
border and no alignment, so 73% of uploads take opengx's per-pixel unaligned path instead of
the 4x4-block fast path. Round each reserved rectangle to multiples of 4 and place the texel
origin on a 4-boundary (the guard border stays inside the rounding). Cost: up to 3 texels of
padding per edge inside a 256x256 page; watch `ogx: gc` (garbage collections) for the packing
cost. This is also what makes whole-block DMA stores safe (step 2): a sub-rectangle's edge
blocks must not be shared with a neighbour, otherwise a block store overwrites the neighbour's
texels. The review panel was unanimous that alignment beats read-merge-store or per-edge CPU
paths.

### Step 2. Fused CLUT -> tile kernel with locked-cache output bands
Today: pass 1 expands each texel through the palette into a 32-bit word of `texturepart`
(256 KB, MEM1); pass 2 (opengx `_ogx_scramble_4b_sub`) reads that back, writes the opaque page
*and* the semi-transparent page (two 16-bit stores per texel), then `DCFlushRange`s the whole
128 KB page. Replace both with one pass, per 4-texel-row band:

- palette: 256 x u16, already in RGB5A3 form (`| 0x8000` applied; semi-transparent variant
  handled per the `semiFlg` rule), `LCAlloc`ed once per upload (1 KB);
- source: read through the normal cache (4 rows of 4-bit texels are 128 B/row, 8-bit 256 B;
  the VRAM is written by the CPU constantly, so DMA-loading it would need a store-back first
  and buys nothing);
- output: two LC band buffers (opaque + semi), each `W_BLOCK(dx) * 32` bytes (<= 2 KB for a
  full 256-wide band), double-buffered = 8 KB; expand and tile straight into them (a band of
  the sub-rectangle is `W_BLOCK(dx)` *contiguous* 32-byte blocks in the destination page);
- `LCStoreBlocks` each finished band to `currtex->data + ((by * W_BLOCK(256)) + bx0) * 32` and
  the same offset in `semiTransData`; poll `LCQueueLength()` before each kick; `LCQueueWait(0)`
  once at the end of the upload, before `GX_InitTexObj`/the draw;
- no `DCFlushRange` of the page any more; `DCInvalidateRange` the written blocks only where
  the CPU reads texture memory afterwards (the debug-only `probe_texel`/`probe_equiv`, and the
  lazy `semiTransData` first-upload `memcpy` in opengx, which must copy from the page after a
  drain).

Correctness harness, all existing: `ogxeq: mism=0` (texel-equivalence detector, PERF_PROF_GPU
build) proves every opaque textured draw samples the right texel; frame dumps against Old
Soft (`scripts/dolphin_run.sh`, `sheet.py`); `CACHE=1` runs for the coherency rules. Speed:
hardware `texk:` before/after, plus `ogx: unaligned` going to 0 after step 1.

Expected: on hardware, the upload cost falls by the 128 KB flush, the 256 KB staging round
trip and the second pass; in Spyro gameplay that is a fraction of 0.9%; in a texture-streaming
game it is the whole upload cost. Do not expect a frame-rate change in Spyro from this step.

### Step 3. MDEC: tables and block in the locked cache -- IMPLEMENTED 2026-10-19, TAKEN OUT 2026-10-21 (3ecfa64d)
`psxDma1` -> `rl2blk` -> `idct` -> `yuv2rgb15/24` per 16x16 macroblock. Working set: `iq_y`,
`iq_uv`, `zscan`, `aanscales` (64 ints each, 1 KB), the `blk[DSIZE2*6]` block (1.5 KB), the
16x16 output (512/768 B). `LCAlloc` the tables and `blk` into the LC so the streaming output
cannot evict them; keep writing the output through the normal cache (the game DMAs it to the
GPU next, and the GPU plugin reads it with the CPU, so it must stay coherent -- no LC DMA out).
Measure with `mdec_us` on hardware. If the IDCT arithmetic dominates (likely), the follow-up is
a paired-single IDCT, which is a different plan.

**Implemented (branch `try/lc-step34`):** `LC_MDEC`, 2.25 KB = `zscan` + `iq_y` + `iq_uv`
(3x256 B) + `blk` (1.5 KB), packed after the other two regions (12.25 of 16 KB used). The
tables are mutable (game quant-table uploads, savestate restore), so the masters stay
authoritative and `psxDma1_body` copies them in once per DMA with plain stores -- no DMA, no
cache maintenance, `sync=` on the `lc:` line. `aanscales` is a read-only constant and stayed
outside the region; the output stays on the normal path as the plan asks.

Dolphin A/B daily run `lc34` (Gex intro, MediEvil title FMV; masks 0 vs 4 vs 7, all three
regions pack and run together): VRAM dumps and per-game-end state hashes are bit-identical,
`sync=` equals the DMA count (2480 / 9404), and `mdec_us` is neutral (2403.6 ms off vs
2415.2 ms on over 1800 vblanks). Neutral is the expected Dolphin result -- it does not model
the stall the region removes -- so the hardware `mdec_us` is the number still open.

### Step 4. SoftGPU frame conversion (soft plugins only)
`gx_convert_us` in SoftGPU/drawGX.c converts the whole PSX framebuffer to a GX texture every
frame: the textbook streaming kernel (up to 1 MB in, ~0.6 MB out per frame). Source rows in by
LC DMA (the soft rasterizer wrote them: `DCStoreRange` first, or convert straight from the
cache), tiles out by LC DMA, double-buffered. Only worth it if the soft plugins are a target;
they are the ground-truth renderers here, not the fast path.

### Step 5. SPU hot buffers (cheap experiment) -- MEASURED 2026-09-22, see below
`SSumLR` (NSSIZE*2 ints, ~7 KB), `ChanBuf`, `RVB`: `LCAlloc` them instead of MEM1 statics; no
DMA. It is a one-line experiment per buffer and the gaussian voice interpolation table would
also fit.

**The whole sound system does not fit, and the mixer is not 0.9%.** Both numbers above were
wrong and are corrected here.

`PERF_PROF_SPU` splits the mixer. Crash 3's title, 153 s of wall:

| stage | time | share of wall |
|---|---|---|
| ADPCM decode + interpolation | 7.91 s | 5.17% |
| per-channel mix | 2.77 s | 1.81% |
| envelope (MixADSR) | 1.70 s | 1.11% |
| reverb | 0.039 s | 0.025% |
| SPU total | | about 8.1% |

So the SPU is eight times the 0.9% this plan assumed, and reverb -- which an earlier note in
this file put at 7.8% -- is one part in four thousand. That 7.8% was the whole SPU figure
attributed to one part of it.

Sizes, with NSSIZE = 914:

| buffer | bytes |
|---|---|
| `iFMod[NSSIZE]` | 3,656 |
| `RVB[NSSIZE*2]` | 7,312 |
| `ChanBuf[NSSIZE]` | 3,656 |
| `SSumLR[NSSIZE*2]` | 7,312 |
| **flat buffers** | **21,936** |

against a 16,384-byte locked cache: 1.3x over before the 24 channel states, the 24 sample
buffers and the 2 KB gaussian table, which together come to roughly 29.8 KB, or 1.8x. And the
memory the decoder actually misses on is the 512 KB of SPU RAM it reads sample data from,
which can never be locked.

What would fit is the gaussian table (2 KB) plus the channel state (~6 KB). Whether that helps
cannot be answered here -- see the note in section 1 about Dolphin and cache misses -- so it
belongs in the hardware session, with the counters below.

### Probes to build the hardware session around
All of these are in the tree and off by default; each says how to turn it on in its own
comment in `Gamecube/perf_prof.h`.

| gate | what it gives |
|---|---|
| `PERF_PROF_PMC` | Broadway's four performance counters over a whole run. This is the one the locked-cache work needs: it is the only way to see stalls, and Dolphin cannot show them. The event selects are build-time (`PMC_MMCR0`, `PMC_MMCR1`) so a hardware session can try a miss event without a code change. Defaults are processor cycles and instructions completed, whose ratio is what moves when stalls go away. |
| `PERF_PROF_SPU` | the mixer split above, per stage |
| `PERF_PROF_MDEC` | the video decode split into rl2blk+IDCT and colour conversion |
| `PERF_PROF_BIOS` | every HLE BIOS call by name, and the time in the whole HLE dispatch |
| `PERF_PROF_NETWAIT` | what a one-second wait costs, old shape against new |
| `PERF_PROF_GTE` | the geometry coprocessor, and which of its 22 operations a game uses |
| `PERF_PROF_GPUSPLIT` | the GP0 and DMA2 time divided into image transfers, the off-screen software rasterizer, the primitive functions by class of command, and the reading of the display list. Needs `PERF_PROF_CPU` beside it. |

Verified encodings for `PMC_MMCR0`/`PMC_MMCR1` (they are also what Dolphin's interpreter
implements, `PowerPC.cpp` `UpdatePerformanceMonitor`): PMC1 select 1 = processor cycles,
PMC2 select 1 = processor cycles and 11 = loads and stores completed, PMC3 select 11 = FPU
instructions completed. PMC1 select 2 = instructions completed is in the 750CL manual and is
not implemented by Dolphin, so it reads 0 there and counts on a Wii. A 120 s Spyro run under
Dolphin gave `pmc1=1149497231 pmc2=0`, which is the shape to expect.

### Not a locked-cache candidate: `hw_gpu`
Two thirds of guest time in Dolphin's accounting is the GPU command path
(GlesGpu/gpuPrim.c -> opengx `glDrawArrays` -> GX write-gather FIFO). It is control-heavy code,
not a stream; the tools for it are GX display lists, batching and vertex-format packing, and
the first question is whether hardware agrees with the 66% at all (step 0). Give it its own
task once the hardware numbers exist.

## 4. Verification checklist for any LC change
1. `CACHE=1` Dolphin run: catches a missing `DCStoreRange`/`DCInvalidateRange` (Dolphin models
   the dcache in that mode and the LC DMA writes memory behind it).
2. `ogxeq mism=0` and `oob=0` unchanged; `ogx: unaligned` as expected; `gc` not exploding.
3. Frame-dump contact sheet against the previous build (same input script).
4. Hardware `perf.log`: the kernel's own `*_us` line, and `wall_us` vs `vblanks` for speed.
5. Release build boots on hardware (BSS/alignment differences do not show in Dolphin).
