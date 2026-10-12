# Button mapping files

Status: changed 2026-10-12 on branch try/buttoncfg, from main 04e4dd7. The host test and the release
build pass. Not run on hardware or in Dolphin.

This page describes the system as it is now. Section 3 says which problems the change fixed.

## 1. Terms

| Term | Meaning | Where it lives |
|---|---|---|
| Live mapping | The buttons a pad uses now. One per controller type and pad number (0 to 3). | RAM: `controller_t.config[4]` (controller.h) |
| Slot | A saved copy of a mapping. Slots 1 to 4. | RAM: `config_slot[0..3]` (controller.h) |
| Game slot | A saved copy for one disc. The menu shows it as "Game". | RAM: `config_slot[4]` (controller.h) |
| Slot file | The file of one controller type. Holds Slots 1 to 4 and the screenshot combinations. | Disk: `wiistation/buttons/*.cfg` |
| Game file | The file of one disc. Holds the Game slot of every type. | Disk: `wiistation/buttons/games/<CdromId>.cfg` |
| Type | One controller family, with one identifier letter (G, H, C, N, W, P, D). | `controller_t.identifier` |
| Device | The SD card (`sd:`) or the USB device (`usb:`). | `buttonDevice` (RAM) |
| Set | The slot files of every type on the build, read from one device. | Disk |

## 2. How it works now

### 2.1 Files

| Type | Identifier | File in `wiistation/buttons/` | Old name in `wiistation/` |
|---|---|---|---|
| GameCube pad | G | `gamecube.cfg` | `controlG.cfg` |
| USB HID gamepad | H | `usb-hid.cfg` | `controlH.cfg` |
| Classic Controller | C | `classic.cfg` | `controlC.cfg` |
| Wiimote with Nunchuk | N | `wiimote-nunchuk.cfg` | `controlN.cfg` |
| Wiimote alone | W | `wiimote.cfg` | `controlW.cfg` |
| Wii U Pro Controller | P | `wiiu-pro.cfg` | `controlP.cfg` |
| Wii U GamePad | D | `wiiu-gamepad.cfg` | `controlD.cfg` |
| Game file (per disc) | - | `games/<CdromId>.cfg` | `<CdromId>.btn` |

The names come from `Gamecube/buttonfiles.c`. The identifiers are in each driver's `controller_t`
(`Gamecube/gc_input/controller-*.c`). The GameCube-only build has the G type only.

### 2.2 The ways data moves

| Action | Code | What it copies | Disk |
|---|---|---|---|
| Configure Buttons, Save (any slot) | `Func_SaveConfig` (ConfigureButtonsFrame.cpp) | Live mapping to the chosen slot in RAM, then `saveAllButtonConfigs` | Yes: all slot files and the Game file |
| Settings, Save Button Configs: SD or USB | `Func_SaveButtonsSD`, `Func_SaveButtonsUSB` (SettingsFrame.cpp) | Calls `saveAllButtonConfigs`, same as Save | Yes: all slot files and the Game file |
| One writer | `saveButtonConfigs` (GamecubeMain.cpp) | Every slot of every type, then the Game file | Yes |
| Load | `Func_LoadConfig` | Slot (or Game slot) to the live mapping | No |
| Restore Default | `Func_DefaultConfig` | Built-in mapping to the live mapping | No |
| Boot | `loadSettings`, then `loadControllerMapping` | One set, from one device, to the slots | Read |
| Game start | `loadSeparatelySetting`: `loadControllerMapping` if Auto Load Slot changed, then `loadGameButtons` | Set and Game file to RAM | Read |
| Auto Load Slot | `Func_ToggleButtonLoad` | Nothing at once. Applies at the next game start | Only when settings are saved |

### 2.3 Device choice

- `buttonDevice` holds "usb" or "sd". Every Save writes to it. Every set read sets it.
- At boot it is the device the set came from. A USB start reads SD only when SD has a full set and USB
  does not. Then it writes SD.
- Settings, Save Button Configs (SD or USB) sets it to the row's device and writes all files there.
  This is the only place that chooses a device by hand.
- A set is read from one device only. `button_read_device` picks the preferred device when it has
  every file. Otherwise it picks the other device when that one has every file. Otherwise it picks the
  device with more files.
- The Game file is read from `buttonDevice` first, then from the other device. It is saved to
  `buttonDevice`.

### 2.4 Names on read and write

- Reading a file: the new name first. The old name only when the new name is missing
  (`button_slot_read`, `button_game_read`).
- Writing: the new names only. Old files are never deleted.
- A build before this change reads only the old names. See section 5, question 2.

### 2.5 File format

Unchanged: version 1, binary. Magic `W`, `X`, identifier, version. Four slots of 23 fields, then four
screenshot indices. The Game file is one such block per type, in `controller_ts` order.

A button field is an index into the type's button table. A bad index becomes 0 with no message.
The format has no count and no checksum. See `Gamecube/PlugPAD.c` (`read_slot`, `write_slot`).

### 2.6 Auto Load Slot

- Values: Slot 1 to 4, Default, Auto. Stored as `LoadButtonSlot` in `settings.ini`.
- Default in code: Auto (GamecubeMain.cpp, `loadSettings`).
- Auto: the live mapping is the game's Game slot. With no Game file, the Game slot is the default, and
  so is the live mapping. The previous game's mapping does not stay (fixed in 611a27b).
- A change applies at the next game start.

## 3. Problems and status

Each problem names the cause it had. "Fixed" means the change removed that cause.

| # | Problem | Status |
|---|---|---|
| P1 | Configure Buttons Save wrote RAM only. | Fixed. Save writes every file at once. |
| P2 | The live mapping had no direct path to a file. | Fixed. Save writes the slots, which hold the live mapping after Save. |
| P3 | Settings Save wrote seven files one at a time and could leave a half-written set. | Open. Every write is tried. The message says "Could not". A file written before the error stays changed. A temporary name and a rename would fix this. |
| P4 | Auto with no Game file kept the last game's mapping. | Fixed earlier, in 611a27b. |
| P5 | Changing Auto Load Slot re-read the files and discarded unsaved slot edits. | Moot. Every slot edit is written at once. A failed write still leaves RAM ahead of the disk until the next Save. |
| P6 | USB and SD could disagree. | Fixed for reads and writes. One set comes from one device. Writes go to the device the set came from. The Game file follows the same device. |
| P7 | The format cannot change safely (index order, silent refusal of other versions). | Open. See section 4. |
| P8 | Slots are per type, not per player. | Open. |
| P9 | Words and labels disagree. SETTINGS.md said Configure Buttons wrote the slot files. | Partly fixed. SETTINGS.md section 6.2 now says who writes. "Default" still means the built-in mapping in Auto Load Slot and "Restore Default Config" in Configure Buttons. |
| P10 | No undo. Restore Default, Load and Save replace without a prompt. | Open. |

## 4. Decisions

- Option A from the earlier analysis, with the device rule above: one Save writes everything.
- The Settings rows stay. They are the one place that chooses a device by hand. Both rows and Configure
  Buttons Save call `saveAllButtonConfigs`, so one message and one writer serve all of them.
- The names follow the controller's menu name and identifier. The HID type is a USB HID gamepad, not a
  GameCube pad. The N type is a Wiimote with a Nunchuk, so it has its own file.
- Migration reads the old names only when the new name is missing. No converter, because the content
  format did not change.
- The format stays as it is. A text format (Option D) waits for a need.

## 5. Open questions

1. Hardware test: Save on USB and on SD. Start from USB with files on SD only. Start from SD with files
   on USB only. Check that a set never mixes.
2. Downgrade: a build before this change reads the old names. A Save with this build leaves those old
   files stale. Either document it or write the old names too. Decide before release.
3. P3: a failed write can leave one device half-written. Decide whether a temporary file and a rename
   are worth the code.
4. Version: `Gamecube/version.h` is 5.10.0. This change may be a Minor step, and needs a note in
   `Gamecube/release/apps/README`. Not changed here.
5. Auto Load Slot still applies at the next game start. Keep it, or apply it at once?

## 6. Test

Host test of the names, the migration and the device choice:

    cc -O2 -o button_files_test tests/button_files_test.c Gamecube/buttonfiles.c && ./button_files_test

The Wii's file calls (GamecubeMain.cpp) are not covered. They need the hardware check in section 5.

## 7. Source references

- `Gamecube/buttonfiles.h`, `Gamecube/buttonfiles.c`: names, paths, migration, device choice.
- `tests/button_files_test.c`: the host test.
- `Gamecube/GamecubeMain.cpp`: `buttonDevice` (493), `buttonFileExists` (495), `buttonDeviceReady` (501),
  `loadControllerMapping` (508), boot calls (660, 679), `writeGameButtons` (1222), `saveButtonConfigs`
  (1242), `openGameButtons` (1269), `loadGameButtons` (1288), reload in `loadSeparatelySetting` (1364).
- `Gamecube/menu/ConfigureButtonsFrame.cpp`: `saveAllButtonConfigs` (813), `Func_SaveConfig` (831).
- `Gamecube/menu/SettingsFrame.cpp`: `Func_SaveButtonsSD` (1679), `Func_SaveButtonsUSB` (1684),
  `Func_ToggleButtonLoad` (1694).
- `Gamecube/PlugPAD.c`: `load_configurations` (457; the Auto copy at 478), `write_slot` (492),
  `save_configurations` (522), `save_game_slot` (538).
- `SETTINGS.md`: the `LoadButtonSlot` row and section 6.2.
