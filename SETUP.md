# Setting up WiiStation

This takes you from nothing to WiiStation running, in three levels. Each level works without the
next one: stop where you want. Windows 10/11 is assumed (Linux works with the same names and your
own paths).

## Level 0: get the code

```bash
git clone --recursive https://github.com/Monsterray/WiiStation.git
```

Already cloned without `--recursive`? Run `git submodule update --init` in the clone.

## Level 1: build `boot.dol`

1. Install devkitPro with its graphical installer (https://devkitpro.org/wiki/Getting_Started).
   Tick "Wii development". It installs to `C:\devkitPro`.
2. Open the devkitPro shell (Start menu, "MSYS2", from devkitPro) and go to the clone, for
   example `cd /c/projects/WiiStation`.
3. `sh scripts/setup-devkitpro-windows.sh`. It installs the Wii libraries this project needs
   (pacman packages, plus a pinned old compiler for the old `LIBOGC=2` build).
4. `scripts/wsx.sh build release`. It prints the DOL: `Gamecube/WiiSXRX_Release.dol`.

The build finds devkitPro at `C:\devkitPro`. If you installed it somewhere else, put
`DEVKITPRO=<folder>` in `~/wii-tools.env` (see "Tool paths" below).

## Level 2: run it in Dolphin

1. Install Python 3.10 or later (https://www.python.org/downloads/; tick "Add python.exe to PATH").
2. Download Dolphin (https://dolphin-emu.org/download/, the development version zip) and unzip
   it to a folder of its own, for example `C:\tools\Dolphin-x64`. Make an empty file
   `portable.txt` next to `Dolphin.exe`, so Dolphin keeps its settings and SD card in that folder.
3. In the clone: `python scripts/tools.py setup`. It finds Python, Dolphin and devkitPro, writes
   `~/wii-tools.env`, and asks for a game-library folder (any empty folder, for example
   `C:\wiistation-lib`; the test scripts use it, and it can stay empty for now).
4. Put your games in Dolphin's SD folder: `<Dolphin folder>\User\Load\WiiSDSync\wiistation\isos\`
   (`.cue`/`.bin`, `.chd`, `.pbp`, `.iso` and more). They must be your own discs.
5. Optional: a PS1 BIOS from your own console in `...\WiiSDSync\wiistation\bios\` (for example
   `SCPH1001.BIN`). Without one, WiiStation uses its built-in BIOS, which runs most games.
6. From the devkitPro shell: `sh scripts/wiistation_play.sh release`. Dolphin opens WiiStation;
   pick a game with the GameCube pad or keyboard (Dolphin's Controllers settings).

`python scripts/tools.py check` shows each tool, where it is, and which levels work.

## Level 3: run it on a real Wii

You need a Wii with the Homebrew Channel, on the same network as the PC. Find the Wii's address
in the Homebrew Channel (press Home; it is shown at the top), then add it to `~/wii-tools.env`:

```
WII_IP=192.168.1.50
```

**Try it once, from memory:** with the Wii in the Homebrew Channel,

```bash
python deps/hbc-reborn/tools/hbc.py --wii 192.168.1.50 send Gamecube/WiiSXRX_Release.dol
```

WiiStation starts. It reads games from the Wii's SD card (or USB drive), folder
`/wiistation/isos/`, and a BIOS (optional) from `/wiistation/bios/`. Copy them there with a card
reader.

**Install it on the SD card** (so the Homebrew Channel lists it, with its fonts and languages):
the install script needs hbc-reborn's Homebrew Channel running, which adds file commands. It runs
from memory; nothing is installed on the Wii:

1. Download `hbc-1.16.0.dol` from https://github.com/Monsterray/hbc-reborn/releases.
2. `python deps/hbc-reborn/tools/hbc.py --wii 192.168.1.50 send hbc-1.16.0.dol`
3. `python scripts/wii_install.py`

The script copies only new or changed files and deletes nothing, so your settings, saves and
games on the card stay.

## Tool paths: `~/wii-tools.env`

Every script finds tools through one file in your home folder, one `NAME=path` line per tool.
`python scripts/tools.py setup` writes it; edit it by hand any time. A variable of the same name in
the environment wins, for a one-off. A script that needs a missing name stops with one line saying
which name and which part of this file.

| Name | What | Needed for |
|---|---|---|
| `DEVKITPRO` | devkitPro folder (default `C:/devkitPro`) | level 1 |
| `PYTHON` | `python.exe` | level 2 |
| `DOLPHIN` | `Dolphin.exe` | level 2 |
| `GAME_LIBRARY` | the game library for the test scripts | level 2 (test runs) |
| `WII_IP` | your Wii's address | level 3 |
| `WII_BENCH` | the wii-bench queue's `wiibench.py` | extra: queue |
| `WIICAP` | `wiicap.py` (HDMI capture card) | extra: capture |
| `DOLPHIN_MOVIE` | dolphin-movie checkout | extra: movie recording |
| `PADTEST` | PadTest DX checkout | extra: controller tests |
| `PS1_TESTS` | ps1-tests folder | extra: accuracy tests |
| `CHDMAN` | `chdman.exe` (MAME) | extra: disc conversion |
| `FFMPEG` | `ffmpeg.exe` | extra: audio and video compares |
| `SEVENZIP` | `7z.exe` | extra: archives |

## Extras

None of these is needed to build, play or install. Each one is for a kind of testing.
The test scripts also need Pillow: `python -m pip install pillow`.

- **Queue (`WII_BENCH`).** For a workshop with several PCs or agents and shared test Wiis: the
  wii-bench queue gives one job at a time the Wii. With `WII_BENCH` set, every Wii script runs
  only as a queue job; without it, scripts talk straight to `WII_IP`. See `Docs/BENCH_TESTING.md`.
- **Unattended Dolphin runs** (`scripts/wsx.sh run`, `chain`, `state`): need `GAME_LIBRARY` laid
  out as `games/<game folder>/` plus `games.tsv` (`python scripts/library_check.py` checks it).
  See `.claude/skills/wiistation-diagnostics`.
- **Capture card, movie recording, PadTest DX, ps1-tests, chdman, ffmpeg, 7-Zip:** set the name
  in the table above; the script that needs it says so.

## Safety on shared Wiis

`scripts/wii_targets.py` lists Wiis that test scripts must never touch (a family's everyday Wii,
for example), by address. Every Wii script refuses them, with or without the queue. Add yours
there or in `$WII_PROTECTED` (comma-separated).
