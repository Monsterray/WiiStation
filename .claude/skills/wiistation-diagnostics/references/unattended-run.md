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
frame dumping slows Dolphin (allow 1.5x). Frame index ≈ vblank for NTSC titles.

Only one Dolphin may run: `tasklist | grep -i dolphin` before every run; if it is the user's,
wait or ask. The frames of a previous run must be moved out before the next.

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
FPS = 0              # no overlay in the frames
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

## Reading the results

- `python scripts/ptrace_summary.py <outdir>/ptrace.log [--big AREA] [--show N]` — per-present
  entry counts, control commands (fill / draw area / display), big primitives, semi-transparent
  ones, and one line per VRAM→CPU read with mapping, capture result, merged pixels, state bits.
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
