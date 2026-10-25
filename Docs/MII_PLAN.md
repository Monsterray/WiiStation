# Miis as players: plan

Goal: each Mii on the console is a player. The menu shows who is playing; **Local** achievements
(the unlock journal) are kept per Mii. Official mode (one RetroAchievements account per card) is
not changed. Status: plan, 2026-10-24. Research: the facts below, with sources, came from WiiBrew,
Dolphin's source and libmii; anything unconfirmed is marked.

## What the Wii gives us

| Item | Fact |
| --- | --- |
| File | NAND `/shared2/menu/FaceLib/RFL_DB.dat`, 0x1F1E0 bytes (Dolphin `Common/NandPaths.cpp:86`) |
| Layout | `RNOD` magic, then 100 records of 0x4A bytes from offset 4; an all-zero record is an empty slot; the Parade area (`RNHD`, 0x1D00) is undocumented; CRC-16/CCITT (poly 0x1021, init 0) over 0..0x1F1DD, big-endian at 0x1F1DE |
| Record | 0x00 flags (girl, birthday, favourite colour 0-11, favourite), 0x02 name (10 UTF-16BE chars), 0x16/0x17 height/weight, 0x18 Mii ID (bit 31 normal, bit 30 foreign, bits 28-0 creation time in 4 s ticks), 0x1C System ID (from the console's MAC), 0x20 face/hair/... bit fields, 0x36 creator name |
| Identity | (Mii ID, System ID), 8 bytes at 0x18. Slot index and name are not stable. Whether an edit keeps the Mii ID is **unconfirmed** |
| Access | Read-only through ISFS, as libogc's `conf.c` reads `/shared2/sys/SYSCONF`. Never write it: the Wii shows a Mii error after any change |
| Dolphin | Ships no RFL_DB.dat; it exists only after the Mii Channel or a game made one. Tests stage a fixture |
| Wii Remote Miis | Up to 10 on the remote (address 0x0FCA, `RNCD`), readable with `wiiuse_read_data` (**untested**). Not needed for this plan |
| Drawing a Mii | No homebrew renderer exists (Nintendo's RFL is not available; the WiiBrew 2D compositor has no licence). Name plus a favourite-colour chip is what is safe to draw |

## Design

- **`mii.c`** (new, pure C, no libogc in the parser): `mii_parse(buf, len, out[], max)` checks
  size, magic and CRC and returns the Miis (key = 16 hex chars of Mii ID + System ID, name as
  UTF-8, favourite colour, favourite flag). The Wii side reads the file once at start-up with
  `ISFS_Open(..., ISFS_OPEN_READ)`; a missing file, size 0, bad magic or bad CRC means "no Miis".
- **Player setting** `Player` (settings.ini, `[General]`): a Mii key, or empty for Guest. A
  Mii that is no longer on the console falls back to Guest with a notice, and its unlocks stay
  in the journal (keyed by the Mii's key; nothing is deleted).
- **Menu**: a Player row on the main menu panel (name and colour chip, the PS1 Save Block style
  of `Docs/design/PS1_STYLES.md`), and a picker listing the Miis in Plaza order plus Guest.
  Changing the player while a game runs reloads the achievement state (below).
- **Journal per Mii**: one file, as now, with the player key as a new column:
  `<set digest> <id> <player key> <title>`. A line without a key (every journal written so far)
  belongs to Guest. `restore_journal()` marks earned only the current player's lines. One file
  keeps the 1 MB cap and the fail-closed rules of `achievements.c` as they are.
- **Save states**: the WAO1 achievement section records the player key. Loading a state made by
  another player keeps the game state but takes the earned flags from the journal of the player
  now playing, so a state never moves one Mii's unlocks to another.
- **Unlock toast and list**: "Mii-name unlocked ..." on the toast; the Achievements page shows
  the current player's progress, with the player's name in its header.
- **Privacy**: Mii names and creator names are personal. Logs, perf.log and crash reports print
  only the first 8 hex characters of the key; the creator name is never read into memory.

## Phases and checks

| Phase | Work | Check |
| --- | --- | --- |
| 0 | Read-only probe on the **bench** Wii: open RFL_DB.dat, print size, magic, CRC result, Mii count and key prefixes (no names) to perf.log | One bench job; never on a protected Wii |
| 1 | `mii.c` parser + host test (`tests/mii_test.c`, `tests/host/hostcheck.h`) on a fixture made by `scripts/mii_fixture.py` (fictional Miis, real CRC): empty slots, bad CRC, short file, foreign bit, 10-character names, non-ASCII names | Host test |
| 2 | ISFS read at start-up, `Player` setting, menu row and picker | Dolphin with the fixture staged in the profile's NAND (`Wii/shared2/menu/FaceLib/`); `wsx.sh menu` pictures; bench Wii with its own Miis |
| 3 | Journal column, restore per player, WAO1 player key, toast/list name | Host test for the journal (old lines = Guest, two players, cap); Dolphin chain: Local set, player A unlocks, switch to B (none earned), back to A (restored), state from A loaded as B |
| 4 (optional) | Icons: a PC script renders each Mii once into `sd:/wiistation/miis/<key>.png` | Only if the colour chip is not enough; needs a Wii-to-Studio conversion and a network service, so it is opt-in |

## Decisions for the owner

1. Who picks the player: a Player setting in the menu (default, proposed), or a prompt at every game load?
2. Co-Op and multitap: only player 1's Mii earns (proposed), or every connected player's Mii?
3. Existing unlocks: stay with Guest (proposed), or move once to a chosen Mii?
4. Official mode later: one RetroAchievements account per Mii (a token file per key), or one per console as now?
