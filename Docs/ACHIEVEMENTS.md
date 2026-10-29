# Achievements

This is the reference for the WiiStation achievement engine. It tells what the engine does
today, how to set it up, how to test it and what is planned.

- What we learned from outside sources (rcheevos, RetroAchievements rules, PS1 hashing, TLS,
  DuckStation, other systems): [ACHIEVEMENTS_RESEARCH.md](ACHIEVEMENTS_RESEARCH.md).
- Test runs, queue job IDs, measured numbers and open work:
  [ACHIEVEMENTS_NOTES.md](ACHIEVEMENTS_NOTES.md).

## Status

| Part | Status on 2026-10-24 |
|---|---|
| Local evaluation of a definition file | In the product. Off by default. Merged into main from `codex/achievement-engine` (c29cafd9, 2026-10-12). |
| Modes Off, Local, Test, Official | In the product (775ae73a, 2026-10-23). Official: RetroAchievements in Casual. |
| Official mode per Mii | In the product (5.19.0, Docs/MII_PLAN.md steps 4a and 4c): the Mii that plays Official logs in with its own account file; the other seated Miis earn the same set Local, with encore mode on and nothing sent twice. See [Official mode with Local players](#official-mode-with-local-players). |
| Unlock journal | In the product. An unlock is written when it triggers (f9ca407b). Records cut short by a power loss are ignored (2026-10-23). |
| Save-state progress (`ACH1` section) | In the product. Local: `WAC1`; Official: `WAO1` (rc_client's progress). A game reset resets the progress in both. |
| Menu controls | General tab, Cheevos: Mode, Unofficial, Encore, Challenge icons, the set file. Current ISO, Achievements: the book (2026-10-29), see [The book](#the-book). ISO Info: the achievement status line. |
| Unlock toast | In the product (2026-10-23): the tier card slides in at the top right on each unlock, in both GPU plugins. See [Unlock toast](#unlock-toast). |
| Indicators | In the product (2026-10-24, `ach_toast.c`): the game card when a set loads (the Disc Read disc spins up; N of M earned, points), a progress panel at the bottom right when measured progress crosses a new 10 % step (2026-10-29, see [Progress pop-ups](#progress-pop-ups)), one box per primed challenge for as long as it is primed (the setting `AchievementChallenge` hides them). The pop-ups sit 16 px from the right edge of the picture. Local sets get them from rc_runtime's events. |
| Unlock pictures | In the product (2026-10-29, `Gamecube/ach_shot.c`): the game's frame at each unlock, saved to `sd:/wiistation/achievements/shots/`. See [Unlock pictures](#unlock-pictures). |
| Badges | In the product (2026-10-24, `Gamecube/ach_badge.c`): RetroAchievements pictures, 64 x 64 RGB5A3 (15-bit, the PS1's colour), in the card, the panel and the boxes. Cached on the card (`achievements/badges/`) and in a 256 KB pool; asked for when the notice comes. |
| RetroAchievements login, awards, Rich Presence | In the product (Official, Casual). Rich presence is shown on the list page and sent with rc_client's pings. |
| Leaderboards | Read only: the list and each board's ten best scores and the player's own (Achievements > Leaderboards). RetroAchievements takes leaderboard entries in Hardcore only, so a Casual client submits none. |
| Hardcore | Not implemented: needs RetroAchievements to approve WiiStation as a client. |
| `rc_client`, API and PS1 hash code | Built into the Wii DOL (2026-10-23): `achievements_official.c` logs in from `ra_account.ini`, identifies the disc by the upstream PS1 hash in every format WiiStation boots (BIN/CUE, ISO, IMG/CCD, MDF/MDS, TOC, CBN, CHD, PBP), read at game load through WiiStation's own CD plugin; the same hash as upstream rcheevos' hasher (Frogger in 16 formats, Spyro 2 in 3: `scripts/chains/ach_hash_formats.txt`). On the bench Wii with the account, every format copy on its card finds its set: Frogger in 16 formats (31 achievements), MediEvil and Rayman as CHD and PBP (50, 58): `scripts/chains/ach_official_formats.txt`, job 20261023-212834-b319a4, evaluates each frame and sends unlocks to the toast with their rarity. Working on the bench Wii (2026-10-23): logged in, Spyro identified, its 89-achievement set loaded in Casual. No unlock has been earned yet. |
| HTTPS (Mbed TLS 3.6.4 + curl 8.16.0) | In the Wii build when `scripts/build-ra-https.sh` has installed it (`Gamecube/ra_transport.c`, a worker thread, one kept-open connection). Needs, on the card in `sd:/wiistation/achievements/`: `tls_seed.bin` (64 secret random bytes for this Wii) and `ra_account.ini` (both put by `scripts/ra_setup.py`), and `cacert.pem` (shipped with every install, pinned). The clock learns its time zone from the server. See [Official setup](#official-setup). Costs: [the notes](ACHIEVEMENTS_NOTES.md#official-mode-cost-on-the-bench-wii). |

The engine uses the upstream rcheevos C runtime at commit
`683197d8dc92555a51e8280a4123d071ed66bff3`. The unlocks are local. They are not official
RetroAchievements awards.

## Settings

Put the keys in the `[Achievements]` section of `settings.ini`, or in the per-game settings
file. Set them before you load the game. A path is a complete device path in quotes.

```ini
[Achievements]
Achievements = 1
AchievementFile = "sd:/wiistation/achievements/TEST12345.txt"
AchievementLog = ""
```

| Key | Values | Default | Effect |
|---|---|---|---|
| `Achievements` | 0 = Off, 1 = Local, 2 = Test, 3 = Official | 0 | Local evaluates the set and keeps unlocks in the journal. Test evaluates the set, but does not read or write the journal. Official: RetroAchievements, Casual. Off stops all frame evaluation. A value 1 from before the modes is Local. |
| `AchievementUnofficial` | 0, 1 | 0 | Official: the set's Unofficial (unpromoted) achievements too. RetroAchievements does not keep their unlocks. |
| `AchievementEncore` | 0, 1 | 0 | Official: achievements already earned can be earned again this session (the card shows again; nothing new is sent). Also on, whatever this says, while a Local Mii is seated next to the Official one ([Official mode with Local players](#official-mode-with-local-players)). |
| `AchievementChallenge` | 0, 1 | 1 | The flashing icons of achievements that are one step away (RetroAchievements' challenge indicator; a Local set's primed ones). 0 hides them, at once. Menu: General, Cheevos, "Challenge icons". |
| `AchievementSpectator` | 0, 1 | 0 | Hidden, for scripted runs on a real account: everything is evaluated and shown, no unlock is sent. Once on for a loaded set, rc_client keeps it on. |
| `AchievementUTCOffset` | `"+420"` | empty | Optional: the minutes to add to the Wii's clock for UTC. Empty: the Wii learns it from the server (`utc_offset.txt`). Set, it wins and nothing is learnt. |
| `AchievementFile` | `"path"` | empty | The definition file. One of its `Game=` serials must match the loaded disc. |
| `AchievementLog` | `"path"` | empty | The unlock journal. Empty uses the `AchievementFile` path with `.unlocks` added. The journal path must not be the definition path. |
| `AchievementBenchRoot` | `"path"` | empty | Bench builds only (`ACHIEVEMENTS_BENCH`). It sends card reads, writes and deletes, and chain cleanup, to a fresh private directory. Product builds do not read it. |

- Load the game again after you change a key, the file or the set.
- Make the parent folder yourself. The engine does not make it.
- The settings Save writes `Achievements`, `AchievementFile` and `AchievementLog`. No menu
  page shows them. The settings table gives `Achievements` a label and a help line, but no
  menu row uses it.
- There is no password key and no account key.
- The key `AchievementUTCOffsetMinutes` is removed; `AchievementUTCOffset` replaced it and is
  now optional (the clock learns its time zone, see [Official setup](#official-setup)).

## Definition file

```text
# Synthetic fixture only; these addresses are not achievements for a real game.
Game=TEST12345
Title=Fixture
1|Three consecutive hits|0xH000010=1.3.
2|Little-endian 16-bit word|0x 000020=4660|25|progression|Reach the second area
3|Scratchpad byte|0xH200001=9|50|win
```

1. The first line that is not a comment must be `Game=<CdromId>`. The ID must match the loaded
   disc. If it does not match, the engine rejects the set. A multi-disc game names every disc:
   `Game=SLUS00892,SLUS00908` (comma-separated), more `Game=` lines, or both, all before the
   first achievement. At most 8 serials, each at most 9 characters.
2. An optional `Title=<game name>` line names the game for the Beaten and Mastered toasts
   ("Spyro beaten"). Without it, the toast says "Game".
3. Each achievement line is `id|title|condition`, with three optional fields after it:
   `|points|type|description`. Each optional field can be empty.
   - `points`: 0 to 1000. Without rarity (all Local sets), the points give the toast's tier.
   - `type`: `standard` (the default), `missable`, `progression` or `win`, as RetroAchievements
     uses them. Progression and Win decide when the game is beaten.
   - `description`: the rest of the line (it can contain `|`), up to 95 bytes.
4. The condition uses the upstream rcheevos format.
5. The engine ignores blank lines and lines that start with `#` or `;`.

| Limit | Value |
|---|---|
| File size | 64 KiB. The file must not contain a NUL byte. |
| Achievements | 128 |
| ID | Decimal, 1 to 4294967295, unique in the file |
| Title | 79 bytes |
| Condition | 2048 bytes |
| Points | 0 to 1000 |
| Description | 95 bytes |

- One bad line rejects the whole set: a bad ID, a duplicate ID, a bad condition, an empty
  title or condition, or a value past a limit. An edited file thus cannot silently change scope.
- A set with no achievements is rejected.
- The IDs are local IDs. They are not official service awards.
- The `Game=` serial is a local safeguard. It is not the RetroAchievements content hash. See
  [PS1 hashing](ACHIEVEMENTS_RESEARCH.md#ps1-hashing).
- A condition that is true at the start does not award at once. The runtime stays in its
  WAITING state until the condition becomes false.
- Conditions on little-endian values, 32-bit values with the high bit set and IEEE floats work.

## Unlock toast

`Gamecube/ach_toast.c` draws a card at the top right of the picture when an achievement unlocks.
The design, the tiers and the mockups are in [design/PS1_STYLES.md](design/PS1_STYLES.md).

| Tier | When | Effect |
|---|---|---|
| Blue | 10 % of players or more, or under 10 points | slides in |
| Bronze | under 10 %, or 10 points or more | one white flash |
| Silver | under 5 %, or 25 points or more | rays from the icon |
| Gold | under 2 %, or 50 points or more | flash and rays |
| Crystal | under 1 %, or 100 points | rays and a white core |
| Finish Line | the unlock that beats the game | the card flashes into black, chequered, gold trim |
| Platinum | the unlock that completes the set | the card bursts into brushed platinum |

- A Local set has no rarity, so the points give the tier. Official mode will give rarity.
- The card shows the title, the points (top right), the description, `BLOCK x/y` and the set
  meter. The icon and the chip come from the type: trophy (standard), key and `PROGRESS`,
  hourglass and `MISSABLE`, flag and `WIN`.
- The engine calls `achievements_notify` once per new unlock (`achievement_notice_t`, in
  `achievements.h`), with the Beaten and Mastered flags. `loadISO` points it at `ach_toast_push`.
  Earned achievements restored from the journal or a state do not notify.
- The card, the progress panel and the challenge icons end 16 px (of the 640 wide picture) from the right edge of the picture on the TV. In Force 16:9 (`ScreenMode` 2) the overlay's ortho is -104..743 over the whole EFB, and the margin is 16 x 848 / 640 units of it: the edge is the screen's (OpenGX's game fills it; the soft plugins' 4:3 picture sits in the middle with black bars, and the pop-ups go over the bar). Counted from x = 640, as until 2026-10-29, they ended 14 % of the screen short of the edge on OpenGX (user, real Wii).
- A pop-up that moves keeps moving when the game does not present: OpenGX `OverlayToastIdle` (GlesGpu/gpuPlugin.c) shows the last presented frame again with the pop-up each vblank after 5 without a present (`flipEGL` keeps that frame in `ovl_buf` while a pop-up animates, also for presents that clear the EFB; the game's own EFB is put aside and back); the Soft plugin presents again in `LIB_GPUupdateLace` (gpulib), and a blanked display shows the pop-up over black (`gc_vout_flip` with no picture).
- Up to 4 toasts wait in a queue. Each one shows for 5 s (6 s for Beaten and Mastered).
- It is drawn in the present's overlay, on the picture sent to the TV, never in the game's frame:
  OpenGX in `flipEGL` (`OverlayAchToast`), the Soft plugins in `GX_Flip`.
- Its clock runs only while it is drawn. A still screen that presents no frames holds it.
- The test is `scripts/chains/ach_toast.txt` with the set from `scripts/ach_toast_fixture.sh`:
  every tier over Spyro's attract demo, on OpenGX and then on the Soft plugin, in one boot.
- The 2 x 2 PS1 dither is one repeated 4 x 4 IA8 texture over the card (one quad).
- Cost on the bench Wii (2026-10-23, Spyro, 3300 vblanks, off/on twice): about 0.2 ms per
  frame while a toast shows (136 us to issue it, +60 us of GPU wait), nothing otherwise; speed
  1.00 either way. Details in [the notes](ACHIEVEMENTS_NOTES.md#unlock-toast-cost-on-the-bench-wii).

### Progress pop-ups

The progress panel (bottom right) shows a measured achievement ("12/40") only when its progress
crosses a new 10 % step: 10 %, 20 % ... of the target. The rule is `Gamecube/ach_progress.h` (plain C,
`tests/ach_progress_test.c`); Official mode applies it to rc_client's PROGRESS_INDICATOR events
(`achievements_official.c` `indicator()`), Local and Test mode to rc_runtime's progress events.

- The first reading after a game load, a disc swap, a reset or a state load is the baseline, not
  news. At boot the game's memory is not set up yet: its numbers jump while it starts.
- For the first 180 vblanks (3 s) after those moments every reading only sets the baseline; at the
  end of that time each measured achievement's step is taken as it is (rc_client and rc_runtime send no
  event for a first reading), so the first real change after it counts.
- The step kept is always the latest: progress that falls back and climbs again shows again.
- Official mode keeps the steps of up to 128 achievements; one forgotten starts at a new baseline (a
  pop-up may be missed, never shown by mistake).

### Unlock pictures

When an achievement unlocks (Official, or Local for any player; not in Test mode, which keeps
nothing) the game's frame is saved to `sd:/wiistation/achievements/shots/<set id>/<achievement id>.png`.
The set id is the game's RetroAchievements id (Official) or the first 16 characters of the set file's
digest (Local). The first unlock of an achievement on this console keeps its picture: an existing
file is never replaced. The picture is VRAM's displayed area as the screenshot button takes it, so
neither the card nor any overlay is in it.

It is made without a visible hitch (`Gamecube/ach_shot.c`):

1. At the unlock (a vblank, emulator thread) the displayed picture is copied out of VRAM as PNG scanlines
   into MEM2 (`screenshot_grab`; OpenGX reads its EFB first). That is the only cost in the frame.
2. At each following vblank deflate takes one 24 KB slice of the oldest waiting picture
   (about a millisecond). No thread is used for this: the frame limiter spins, so a low-priority thread
   would not run while a game does.
3. A worker thread above the emulator's priority writes the file (a temp file, then a rename). It only
   waits on the card, so the emulator runs meanwhile.

Three pictures can wait; more are dropped with a line in the log. Memory is MEM2 only (the picture, its
PNG and a 400 KB workspace given to zlib), freed when the file is made. When the game stops for the menu
the waiting pictures are finished first, so the book finds them. perf.log has a line per picture
(`ach_shot:`) with the cost in the frame (grab), the slices and the write.

Measured so far (2026-10-29, Dolphin, so host time, not the Wii's): a 512 x 240 picture costs 2.6 to 11 ms
in the frame (grab), 16 slices of at most 1.3 to 4.4 ms (12 to 35 ms in all, spread over 16 vblanks), and
6 to 11 ms on the worker for the write. **Not measured on the bench Wii yet**: its queue was held by
another project's jobs all session (job 20261029-030955-d698b0, cancelled; the chain is
`scripts/chains/ach_book_bench.txt`, the recipe is in its header). The numbers to read there are the grab
(a hitch of that size in one frame) and the longest slice; if either is over about 4 ms, shrink `SLICE_IN`
in `ach_shot.c` or grab only the rows a slice needs.

### The book

Current ISO, Achievements (`Gamecube/menu/AchievementsFrame.cpp`). A header with the player's coin and
name, the game, "12 / 89 earned", "95 / 610 points" and a bar; tabs In progress (not earned, measured
progress above 0, most progress first), Locked and Earned (newest first when the time is known); a fourth
tab, Leaderboards, when the set has some. A row: badge (a plain tile in a Local set; dimmed when not
earned), title, description, points, and for a measured achievement a bar with its count. A opens the
detail: a large badge, title, description, progress, points, rarity (Official), type (progression, win
condition, missable), when it was earned (Official: the server's date; Local keeps no time, so the
picture's date is shown under the picture) and the unlock picture, large (decoded to 256 x 192, 98 KB of
MEM2, one at a time, freed when the detail closes).

Buttons: Up/Down choose, Left/Right the tabs (in the detail: the previous and next achievement), A opens,
B goes back (from the list: to Current ISO). The player switch is unchanged: L/R (GameCube), +/- (Wii
Remote, Classic). The pointer clicks tabs and rows. Text is never below scale 0.5 (the Players menu's
body text); titles shrink to fit, then are cut with "...".

If the player opens the menu while any pop-up is on screen (the unlock card, the progress panel or a
challenge icon), the menu opens the book on that achievement's detail (`ach_toast_menu_opened`,
`ach_toast_take_jump`, `Func_AchievementsOpenOn`).

## Memory map

The engine exposes the published rcheevos PS1 map, and nothing more:

| RA logical addresses | PS1 real addresses | WiiStation source |
|---|---|---|
| `0x000000–0x00FFFF` | `0x00000000–0x0000FFFF` | First 64 KiB of `psxM` (kernel RAM) |
| `0x010000–0x1FFFFF` | `0x00010000–0x001FFFFF` | Rest of the 2 MiB `psxM` |
| `0x200000–0x2003FF` | `0x1F800000–0x1F8003FF` | 1 KiB scratchpad at the start of `psxH` |

- `achievements_read_memory` returns the guest bytes as they are. It does not swap them.
- A read that crosses the RAM/scratchpad boundary returns bytes from both.
- A read past the map end returns a short read. A read that starts past the end returns 0.
- BIOS, VRAM, memory-card contents and hardware registers are not in the map.
- `deps/rcheevos` has one local patch: `rc_read_memory` in `src/runtime/rc_memref.c`
  assembles guest bytes in little-endian order on the big-endian Wii. Keep the patch when you
  update rcheevos, unless upstream has the fix. Its history is in
  `deps/rcheevos/README.wiistation.md` and in
  [the notes](ACHIEVEMENTS_NOTES.md#big-endian-bug).

## Lifecycle

| Event | Code | What the engine does |
|---|---|---|
| Game load | `loadISO` (`Gamecube/GamecubeMain.cpp`) calls `achievements_load(CdromId)` after settings and boot | Reads and checks the set, restores journal unlocks, validates addresses |
| Each emulated frame | `psxcounters.c` calls `achievements_frame()` at the frame end (`hSyncCount` reaches `HSyncTotal`) | Evaluates once per emulated frame, also on frames that the display skips |
| Reset | `r3000a.c` calls `achievements_pause()` before the BIOS runs, then `achievements_reset()` | No evaluation during BIOS code. Hit and progress state reset. Earned achievements stay earned. |
| CPU stop (menu, state operation) | `achievements_flush()` after each `psxCpu->Execute()` | Writes unlocks that are not yet in the journal |
| Disc swap | `loadISOSwap` reads the new disc's serial (`CdromReadId`) and calls `achievements_change_media(image, serial)` | Official: rc_client checks the disc. Local/Test: the set goes on when a `Game=` line names the disc, else it closes until the next full game load |
| Shutdown | `r3000a.c` calls `achievements_close()` | Writes pending unlocks and destroys the runtime |

- The frame hook does no allocation and no network calls. Its only file write is the journal
  append when an achievement triggers. That is rare: one short line.
- A disc swap closes the set because the engine cannot associate two discs of one game. See
  [Multi-disc games](#multi-disc-games).

## Unlock journal

Each line is `<definition MD5> <id> <player key> <title>`. The MD5 is the digest of the exact
definition file (Official mode's Local players: the digest of the RetroAchievements game, see
[Official mode with Local players](#official-mode-with-local-players)). The player key is the Mii's creation ID as 16 lowercase hex characters
(Players menu, `Docs/MII_PLAN.md`). A line without a key, `<MD5> <id> <title>`, is Guest's: this
is every line written before 5.19.0, and Guest still writes it (a Guest line whose title starts
with a word that looks like a key has `-` where the key goes).

- Local mode: each seated Mii whose port has a connected pad earns; Guest earns only when no
  Mii does. One line per Mii that earned it, and each achievement is earned once per Mii.
- At load, the engine restores, for each Mii now playing, the lines with its key; the lines of
  Miis not playing stay in the file and are never deleted.
- The set's earned state is the union of the players' unlocks. rcheevos keeps an achievement
  active while any player lacks it; when it triggers, only the players without it get the
  line and the toast names them.
- A set loads again for the new players when the seats or the pads change between two stretches
  of play (`achievements_players_changed()`, called as the game goes on after the menu).

- The engine appends a line when an achievement triggers. If the write fails, the engine
  tries again at each CPU stop and at shutdown. It never tries again per frame.
- At load, the engine restores the unlocks whose MD5 matches the current file.
- Only whole lines count. A line without a newline at the end (a record cut short by a power
  loss) restores nothing. The next append starts on a new line. A fragment thus cannot grant a
  wrong unlock or swallow the next record.
- The journal limit is 1 MiB. Past it, an append fails, and a load refuses the whole set.
  A partial restore would earn the rest again and write duplicate lines.
- Any edit to the definition file, also to a comment, changes its MD5. The edited file thus
  starts a new local unlock namespace. The MD5 is a consistency check. It is not
  authentication.
- The journal is not a durable outbox for official awards.

## Save states

States carry an optional `ACH1` section in the `WSXE` extension (`misc.c`). The PCSX state
layout does not change.

| Part | Content |
|---|---|
| Header, 36 bytes | `WAC1` version marker, 16-byte definition MD5, 128 earned bits (the union of the players) |
| Body | Upstream sized runtime progress (`rc_runtime_serialize_progress_sized`) |
| Players, only when Miis played | Their keys (16 bytes each), their count (1 byte) and `WK`, after the body. A state made by Guest has none: it is byte for byte the state of 5.18. |
| Size limit | 256 KiB (`ACHIEVEMENTS_STATE_MAX`) |

- A state for a different set, an old state, a state with no section or a corrupt section
  resets the runtime progress.
- Loading an older state never revokes an earned local achievement.
- A state never moves one Mii's unlocks to another: the earned bits of a state apply only when
  the same single player (one Mii, or Guest) plays now. With other players, the game state is
  loaded and the earned flags come from the journals of the Miis seated now.

## Source files

| File | Content |
|---|---|
| `achievements.c`, `achievements.h` | The engine. All calls belong to the emulator thread. |
| `Gamecube/ach_toast.c`, `ach_toast.h`, `ach_icons.h` | The unlock toast and its 16 x 16 icons |
| `deps/rcheevos/` | Pinned rcheevos `include/`, `src/runtime/`, `src/util/`, `src/client/`, `src/api/`, `src/hash/`. The emulator compiles only the runtime and util sources. |
| `Gamecube/achievements_bench.cpp` | Bench-only hooks (`ACHIEVEMENTS_BENCH`): stop-boundary counts, reset and disc-swap checks, card isolation, return to HBC after a chain. Product builds omit it. |
| `misc.c` | The `ACH1` state section |
| `tests/achievement_seed.c`, `tests/achievement_clock.c` | TLS seed storage and the UTC clock (with the Date-header time zone): built into the product by `Gamecube/ra_transport.c`, tested on the host. |
| `achievements_official.c` | Official mode: rc_client, the PS1 hash through the CD plugin, the list, leaderboards, rich presence, states |
| `Gamecube/ra_transport.c` | HTTPS worker; the clock's time zone learnt from verified responses |
| `Gamecube/ach_badge.c`, `ach_badge.h`, `ach_badge_conv.h` | Badge pictures: download, card cache, PNG decode (libpng), RGB5A3 |
| `Gamecube/ach_players.h`, `ach_players.c` | Who earns (Local): the seated Miis on connected pads, the engine's hook `achievements_players` |
| `Gamecube/ach_account.h` | Which account file a link means, the RetroAchievements set's journal digest and path (plain C) |
| `tests/achievements_ra_test.c`, `tests/ach_account_test.c` | Host tests: the Local side of Official mode (who, journal, restore, the page, a failed account, the cap); the account paths and names |
| `tests/achievements_players_test.c` | Host test: old lines are Guest's, two Miis, a swap, a missing Mii, the 1 MiB cap, the union, states of other players |
| `Gamecube/menu/AchievementsFrame.cpp` | The book: tabs, rows, the detail with the unlock picture, the leaderboards (one player at a time) |
| `Gamecube/ach_progress.h` | The 10 % step rule of the progress pop-up (plain C) |
| `Gamecube/ach_shot.c`, `ach_shot.h` | The unlock pictures: grab, sliced compression, the worker's write; the file names (plain C) |
| `Gamecube/ach_picture.c`, `ach_picture.h` | One unlock picture as a 256 x 192 RGB5A3 texture for the book |
| `scripts/ra_setup.py`, `scripts/ra_ca_bundle.py` | The per-Wii files (seed, account; `--ra-name NAME` puts an account in `achievements/ra/NAME.ini` for a Mii); the pinned CA bundle and its checks |

The runtime objects are compiled without fast-math and LTO. This keeps the floating-point
semantics of conditions under the core's `-Ofast` flags.

## How to run the tests

### Host tests

The host tests need a Linux C toolchain with ASan and UBSan. On this Windows machine, run them
in WSL. Git Bash's `cc` has no C headers, and clang on Windows has no POSIX `mkdir`.

```sh
wsl.exe -e bash -lc "cd /mnt/c/projects/WiiStation && bash scripts/test-achievements.sh"
```

| Script | What it tests |
|---|---|
| `scripts/test-achievements.sh` | The engine with the vendored runtime. Cases: memory boundaries, little-endian and scratchpad conditions, hit-count restore, pause and reset, monotonic unlocks, journal reload, changed-set rejection, corrupt progress, disabled mode, duplicates, malformed definitions, size limits. ASan covers all sources. UBSan covers WiiStation's code only, because upstream parser offset expressions trigger Clang UBSan diagnostics. |
| `scripts/test-achievements-client.sh` | Gets and checks the pinned upstream test archive. Runs the upstream runtime, API and client suites with ASan. The upstream hash tests are off. |
| `scripts/test-achievements-entropy.sh` | The Mbed TLS NV-seed contract with a public dummy seed. See [the notes](ACHIEVEMENTS_NOTES.md#tls-seed-and-utc). |
| `tests/ach_progress_test.c` (in `scripts/test-achievements.sh`) | The 10 % step rule and the unlock pictures' file names. |
| `scripts/settings_check.py` | Settings consistency. Use `C:/Python312/python.exe` on this machine. |

### Wii tests

All Wii runs go through the bench queue (`C:/tools/wii-bench/wiibench.py`). Read
[bench testing](BENCH_TESTING.md) for the queue workflow. Do not start a hardware run directly.
Do not make lease environment variables yourself. Each run freezes its DOL, matching ELF,
collector and HBC client, with SHA-256 hashes, and keeps raw evidence under `.runs/bench/`.

The HTTPS and seed builders (`build-achievements-native-https-test.sh`,
`test-achievements-entropy.sh --wii`) use libogc v3 and devkitPPC r50, as `scripts/build.sh`.
The other standalone Wii test builders (`build-achievements-wii-test.sh`,
`build-achievements-client-test.sh`, `build-achievements-hash-wii-test.sh`) still require
libogc2 and devkitPPC r41-2. `build-achievements-wii-test.sh` stops if the compiler is not
release 41. The commands below use the toolchain path of the 2026-10-10 setup.

Engine suite (`tests/achievements_wii.c`). Wait for the build to finish before you freeze it:

```sh
export DEVKITPRO=/tmp/wiistation-devkitpro-local
export DEVKITPPC="$DEVKITPRO/devkitPPC-r41-2"
bash scripts/build-achievements-wii-test.sh .runs/bench/unit-build
python3 scripts/bench_achievements.py prepare \
  --dol .runs/bench/unit-build/boot.dol \
  --client ../hbc-reborn/tools/hbc.py --bundle .runs/bench/unit-frozen
python3 "C:/tools/wii-bench/wiibench.py" add \
  --name wiistation-achievements-unit --agent wiistation --timeout 240 \
  --cwd "$PWD" -- python3 .runs/bench/unit-frozen/bench_achievements.py run \
  --bundle .runs/bench/unit-frozen
```

Emulator matrix with the synthetic fixture disc. `build-achievements-bench.sh` runs
`scripts/build.sh debug` with `-DACHIEVEMENTS_BENCH` added to `PROBES`:

```sh
PROBES='-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0' \
  bash scripts/build-achievements-bench.sh
python3 scripts/achievement_fixture.py .runs/bench/fixture
python3 scripts/bench_achievement_games.py prepare \
  --dol Gamecube/WiiSXRX_debug.dol --client ../hbc-reborn/tools/hbc.py \
  --fixture .runs/bench/fixture --bundle .runs/bench/games-frozen
python3 "C:/tools/wii-bench/wiibench.py" add \
  --name wiistation-achievements-games --agent wiistation --timeout 600 \
  --cwd "$PWD" -- python3 .runs/bench/games-frozen/bench_achievement_games.py run \
  --bundle .runs/bench/games-frozen
```

Do not build in the main tree while the user records movies: build in a worktree. Use the
queue's `status`, `run` and `wait` commands to follow a job.

Gameplay capacity pilot. Build with the timing probes, then use
`scripts/bench_gameplay.py` with `scripts/gameplay_profiles/spyro_capacity.json` and
`scripts/autoinput/spyro_long_play.txt`. `bench_gameplay.py prepare --help` lists the frozen
inputs. Send the frozen `bench_gameplay.py run --bundle ...` through `wiibench.py add`.

```sh
DEVKITPRO=/tmp/wiistation-devkitpro-local \
DEVKITPPC=/tmp/wiistation-devkitpro-local/devkitPPC-r41-2 \
PROBES='-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0 -DPERF_PROF_VSIG=0 -DPERF_PROF_ACHIEVEMENTS=1' \
bash scripts/build-achievements-bench.sh
```

- `PERF_PROF_VSIG=0` removes the per-frame RAM signature work and its allocation. Timing passes
  need it. The end RAM and VRAM hashes stay available.
- `PERF_PROF_ACHIEVEMENTS=1` counts evaluation time, the longest call and the calls. These
  counters and the test-only paths add no timer cost to the product.
- `scripts/gameplay_results.py` summarizes paired runs. See
  [the notes](ACHIEVEMENTS_NOTES.md#accepted-spyro-capacity-campaign).

Collector regression tests: `tests/bench_achievements_test.py`,
`tests/bench_achievement_games_test.py`, `tests/bench_gameplay_test.py`,
`tests/gameplay_results_test.py`.

## Planned

Decisions of the project owner, 2026-10-23.

### Modes

| Mode | Definitions | Unlocks |
|---|---|---|
| Test | A definition file, to try it out | Not kept. The journal is not read or written. |
| Local | The user's own definition file | Kept in the journal. Local mode never sends an unlock to RetroAchievements. |
| Official | RetroAchievements sets | Casual only. Hardcore needs RetroAchievements to approve WiiStation as a client. |

The mode switch is in the product (setting `Achievements`, 775ae73a). Test and Local work.
Official works in Casual (2026-10-23): login, the PS1 hash in every format, disc swaps.

### Official setup

What a Wii needs on its card, in `sd:/wiistation/achievements/`, and where each comes from. Nothing
here is typed by hand, and no secret passes through a log or a person's screen.

| File | Comes from | Safety |
|---|---|---|
| `cacert.pem` | Every install (`wii_install.py`): `Gamecube/release/achievements/cacert.pem` | Pinned: `cacert.pem.source` holds its SHA-256 and origin (MSYS2 `ca-certificates-20241223-1`, Mozilla NSS); the install refuses another file. `scripts/ra_ca_bundle.py` checks the pin, lists expired and expiring roots, and connects to retroachievements.org and media.retroachievements.org trusting only this bundle. `ra_ca_bundle.py update SRC --source TEXT` replaces it only after every check passes; the script never downloads a bundle. |
| `tls_seed.bin` | `scripts/ra_setup.py --seed` | 64 bytes of `os.urandom`, made in a private temp folder, sent through HBC, zeroed and deleted; never printed. Never replaces a seed that is there. |
| `ra_account.ini` | `scripts/ra_setup.py --account FILE` | Sent unopened (only its size is checked). Never replaces a file that is there (it holds the token after the first login). |
| `ra/<name>.ini` | `scripts/ra_setup.py --account FILE --ra-name NAME` | The same, for an account a Mii links to: NAME is letters, digits, `_` and `-`, under 32 characters. |
| `utc_offset.txt` | The Wii itself | The time zone, learnt from the `Date` header of a response that came over verified TLS: rounded to the quarter hour, bounded to 14 hours (a lying server moves the clock by hours, never a certificate's validity by years), written through a temp file. Summer time is learnt the same way. A clock more than 14 hours from the server's is reported, not learnt: set the Wii's date. |

`ra_setup.py` runs on the bench Wii as a queue job; on a production-style Wii (`--production`,
`--lrwii`) only when asked, directly, after the HBC-idle check, and it writes only those two
files. Until the Wii has learnt its time zone (its first answer), its clock is taken as UTC (or
`AchievementUTCOffset`): only a certificate issued in the last few hours could look not yet
valid then.

### Official login

- The account file is a Mii's (Docs/MII_PLAN.md, step 4a): the Mii that plays Official
  (`players_official()`) is linked to `sd:/wiistation/achievements/ra/<name>.ini`, or to this
  Wii's `sd:/wiistation/achievements/ra_account.ini` (link `*`; also what is used when no Mii
  is linked to the Official seat). `Gamecube/ach_account.h` turns a link into a path
  (`ach_account_path`, host-tested in `tests/ach_account_test.c`): a name with a `/`, `\`, `:`
  or a leading `.` is no file name and counts as a failed account. The files have `User=` and
  `Password=`.
- The menu's early login (once, at the first menu frame) uses the account of the moment. A game
  load that wants another account than the logged-in one logs out (`rc_client_logout`) and in
  again; a login still on its way is waited for first (its answer decides, by generation, so an
  old answer never counts for the new account). An account never changes in the middle of a game.
- WiiStation logs in once, writes `Token=` to the file in use (a `.new` temp file next to it,
  then a rename) and deletes the `Password=` line.
  (The research advised to keep a token only when the user opts in. This decision replaces
  that advice.)
- A linked account that does not work (no file, no `User=`, a failed login): that Mii earns Local
  for the game. A Local set needs the set's definitions, and those come from a session: this
  Wii's `ra_account.ini`, if there is one, loads the set in spectator mode (nothing reaches the
  account), and every seated Mii, that one too, earns Local. The Mii's page says why
  ("Account file missing: earns Local"). Without `ra_account.ini` there is no set (no session
  can fetch it): the status line says why. This Wii's own account failing has no fallback: no
  set, as before. Nothing tries a password again by itself; a new game load is a new attempt.
- The password never goes in `settings.ini`. The password never goes in a log.
- Logout deletes the stored token. An account change keeps caches, pending awards and progress
  apart for each account.
- Do not call the token file encrypted. An SD card is readable storage. A key next to the file
  does not protect it.
- Keep the token out of frozen manifests, state metadata, crash collection and exported
  configurations. Redact the password, the token, the Web API key, POST bodies and
  authenticated URLs from all logs. Log the operation, the status and the time instead.

### Official mode with Local players

One RetroAchievements session is one user, so one Mii plays Official (Docs/MII_PLAN.md, "Official
mode per Mii"). The other Miis seated on a port with a connected pad earn the same set Local.
No Local Mii seated (Guest alone, or the Official Mii alone): nothing here runs, and Official is
as it was. `achievements_ra_*` (`achievements.c`) is the Local side; `achievements_official.c`
feeds it.

| Part | What happens |
|---|---|
| Who | `achievements_ra_plan(official_key)` at the game load: the Miis the players hook gives (`ach_players_get`: seated, pad connected, still on the console) except the Official Mii. With no Official Mii (this Wii's account), all of them. |
| The set | When rc_client has loaded the set, its achievements (id, title, description, badge, points, rarity, type) become the Local side's table (a malloc'd table of at most 256; sorted by id). The journal, the bits per Mii and the page are Local mode's. |
| Journal | `sd:/wiistation/achievements/ra_local/<game id>.unlocks`, one file per game, lines `<digest> <id> <player key> <title>` as in Local mode. The digest is `5241` (ASCII `RA`), 20 zeros and the RetroAchievements game id as 8 hex digits (`ach_ra_digest`): 32 characters, never an MD5. `AchievementLog` does not apply here. A journal past 1 MiB turns the Local side off for that game (logged); the Official side goes on. |
| Encore | While a Local Mii is seated, encore mode is on: rc_client activates every achievement, also the ones the account has. A trigger of one the account has leaves the Official side with nothing to do. |
| Server calls | rc_client's `rc_client_award_achievement` marks the achievement earned and calls `can_submit_achievement_unlock` and then, if that says yes, sends the award, whether or not the account had it (the pinned `rc_client.c`, `rc_client_raise_achievement_events`). The server answers a repeat with a success and a message, so nothing breaks, but WiiStation does not send it: `can_submit` (a field of `rc_client_t.callbacks`; the pinned header has no setter) says no for an id the account had when the set loaded or got since. |
| A trigger | The account gets the server unlock and its toast only if it did not have the achievement. Each Local Mii without it gets a journal line, and the toast names them. Both sides new: two toasts. |
| Toast | A tag over the names: "RetroAchievements" (the Official Mii's: `Kat, 8.1% have it`) or "Local" (`Ollie and Rina unlocked`). Without Local Miis the card is as before, with no tag. |
| Page | The Official Mii first (rc_client's list: earned is `unlocked`, so an achievement the account has shows earned in encore mode too), then each Local Mii (their journal). The Local view shows no measured progress and no rich presence. |
| States | Official's `WAO1` section is as it was. A state does not carry the Local Miis' earned flags: they come from their journals. |
| Logs | A Mii key shows as its first 8 hex characters, never a name. |

### Multi-disc games

- Official mode keeps the set through a disc swap (2026-10-23). `loadISOSwap` calls
  `achievements_change_media` once the new disc is open: the disc is hashed then, from the
  menu, and `rc_client_begin_change_media` checks it. Until rc_client answers, nothing is
  evaluated (`rc_client_idle` only). The same game's disc goes on. A disc of another game, or
  one RetroAchievements does not know, unloads the set until the next game load (the pinned
  rc_client's `rc_client_find_game_hash` gives the disc's game).
- A swap before the login is done needs no check: the set then loads from the new disc.
- Test: `scripts/chains/ach_discswap.txt` (the `discswap` script line).
- Local and Test mode keep the set through a swap to a disc its `Game=` lines name
  (2026-10-24). `loadISOSwap` reads the new disc's serial with `CdromReadId()` (misc.c: the
  label and serial only, none of `CheckCdrom`'s region, hack or PPF changes) and passes it
  to `achievements_change_media`. A disc the set does not name, or one with no PS1 boot file,
  closes the set until the next game load. Unlocks, journal and toasts carry on unchanged.
- Test: `scripts/chains/ach_discswap_local.txt` (FF8 disc 1 -> disc 2 goes on -> Frogger
  closes; the set is `tests/fixtures/ach_multidisc`). A swap in the middle of a game that is
  reading its disc can crash the game: FF8's intro hit Lightrec fault 1 at vblank 1270 after
  the swap at 900. That is the game reading another disc's data, not the set.
- A `.exe` gets no achievements in any mode: it has no disc to identify. The ISO Info page and
  the message after a load say so (`achievements_status`).

### Prerequisites for Official mode

| Prerequisite | State |
|---|---|
| HTTPS stack (Mbed TLS 3.6.4 + curl 8.16.0) | Proven on the bench Wii on 2026-10-11. Built for libogc2 and devkitPPC r41-2. It must be rebuilt for libogc v3, the default since WiiStation 5.13.0. |
| Strong TLS entropy | The proven probe uses a public dummy NV seed. A secret seed for each Wii needs confidential provisioning. See [TLS entropy](ACHIEVEMENTS_RESEARCH.md#tls-entropy). |
| Correct UTC | Needs a reviewed UTC source. See [UTC clock](ACHIEVEMENTS_RESEARCH.md#utc-clock). |
| `rc_client` connected to the emulator | Not done: login, PS1 hash through WiiStation's own disc reader (also CHD and PBP), frame and idle pump, nonblocking transport. |

### Design rules for Official mode

These rules come from [the research](ACHIEVEMENTS_RESEARCH.md).

1. **Identity.** Use the upstream PS1 hash. A disc serial, an image MD5, a card file name or the
   save inventory is not a substitute. An unknown image gets an "unsupported version" message,
   never a guessed set.
2. **Disc reader for hashing.** Use a dedicated read-only image handle. Hashing must not move
   the live emulation CD position, close shared handles or touch image or decompression state
   that is not thread-safe. Support BIN/CUE, ISO, CHD and multi-disc PBP one at a time. Reject
   other formats. For PBP, select PlayStation and the current disc explicitly. Keep a cached
   hash only with a strong file identity. Changed content clears it.
3. **One owner thread.** The emulator thread owns `rc_client` and all memory reads. It evaluates
   once per emulated frame, runs completed callbacks and sends small UI events. Call
   `rc_client_set_allow_background_memory_reads(client, 0)` before an asynchronous load. Call
   `rc_client_idle` while paused and in menus.
4. **Transport worker.** A worker owns bounded HTTP requests, DNS, TLS, downloads and timeouts.
   It never reads emulated RAM. It copies the request URL, body and content type before the
   `server_call` callback returns. It completes each request exactly once. Generation IDs stop
   a response for an old game or login from changing the new session.
5. **No new protocol.** Use the rcheevos Connect flow. The public Web API is not an award
   endpoint. Never send credentials or tokens over plain HTTP. Do not write a TLS
   implementation or a certificate-check shortcut.
6. **Offline.** Use the upstream retry behaviour first. Show local unlocks, pending submissions
   and confirmed unlocks differently. Never make official awards from local records or old
   memory-card milestones. Test an offline start and a network drop after login as two cases.
7. **Network sharing.** Do not make Official mode need SMB settings. Do not share a libsmb2
   context with HTTP. Do not call a second network initializer during HBC-agent start.
   Measure HTTPS together with SMB streaming.
8. **UI.** Add an Achievements settings panel and a list for the current game: on and off,
   login and logout, Casual status, identification state, earned and locked entries, progress,
   retry and error status. Controller and keyboard navigation. Text popups first, badges later.
   A bounded notification queue and a capped icon cache. Notifications work with both GPU paths,
   with 240p and interlace, and after a resolution change. A network or image error never pauses
   the game. A debug overlay is not enough: each graphics backend needs a release path that
   restores GX state.
9. **Storage.** Keep set data, badges and local progress under `sd:/wiistation/achievements/` or
   the selected USB device, apart from `.mcd` files. Evict downloads before emulator RAM. Never
   put disc images or card contents in a service request.
10. **States.** Restore runtime progress with rcheevos serialization. Reject a state for another
    game or set. Reset progress for an old state without data. Never revoke a confirmed server
    unlock when a state loads.
11. **Cards.** Imported memory cards stay usable in Casual. Their contents do not prove a past
    unlock.
12. **Hardcore.** Hardcore stays off until RetroAchievements approves WiiStation and all
    [Hardcore rules](ACHIEVEMENTS_RESEARCH.md#hardcore-rules) are in place. Enforce the rules in
    the common functions, not only with greyed menu buttons. Include statetool, autoboot,
    per-game configurations and forced state loads. Review `fastLoad`, timing and game hacks,
    HLE BIOS and CPU options against service policy. Do not ban ordinary frameskip or allowed
    fast-forward without evidence. A change to Hardcore needs a reset. Disc swaps and resets
    send client notifications. Never make WiiStation look like an approved emulator.

### Code that Official mode touches

- `psxcounters.c`: all CPU and GPU paths go through one emulated vertical-frame path.
  `VBlankStart` calls `gpuPtr->updateLace()` and `SysUpdate()`. The frame-end branch increments
  `frame_counter`. Never tie evaluation to the Wii screen refresh or an FPS counter.
- `psxmem.c`: `psxM` is the 2 MiB RAM. `psxH` holds hardware and scratchpad storage.
- `Gamecube/GamecubeMain.cpp`: `loadISO` does image load, identification, reset, settings and
  cards. `loadISOSwap` clears `CdromId` and opens the disc again. A stale serial or file name
  must not select a set.
- `misc.c`: the `WSXE` extension has tags, lengths and skips unknown tags. Bound the
  achievement section and tie it to the game hash, the runtime version and the definition
  identity.
- `Gamecube/fileBrowser/fileBrowser-SMB.c` starts the network in a worker. Its connection state
  is private and made for SMB. Make general only the network readiness that achievements need.
- `Gamecube/fileBrowser/smb2dev.c` pumps asynchronous reads while games run. HTTPS will compete
  with it for IOS and network time.
- Both Wii makefiles set big-endian macros. Neither links an HTTP or TLS stack today.
  `mem2_manager.c` and the profiler measure allocation pressure and run time.
- Menus are in `Gamecube/menu/`.

### Phases and exit gates

| Phase | Work | Exit evidence |
|---|---|---|
| 0. Feasibility | Pin rcheevos. Start the client and user-agent approval talk. Compile the C runtime with devkitPPC. Prototype verified HTTPS. Measure memory and transport contention. | Cross-build works. The Wii authenticates with verified TLS. Memory and stack peaks and SMB contention are recorded. No promise of Hardcore. |
| 1. Deterministic local core | Byte callback, shared frame hook, image adapter and hashes, reset and swap lifecycle, fixture sets without submissions | Endian and bounds checks pass. One evaluation per emulated frame, also with frameskip. Hashes match a trusted reference for each supported format. |
| 2. Casual client | Login, async responses, set load, events, game list, minimal popups, retry and error paths | A dedicated test account gets real Casual awards. No false award for a wrong region or hash. Errors leave games playable. No secrets in logs. |
| 3. Usable beta | Progress indicators, cached badges, state section, reconnect and offline policy, multi-disc, SD and USB | A state round trip keeps hit and measured progress. Old states work. No stale-game callbacks. No duplicate awards after reconnect. Power-loss and storage-removal behaviour documented. |
| 4. Hardcore | Feature restrictions, leaderboards, Rich Presence, all indicators, secure queue, approved user agent | Current upstream rules met, all bypass paths tested, approval received. If not, stay Casual. |
| 5. Local mode | Local profiles and sets through the rcheevos runtime, own namespace and progress | Works without an account. Cannot send local unlocks to RetroAchievements. |

- Phase 0 is done in part. The runtime cross-builds, and verified TLS works on the bench Wii.
  No login occurred. Phase 1 is done except the hash adapter in the emulator. Phases 2 to 5
  have not started. See [the notes](ACHIEVEMENTS_NOTES.md#what-remains).
- Do not add these during phases 0 to 3: a multi-provider layer, an authoring toolkit, a new
  backend server, a libretro rewrite or a mandatory hardware adapter.
- If native HTTPS fails, write a measured comparison of the ESP32 adapter and a companion
  transport, and choose one. Never fall back silently to unencrypted credentials.
