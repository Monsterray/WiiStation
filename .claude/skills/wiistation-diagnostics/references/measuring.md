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

- **The controller matrix**: `bash scripts/padtest_dx.sh` (about two minutes, one Dolphin
  boot, ends in `matrix: N/12 cells PASS`, one PASS/FAIL line per cell). It runs PadTest DX
  2.4.0 (C:\projects\padtest, `bash build.sh` there first) once per setup: ports None / pad /
  multitap, slots filled or empty, ControllerType 0/1/2, a multitap -> pad -> multitap
  `padtype` switch, and two `SioTiming=1` cells that also judge byte and /ACK times.
  `--cells a,b` for some, `-v` for what each received, `--frames` for pictures;
  `python scripts/padtest_dx.py cells` lists them, `show VRAM.BIN` decodes one status block.
  The cells are a table in `scripts/padtest_dx.py` (CELLS); add a setup there, not a script.
- How the matrix tells slots apart: GameCube pad 1 (the only one in the test profile) plays
  port 1 or slot 1A through the driver (`padsweep 1 fast`); every other port and slot gets
  the script's `sweep`, each pressing the buttons in its own order, and the ROM records the
  frame of each button's first press. A slot fed by the wrong controller fails with
  "buttons in pX order, want pY". Multitap 2's slot A defaults to GameCube pad 1 as well
  (`PadAssign7=0`): the matrix sets `PadAssign7=1`, or both slot A's get the same pad.
- Script ports for slots: lines `p1b`..`p1d`, `p2b`..`p2d` (slot A = `p1`/`p2`), `sweep V
  p1b ...`, up to eight `padtype` lines (SETTINGS.md section 11). `script_port()` in
  PadSSSPSX.c maps pads to them.
- The input script's clock (`frame_counter`) ticks at the END of the frame (psxcounters.c,
  `HSyncTotal`), not at the vblank IRQ (`VBlankStart`), so a scripted button changes in the
  middle of a game's vblank-time pad reads. Two reads in one frame can differ. Not a game
  bug, but a test that compares two reads must allow it (the ROM's `long_diff` does).
- PadTest DX's own frame budget is in its status block (`last frame N scanlines, reading the
  ports A + B`, root counter 1): eight slots at `SioTiming=1` read in 136 of 263 lines.
  Formatting text with sprintf per byte once cost a quarter of the frame there.
- **Test the test on the old code**: the 5.3.0 PadSSSPSX.c/sio.c/psxbios.c over the new
  tree, with only script_port() and the sweep hooks added to PadSSSPSX.c, failed 10 of 10
  cells -- the evidence for every protocol fix of that commit. A matrix that passes on new
  code only shows the code matches its own model.
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

## Co-Op and the input menus (2026-10-04, 5.4.0)

- Co-Op (`PadType1/2` = 5, Gamecube/coop.c): players are virtual controllers 10..25
  (`COOP_VC`); mixing rules are `coop_buttons`/`coop_axis` in psx_analog.h, host-tested in
  tests/psx_analog_test.c. A script drives a Co-Op port like a pad (`script_port`), so a
  recording replays through it: `.runs/inputs/coop_crash.txt` must end in the same `state:`
  as the plain-pad replay. Mixing two controllers needs real pads (Dolphin: map GC pad 2).
- Menu screenshots: `menupage 20` (Configure Input), `21..36` (Customize, port*8+player)
  besides 1..10. The hook re-reads the script: a read before the SD card was mounted (the
  power-on controller assignment asks for scripted ports) found nothing and was kept, and
  every `menupage` silently did nothing (fixed 2026-10-04).
- **Style-A buttons (BTN_A_NRM/BTN_A_SEL) make themselves 56 high in their constructor**,
  whatever height they are given; a smaller row overlaps the next until `setBounds()` gives
  the height back. Labels are not clipped: text wider than its button runs over the edge.
  `setAutoSize(BTN_FIT_WIDTH)` + `setBounds()` fits a button to its label (+28 px).
- `scripts/menu_text_width.py` checks SettingsFrame's and OptionsFrame's tables (rows,
  gaps, label fits, logo). A new SettingsFrame button must be added to its TAB_ROWS, or the
  checker fails: button 79 (Stick D-pad, 5.3.0) was missing, and the row it checked then
  had uneven gaps and a too-narrow button. ConfigureInputFrame lays itself out in code
  (layoutTables) and is checked by screenshot only.
- **A `vram=` difference in chained games 4+ with equal `ram=`/`pc=`/`cycle=`** was the
  last 7..23 pixels of row 511 keeping game 3's data (CTR's 0x8000). OpenPlugins() points
  the shared psxVuw at gpulib's gpu.vram (globalVram + 4 KB, aligned up to 64) via
  renderer_set_config, and GL_GPUinit cleared only its own smaller VRAM_SIZE from
  globalVram[0]: how much was left moved with the linker's placement of globalVram, so
  every build had its own hash (first 0x8000 pixel at x 1001/1011/1015/1017 across old
  runs). Both inits now clear the whole array. Find such a cause by diffing vram_NN.bin
  (find the first differing pixel) before suspecting the guest.
- **Button sizing is CSS-like (5.4.2):** `setPadding(all | v,h | t,r,b,l)` (default 10/16,
  was 8/14 as the totals 16/28 of the removed `setAutoSizePadding`), `setMinWidth`,
  `setMaxWidth` (a label too wide for the max is drawn smaller). Fitted widths are whole
  even pixels: style-A images are two mirrored halves and a fractional x or width shows a
  seam down the middle. Configure Buttons fits its mapping buttons every frame
  (fitMappingButtons): min = table width, max = half the free space to a neighbour beside it.
  `menupage 40..65` photographs it per virtual controller.
