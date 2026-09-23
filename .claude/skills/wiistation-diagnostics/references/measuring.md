# Measuring, comparing, and proving a change

The tools for "is it faster", "does it still look and sound the same", and "where does the
time go", all driven from `scripts/wsx.sh`. Use them instead of writing inline Python: each
one prints a screen or less, and every one of them exists because the same comparison was
hand-written several times.

## Build with the right probes

`bash scripts/wsx.sh build debug PRESET` (or `PROBES=PRESET bash scripts/build.sh debug`):

| preset | what | use it for |
|---|---|---|
| `light` | counters, no primitive trace, no GX sample rings | ordinary runs (wsx.sh's default) |
| `deep` | light + GPU split (`PERF_PROF_GPUSPLIT`), GTE, SPU split | ranking subsystems; what `--detail` reads |
| `pmc` | light + Broadway's performance counters | hardware sessions (Dolphin counts cycles only) |
| `min` | light without the per-slice CPU timing | cheapest |
| `all` | everything, trace included | primitive-trace work |

`PROBES="-D..."` still takes raw defines. Build switches worth knowing:
`-DOGX_STATE_CACHE=0` (GX state cache off), `-DSUBTEX_ALIGN=0` (old texture placement),
`-DCMD_LOG_2D` etc. (per-primitive logging, opt-in).

## Chains: many games, one boot

`bash scripts/wsx.sh chain NAME FILE --secs LIMIT` runs a chained autoboot (format in
`Gamecube/GamecubeMain.cpp` above `chainLoad()`), waits for the console to power off, and
prints the table. Files in `scripts/chains/`:

| file | what | time |
|---|---|---|
| `all.txt` | every game in the sync folder, 3600 vblanks each | ~12 min |
| `three.txt` | Spyro (3D), Crash Bash (texture streaming), Micro Machines (FMV) | ~3.5 min |
| `lc_ab.txt` | Spyro and Crash Bash with the locked cache off, then on | ~4.5 min |
| `cd_ab.txt` | Spyro, Medievil, CTR, Point Blank with a 16/64/256 KB CD buffer, then 64 KB + read-ahead | ~17 min |
| `fmv_ab.txt` | Micro Machines, Medievil, FF7 video with `FmvColour` 15-bit, then 24-bit | ~4.5 min |
| `smoke.txt` | two short games: does a chain still work | ~40 s |
| `gpu_carry.txt` | FF7, Medievil, FF7: does a game inherit GPU state from the last one (rows 1 and 3 must match) | ~3 min |
| `carry.txt` | FF7, Medievil, FF7, FF7: any state carried between games (every FF7 row must match; `carry:` line) | ~4 min |
| `ff7x3.txt` | FF7 three times: the three rows must match | ~3 min |

A chain line can set settings for its game alone -- `3600 sd:/wiisxrx/x.txt LockedCache=3
SoundRateControl=0` -- and they go back before the next game. Listing a game twice with
different settings is how one boot compares a setting on and off (the only way on a Wii,
where moving the SD card is the slow part). Games use memory cards normally; the chain deletes
each game's card files when that game ends, so both halves of an A/B boot the same way. `--env FRAMES_DUMP=True --env KEEP=100000` keeps every
frame; `--env AUDIO_DUMP=1` keeps the audio.

## Recording how a game is played

`bash scripts/movie_capture.sh GAME [NAME] [--mins M]` boots GAME (part of its folder name in
`all.txt`) as a one-game chain whose script is the line `record`; the person plays, and
WiiStation writes every change of the real pad, in emulated vblanks, to the card. The script
saves it as `scripts/autoinput/NAME_play.txt` and prints the chain line that plays it
(`... PadAutoAssign=1`). `--play NAME` boots it back in a window to check it. Use this, not a
Dolphin movie: a `.dtm` counts host frames, which drift from the game's vblanks whenever the
emulation runs below full speed (`dtm2autoinput.py` is for old movies only). Digital pad:
buttons and D-pad only, so a game that needs the analog sticks cannot be recorded.

## Reading and comparing

| command | answers |
|---|---|
| `wsx.sh table NAME` | per game: load, speed, psx, gte, gpu, poly, hle, spu, mdec, cd (% of wall) |
| `wsx.sh table NAME --detail gpu` | the GPU split: loop parts, primitive classes, OpenGX draw, GP1, present |
| `wsx.sh table NAME --detail tex` | texture uploads: CLUT, tiling, new/unaligned/invalidated |
| `wsx.sh table NAME --detail cpu` | slices, HLE soft calls, nested entries, BIOS, GTE |
| `wsx.sh table NAME --detail lc` | locked-cache regions and DMA |
| `wsx.sh table NAME --detail cd` | CD reads, the SD card's own commands (count, KB, size histogram, wait; times real on a Wii only), read-ahead hits, CHD misses |
| `wsx.sh table NAME --detail pmc` | performance counters (a `pmc` build) |
| `wsx.sh table NAME --detail upl` | screen re-uploads asked for and done, and which early return skipped the rest |
| `wsx.sh compare A B [--detail G]` | two chains game by game: A, B, B - A |
| `wsx.sh compare A [--detail G]` | one A/B chain: each on/off pair |
| `wsx.sh frames A B [--crop out.png]` | two runs' frame dumps pixel by pixel, overlay masked |
| `wsx.sh audio A B` | two runs' audio dumps sample by sample |

The groups and their fields are the `DETAIL` table in `scripts/chain_table.py`; add a field
there when a probe gains one. Keep a run you will compare against later with
`scripts/baseline_add.py` (see the baselines memory).

## What counts as proof

- **Speed under Dolphin:** two runs of the same build and chain agree to 0.1% of wall in
  every column. A larger move is the change. Dolphin charges instructions, not cache misses
  or GPU stalls: a change whose gain is fewer misses (the locked cache, flush sizes, fewer
  GX_DrawDone) shows nothing here, and needs the `pmc` build on a Wii.
- **Rendering:** the emulation is deterministic, so frame N is the same guest frame in two
  runs. `wsx.sh frames` must report 0 differing outside the overlay. A few pixels with steps
  of 100+ in a texture change are texel rounding (a texture's position moved), not a fault;
  look at the crop.
- **Audio:** two runs of the same build are bit-identical. **But the output rate control
  steers by Wii time**, so any change that moves Wii-side timing changes the resampled
  output though the SPU's samples are the same. Prove a mixer-side change with a chain line
  that sets `SoundRateControl=0 SoundTempo=0` for both runs; then `wsx.sh audio` must say
  `identical`. (Measured with the locked cache: 857,344 samples differed with rate control
  on, none with it off.)
- **Behaviour:** interrupt counts (`irq:`), exceptions (`bios:` exc) and vblanks match to
  within 0.1% for a timing-neutral change; a change that is meant to be exact, like
  `c38d4d1`, matches to the cycle.

## Probes that lie, and were fixed

Check a probe's cost and gate before trusting a debug-build share of wall: four debug-only
costs were charged to the GPU until 2026-09-22 (per-primitive sprintf, VRAM hashes, frame
hashes in UploadScreen, OpenGX texel checks). The pattern to grep for is a loop or a
`sprintf` whose only consumer is a log line; guard it with `logFileEnabled()`.
