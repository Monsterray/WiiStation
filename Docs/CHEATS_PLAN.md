# Cheat engine -- plan (for review, 2026-10-10)

Nothing built yet. Monty's brief (2026-10-09): do it like DuckStation, ship no cheats (say where to
get them), and let the engine take new formats later. Research: `C:\projects\research\cheats\FINDINGS.md`.
DuckStation's code is CC-BY-NC-ND: read for behaviour only; the engine is written from the public
spec (`chtdb/cheat-format.txt`).

## What the files look like (measured in chtdb, 2026-10-10)
- 4,497 `cheats/SERIAL.cht` + 137 `patches/SERIAL.cht`; every code is `Type = Gameshark`,
  `Activation = EndFrame`. No `SERIAL-HASH.cht` files exist yet.
- Names group with `\` ("Technique Slot Modifier\Select Technique 6"); `Option = Name:value` and
  `OptionRange = lo:hi` fill `?` nibbles in the code.
- **Size:** SLUS-01251.cht is 7.9 MB / 5,405 cheats; 6 files exceed 100 KB. Never load a file whole.
- Code types by use: 80 45.7k, 30 22.5k, 90 7.7k, D0 6.0k, A7 3.5k, 50 2.0k, A6 1.8k, E0 675,
  00 466, D3 393, E2 323, 20 313, D7 262, C0 227, F5 176, A4 111, D1/D2 150, C2 65, F4 60,
  1F 38, A0 35, 51 29, the rest under 25 each.
- Patches carry setting overrides: OverrideCPUOverclock 76, DisableWidescreenRendering 63,
  OverrideAspectRatio 61, Enable8MBRAM 12.

## Design

### 1. Formats are plug-ins; the engine runs one instruction set
```
cheat_format { name; probe(path, head) ; scan(file, add_entry) ; load(entry, program*) }
```
- `scan` streams the file once and records per cheat only: name, group, description, options,
  and the body's file offset/length (MEM2, ~40 B a cheat: 220 KB for the 7.9 MB file).
- `load` parses one body into the engine's **program**: a list of `{op, addr, value, ...}` with
  GameShark semantics (every PS1 format is GameShark-shaped underneath).
- Formats, in order: **DuckStation .cht** (now); PCSX `[desc]`/`[*desc]` (.cht) and libretro
  `cheatN_code` (.cht) later -- each a ~100-line file, picked by `probe`. Encrypted Xploder/AR codes:
  unsupported (no emulator decodes them). A future non-GameShark `Type =` gets its own program op set.

### 2. The engine (pure C, no libogc: host-testable)
- Applied once per emulated vblank in `psxcounters.c` beside `frame_counter++` (EndFrame); one
  branch when nothing is on, so chains/baselines are unchanged.
- Memory through a small interface (`read8/16/32`, `write8/16/32`) over `psxM` (2 MB, mirrors masked)
  and the scratchpad. A write **only when the value differs**, then `psxCpu->Clear(addr, 1)` so
  Lightrec drops any block compiled from it (ASM cheats patch code; re-applying a constant every
  vblank must not recompile every frame).
- Op support, phase 1 (covers >99% of codes): 80 30 90, 10 11 20 21 60 61, 81 82 31 32 91 92,
  1F A5, D0-D3 E0-E3 A0-A3 (conditional chains: a failed one skips the chain and its target, as the
  spec reads), A6 A7 A8 (with restore on disable), F0-F3 F5, 50 53, C0 C3-C6 A4 (block until
  `00000000 FFFF`), D4 D5 D6 D7 (jokers: port 1's pad bits from the pad plugin), C2, 00 (nop/terminator).
  Phase 2: 51/52 (register VM), C1 (delay), F4 (find/replace). Unknown types: logged and the cheat
  is marked "unsupported" in the list, never applied.
- Manual-activation codes: an "Apply now" press instead of every vblank.

### 3. Files on the card
- `<dev>:/wiistation/cheats/SERIAL.cht` and `.../patches/SERIAL.cht`; SERIAL = CdromId with a hyphen
  after its letters (`SLUS00598` -> `SLUS-00598`; also tried without). usb: before sd:, as settings.
- Enabled list per game: `<dev>:/wiistation/cheats/enabled/SERIAL.txt`, one `name = option value`
  line each (a separate file: the per-game `.cfg` is rewritten whole from the settings table).
- At game load: read the enabled list; if not empty, scan the game's file and `load` just those
  (a scan stops once all are found). The full list is scanned only when the Cheats page opens,
  behind the loading bar.
- Global setting `Cheats = 1` (0 = never load any, e.g. for netplay or tests).
- Patch overrides: OverrideCPUOverclock -> nearest CpuSpeed; the others logged and ignored.

### 4. The menu
A **Cheats** button on the Current ISO / in-game menu opens a list page: groups as folders
(`\`), each cheat a Toggle row, options as Choice/Cycle (`Option`) or Range (`OptionRange`) rows
under it, paged with L/R. This is the menu framework's List and Range rows
(Docs/MENU_FRAMEWORK_PLAN.md): the cheats page should be built on the framework, so the framework's
List step comes first.

### 5. Testing
- `tests/cheats/` host build (clang): every op against a fake RAM array, chains, restore, `?`
  substitution, a parse of all 4,634 chtdb files (counts per op, no crash, unsupported list).
- Input-script keyword `cheat "Name" [option]` enables a cheat in scripted runs; the `ramcell`
  probe shows the write landing in Dolphin; a `menukeys` script drives the page.
- Before/after `replay3` chains with Cheats=0 to prove zero effect when off.

### 6. Docs for players (no cheats shipped)
`Docs/CHEATS.md` + README: download https://github.com/duckstation/chtdb (Code > Download ZIP), copy
`cheats/` and `patches/` into `sd:/wiistation/`; or libretro-database `cht/Sony - PlayStation`
(CC BY-SA 4.0, PCSX/libretro format, once that format lands). Cheats are per their authors.

## Steps that each ship
1. Engine + DuckStation format + host tests + `cheat` script keyword (no menu; Dolphin-proved).
2. Enabled list + game-load apply + the `Cheats` setting.
3. Cheats page (after the framework's List/Range rows).
4. Patches + overrides; PCSX/libretro formats; phase-2 ops.

## Decided (Monty, 2026-10-11)
- The Cheats page opens from the Current ISO screen.
- With cheats on, the game says so in bold ("Cheats on" in the status box), and save states made
  with cheats on are marked.
- `Cheats` defaults to off.
- Built after the libogc3 update and the menu framework.

## Questions for Monty
1. Cheats page from the main menu's Current ISO screen, the in-game menu, or both?
2. Should a game with cheats on say so (e.g. "Cheats on" in the status box) -- and should save
   states made with cheats on be marked?
3. Default for the `Cheats` setting: on (codes only run if enabled per game anyway)?
