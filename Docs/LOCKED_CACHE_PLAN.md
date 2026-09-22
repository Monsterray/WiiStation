# Locked-cache plan: what belongs in the 16 KB L1 scratchpad, and in what order

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

Three cautions, in order of importance:

1. **These are Dolphin's instruction-count estimates, not time.** Dolphin charges ~80 cycles
   per texel to a loop that is a load, a table lookup and a store, and it models no cache
   misses at all. The locked cache's whole benefit is the removal of misses and of a 128 KB
   `DCFlushRange` per upload, so its gain is invisible here by construction. The same debug
   build writes the same `perf.log` on a real Wii (`sd:/wiisxrx/perf.log`); one hardware run of
   each game is the actual baseline. Earlier hardware notes (OpenGX at 98% speed with 62% wall
   idle) already contradict the 66% `hw_gpu` figure, so expect the hardware ranking to differ.
2. **The CLUT path is 0.9% of guest time in Spyro gameplay** because the cache hits 99.7% of
   the time. It matters during level loads and in games that stream textures (2D games with
   large per-frame sprite uploads). Sizing it needs such a game on hardware, not Spyro.
3. **MDEC is the largest streaming kernel measured** and it is CPU-bound in a different way:
   integer IDCT math over 64-entry blocks. The locked cache helps it only by keeping its tables
   and block resident; the arithmetic itself is a paired-single or integer-optimization
   question.

## 3. The order of work

### Step 0. Hardware baseline (no code)
Run `Gamecube/WiiSXRX_debug.dol` (this build, with the `texk:` probes) on the Wii for Spyro,
Medievil's intro, and one texture-streaming 2D title; copy `sd:/wiisxrx/perf.log` back. Read
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

### Step 3. MDEC: tables and block in the locked cache
`psxDma1` -> `rl2blk` -> `idct` -> `yuv2rgb15/24` per 16x16 macroblock. Working set: `iq_y`,
`iq_uv`, `zscan`, `aanscales` (64 ints each, 1 KB), the `blk[DSIZE2*6]` block (1.5 KB), the
16x16 output (512/768 B). `LCAlloc` the tables and `blk` into the LC so the streaming output
cannot evict them; keep writing the output through the normal cache (the game DMAs it to the
GPU next, and the GPU plugin reads it with the CPU, so it must stay coherent -- no LC DMA out).
Measure with `mdec_us` on hardware. If the IDCT arithmetic dominates (likely), the follow-up is
a paired-single IDCT, which is a different plan.

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
