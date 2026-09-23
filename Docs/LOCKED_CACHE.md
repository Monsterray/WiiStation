# The locked cache in WiiStation

Code: `Gamecube/lc.h`, `Gamecube/lc.c`. Plan and history: `Docs/LOCKED_CACHE_PLAN.md`,
`Docs/GPU_CPU_PLAN.md`.

## What it is

Broadway's L1 data cache is 32 KB. Half of it can be *locked*: that 16 KB stops caching and
becomes memory at `0xE0000000` that never misses, with a DMA engine of its own to and from
main memory. Everything else then runs with a 16 KB data cache. So a use of the locked cache
is only worth it if it saves more misses than the smaller cache costs, and only a Wii can say
whether it does.

## How WiiStation shares it

The 16 KB is divided into **regions**. Each region is one use, one line in the table in
`lc.c`, and one bit in the `LockedCache` setting:

| bit | region | size | user | why it is there |
|---|---|---|---|---|
| 1 | `spu-gauss` | 2 KB | `dfsound/dfspu.c` | the SPU's interpolation table: 4 reads per sample per voice; the SPU is 1.8-10.9% of wall |
| 2 | `tex-tile` | 8 KB | `deps/opengx/gc_gl.c` | texture tiling output, sent out by DMA with no flush; the tiling pass is 4.0% of wall in Crash Bash |
| | (free) | 6 KB | | |

At every game start (`go()`) `lc_configure()` packs the regions whose bits are set, copies in
any fixed contents, and locks the cache -- or unlocks it when none is set, so an unused locked
cache costs nothing. A region that is off is simply absent: `lc_get()` returns NULL, and its
user runs its ordinary code. **Every user keeps that ordinary path.** It is what makes a region
safe to turn off, and what makes on-against-off comparisons possible.

`LockedCache` is a settings-file key (`settingsRX2022.cfg`), not a menu item: `0` all off
(the default), `1` spu-gauss, `2` tex-tile, `3` both. Every region stays off by default until
a hardware run has shown it helps.

## Adding a region

1. In `lc.h`: append an id to `enum lc_region` (never renumber: the id is the setting's bit)
   and a `LC_..._BYTES` size, a multiple of 32.
2. In `lc.c`: add its line to `lc_regions[]` -- name, size, owner, the measurement that
   justifies it, and fixed contents if it has any -- and its size to `LC_SUM`. The build
   fails if the regions exceed 16 KB, or if `LC_SUM` forgets one.
3. In the user: call `lc_get(LC_...)` once per batch of work, not per item, and fall back to
   the ordinary buffer when it returns NULL.
4. For DMA: `lc_store(dst, lc, bytes)` / `lc_load(lc, src, bytes)` (32-byte aligned, a
   multiple of 32; they handle the ordinary cache's lines), then `lc_wait()` before anything
   -- the CPU or the GPU -- reads what the DMA wrote.
5. Prove it correct in Dolphin (below), add a line to `scripts/chains/lc_ab.txt` if it needs a
   different game, and add the region to the table in this file.

Candidates recorded but not yet regions: the PSX scratchpad (1 KB; needs an MMU page mapped
onto the locked cache, because Lightrec reaches guest memory through 4 KB pages that the
scratchpad shares with the hardware registers) and the FMV frame conversion (4.2% of wall in
Micro Machines; the same band pattern as `tex-tile`).

## Proving a region correct (Dolphin)

Dolphin runs the locked cache and its DMA correctly, so correctness is checked here. Speed is
not: Dolphin models no cache misses and completes the DMA at once.

- Rendering: two runs, `LockedCache=0` and on, every frame dumped; `wsx.sh frames A B` must
  report 0 differing.
- Sound: the same with `SoundRateControl=0 SoundTempo=0` on both chain lines (the output rate
  control steers by Wii time, and the locked cache changes Wii-side timing); `wsx.sh audio A B`
  must say identical.

Measured 2026-09-22, Crash Bash: 235 frames, 0 differing; 1,305,728 audio frames, identical,
for each region on its own and both together.

## Measuring a region (a Wii)

1. Build with `bash scripts/wsx.sh build debug pmc`.
2. Copy `scripts/chains/lc_ab.txt` to the card as `sd:/wiisxrx/autoboot.txt`, with
   `scripts/autoinput/spyro_title.txt` beside it, and the DOL.
3. Boot. The console powers off when the chain is done.
4. Copy `sd:/wiisxrx/perf.log` back into a run directory and read it with
   `python scripts/chain_compare.py RUN` (each on/off pair) and
   `python scripts/chain_table.py RUN --detail lc` / `--detail pmc`. IPC is `pmc2 / pmc1`; the
   default counters are cycles and instructions completed.
5. A region whose pair shows no gain stays off. One that helps can become the default.
