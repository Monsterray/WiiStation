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
| `hprof` | light + the sampling profiler (PMC1 overflow, 100 us) | where the CPU is, per function; not with `pmc` |
| `min` | light without the per-slice CPU timing | cheapest |
| `all` | everything, trace included | primitive-trace work |

`PROBES="-D..."` still takes raw defines. Build switches worth knowing:
`-DOGX_STATE_CACHE=0` (GX state cache off), `-DSUBTEX_ALIGN=0` (old texture placement),
`-DCMD_LOG_2D` etc. (per-primitive logging, opt-in).

### The sampling profiler (`hprof`)

`Gamecube/hprof.c` + `hprof_entry.s`: PMC1 counts cycles; at each overflow (every 72900
cycles, 100 us) the 0xF00 exception counts the interrupted PC in 32-byte buckets. Each chained
game writes `hprof_NN.bin`; `dolphin_run.sh` and `wii_lab.py` bring them back. Keep the ELF of
the DOL that ran (`cp Gamecube/WiiSXRX_debug.elf` beside the DOL copy):

```bash
python scripts/hprof_view.py .runs/NAME/hprof_01.bin --elf DOL.elf --top 30
python scripts/hprof_view.py .runs/NAME/hprof_01.bin --elf DOL.elf --callers
```

- `busy%` is the share without `idle_func` (the limiter's idle thread). `FrameCap`'s samples
  are mostly its final spin (up to 1 ms a frame): waiting, not work.
- `jit` = samples in Lightrec's code buffer (the guest's recompiled code), one number.
- `--callers` lists who called the 64-bit divide helpers (`__udivdi3` etc.). They are leaves,
  so LR is the caller. Found 2026-10-01: `timeGetTime()` (limiter) and `perf_now_us()` (probes)
  divided a u64 per call; both now multiply by 2^24/60.75.
- Samples land only with MSR[EE] on. Time in exception handlers shows at the next instruction.
- Dolphin emulates the exception, so the profiler runs there. There it counts instructions
  (no cache misses), and its cycle estimate gives about 1.9x the samples of a Wii.
- On a Wii the idle thread dozes and PMC1 stops, so there are no idle samples: the shares
  are of busy time.
- **Re-arm with isync, interrupt off first.** Written as PMC1 then MMCR0 with no isync, a
  real Wii sometimes took a second performance-monitor exception at 0x00000F00 itself: HBC
  showed "performance monitor exception (15)", pc 00000f00, a crash with MSR[RI] clear that
  only the c_default_exceptionhandler wrap records. Before the wrap these runs came back
  with "no crash reported". Dolphin never showed it.

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
emulation runs below full speed (`dtm2autoinput.py` is for old movies only). Both ports are
recorded: port 2 lines read `p2 <vblank> <mask>`, and a script with any stands in for a pad
on port 2. The script draws both pads with their keys (`scripts/pad_layout.py`); a port 2
with nothing bound gets a keyboard layout (numpad D-pad) in the test profile's copy only.
`--ct 1` records with a DualShock (analog games). The recording's header names its build
(commit, DOL SHA-1), settings and pad type, and its chain line sets ControllerType: replay it
with that line, alone -- 4th in a chain with the wrong pad type it went its own way.

**Does a replay follow the session?** `python scripts/replay_check.py
.runs/capture_<name>_<date>/perf.log <replay>/perf.log --game N` compares only guest-side
counters (interrupts, CD reads, SIO/pad traffic, BIOS calls, GTE calls) at reports with the
SAME vblank -- reports come every 1800 presents, which depend on the host, so compare by
vblank, never by report number (2026-09-30: a "drift" that was a cut-short run's last report).
Host microseconds and OpenGX's drawn-primitive counts always differ and mean nothing here.
The `cd` field is `reads`/`bytes` only: `seq`/`rand` say how the host read the image (CdPrefetch
changes them). `--all` goes past the first difference: an offset that stays fixed is timing,
one that grows is a different game path.

**A CPU-core change moves every recording's timing.** 2026-10-01, Lightrec synced to upstream
(phase 2): `vsig_cmp.py` showed main and phase 2 part at vblank 1, in the BIOS, with the vblank
2-20 cycles apart at another pc. A dynarec services an event at the end of the block that
crosses it, so different block cuts deliver every IRQ a few cycles off. Spyro absorbed it
(same CD reads, pad polls and end VRAM); Crash 3 (variable frame rate) polled the pad 12 times
fewer by vblank 10800 and its input landed on other frames. That is not a bug: re-record. To
check a core change, A/B the same recording under both DOLs (`state:` lines, pad/CD counters)
rather than against an old capture log, which earlier core changes have already moved.

Fixed 2026-09-30 (ten-minute Spyro and Crash 3 sessions went out of step on replay, in Dolphin
and on the Wii): **port 2 must be plugged in on replay exactly as it was while recording.**
A session with a real pad 2 (movie_capture passes SIDevice1=6) has port 2 auto-assigned; the
replay had no pad there, `PadType2` defaults to none, and the game polled one pad instead of
two -- half the SIO interrupts from the first second. Now auto_assign_controllers() makes any
scripted port a GameCube-style digital pad (PlugPAD.c, and no Dolphin emulated Wiimote may
take it), and recordings with p2 lines carry `PadType2=1` in their chain line. Recordings
also stamp a change made at a vblank's second pad read (`<vblank> <mask> <read>`;
perf.log `pad: ... ambiguous=` counts unstamped lines a replay applied in a two-read vblank),
and the recorder no longer fsyncs on every change (a dropped frame per button press).

## Save states: run settings from one moment

Docs/SAVE_STATES.md. `wsx.sh state NAME GAME --at V [--rec REC]` saves a game at vblank V on
the test card; `wsx.sh abstate RUN NAME VBL SETS...` runs each settings group ("K=V,K=V", `-`
for none) from that moment for VBL vblanks, in one boot, and prints the table. A chain line's
`State=NAME` does the same by hand. Script lines: `state save|load NAME V`, `statefp V`,
`statecheck V N` (save, run N, fingerprint; load, run N, fingerprint; `match` or the parts
that differ, plus the bytes a load did not restore). A loaded state continues exactly as the
saved game did -- checked on seven games, and across boots -- because of four fixes found
this way (timers, events, SPU mixer, GPU draw state; see the doc). Run `statecheck` again
after changing anything a state holds.

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
- **Compiler (Lightrec/Lightning):** a change meant to emit the same code must give the
  same `jitcode: bytes= shape=` in every game of a chain (checksums of all emitted code,
  deps/lightrec/lightrec.c). `sum=` covers every bit, so it also moves when only the heap
  layout does (Lightrec emits `struct block` pointers as immediates): any change to
  allocation sizes or to BSS. `shape=` drops each word's low 16 bits. It can still move
  when the DOL's text layout moves, because `b`/`bl` from generated code to C functions
  carry displacement bits in the high half (a classify table that grew the text did this,
  2026-10-01). Then compare A and B builds with the same probes. VRAM alone is weaker proof. The upstream behaviour of the
  Lightning changes is one flag away: `make -C deps/lightning DEVKITPPC=... TMP=... TEMP=...
  EXTRA_CFLAGS=-DLIGHTNING_UPSTREAM=1`, then `wsx.sh build` (touch lightning.c after to go
  back). Compile cost per pass: `lopt:`, `lightning:`, `jitparts:`, `jit:` in perf.log.
  On 32-bit PPC, `__builtin_ctzll` is a libgcc call (`__ctzdi2`); check with
  `powerpc-eabi-nm build/lightning.o`.

## Probes that lie, and were fixed

Check a probe's cost and gate before trusting a debug-build share of wall: four debug-only
costs were charged to the GPU until 2026-09-22 (per-primitive sprintf, VRAM hashes, frame
hashes in UploadScreen, OpenGX texel checks). The pattern to grep for is a loop or a
`sprintf` whose only consumer is a log line; guard it with `logFileEnabled()`.

## Soft GPU accuracy and cost

- `scripts/softgpu_check.sh --compare before` -- six games on both soft plugins (0 old,
  1 new), statefp VRAM/RAM hashes at fixed vblanks. Emulation is deterministic, so any
  change to the soft rasterizer that changes a pixel shows as DIFFERENT. Use it for every
  soft GPU optimization; `--save NAME` stores a new baseline in `baselines/softgpu/`.
- Both soft plugins use one rasterizer, `SoftGPU/soft.c` (plugin 0 dispatches from
  `gpulib/oldGpu.c`, plugin 1 from `SoftGPU/gpulib_if.c`). `PeopsSoftGPU/` is not built.
- A `PROBES=deep` debug build writes `gpucmd:` (time per GP0 command) for every GPU
  plugin; `scripts/gpucmd_table.sh [perf.log]` sums it per chained game.
- `le16_store` (gpulib/gpu.h) has a memory clobber: every global is read again after each
  pixel store. Read the globals into locals before a span loop (see `drawPoly3Gi`).

## Controller and multitap tests (2026-10-02)

- **Replay a recording only as a chain** (`wsx.sh chain NAME FILE`, the chain line from the
  recording's header). `wsx.sh run --autoboot` boots by another path with other timing: the
  user's Crash Bash session went off course at once that way.
- An input script's port 1 (port 2) also drives multitap slot 1A (2A) when that port is a
  multitap (`script_port()` in PadSSSPSX.c), so a recording made with a pad plays a multitap game.
- `padtype <vblank> <port 1|2> <type 0..4>` in the script changes the port's type as the
  Configure Input menu does (`set_port_type()`, PlugPAD.c), with a perf report just before.
- perf.log `mtap: port= full= single= tap0= tap1= addr=a/b/c/d hle=` counts multitap replies
  (all four slots or one), the TAP byte the game sent, single-slot addresses and HLE BIOS polls.
  Crash Bash: TAP 1 on every poll, all replies full, no HLE polls.
- **Contact sheets with different steps are not comparable frame by frame**: `sheet.py ... 1
  $((f1/58+1)) $f1` picks other frames for runs with other frame counts. Two runs part where
  `vsig_cmp.py A/vsig_01.bin B/vsig_01.bin` says, not where two sheets look different.
- Automatic assignment raced the pads' first scan (a second Dolphin pad answered or not), and
  the game saw one or two players; auto mode now gives multitap slots their GameCube pads by
  position. Use `PadAutoAssign=0` and explicit `PadTypeN` for a deterministic pad setup.
- Crash Bash's pause text needs the GPU list walked in slices (`gpu_slow_llist_db`): every
  renderer honours `progress_addr` since 5.3.0 (Docs/GAME_HACKS.md).
- Soft Timed (gpulib) had its own frame limiter (SoftGPU/drawGX.c) that waited a frame after
  every call, late or not: in vtl, `limit` > 0 in a vblank that is already over 17 ms is that
  bug. It now uses the shared limiter (oldGpuFps.c FrameCap).
