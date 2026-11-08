# The play build: a debug WiiStation to play with

`WiiSXRX_play.dol` is a debug build made for ordinary play on a real console. It looks and
plays like the release build (no debug text on the screen; the FPS line only when **Show FPS**
is on, and then the release one), and it keeps a record of every game session on the SD
card. Afterwards the record says what was slow, what went wrong, and in which game and scene.

It is installed as a second app, **WiiStation (debug)**, beside the normal WiiStation. Both
use the same settings, saves, memory cards and games.

## Build and install

```bash
bash scripts/wsx.sh build debug play          # Gamecube/WiiSXRX_play.dol (+ .elf: keep it)
```

Bench Wii (always through the queue):

```bash
python "$WII_BENCH" add --cwd <repo> \
    --name "WiiStation debug install" -- python scripts/wii_install.py --play-build
```

Monty's own Wii, only when he asks (run directly, not queued). It checks first that HBC is
idle on the TV, then writes `sd:/apps/WiiStationDebug/` (boot.dol, meta.xml, icon.png) and
nothing else:

```bash
python scripts/wii_install.py --play-build --production
```

`--dry-run` stages the files in `.runs/wii_install/` and talks to no Wii. The meta.xml says
`WiiStation (debug)` and the version of the build (`5.15.0-dev <commit>`).

## Play

Start **WiiStation (debug)** from the Homebrew Channel and play as usual. Nothing has to be
switched on. Quit, loading another game, or the power button (it goes through the menu) close
the session with an end line. A crash, a hang, a reset or pulling the plug leave none: the
heartbeat then says what WiiStation was doing last.

## What it writes, and where

Everything goes to **`sd:/wiistation/debuglogs/`**. Nothing else on the card is written
(no settings, saves, cards or states; screenshots and save states only when you make them,
as in the release build).

| File | What | Bound |
|---|---|---|
| `play.log` | One block per game session: start line (date, build, game ID, file), the settings file read, every numeric setting each time the game starts from the menu (`play: go`), a perf report every 1800 frames and at every return to the menu, a crash or hang that HBC kept from an earlier run (`crash:`), a fatal stop (`fatal:`), and the end line. | over 4 MB it becomes `play_old.log` (the older one is deleted) |
| `vtl_<date>-<time>.bin` | The session's timeline: for every emulated vblank, the time it took and where it went (CPU core, GPU, sound, recompiler, frame limiter), and the frames shown. 14 bytes a vblank, 3 MB an hour. | 16 MB a session (about 5 hours); at each session start the oldest files go while all of them take more than 32 MB |
| `hb.log` | The heartbeat, every 2 s: what WiiStation is doing (menu, loading, game), the frame counts, and any system call that has not come back for half a second. Shows where a freeze froze. | over 1 MB it becomes `hb_old.log` |
| `dvd.log`, `pcring.log` | Only when loading from DVD, and only with the interpreter CPU core when the guest jumps out of code. | small |

So the folder stays near 60 MB at most. Settings that hold text (folders, share names and
logins) are never written.

**No stalls.** The emulator thread never writes the card. Log lines and timeline records go
into two RAM queues (64 KB each, in MEM2); the heartbeat thread appends them to the files every
2 s (a few KB), and the queues are emptied at the safe points (a game closing, a fatal stop). If
the card cannot keep up, what does not fit is dropped and counted on the session's end line
(`text_drops=`, `vtl_drops=`); the game never waits for it. The one contact left: while the
heartbeat thread appends, a CD read from an image on the same card waits for the card (a few
milliseconds), as the debug build's hb.log has always done.

**What it does not record:** the primitive trace, the per-transfer VRAM log, the per-vblank RAM
hash (vsig), the audio and pad timelines, the VRAM dump at vblank 6000. Those belong to the
`light`/`all` debug builds and Dolphin runs (skill `wiistation-diagnostics`, references/probes.md).

## Getting the logs back

- Take the SD card to the PC and copy `wiistation/debuglogs/` (the whole folder, or
  `play.log`, `hb.log` and the `vtl_*.bin` of the sessions in question).
- Or over the network, read-only: `hbc.py get sd:/wiistation/debuglogs/play.log` (and
  `hb.log`, then the one `vtl_*.bin` wanted), small files only, under the rules for each Wii in
  AGENTS.md (bench: through the queue).

A crash or hang also stays in HBC's crash block: `hbc.py crash --elf WiiSXRX_play.elf` /
`hbc.py lastlog` (read-only). The play build copies that block into `play.log` at its next
start, so the card has it too.

## Reading them

```bash
python scripts/play_logs.py FOLDER            # one block per session
python scripts/vtl_view.py FOLDER/vtl_<id>.bin --around VBLANK   # one stretch, vblank by vblank
```

`play_logs.py` prints, per session: the game and file, the build, when and how long; the
settings file and the main settings, and any changed in the menu between stretches; speed and
load of the whole session and of each stretch between menu visits; the slowest runs of
vblanks from the timeline, with when they happened (time played, which stretch); and how it
ended: the end line, a fatal stop, a crash HBC reported, or no end line and the last two
heartbeat lines. The perf reports in `play.log` are the usual `perf.log` blocks, so
`scripts/chain_table.py` fields and the skill's references/probes.md explain each line.

## What it costs

Measured in Dolphin, 2026-10-19 (`scripts/chains/play_cost.txt`, `--fast`, Wii time): each game
runs 14400 vblanks with the frame limit off, so the emulated seconds it takes are its cost.
Same build, same game: the same seconds to the second on a second run.

| Game, frame limit off | Release | Play build | Debug (`light`) |
|---|---|---|---|
| Spyro (3D) | 118 s | 120 s (+1.7%) | 132 s (+12%) |
| Crash Bash (2D, streaming) | 103 s | 105 s (+1.9%) | 117 s (+14%) |
| Micro Machines V3 (FMV) | 134 s | 143 s (+6.7%) | 156 s (+16%) |
| All three | 355 s | 368 s (+3.7%) | 405 s (+14%) |

At normal speed every game ran at 1.00x in all three builds; the play build's load (the share
of the time the emulator works) was 38% / 46% / 35%, the debug build's 43% / 51% / 41%. What
the play build leaves out against `light`: the per-vblank RAM hash (vsig), the on-screen text
and its formatting (every flip and every MTC0), and the SD writes from the emulator thread.
It keeps Lightrec's asserts (the debug Lightrec, so a play session finds its bugs; the same
seconds as release's Lightrec in Dolphin). Dolphin
counts instructions, not cache misses: the bench Wii can differ, read the load in `play.log`.

The screen: frame dumps of release and the play build (Show FPS on, Crash 3 and MediEvil, 568
frames) are identical outside the FPS line; the FPS line differs only in its digits. The
`light` debug build differs in 364 of the 568 frames (its debug text).

## For developers

- Build preset: `scripts/build.sh` `PROBES=play`: `-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0
  -DPERF_PROF_VSIG=0 -DPERF_PLAY=1 -UDISP_DEBUG -USHOW_DEBUG` on top of the debug Makefile, and
  the debug Lightrec (`lightrecWithLog`, asserts on); built in `Gamecube/build_play/`, output `WiiSXRX_play.dol`/`.elf`.
- `Gamecube/play_log.c` holds the queues, the files and the session lines; `perf_log_open()` /
  `perf_log_close()` (perf_prof.h) are where every `perf.log` line goes in every build (the
  plain file in the others). `GamecubeMain.cpp`: session start in `loadISO()`, end in
  `SysClose()` and at a chain's end, `play: go` / `play: menu` in `go()`.
- A Dolphin run of the play build brings the folder back as `.runs/NAME/debuglogs/`
  (`dolphin_run.sh`); `scripts/chains/play_cost.txt` measures the cost against release.
