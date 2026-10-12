# WiiStation's folders, and moving from `wiisxrx` to `wiistation`

Since 5.6.0 WiiStation reads and writes everything under **`<device>:/wiistation/`**, where
`<device>` is `sd` or `usb`. Older versions (WiiSXRX, WiiStation up to 5.5) used `/wiisxrx/`.
WiiStation does **not** move anything for you, and it never reads `/wiisxrx/` any more. This
page says what goes where, for players and for anyone running the test tools.

## The layout

| What | New place | Old place | Notes |
|---|---|---|---|
| Settings | `wiistation/settings.ini` | `wiisxrx/settingsRX2022.cfg` | New format, in [sections]. Every old key still exists with the same name and values; `wiistation/example_settings.ini` lists them all with their defaults. |
| One game's settings | `wiistation/settings/<serial>.ini` | (none) | Written by Settings > General > Save Settings > **This Game**. |
| Memory cards | `wiistation/saves/*.mcd` | `wiisxrx/saves/` | Same file format: copy them across. |
| Save states | `wiistation/savestates/` | `wiisxrx/savestates/` | States from 5.5 and older may not load in newer versions. |
| BIOS | `wiistation/bios/SCPH1001.BIN` | `wiisxrx/bios/` | Also `SCPH5501`, `SCPH1000`, `SCPH5500` (NTSC) and `SCPH1002`, `SCPH5502` (PAL). |
| Button layouts | `wiistation/control*.cfg` | `wiisxrx/control*.cfg` | Same format (version 1): copy them across. |
| Games | `wiistation/isos/` | `wiisxrx/isos/` | Or leave them where they are and list the folder in `RomDirSD` / `RomDirUSB` (below). |
| Fonts, languages, default button maps | `wiistation/fonts`, `lang`, `controllers` | `wiisxrx/fonts` | Come with the release zip; do not copy old ones. |

The app itself stays in `apps/WiiStation/`.

## Moving a player's card (5.5 or older to 5.6 or newer)

1. **Back up** the whole `wiisxrx` folder to a PC first.
2. Make `wiistation/` and copy into it: `saves/`, `bios/`, `control*.cfg` (and `savestates/` if
   you want to try them).
3. Settings: either start from `example_settings.ini` (rename it `settings.ini`) and set
   things again in the menu, or copy your values over -- every key name is the same as in
   `settingsRX2022.cfg`.
4. Games: move `wiisxrx/isos` to `wiistation/isos`, **or** keep them and add the folder:
   `RomDirSD = "/wiistation/isos, /wiisxrx/isos"` (a comma-separated list; `RomDirUSB` the same
   for USB). Load from SD then shows both folders.
5. Leave `wiisxrx/` until everything works, then delete it if you want the space.

This is what was done for the maintainer's own Wii on 2026-10-09 (backup in
`C:\tools\wii-backups\production-2026-10-09` on the dev PC): data copied, games left in
`wiisxrx/isos` and listed in `RomDirSD`.

## Test tools and automation

Everything in this repo uses the new paths. If you have your own setup, check these:

- **Chain files** (`scripts/chains/*.txt`, your own): the game folder line is
  `sd:/wiistation/isos/<Game>` (or `usb:/wiistation/isos/<Game>`); input scripts named on a chain
  line are `sd:/wiistation/<name>.txt`.
- **Single-game autoboot files**: `wsx.sh run --autoboot spyro|bios` stages
  `autoboot_*.txt` from **`C:/tools/Dolphin-x64/User/Load/WiiSDSync_paused_by_claude/`** -- a
  folder outside the repo, so a search of the repo does not find old paths in it. They were
  still `sd:/wiisxrx/...` until 2026-10-09 (the run then sat in the menu); fixed.
- **Dolphin profiles** (`.dolphin`, `.dolphin2`, `.dolphin_*`): test games go in
  `<profile>/Load/WiiSDSync/wiistation/isos/`, BIOS in `.../wiistation/bios/`. Old save-state notes
  are left in `.dolphin/Load/WiiSDSync/wiisxrx/states/` and are not used.
- **The bench Wii** (192.168.8.213): `sd:/wiistation/{isos,bios,saves,fonts,lang,controllers}` and
  `usb:/wiistation/isos` (Gex 3 lives on USB: the SD card is full). There is no `wiisxrx` on it
  (checked 2026-10-09). Upload with `hbc.py put` through the bench queue, using **Windows paths**
  (`C:/...`): `hbc.py` is a Windows Python and cannot open `/c/...` paths.
- **Code**: the defaults are in `Gamecube/fileBrowser/fileBrowser-libfat.c` (`biosDir_libfat_*`,
  `saveDir_*`), `Gamecube/GamecubeMain.cpp` (settings paths, `romDirSD`/`romDirUSB`) and
  `Gamecube/PadWiiSX.c` (`sd:/wiistation/autoinput.txt`).

To find leftovers in your own setup:

```bash
grep -rln "wiisxrx" scripts/ .runs/*.txt /c/tools/Dolphin-x64/User/Load/WiiSDSync_paused_by_claude/
```
