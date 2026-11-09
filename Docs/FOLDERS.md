# WiiStation's folders, and moving from `wiisxrx` to `wiistation`

Since 5.6.0 WiiStation reads and writes everything under **`<device>:/wiistation/`**, where
`<device>` is `sd` or `usb`. Older versions (WiiSXRX, WiiStation up to 5.5) used `/wiisxrx/`.
WiiStation does **not** move anything for you, and it never reads `/wiisxrx/` any more. This
page says what goes where, for players and for anyone running the test tools.

## The layout

| What | New place | Old place | Notes |
|---|---|---|---|
| Settings | `wiistation/settings.ini` | `wiisxrx/settingsRX2022.cfg` | New format, in [sections]. Every old key still exists with the same name and values; `wiistation/example_settings.ini` lists them all with their defaults. |
| Players | `wiistation/players.ini` | (5.18.0, 5.18.1: `[Players]` in `settings.ini`) | The Mii seated on each port, the Official Mii and the account links (`[Accounts]`); the Players menu writes it when it closes. Old seat keys in `settings.ini` are still read. |
| Mii accounts | `wiistation/achievements/ra/<name>.ini` | (new in 5.19.0) | One RetroAchievements account per file, lines as in `ra_account.ini`; a Mii's Edit page links one by its name. |
| Local unlocks of a RetroAchievements game | `wiistation/achievements/ra_local/<game id>.unlocks` | (new in 5.19.0) | Official mode: the Miis besides the Official one earn the game's set here, as in a Local journal (`Docs/ACHIEVEMENTS.md`). Made by WiiStation. |
| One game's settings | `wiistation/settings/<serial>.ini` | `wiistation/settings/<serial>.cfg` | Written by Settings > System > Save Settings > **This Game**: only the settings that differ from `settings.ini`, which the game reads first (SETTINGS.md 1.4). 5.6.1 to 5.13.1 wrote `.cfg` (and never read it); the `.cfg` is read when there is no `.ini`. |
| Memory cards | `wiistation/saves/*.mcd` | `wiisxrx/saves/` | Same file format: copy them across. |
| Save states | `wiistation/savestates/` | `wiisxrx/savestates/` | States from 5.5 and older may not load in newer versions. |
| BIOS | `wiistation/bios/SCPH1001.BIN` | `wiisxrx/bios/` | Also `SCPH5501`, `SCPH1000`, `SCPH5500` (NTSC) and `SCPH1002`, `SCPH5502` (PAL). |
| Button layouts | `wiistation/control*.cfg` | `wiisxrx/control*.cfg` | Same format (version 1): copy them across. |
| Recent games | `wiistation/recent.ini` | - | The last 20 games loaded from the explorer, newest first. See SETTINGS.md 1.6. |
| Cover art | `wiistation/covers/` (`usb:` too) | - | A pack made by `scripts/cover_pack.py` (`index.txt` plus the images); the explorer shows a game's cover from it. |
| Games | `wiistation/isos/` | `wiisxrx/isos/` | Or leave them where they are and list the folder in `RomDirSD` / `RomDirUSB` (below). |
| Fonts, languages, default button maps | `wiistation/fonts`, `lang`, `controllers` | `wiisxrx/fonts` | Come with the release zip; do not copy old ones. |
| Debug logs | `wiistation/debuglogs/` | - | Only the debug app, WiiStation (debug), writes here (60 MB at most): [DEBUG_PLAY.md](DEBUG_PLAY.md). |

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
a backup folder outside the repository (`production-2026-10-09`) on the dev PC): data copied, games left in
`wiisxrx/isos` and listed in `RomDirSD`.

## Test tools and automation

Everything in this repo uses the new paths. If you have your own setup, check these:

- **Chain files** (`scripts/chains/*.txt`, your own): the game folder line is
  `sd:/wiistation/isos/<Game>` (or `usb:/wiistation/isos/<Game>`); input scripts named on a chain
  line are `sd:/wiistation/<name>.txt`.
- **The test layout (library, Dolphin profiles and the bench Wii's card, since 2026-10-21):**
  `isos/<Game>/` for each real game (multi-disc games hold one folder per disc),
  `isos/Formats/<Game> [FORMAT]/` for the format-test copies (Frogger, MediEvil), and
  `isos/Tests/` for test programs (PadTest, PadTestDX, gtetest, ps1t_*). The library
  (`C:/wiistation-lib/games`) is the master: `scripts/card_sync.py D:/ [--apply]` makes a
  mounted bench card match it (it copies, never deletes). Big games go on the card that way:
  the HBC agent's network put stops at 512 MB a file.
- **Single-game autoboot files**: `wsx.sh run --autoboot spyro|bios` stages
  `autoboot_*.txt` from **`<Dolphin install>/User/Load/WiiSDSync_paused_by_claude/`** -- a
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
grep -rln "wiisxrx" scripts/ .runs/*.txt <your Dolphin folder>/User/Load/WiiSDSync_paused_by_claude/
```

Disc art: `scripts/disc_art_fetch.py` downloads the round disc label (ScreenScraper media `support-2D`) for the games in `games.tsv` into `GAME_LIBRARY/cover_sources/disc/<SERIAL>.png` (re-runnable, `--dry-run`, `--limit N`, `--only SERIAL`; login from `GAME_LIBRARY/screenscraper.ini`). Then `cover_pack.py --disc-dir <that folder>` puts them in the pack.
