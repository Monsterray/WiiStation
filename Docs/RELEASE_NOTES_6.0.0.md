# WiiStation 6.0.0

WiiStation is a PlayStation 1 emulator for the Wii (and for Wii U in vWii/WiiVC use). 6.0.0 is the first
release on GitHub. It brings a rebuilt menu and the achievement system, and it collects what changed in
WiiStation since the last public versions (WiiSXRX / WiiSXRX_2022, "Beta 4.5").

Download `WiiStation-6.0.0.zip`.

## Install / upgrade

1. Unzip the file onto the **root of your SD card** (or the root of the USB drive you start from).
   It adds `apps/WiiStation/` (the Homebrew Channel app) and `wiistation/` (controllers, fonts,
   languages, `example_settings.ini`, the certificate list for Official achievements, and empty
   `isos/` and `bios/` folders).
2. Put your games in `wiistation/isos/` (or list your own folders in `RomDirSD` in `settings.ini`).
   A BIOS is optional: copy `SCPH1001.BIN` (or another PS1 BIOS) to `wiistation/bios/`.
3. Start WiiStation from the Homebrew Channel.

**Upgrading from 5.6 or newer:** unzip over the card and say yes to overwrite. The zip contains
no settings, memory cards, save states, games or BIOS, so yours stay as they are. One change you
will see: **Players start empty.** Mii seats are no longer remembered between starts, so seat your Miis
again in Players (Official account links and recent players are kept). The main menu shows a small
amber dot on Players while a seat that is in use has no Mii, and a short "PLAYERS" note appears
after the game card.

**Upgrading from 5.5 or older, or from WiiSXRX:** everything lives in `wiistation/` now, not
`wiisxrx/`. Nothing is moved for you: see [FOLDERS.md](FOLDERS.md) for the steps. Save states from
5.5 and older may not load.

Every setting, with its default and what it does, is in `wiistation/example_settings.ini` and in
[SETTINGS.md](../SETTINGS.md).

## What is new for players

### New menu
- The whole menu was rebuilt on one shared set of pages: the same layout, the same buttons and the same
  L/R tab switching everywhere (L and R, - and + on a Wii Remote, ZL and ZR on the Wii U GamePad).
- B on the main menu goes back into the game you started. The menu combination does the same from the
  achievements book.
- Pop-ups sit at the screen's real right edge, and small text is crisp.
- A game card with an animated disc shows while a game loads.
- ISO Info (Current ISO > ISO Info) is a page of its own: file, label, game ID, size, tracks, system,
  automatic fixes, achievements, and the game's disc picture when you have a cover pack.
- Players: a page to seat a Mii on each port, edit its player settings and link an account.

### Achievements
Off by default. Choose the mode in Settings > General > Achievements.
- **Official**: RetroAchievements sets, earned with your RetroAchievements account. **Casual only**
  (see RetroAchievements, below).
- **Local**: the RetroAchievements set, but nothing is ever sent. Every seated Mii (or Guest) earns into
  a file on your card. The set is downloaded once and saved; after that no network is needed.
- **Custom**: your own set file; unlocks are kept in a journal. **Test**: the same, nothing kept.
- **The book** (Current ISO > Achievements): the game, "12 / 89 earned", points, tabs for In progress,
  Locked and Earned, a detail page for each achievement, and the picture of the moment you earned it
  (saved in `wiistation/achievements/shots/`).
- **Pop-ups**: an unlock card (tiers Blue, Bronze, Silver, Gold, Crystal), progress panels at each 10 %
  step, a box for each achievement one step away, and a card when a game's set loads.
- **Several players**: each Mii seated on a port with a controller earns its own achievements, up to
  8 Miis. The toast names who earned it ("Kat and Ollie unlocked", "4 players unlocked").
- **Local Mii leaderboards**: each seated Mii's best score on every board of a set, kept on this Wii;
  nothing is sent.
- Save states keep achievement progress. A multi-disc game keeps its set when you swap discs.

### Controllers
- **Multitap: up to 8 players.** **Co-Op**: up to 8 controllers play one PlayStation port together.
  **PadMirror** (`PadMirror = 1`): one controller drives every seat (Crash Team Racing 4-player with
  one pad).
- USB gamepads: a pad with no controller file works at once (WiiStation reads what the pad reports), and
  every button, hat and axis can be mapped, L3 and R3 too. Test USB Pad writes a controller file for you.
- Wii U Pro Controller and Wii U GamePad (WiiVC) work, with the Wii Classic and Classic Pro, GameCube
  pads, Wii Remote and Nunchuk, and the NES/SNES Classic.
- PS1 pad types: Digital, Analog (DualShock), Stick, **Dual Analog** and **Analog Joystick**. The ANALOG
  button has a key combination; games that ask the pad about its modes (Gex 3, MediEvil) start in analog
  by themselves (`AnalogAuto`).
- GunCon and Justifier light guns and the mouse, with a crosshair. Rumble on the DualShock's two motors.

### Games and discs
- Disc images: `.cue/.bin`, `.ccd/.img/.sub`, `.toc`, `.mds/.mdf`, `.iso`, `.pbp` (every disc of a
  multi-disc PBP), `.cbn`, `.chd` (v1 to v5) and PS-X `.exe`. Compressed images are unpacked ahead, so they
  load as fast as a plain `.bin`.
- **Load from DVD**: burn your games to a data DVD-R and choose Load from DVD. No cIOS needed. Many Wiis
  made after 2008 (and the Wii mini and Wii U) cannot read DVD-R.
- **Network shares** (SMB2/SMB3): games from a NAS or PC, with `RomDirSMB`.
- **Covers and disc pictures**: the file browser shows a game's cover from a pack in
  `wiistation/covers/`. The pack is not part of this zip.
- **Save states** (ten slots) and memory cards on SD or USB, with per-game settings (Save Settings >
  This Game keeps only what differs from `settings.ini`).
- Crash Team Racing starts at CPU Speed 150 % so 4-player races hold 30 frames a second.

### Speed and accuracy
- Lightrec recompiler, synced to upstream. New code compiles on a background thread and about a third
  cheaper, so levels that load a lot of code stutter less.
- Several games in one session no longer run the Wii out of memory.
- The soft renderers draw as a PS1 GPU does by default (Soft Mode, GPU Timing); OpenGX keeps the PS1 mask
  bit. A set of PS1 test programs is checked against a real PlayStation ([ACCURACY.md](ACCURACY.md)).
- Sound output rate control (no crackle, no drift). Games on the Homebrew Channel report hangs and
  crashes instead of freezing the Wii; a hang at start-up returns to the channel after 90 seconds.

## RetroAchievements

Official mode earns achievements on your RetroAchievements account in **Casual** (softcore) mode.
Hardcore is not available yet: RetroAchievements validates each emulator for Hardcore, and WiiStation
has not been validated. Leaderboard scores are not sent to RetroAchievements; you can see the boards'
top scores in the achievements book, and WiiStation keeps this Wii's own best scores for each Mii
(Local leaderboards, never sent).

You need a RetroAchievements account for Official mode, a certificate list (`wiistation/achievements/
cacert.pem`, in this zip) and a random seed file on the card. The setup is in
[ACHIEVEMENTS.md](ACHIEVEMENTS.md), "Official login".

## Known issues

- **CD audio from `.pbp`, `.chd` and `.cbn` images starts about 24 ms late** against the picture, compared
  with the same game as `.cue/.bin`. You cannot hear a gap; music sits slightly behind the action.
- **Crash Team Racing, Tiger Temple: the rain can run a little under full speed** on a real Wii (measured
  about 0.98x over a whole race, up from 0.88x before 5.17.0).
- **CPU core "Old PPC dynarec" (Core = 2) is broken**: some games jump to address 0 as they start. The
  default (Lightrec) and the interpreter are not affected. Do not select it.
- Games from the network share and DVD are not read ahead, so compressed images cost more there.
- Not supported: `.ecm`, `.nrg`, `.cdi`, `.m3u` playlists, and loose audio-track files.

Details: [PARKED_BUGS.md](PARKED_BUGS.md).

## Credits

WiiStation is by Monty Perrotti, based on WiiSXRX_2022 by xjsxjs197 and WiiSXRX by NiuuS, which come from
WiiSX (emu_kidid, sepp256, tehpola) and PCSX. Lightrec is by pcercuei; GNU Lightning, rcheevos, libchdr
and libsmb2 are used. The full list is in the README inside the zip.
