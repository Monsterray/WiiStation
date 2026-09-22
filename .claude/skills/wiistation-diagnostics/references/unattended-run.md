# Unattended runs in Dolphin

## Layout on this machine

| Thing | Where |
|---|---|
| Dolphin | `C:\tools\Dolphin-x64\Dolphin.exe`, user dir `C:\tools\Dolphin-x64\User\` |
| SD sync folder (master; synced INTO the image at launch, never back on a kill) | `User\Load\WiiSDSync\wiisxrx\` |
| SD image (locked while Dolphin runs) | `User\Load\WiiSD.raw` |
| Staged test files kept out of the way | `User\Load\WiiSDSync_paused_by_claude\` (autoinput*.txt, settings*.cfg, autoboot_bios.txt) |
| Frame dumps | `User\Dump\Frames\framedump_<n>.png` (824x480) |
| Dolphin log | `User\Logs\dolphin.log`, timestamps are `MM:SS:mmm`, not hours |
| Builds | `Gamecube\WiiSXRX_debug.dol` (run this), `Gamecube\WiiSXRX_Release.dol` (never boot the release .elf under Dolphin: unzeroed BSS crashes) |

## The short loop: `scripts/wsx.sh`

Use this first; it exists so that a build-run-read cycle costs three short commands and
three short outputs instead of long paths, log tails and three analysis tools.

```bash
scripts/wsx.sh build debug light          # or: build release. Prints the DOL and time, or the first errors.
scripts/wsx.sh run rc_cpu                 # debug DOL, Spyro speech script, audio dump, then the summary
scripts/wsx.sh run rc_dsp --dsp           # DSP sound path: SoundHwAccel=1 + Dolphin DSP LLE
scripts/wsx.sh run legacy --dol release --set SoundRateControl=0,SoundTempo=1
scripts/wsx.sh run title --input title --secs 120 --nodump
scripts/wsx.sh summary rc_cpu rc_dsp legacy   # one block per run and a comparison table
scripts/wsx.sh runs                       # what is in .runs/
```

- Each run directory (`.runs/NAME`, gitignored) holds `boot.dol` (a copy, so a rebuild
  between queued runs cannot change what boots), `settings.cfg`, `autoinput.txt`,
  `run.info` (dol, settings, env, commit), `run.log` and everything dolphin_run.sh collects.
- `build` refuses while Dolphin is running (a compile skews the run's timing; `FORCE=1`).
- `run_summary.py` reads `perf.log` (last `xa:`/`xamix:`/`rate:` lines), `atrace.log`
  (core speed, XA gaps, rate-control nudge and queue per 10 s) and the largest
  `*dspdump*.wav` (runs of exact digital silence 3..400 ms: count, total, histogram, when;
  longer ones are the game's own silences). `--detail` lists each silence run.
- Input scripts live in `scripts/autoinput/` (`spyro_speech.txt`: title, new game, skip the
  intro, stay in the level with the dragon speech, audio timeline from vblank 2500;
  `spyro_title.txt`: the same with the timeline from vblank 700).
- Queue several runs in one shell line (`run a ...; run b ...; summary a b`) and read one
  table; a Dolphin run cannot overlap another (SD image lock), and nothing else should run
  alongside it if timing is being measured.
- Editing sources from a tool: `scripts/patch_text.py SPEC.py` applies exact-match edits
  from a Python spec (`EDITS = {path: [(old, new), ...]}`) with the file's own line ending
  and refuses anything that does not match exactly once. Write the spec with the Write
  tool: shell heredocs lose backslashes.

## The loop: `scripts/dolphin_run.sh`

```
KEEP=3200 SHEET_STEP=150 bash scripts/dolphin_run.sh <outdir> <seconds> [autoinput] [settings] [autoboot]
```

It: backs up and edits `Dolphin.ini` (`[Movie] DumpFrames = True`), `GFX.ini`
(`[Settings] DumpFramesAsImages = True`, `PNGCompressionLevel = 1`), `Logger.ini`
(`[Logs] FRAMEDUMP = True`); stages the three files into the sync folder (autoboot only if
given, restoring the original after); runs `Dolphin.exe -b -e <debug .dol>`; sleeps; kills
Dolphin; restores everything; keeps the last `KEEP` frames in `<outdir>/frames/`; extracts
`wiisxrx/ptrace.log` and `vram.bin` with 7-Zip; reads `perf.log` with
`scripts/sdimage_read.py` (7-Zip reports "Data Error" on a file whose directory entry was
updated after the last cluster flush, which a kill always leaves behind); prints the last
`sio:`/`offsoft:` counter lines; builds `sheet.png` with `scripts/sheet.py`.

Timing: boot to menu ≈ 15 s, autoboot into a game ≈ 20 s; a PSX vblank is 1/60 s of emulated
time and the emulation runs near real time, so vblank N happens ≈ 20 + N/60 s after launch;
frame dumping slows Dolphin (allow 1.5x).

**Frame index is the PRESENT index, not the vblank.** Dolphin writes one PNG per XFB it is
given, so a game that swaps buffers every second vblank produces one dump per two emulated
frames and no duplicates at all (measured on Crash 3's title: 1215 presents over 2652
vblanks, 0 of 118 consecutive dumps identical; `perf.log`'s `--- perf frames=N ---` is the
present count and `wall: vblanks=` the vblank count). Treating a dump index as a vblank
silently doubles every animation period you measure. The debug build's EC trace entries
carry the vblank of each present, which is also how you tell an even cadence from a
stutter. Two runs are comparable frame-for-frame only while both present at the same rate;
a debug DOL reaches a given scene at a similar present index but a much later wall time.

Only one Dolphin may run. `dolphin_run.sh` refuses to start while a `Dolphin.exe` exists and
kills only the PID it launched; earlier versions killed by image name and closed the user's own
session twice. The symptom of a collision is `Rename failed ... Access is denied` in dolphin.log
and a boot without SD card (no autoboot, no frames). The frames of a previous run must be moved
out before the next.

## Files that drive the run

**`autoboot.txt`** (SD sync folder, read after settings load): line 1 the ISO folder, line 2
the file name (matched with `strcasestr`, so it must be unique); or just `BIOS` to run the
BIOS shell via the menu's Execute Bios path. Delete it (or pass an empty file as the run
script's autoboot argument, `WiiSDSync_paused_by_claude/autoboot_none.txt`) to boot to the
WiiStation menu itself — that is how menu rendering (fonts, buttons) gets frame-dumped. The
menu cannot be navigated by autoinput.txt (it reads the host pads, not the PSX port), so only
the first screen is reachable unattended. A savestate-
anchored Dolphin movie (`.dtm` with `from_savestate=1`) cannot replace it: it restores the
recorded binary, so code changes are invisible under it.

**`settingsRX2022.cfg`** (bare `key = value`, unlisted keys keep compiled defaults — NOT the
user's values; per-game files in `wiisxrx/settings/<CdromId>.cfg` override it when the game
has one). Keys that unattended runs need:

```
gpuPlugin = 2        # 0 Old Soft (ground truth), 1 New Soft, 2 OpenGX
FPS = 1              # the user wants the FPS overlay visible while runs play; it lands in the frames too
PadType1 = 1         # sio.c polls a port only when padType[0] != 0
PadAutoAssign = 0    # otherwise auto-assign resets PadType1 to 0 when no host pad exists
BiosDevice = 1       # for BIOS runs; SCPH1001.BIN in wiisxrx/bios/
```

All keys are documented in `SETTINGS.md`.

**`autoinput.txt`** lines `<vblank> <hex PSX mask>` (Start 0008, Cross 4000, Circle 2000,
Triangle 1000, Square 8000, Up 0010, Right 0020, Down 0040, Left 0080, Select 0001), plus
`trace <vblank>` (up to 8; arms a 16-present primitive-trace episode) and `dump <vblank>`
(one VRAM dump + a perf report at that moment). Parsed when the pad plugin opens
(`autoinput_load()` in `Gamecube/PadWiiSX.c`); a script with presses also makes port 1 report
a connected digital pad. Known timelines: Spyro title Start ~1400-1500, level pause ~2100 or
~3634 with the recorded movie; BIOS shell menu visible from ~600, X at 1500 opens Memory
Card, Triangle back, Down, X opens CD Player.

**Movies → scripts:** `scripts/dtm2autoinput.py MOVIE.dtm --offset N [--trace-after-start]`
decodes a Dolphin `.dtm` (8-byte pad polls interleaved with Wiimote records, 2 polls per VI)
into autoinput lines; find the offset once from the first Start press.

## Keeping a run as a baseline

Runs worth comparing against later go into `baselines/` with `scripts/baseline_add.py <run dir>
--id <date>_<game>_<variant> --game ... --purpose ... --settings <staged cfg>` (see
`baselines/README.md`). It keeps perf.log, the run log, the settings and a notes page in git,
the audio (as FLAC) and the contact sheet under `baselines/media/` outside git, and appends
the key metrics to `baselines/index.csv`. Compare with `perf_compare.py`, `wav_compare.py`,
`wav_spectrum.py` (both audio tools read FLAC). The scratchpad is per session and was 9 GB of
frame PNGs before this existed: file what matters, then delete `frames/` directories.

## Reading the results

- `python scripts/ptrace_summary.py <outdir>/ptrace.log [--big AREA] [--show N]` — per-present
  entry counts, control commands (fill / draw area / display), big primitives, semi-transparent
  ones, one line per VRAM→CPU read with mapping, capture result, merged pixels, state bits,
  and the screen re-uploads (EA/EB) and presents (EC).
- `python scripts/frame_cycle.py <frames dir> FIRST LAST [--period N] [--grid] [--map OUT.png]`
  — what changes between consecutive dumps and where: changed pixels, bright→dark against
  dark→bright (equal both ways is motion, one-sided is content being lost), the same count
  at six thresholds so a threshold artefact is obvious, a per-cell map of which part of the
  picture moves, and an image with the changing pixels in red. Reach for it before
  believing any "N things vanish per frame" count taken at one brightness threshold.
- `python scripts/vram2png.py vram.bin out.png [--crop X,Y,W,H] [--scale N]` — the 1024x512
  VRAM; display buffers at x<640 (or 512), texture pages and off-screen scratch to the right.
- `python scripts/sheet.py out.png frames_dir START STEP END [cols] [w]` — contact sheet with
  frame numbers burned in; then open single frames with the image reader.
- `python scripts/sdimage_read.py WiiSD.raw wiisxrx/<file> out` — any file from the image.
- Compare plugins by running the same script under `gpuPlugin = 0` and `2` and diffing
  `vram.bin` (texture pages identical ⇒ CPU side fine; display buffers differ by design).

## Restoring the user's setup

The script restores INIs and removes the staged files; `autoboot.txt` stays because the
user's own movies depend on it. Leave `WiiSDSync_paused_by_claude/` as the parking place.
Two permanent GFX.ini changes are deliberate (see dolphin-pitfalls.md).
