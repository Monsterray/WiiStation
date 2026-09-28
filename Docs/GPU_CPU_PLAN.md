# GPU and recompiled-code plan

Status, end of 2026-09-22 (commits b11b30d and after). Eleven-game means before -> after:
HLE 4.7% -> 3.3% of wall, load 50.0% -> 48.7%.

| item | state | measured |
|---|---|---|
| C1 HLE handlers | **DONE** (b11b30d; interpreter follow-up a77f6ea) | 81-97% of recompiler entries were single blocks; HLE share halved in the 3D games |
| G2 FMV present | **PARTIAL**: split and hash guard done; the one-pass fix is open. 24-bit FMV now shown at 24 bits (7cb1671) | Micro Machines 14.4% -> 4.2%. The rest waits for the LC; 24-bit FMV is shown at 15-bit colour -- a fidelity question for the user |
| G1 tiling | **1 and 2 DONE** (block-aligned placement, flush only the rows written); 3 not needed yet | unaligned uploads 83% -> 0; Crash Bash 6.1% -> 4.0% |
| G3 GX state cache | **DONE** (b11b30d) | skips 96% of GX state calls; 0.6-0.7% of wall in Dolphin, more GP traffic saved on a Wii |
| LC | **DONE** (805659f, `Docs/LOCKED_CACHE.md`): `spu-gauss`, `tex-tile`, off by default | frames and audio proven identical; the gain needs a Wii |

**Checked against the code 2026-09-28 (HEAD bc014aa).** Items marked **DONE** are in main;
**PARTIAL** says what is left; unmarked items are open. Next to work on, in the plan's order:
G2's real fix (one pass, no `GX_DrawDone`), the per-sub-upload `GX_DrawDone` (G1), the G4 items,
C2, then the hardware session (the bench Wii and `scripts/wii_lab.py` are ready).

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
     can be aligned. This is step 1 of `Docs/LOCKED_CACHE_PLAN.md`; it needs no locked cache.
     **DONE** (b11b30d, `SubTexReserve` in gpuTexture.c)
  2. Flush only the blocks written. The upload calls `DCFlushRange` on the whole texture
     (up to 128 KB) for a rectangle of about 700 texels. The written blocks are one
     contiguous range per row of blocks, so this is one flush per block row. *Hardware-only
     gain.* **DONE** (`flush_block_rows` in gc_gl.c)
  3. Only if the tiling pass is still above 1% of wall after 1 and 2: expand the CLUT straight
     into RGB5A3 tiles. Today the expansion writes 32-bit texels to `texturepart` and the
     tiling pass reads them back. The saving is that re-read, which no probe has isolated yet;
     the CLUT expansion itself is only 0.2-0.9%.
- **Fix (hardware-only).** Every sub-upload starts with `GX_DrawDone()` (`perf_drawdone()`),
  so the CPU waits until the GPU has drawn everything queued: 415 times a second in Crash
  Bash. It is there for correctness (the GPU may still read the texture). Wait only when the
  texture is in flight: a draw sync token per texture, or two copies of a texture that
  changes often.
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
  rectangle; no `GX_DrawDone` in the path.
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
  should be removed or pointed at `efb_before_geometry_change`.
- **Off-screen test** (`OffscreenSoftDraw`), 0.3-1.1%: it computes bounds for every primitive
  to find the few that land off screen. Skip it while the display and draw areas coincide.
- **Display-list reading**, 0.6-1.5%: word-by-word with a byte swap per word. Low value.
- **FF7's `FinishedVRAMWrite`**, 5.0% in the first chain: texture-cache invalidation for
  42.6 M words of background uploads. Look at `InvalidateSubSTextureArea` after G1.

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
- `glDrawCommon` sets the vertex format on every draw. G3. **DONE** (b11b30d)
- OpenGX has `#ifdef DISP_DEBUG` blocks that `sprintf` per draw (`_ogx_apply_state`,
  `chkTex`). They are compiled out today only because the library is built without
  `DISP_DEBUG`. Guard them with `logFileEnabled()` like the plugin's, so a debug OpenGX
  cannot bring the trap back.
- The whole-texture `DCFlushRange` per sub-upload (G1.2). **DONE** (only the first
  semi-transparent copy still flushes it all, which is correct)
- `psxinterpreter.c`'s `psxCP2[]` table holds `psxNULL` (no parameters) in a table of
  `void (*)(psxCP2Regs *)`. Harmless on PowerPC, but undefined behaviour; give it a matching
  no-op.

## 6. Future work

- Input scripts for the other ten games, so a chain measures gameplay and not attract
  modes. The `dump`/`trace` lines of `scripts/autoinput/crash3_title.txt` show the form.
  **PARTIAL**: recordings (`scripts/movie_capture.sh`) exist for Spyro, Crash 3, Crash Bash,
  Ape Escape and FF7; CTR, MediEvil, Gex, Frogger, Micro Machines and Point Blank have none.
- The hardware session: `scripts/chains/all.txt` as `autoboot.txt`, a build with
  `PERF_PROF_PMC=1`, then `chain_table.py`. One boot, one card transfer. Not run yet; the tools
  are ready: the PMC probes (8214a6d) and the bench-Wii loop (`scripts/wii_lab.py` through the
  `C:/tools/wii-bench` queue), which needs no card transfer.
- Why the same game measures differently in two chain files (caution 2). **DONE**: a game
  inherited the previous game's timers, SPU, SIO and pad (372a470) and display state
  (627af35); both fixed.

## 7. Order of work

1. C1 (HLE trap in ROM): up to about 8% of wall, measurable in Dolphin, in every game. **DONE**
2. G2 split, then its fix: the largest single item (15.1% in Micro Machines), and a 5.6 ms
   present is a frame-time spike a player sees. The split is one probe. **PARTIAL**: the
   split is done, the fix is open.
3. G1.1 (block-aligned placement) and G1.2 (flush the written blocks): both small. **DONE**
4. G3 (state cache), with its invalidation rules. **DONE**
5. The hardware session, then the LC items in the order the hardware numbers give.

The local model panel reviewed this plan. It moved G2 ahead of G1 (all three models agreed)
and made G1.3 conditional. Its other objections were checked against the code and rejected:
block placement is the plugin's choice, a partial flush is one range per block row, and a
PlayStation cannot DMA into or execute from its scratchpad.
