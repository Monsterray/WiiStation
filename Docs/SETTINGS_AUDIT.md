# Settings audit (2026-10-18, from main 84cb091)

Every user-facing setting: where the menu shows it, whether the settings file saves and loads
it, whether `Gamecube/release/example_settings.ini` and `SETTINGS.md` document it, and when a
value read from a file at game start takes effect. Key names are those of 84cb091, except that
`SoundHwAccel` is now `SoundDriver` (renamed in 5.13.x; `setOption` still reads the old name).
See "Status (2026-11-03)" below for the keys added since.

## How it was checked

- One source: the `OPTIONS[]` table in `Gamecube/GamecubeMain.cpp`. `writeSettingLines()` writes
  every row, `setOption()` reads every row; there is no other reader or writer of settings.ini
  (code graph: `settingByKey`/`setOption` callers; `readConfig` adds only the legacy
  `LimitFrames`). Every global the menu code writes (graph: WRITES edges from
  `Gamecube/menu/*`) that is a setting is a table row; the rest is menu state.
- `python scripts/settings_check.py`: table vs example_settings.ini vs SETTINGS.md vs the menu
  tables -- 164 keys, ok.
- `scripts/settings_roundtrip.py`: every key at a non-default value, a Dolphin boot that reads
  it and saves (menuclick 14 = Save SD), the written settings.ini read back from the card image:
  **164 of 164 keys read back the same** (run `rt`, 2026-10-18). The This Game file from the
  menu walk below: 164 of 164 the same as the settings it was saved from.
- When a value takes effect: the graph's USAGE edges for each global (plus grep where the graph
  misses: .inc files, macros), against what `loadSettings()`, `loadSeparatelySetting()` and
  `go()` apply.

## Bugs found and fixed

1. **Save Settings > This Game was never read** (since 019eb6a, 5.6.1): the writer made
   `settings/<ID>.cfg`, the game-start reader looked for `settings/<ID>.ini`. The writer now
   writes `.ini`; the reader takes `<ID>.cfg` on a device that has no `<ID>.ini` (order: USB .ini,
   USB .cfg, SD .ini, SD .cfg, then settings.ini). A PS-X EXE (no ID) no longer reads or writes
   `settings/.ini`/`.cfg`; This Game says so instead of writing one.
2. **DisableXa / DisableCdda were lost at every boot**: `loadSettings()` copied them to
   `Config.Xa`/`Config.Cdda` before it read the file, and nothing copied them after. Now copied
   after the read, and again at game start.
3. **Settings applied only by a call were ignored when a game's file (or a changed settings.ini)
   was read at game start**: `Interpolation`, `SoundTempo`, `SoundReverb` (spu_config),
   `TrapFilter`, `Gamma` (VI writes), `SkipFrames` on Soft Fast (latched when SysInit opened
   the plugin), `DisableXa`/`DisableCdda`. `loadSeparatelySetting()` now applies them as the
   menu does.

Measured in Dolphin (Spyro, speech script, 90 s):

| run | DOL | card | perf.log `settings:` | XA sectors | FPS overlay |
|---|---|---|---|---|---|
| `old_dol_ini` | main (before) | `settings/SCUS94228.ini`: DisableXa=1 FPS=0 | (no probe) | 730 (ignored) | -- |
| `legacy_cfg` | this branch | `settings/SCUS94228.cfg`: DisableXa=1 FPS=0 | `.../settings/SCUS94228.cfg` | 0 | off |
| `ini_wins` | this branch | that .cfg plus the `.ini` This Game wrote | `.../settings/SCUS94228.ini` | 730 | on |
| `menu_thisgame_a` | this branch | menu walk: Load from SD, Spyro, menuclick 62 | `sd:/wiistation/settings.ini` | -- | -- |

`menu_thisgame_a` wrote `wiistation/settings/SCUS94228.ini` (3428 bytes, no `.cfg`); that
file is the `.ini` of `ini_wins`, so one run wrote it and the next read it.

## The table

Saved = the writer writes it; Loaded = the reader takes it (both proven by the round trip);
Documented = example_settings.ini and SETTINGS.md. "Applied" says when a value read at game start
(a game's own file) takes effect; "live" means the code reads the global where it uses it.
Ranges (`PadType<n>`, `Coop<p>Type<n>` ...) are one row each.

| Key | Section | Where shown | Saved | Loaded | Documented | Applied / fixed |
|---|---|---|---|---|---|---|
| `lang` | [General] | General, Select Language | yes | yes | yes | boot / menu only |
| `MenuFont` | [General] | none, settings file only | yes | yes | yes | boot only |
| `FileSortMode` | [General] | none, File browser Z button | yes | yes | yes | menu (file browser) |
| `Core` | [General] | General, Advanced, CPU tab, "CPU Core" | yes | yes | yes | game start (psxCpuInit) |
| `fastLoad` | [General] | General, Fast Load | yes | yes | yes | live (read where used) |
| `RomDirSD` | [Files] | none, settings file only | yes | yes | yes | menu (file browser) |
| `RomDirUSB` | [Files] | none, settings file only | yes | yes | yes | menu (file browser) |
| `BiosDevice` | [Files] | General, Select BIOS | yes | yes | yes | game start (biosFileInit; no HLE fallback check, unlike boot) |
| `BootThruBios` | [Files] | General, Boot Through BIOS | yes | yes | yes | live (read where used) |
| `gpuPlugin` | [Video] | General, Advanced, GPU tab, "GPU Plugin" | yes | yes | yes | game start (loadISO swaps the plugin) |
| `VideoMode` | [Video] | Not in the menu | yes | yes | yes | boot only (Graphics constructor) |
| `TVMode` | [Video] | Video, 240p | yes | yes | yes | live (read where used) |
| `ForceNTSC` | [Video] | Video, Force NTSC | yes | yes | yes | game start |
| `ScreenMode` | [Video] | Video, Screen Mode | yes | yes | yes | live (read where used) |
| `Interlaced` | [Video] | Video, Interlaced | yes | yes | yes | live (read where used) |
| `DeflickerFilter` | [Video] | Video, Deflicker | yes | yes | yes | live (read where used) |
| `TrapFilter` | [Video] | Video, Trap | yes | yes | yes | FIXED: VIDEO_SetTrapFilter at game start |
| `BilinearFilter` | [Video] | Video, texture filter button | yes | yes | yes | live (read where used) |
| `Dithering` | [Video] | Video, Dithering | yes | yes | yes | game start (iUseDither) |
| `MdecChroma` | [Video] | Video, Advanced page, "MDEC Chroma" | yes | yes | yes | live (read where used) |
| `FmvColour` | [Video] | Video, Advanced page, "Video Color" | yes | yes | yes | live (read where used) |
| `Gamma` | [Video] | Video, Advanced page, "Gamma" | yes | yes | yes | FIXED: applyVideoGamma at game start |
| `DeflickerLevel` | [Video] | Video, Advanced page, "Deflicker Level" | yes | yes | yes | live (read where used) |
| `SkipFrames` | [Video] | Video, Frame Skip | yes | yes | yes | FIXED: Soft Fast setframelimit at game start (OpenGX/Soft Timed read it live) |
| `FPS` | [Video] | Video, Show FPS | yes | yes | yes | live (read where used) |
| `EfbSync` | [Video] | Not in the menu | yes | yes | yes | live (read where used) |
| `Audio` | [Audio] | Audio, Enable: Audio | yes | yes | yes | live (read where used) |
| `SoundDriver` (was `SoundHwAccel`) | [Audio] | Audio tab, "Sound Driver"; General, Advanced, SPU tab, "Output" | yes | yes | yes | next load (SetupSound at SysInit, before the read) |
| `SoundResampler` | [Audio] | Audio, Advanced, "Output Resampler" | yes | yes | yes | live (read where used) |
| `SoundRateControl` | [Audio] | Audio, Sync = Rate | yes | yes | yes | live (read where used) |
| `SoundTempo` | [Audio] | Audio, Sync = Tempo | yes | yes | yes | FIXED: setSpuTempo at game start |
| `SoundReverb` | [Audio] | Audio, Advanced, "Reverb" | yes | yes | yes | FIXED: setSpuReverb at game start |
| `SoundMixerPrecision` | [Audio] | Audio, Advanced, "Mixer Precision" | yes | yes | yes | live (read where used) |
| `SoundXaResampler` | [Audio] | Audio, Advanced, "XA Resampler" | yes | yes | yes | live (read where used) |
| `Interpolation` | [Audio] | Audio, Interpolation | yes | yes | yes | FIXED: setSpuInterpolation at game start |
| `DisableXa` | [Audio] | Audio, Enable: XA | yes | yes | yes | FIXED: copied to Config.Xa after the read (boot and game start) |
| `DisableCdda` | [Audio] | Audio, Enable: CDDA | yes | yes | yes | FIXED: copied to Config.Cdda after the read (boot and game start) |
| `CpuTiming` | [Timing] | General, Advanced, CPU tab, "CPU Timing" | yes | yes | yes | go() (ApplyConfig) |
| `GpuTiming` | [Timing] | General, Advanced, GPU tab, "GPU Timing" | yes | yes | yes | live (read where used) |
| `SioTiming` | [Timing] | General, Advanced, Input tab, "Port Timing" | yes | yes | yes | live (read where used) |
| `CpuSpeed` | [Timing] | General, Advanced, CPU tab, "CPU Speed"; per game with Save Settings, This Game | yes | yes | yes | live (read where used) |
| `FrameRate` | [Timing] | Video, "Frame Rate" | yes | yes | yes | game start (syncFrameLimit; also go()) |
| `CdBuffer` | [Performance] | General, Advanced, CD tab, "CD Read Buffer" | yes | yes | yes | next load (image open at SysInit) |
| `CdPrefetch` | [Performance] | General, Advanced, CD tab, "CD Read-Ahead" | yes | yes | yes | next load (pf_start in ISOopen) |
| `CdChdHunks` | [Performance] | General, Advanced, CD tab, "CHD Hunk Cache" | yes | yes | yes | next load (image open at SysInit) |
| `LimiterWait` | [Performance] | Not in the menu | yes | yes | yes | live (read where used) |
| `LimiterDebt` | [Performance] | Not in the menu | yes | yes | yes | live (read where used) |
| `LockedCache` | [Performance] | Not in the menu | yes | yes | yes | go() |
| `NativeDevice` | [Saves] | Saves, Memcard Save Device | yes | yes | yes | game start |
| `StatesDevice` | [Saves] | Saves, Save States Device | yes | yes | yes | live (read where used) |
| `AutoSave` | [Saves] | Saves, Auto Save Memcards | yes | yes | yes | game start (cards loaded after the read) |
| `Memcard0` | [Saves] | Saves, Memcard 1, Memcard 2 | yes | yes | yes | game start |
| `Memcard0File` | [Saves] | Saves, Memcard Type | yes | yes | yes | game start |
| `Memcard1` | [Saves] | Saves, Memcard 1, Memcard 2 | yes | yes | yes | game start |
| `Memcard1File` | [Saves] | Saves, Memcard Type | yes | yes | yes | game start |
| `PadAutoAssign` | [Controllers] | Input, Configure Input, Pad Assignment | yes | yes | yes | go() |
| `ControllerType` | [Controllers] | Input, PSX Pad Type | yes | yes | yes | live (read where used) |
| `AnalogAuto` | [Controllers] | none | yes | yes | yes | live (read where used) |
| `RumbleEnabled` | [Controllers] | Input, Enable Rumble | yes | yes | yes | live (read where used) |
| `LoadButtonSlot` | [Controllers] | Input, Auto Load Slot | yes | yes | yes | game start (mapping reloaded) |
| `LightGun` | [Controllers] | Input, Light Gun | yes | yes | yes | live (read where used) |
| `PadType<n>` | [Controllers] | Input, Configure Input | yes | yes | yes | live (read where used) |
| `PadAssign<n>` | [Controllers] | Input, Configure Input | yes | yes | yes | live (read where used) |
| `PadLightgun<n>` | [Controllers] | Input, Configure Buttons, Gun/Mouse | yes | yes | yes | live (read where used) |
| `CoopPlayers<n>` | [Co-Op] | Input, Configure Input, Customize Co-Op (grid) | yes | yes | yes | live (read where used) |
| `Coop<p>Type<n>` | [Co-Op] | Input, Configure Input, Customize Co-Op (grid) | yes | yes | yes | live (read where used) |
| `Coop<p>Assign<n>` | [Co-Op] | Input, Configure Input, Customize Co-Op (grid) | yes | yes | yes | live (read where used) |
| `Coop<p>Layout<n>` | [Co-Op] | Input, Configure Input, Customize Co-Op (grid) | yes | yes | yes | live (read where used) |
| `Coop<p>Buttons<n>` | [Co-Op] | Input, Configure Input, Customize Co-Op (grid) | yes | yes | yes | live (read where used) |
| `Achievements` | [Achievements] | Achievements | yes | yes | yes | game start (achievements_load) |
| `AchievementFile` | [Achievements] | none, settings file only | yes | yes | yes | live (read where used) |
| `AchievementLog` | [Achievements] | none, settings file only | yes | yes | yes | live (read where used) |
| `smbipaddr` | [Network] | ini only | yes | yes | yes | network thread (boot) |
| `smbsharename` | [Network] | ini only | yes | yes | yes | network thread (boot) |
| `smbusername` | [Network] | ini only | yes | yes | yes | network thread (boot) |
| `smbpassword` | [Network] | ini only | yes | yes | yes | network thread (boot) |
| `RomDirSMB` | [Network] | ini only | yes | yes | yes | menu (file browser) |

## Counts

- 164 keys (76 rows above): 164 saved, 164 loaded, 164 documented; 0 menu settings without a
  key; 0 keys read but undocumented (the legacy `LimitFrames` is now noted in SETTINGS.md
  section 12); 0 documented keys the code no longer reads.
- 8 keys were not applied when a game's file was read (fixed): `DisableXa`, `DisableCdda`
  (also lost at boot), `Interpolation`, `SoundTempo`, `SoundReverb`, `TrapFilter`, `Gamma`,
  `SkipFrames` (Soft Fast), plus the This Game file itself (never read).
- 4 keys take effect only at the next game load when they come from a game's file:
  `SoundDriver` (then `SoundHwAccel`), `CdBuffer`, `CdPrefetch`, `CdChdHunks` (used when SysInit opens the disc, and
  the disc ID that names the file is known only after that). 3 are boot-only: `VideoMode`,
  `lang`, `MenuFont`. Both are now written in SETTINGS.md section 1.4. Not fixed.

## 2026-10-19: a game's file holds only its differences

- **Done** (was open here): a game start sets the defaults, reads settings.ini (USB, else SD),
  then the game's own file on top (`loadGlobalSettings`, `gameSettingsFile`, `loadGameSettings`
  in GamecubeMain.cpp). Save > This Game (`writeGameConfig`) writes only the keys whose value
  differs from that global layer; Load Settings > This Game reads in the same order.
- **Global-only keys** (`GLOBAL_ONLY_KEYS`): `lang`, `MenuFont`, `FileSortMode`, `TabWrap`,
  `RomDirSD`, `RomDirUSB`, `RomDirSMB`, `NativeDevice`, `StatesDevice`, `AchievementFile`,
  `AchievementLog`, `AchievementBenchRoot`, `smbipaddr`, `smbsharename`, `smbusername`,
  `smbpassword`. This Game never writes them, and a game's file is not read for them, so an old
  full game file no longer holds back the share login or a folder. `BiosDevice` stays per game
  (HLE or a real BIOS is a per-game choice; the file name is fixed).
- **Not stored** (`UNSTORED_KEYS`): `FilterTour`, Off at every boot. settings.ini no longer has
  it; a file that does is ignored for it. `settings_check.py` and `settings_roundtrip.py` read
  both lists from the C source.
- An old full game file (.ini or .cfg) still loads: its keys replace settings.ini's as before,
  except the global-only ones. A This Game save with no difference writes a file of comment
  lines only, which still hides an old `.cfg` of the game on that device.

## Open, for a decision

- **Next-load keys** above: reading the game's file before `SysInit()` needs the ID first
  (CheckCdrom opens the disc). Possible, not small.
- `BiosDevice` from a game's file is not checked for a BIOS file (boot falls back to HLE when
  the file is missing; game start does not).
- Legacy `.cfg` files stay on the card; WiiStation never deletes them (SETTINGS.md 1.4 says the
  user may).

## Status (2026-11-03)

Checked against `OPTIONS[]` in `Gamecube/GamecubeMain.cpp` and `Gamecube/release/example_settings.ini`
on main (e857a9c2). The table above is the 2026-10-18 audit and has not been redone: the code now has
214 keys (`python scripts/settings_check.py`: "checked 214 keys, 81 ranges"), and the counts above
(164) are the 10-18 counts.

**Key names fixed in this file:** `SoundHwAccel` is `SoundDriver` (menu label "Sound Driver"). The
example file and SETTINGS.md say the old name is still read (`GamecubeMain.cpp` maps it).

**Keys added since the audit (not audited: saved/loaded/applied-when not re-proven):**
| Key | Section | Where shown | Note |
|---|---|---|---|
| `TabWrap` | [General] | none | global-only key |
| `FilterTour` | [Video] | Video, Filter Tour | never stored (`UNSTORED_KEYS`) |
| `GxTexMin` | [Video] | Advanced > GPU, "Far Textures" | OpenGX trial, see GX_QUALITY.md |
| `GxColor` | [Video] | Advanced > GPU, "GX Color" | OpenGX trial |
| `GxMask` | [Video] | Advanced > GPU, "GX Mask Bit" | OpenGX |
| `SoftGpuMode` | [Video] | "Soft Mode" | soft GPU pixels (Accurate, Hi-Fi, Fast) |
| `JitDelay`, `JitQueueAt`, `JitThreadTest`, `JitPending`, `JitPick`, `JitFull`, `JitLightning` | [Performance] | not in the menu | Lightrec background compiler / GNU Lightning test keys |
| `ChainDump`, `Disc` | [Performance] | not in the menu | chain lines only |
| `GunReticle` | [Controllers] | Input, "Gun Reticle" | |
| `AchievementUnofficial`, `AchievementEncore`, `AchievementChallenge` | [Achievements] | Achievements menu | |
| `AchievementSpectator`, `AchievementUTCOffset`, `AchievementBenchRoot` | [Achievements] | none | `AchievementBenchRoot` is global-only |
| `Player1`..`Player2P8`, `PlayersRecent`, `PlayersOfficial` | [Players] | Players menu | one string key per seat |

`Achievements` is now a 0-4 mode (Off, Custom, Test, Official, Local), not on/off.

**Open items, checked against the code:**
- Next-load keys: still open (4 keys, same reason: the game's file is read after `SysInit()` opens the
  disc). Not done.
- `BiosDevice` check: still open for game start. `loadSettingsNow()` (the Load Settings menu action,
  1c5e371b) and boot (`GamecubeMain.cpp` near line 929) fall back to HLE when the file is missing;
  the game-start path (`loadGameSettings` then `biosFileInit()`) has no such check (read from
  the code, not run).
- Legacy `.cfg` policy: unchanged. The reader still takes `<ID>.cfg` when a device has no `.ini`;
  nothing deletes them.
- `settings_check.py` missed `JitLightning` because its table row ends in a `/* */` comment;
  fixed 2026-11-03 (the checker strips comments). It now says "settings: ok".

## Commands

    python scripts/settings_check.py
    python scripts/settings_roundtrip.py make .runs/rt_in.ini
    KEEP_PROFILE=1 bash scripts/wsx.sh run rt --autoboot none --secs 25 --nodump \
        --input scripts/autoinput/menu_save_sd.txt --settings .runs/rt_in.ini
    python scripts/sdimage_read.py .profiles/<run profile>/Load/WiiSD.raw wiistation/settings.ini .runs/rt/written.ini
    python scripts/settings_roundtrip.py check .runs/rt/settings.cfg .runs/rt/written.ini
    bash scripts/profile.sh rm <run profile>

The debug build writes `settings: <path>` to perf.log for the file each game start reads.
