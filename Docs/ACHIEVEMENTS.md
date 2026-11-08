# Achievements, players and Miis

This is the reference for the WiiStation achievement engine and for the players that earn
achievements: the Miis seated on the PS1 pad ports. It tells what the engine does today, how
to set it up, how to test it, how Miis become players ([Players and Miis](#players-and-miis);
this was `MII_PLAN.md` until 2026-11-04) and what is planned.

- What we learned from outside sources (rcheevos, RetroAchievements rules, PS1 hashing, TLS,
  DuckStation, other systems): [ACHIEVEMENTS_RESEARCH.md](ACHIEVEMENTS_RESEARCH.md).
- Test runs, queue job IDs, measured numbers and open work:
  [ACHIEVEMENTS_NOTES.md](ACHIEVEMENTS_NOTES.md).

## Status

| Part | Status on 2026-10-24 |
|---|---|
| Local evaluation of a definition file | In the product. Off by default. Merged into main from `codex/achievement-engine` (c29cafd9, 2026-10-12). |
| Modes Off, Custom, Test, Official, Local | In the product (775ae73a, 2026-10-23). Official: RetroAchievements in Casual. **5.19.0 names:** what was called Local (your own definition file, unlocks in a journal) is **Custom**; **Local** is now RetroAchievements' set with nothing ever sent, every player earning into the Local journal. See [Local mode](#local-mode). Below, "Custom" is the old Local; "the Local journal" and "the Local side" are the `ra_local` journals of a RetroAchievements set, which Official mode's other Miis and the Local mode earn into. |
| Local mode | In the product (5.19.0, branch `try/modes`): the official set, spectator mode, a request filter, the set saved on the card, every player earns Local. Host tests, a Dolphin run and a bench job: [Local mode](#local-mode). |
| Players menu, per-Mii Local unlocks | In the product: the Players menu 5.18.0 (2c26d9f7), Co-Op seats 5.18.1 (0a91d1c0), `players.ini` and the recent list 5.18.2 (ead7c5a0, 10bece41); per-Mii Local unlocks (932fad3b, merged 9e2ab87a); the Edit slot, account links and Official button (8c727ea2). Open: hardware proof of an Official login with a Mii's own account, a Local player earning on a real set on hardware, a real Local download with an account on the bench Wii. See [Players and Miis](#players-and-miis). |
| Official mode per Mii | In the product (5.19.0, [Players and Miis](#players-and-miis), Official steps 4a and 4c): the Mii that plays Official logs in with its own account file; the other seated Miis earn the same set Local, with encore mode on and nothing sent twice. See [Official mode with Local players](#official-mode-with-local-players). |
| Unlock journal | In the product. An unlock is written when it triggers (f9ca407b). Records cut short by a power loss are ignored (2026-10-23). |
| Save-state progress (`ACH1` section) | In the product. Custom: `WAC1`; Official and Local: `WAO1` (rc_client's progress). A game reset resets the progress in each. |
| Menu controls | General tab, Achievements: Mode, Unofficial, Encore, Challenge icons, the set file. Current ISO, Achievements: the book (2026-10-29), see [The book](#the-book). ISO Info: the achievement status line. |
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
| `Achievements` | 0 = Off, 1 = Custom, 2 = Test, 3 = Official, 4 = Local | 0 | Custom evaluates your own set and keeps unlocks in the journal (it was called Local before 5.19.0; the number is the same, so a settings file keeps its meaning). Test evaluates the set, but does not read or write the journal. Official: RetroAchievements, Casual. Local: the RetroAchievements set, nothing sent, every player earns into the Local journal ([Local mode](#local-mode)). Off stops all frame evaluation. The menu cycles Off, Local, Official, Custom, Test. |
| `AchievementUnofficial` | 0, 1 | 0 | Official: the set's Unofficial (unpromoted) achievements too. RetroAchievements does not keep their unlocks. |
| `AchievementEncore` | 0, 1 | 0 | Official: achievements already earned can be earned again this session (the card shows again; nothing new is sent). Also on, whatever this says, while a Local Mii is seated next to the Official one ([Official mode with Local players](#official-mode-with-local-players)). |
| `AchievementChallenge` | 0, 1 | 1 | The flashing icons of achievements that are one step away (RetroAchievements' challenge indicator; a Local set's primed ones). 0 hides them, at once. Menu: General, Achievements, "Challenge icons". |
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
body text); titles shrink to fit, then are cut with "...". The menu combination (the one that stops a
game for the menu: Start+X on a GameCube pad, + and - together on a Wii Remote, Home on a Classic
Controller) goes straight back into a game that has been started (`MenuContext::isRunning`,
`pad_menu_combo_held` in `PlugPAD.c`); it counts once it was seen released in the book, so the press
that opened the menu does not send the game back at once. The name and coin in the header are those
of the Mii on the seat now: the book asks the seats again when it opens (`achievements_players_changed`),
and in Official mode with no Local Mii it names the Mii that plays Official ("This Wii" for the Wii's
own account), not Guest.

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
| Disc swap | `loadISOSwap` reads the new disc's serial (`CdromReadId`) and calls `achievements_change_media(image, serial)` | Official and Local: rc_client checks the disc. Custom/Test: the set goes on when a `Game=` line names the disc, else it closes until the next full game load |
| Shutdown | `r3000a.c` calls `achievements_close()` | Writes pending unlocks and destroys the runtime |

- The frame hook does no allocation and no network calls. Its only file write is the journal
  append when an achievement triggers. That is rare: one short line.
- A disc swap closes the set because the engine cannot associate two discs of one game. See
  [Multi-disc games](#multi-disc-games).

## Unlock journal

Each line is `<definition MD5> <id> <player key> <title>`. The MD5 is the digest of the exact
definition file (Official mode's Local players: the digest of the RetroAchievements game, see
[Official mode with Local players](#official-mode-with-local-players)). The player key is the Mii's creation ID as 16 lowercase hex characters
(Players menu, [Players and Miis](#players-and-miis)). A line without a key, `<MD5> <id> <title>`, is Guest's: this
is every line written before 5.19.0, and Guest still writes it (a Guest line whose title starts
with a word that looks like a key has `-` where the key goes).

- Custom mode (and Local mode: the same rule): each seated Mii whose port has a connected pad earns; Guest earns only when no
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
| `Gamecube/ach_ra_cache.h` | Local mode: which requests may leave the console, the card's copy of the set (file names, what is kept, how old), the login that is made up (plain C) |
| `tests/achievements_local_test.c`, `tests/ach_ra_cache_test.c` | Host tests: Local mode (Guest and Miis earn, players change in play, the numbers of the modes); the request rules and the cache names |
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
wsl.exe -e bash -lc "cd <repo> && bash scripts/test-achievements.sh"
```

| Script | What it tests |
|---|---|
| `scripts/test-achievements.sh` | The engine with the vendored runtime. Cases: memory boundaries, little-endian and scratchpad conditions, hit-count restore, pause and reset, monotonic unlocks, journal reload, changed-set rejection, corrupt progress, disabled mode, duplicates, malformed definitions, size limits. ASan covers all sources. UBSan covers WiiStation's code only, because upstream parser offset expressions trigger Clang UBSan diagnostics. |
| `scripts/test-achievements-client.sh` | Gets and checks the pinned upstream test archive. Runs the upstream runtime, API and client suites with ASan. The upstream hash tests are off. |
| `scripts/test-achievements-entropy.sh` | The Mbed TLS NV-seed contract with a public dummy seed. See [the notes](ACHIEVEMENTS_NOTES.md#tls-seed-and-utc). |
| `tests/ach_progress_test.c` (in `scripts/test-achievements.sh`) | The 10 % step rule and the unlock pictures' file names. |
| `scripts/settings_check.py` | Settings consistency. Use `python` on this machine. |

### Wii tests

All Wii runs go through the bench queue (`$WII_BENCH`). Read
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
python "$WII_BENCH" add \
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
python "$WII_BENCH" add \
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

## Players and Miis

Goal: each Mii on the console is a player. A Players menu seats Miis on the PS1 pad ports;
**Local** achievements (the unlock journal) are kept per Mii. This part was `Docs/MII_PLAN.md`
until 2026-11-04. It is the plan as written on 2026-10-28 (owner's decisions below), with the
status as of 2026-11-03; the Status table at the top of this file has the one-line state of each
piece. Section names that say "Official mode per Mii" here are the plan; the built design is
[Official mode with Local players](#official-mode-with-local-players), under Planned.

**Status (2026-11-03):** phases 0 to 3 and Official steps 4a, 4b and 4c are built. The Players menu shipped in 5.18.0 (2c26d9f7), Co-Op seats in 5.18.1
(0a91d1c0), `players.ini` and the recent list in 5.18.2 (ead7c5a0, 10bece41), a missing Mii seats
Guest with a notice (35088774). Per-Mii Local unlocks (phase 3): 932fad3b, merged 9e2ab87a. The
Edit slot, account links and the Official button (4b): 8c727ea2. Official session per Mii (4a,
4c): 6c4c17f7, merged b1d68161. Open: hardware proof of an Official login with a Mii's own
account (4a), a Local player earning on a real set on hardware (4c), and a real Local download
with an account on the bench Wii (see the check column below and [Official mode with Local players](#official-mode-with-local-players)).

### The Mii SDK (hbc-reborn `sdk/mii`)

WiiStation uses hbc-reborn's Mii SDK, vendored as `deps/mii` from the same commit as the HBC
agent (`deps/hbc_agent`, HBC 1.12.1, bdf44d2). It is public domain: `mii.c`/`mii.h` (data and
the database), `miidraw.c`/`miidraw.h`/`miibody.c` (drawing with GX). Its README is the reference;
what this plan uses:

| Need | SDK call | Notes |
| --- | --- | --- |
| Read the Miis | `mii_db_load`, `mii_db_get` for slots 0-99, `mii_db_free` | About 0.7 s: once, on a thread, never in the frame loop. **WiiStation never saves** (`mii_db_save`, `_put`, `_add`, `_remove` are not called anywhere). `MII_E_NO_FILE`: the Mii Channel was never opened, so no Miis |
| Identity | `mii.id` (the creation ID) | The key is its 16 hex characters. Slot and name are not stable |
| Name, color | `mii_get_name`, `mii.fav_color` (0-11, `mii_color_name`) | The coin's color is the favorite color, as HBC's picker draws it |
| Coin faces | `mii_res_load`, `mii_model_build` (32-64 px face, normal expression only), `mii_icon`, `mii_model_free` | Needs `sd:/mii/RFL_Res.dat`, which the user saves with HBC's HOME > HBC > Save Mii faces (Nintendo's file: never shipped, never copied by us). Without it a coin shows the name's initial on the favorite color |

Rules the SDK sets that touch WiiStation:

- `mii_icon` draws in the EFB's corner between frames: call it after the menu's last copy and
  before the next frame, then set the menu's GX state again (pixel format, viewport, scissor,
  projection, vertex format, TEV). The menu mixes raw GX and OpenGX, so the icons are made in
  one place (`Gamecube/menu/mii_coins.c`, new) right after `Gui::draw()`'s copy.
- Icons are textures kept in MEM2 (64x64 RGB5A3 = 8 KB, 100 Miis = 800 KB) and made lazily, the
  visible page first. The models are freed as soon as the icon exists; nothing of the SDK stays
  in memory once a game starts (MEM1 is what chains run out of).
- The GX FIFO should stay in MEM1 (SDK README: a FIFO in MEM2 stalled at random on a real
  Wii). WiiStation's is `memalign`ed at start-up, before anything could push the heap into MEM2;
  making it a static array is a one-line safety if that ever changes.
- Dolphin ships no `RFL_DB.dat` or `RFL_Res.dat`. Tests stage a database made by
  `mii_random` + `mii_db_add` on the PC side (`-DMII_NO_OGC`), into the run profile's NAND
  (`Wii/shared2/menu/FaceLib/`); without faces the coins show initials, which is a test case too.

### Owner's decisions (2026-10-28)

1. **A Players menu** (not a prompt at game load): a grid of Mii coins with pages, and the PS1
   pad ports along the bottom.
   - Wii Remote: point at a coin, hold **A+B** to grab it, drop it on a port.
   - Any other pad (Classic, GameCube, Wii U Pro, USB): **A** on a coin picks it up and the focus
     jumps down to the ports; Left/Right chooses a port, **A** drops it there, **B** puts it back.
   - Mock-ups first, to get the menu right (below).
2. **Every seated Mii earns.** An unlock goes to each Mii seated on a port whose pad is
   connected, not only player 1's.
3. **Existing unlocks stay with Guest.** Journal lines written before this feature have no
   player key and belong to Guest.
4. **Official mode per Mii: later,** and only with a design that is elegant. Open questions are at
   the end.

### Players design

- **Seats**: `wiistation/players.ini` (5.18.2; `[Players]` in settings.ini before), one line per
  seat (`Player1=`, `Player2=`, multitap `Player1A=` ... `Player2D=`, Co-Op `Player1P1=` ...
  `Player2P8=`), each a Mii key or empty for Guest; the menu writes it when it closes.
  `PlayersRecent=` keeps the last 15 seated, most recent first: the menu lists them first. A Mii can sit on one port only;
  dropping it on another moves it, dropping it on a taken port swaps the two. Seats are kept
  across sessions. A key no longer on the console reads as Guest, with a notice; its unlocks
  stay in the journal (nothing is deleted).
- **Who earns**: at an unlock, every port that has a Mii and a connected pad (the same test the
  Co-Op pad mapping uses). Guest earns when no seated Mii does (a game with only Guest pads still
  unlocks, as today). One journal line per Mii that earned it.
- **Journal**: one file, as now, with the player key as a new column:
  `<set digest> <id> <player key> <title>`. A line without a key (every journal written so far)
  is Guest's. Each achievement is earned once per Mii. The 1 MB cap and the fail-closed rules
  of `achievements.c` stay.
- **"Earned" in the game**: the game's achievement state is the union of the seated Miis'
  unlocks, so an achievement one Mii has keeps firing for the others who do not have it yet.
  rcheevos sees an achievement as active while any seated Mii lacks it; when it triggers, only
  the Miis without it get the line and the toast names them.
- **Save states**: the achievement section (Local: `WAC1`, `WAO1` is Official's) records the seat keys, after rcheevos' progress (5.19.0). Loading a state made by
  other players keeps the game state, but earned flags come from the journals of the Miis seated
  now, so a state never moves one Mii's unlocks to another.
- **Toast and Achievements page**: "Kat unlocked ..." (two or more: "Kat and Ollie
  unlocked ...", four or more: "4 players unlocked ..."); the page shows one seated Mii at a time
  with L/R to switch, its coin in the header.
- **Privacy**: Mii names are personal. Logs, perf.log and crash reports print only the first 8
  hex characters of a key. The creator name is never read out of `mii`.

### The Players menu (layout D, chosen 2026-10-28)

Mock-ups A-E were shown in the chat; the owner chose **D**, with subtle PS1 port theming.

- **Ports first**, on a band the gray of the PS1's plastic: one card per port (two, or eight
  in two rows with multitap). A card reads like the console's label: the port number large,
  "CONTROLLER" small, the pad connected there, the seated Mii's coin in a round recess, its
  name and, with a game loaded that has a set, "4 / 31 achievements" (that Mii's unlocks in
  this set). A small PS1 controller socket sits in the card's corner; its pins light when a
  Mii is seated on a port with a pad. A port without a pad is a shade darker.
- **The coins** below: a strip of 16 a page (8 x 2), paged with + / - (no arrows at the top).
  A coin is the Mii's head on a disc in its favorite color; **a favorite Mii's coin has a
  gold rim**. Without the face models a coin shows the name's initial. A seated coin is dimmed
  and wears its port in a tag under it. The focused coin's name and progress show on one line under the strip.
- **Input**: Wii Remote pointer, hold A+B on a coin, drop it on a card (on a taken card the
  two swap; back on the strip unseats). A pad: A on a coin picks it up and the focus goes to
  the cards (arrows choose), A seats it, B puts it back; on a card A picks its Mii up, B
  makes the seat Guest. Up from the strip's top row goes to the cards.
- No plugs: a plug-and-cable mock-up was fun but too silly.
- **Co-Op** (5.18.1, the owner's call): a Co-Op port's card is a group, a coin well per
  player and a "..." button. A, a click or a dropped coin opens the same screen with a card
  per Co-Op player (seats `Player1P1` ... `Player2P8`, virtual controllers 10-25); B leaves.
- Details the owner asked for (5.18.1): round coin slots ("Guest" centered in an empty one),
  a solid focus ring, "Port 1A" in a tag under a seated coin, the socket centered on the two
  big cards and in the corner on the small ones, names shrunk to fit, the coins' page
  ("Miis 1 / 2") at the bottom right. US spelling everywhere.

### Phases and checks (players)

| Phase | Work | Check |
| --- | --- | --- |
| 0 | Vendor `deps/mii`; a read-only probe on the **bench** Wii: `mii_db_load`, print the count and key prefixes (no names) to perf.log, time the load | One bench job; never on a protected Wii |
| 1 | Host side: a fixture maker (`scripts/mii_fixture` from `mii.c` with `-DMII_NO_OGC`: fictional Miis, real CRC) and the seat/journal logic in plain C with a host test (`tests/host/hostcheck.h`): old lines = Guest, two Miis, a swap, a missing Mii, the cap | Host test |
| 2 | The Players menu: coins (initials first, then `mii_icon` faces), pages, both input paths, seats in settings.ini | `wsx.sh menu` pictures in Dolphin with the fixture; bench Wii with its own Miis and saved faces; capture card for the TV picture |
| 3 | Journal column, union of earned, per-Mii toast and page, WAO1 seat keys | Dolphin chain: Local set, Mii A unlocks, seat B (not earned), A + B seated (only B gets it), a state from A loaded with B seated |

### Official mode per Mii: the plan (2026-10-28; mock-ups shown in the session)

The owner's answers: accounts are files, linked to a Mii on the Wii; port 1's Mii plays Official
unless a check box moves it; linking opens the file browser, showing RetroAchievements accounts. Revised the same day: an Official
on/off button instead of a check box, and an Edit slot that opens a Mii's player settings.

- **Accounts**: one file per RetroAchievements account in `sd:/wiistation/achievements/ra/`,
  `<name>.ini` with `User=` and `Password=` or `Token=`, exactly like today's `ra_account.ini`
  (after the first login only `User=` and `Token=` stay). `ra_setup.py --account FILE --ra-name NAME`
  puts it there; the script still never opens the file. The name is the file's, so the Wii shows
  a name without reading the file. Today's `ra_account.ini` stays, shown as "This Wii's account".
- **Edit slot** (owner, 2026-10-28): a round "Edit" slot at the end of the coin strip. Drop a
  coin on it (A+B drag, or A then choose Edit with a pad) and the Mii's player settings open:
  RetroAchievements account, Plays Official (On/Off), and its unlock counts (read only). B goes
  back to Players. Later Mii settings go on this page too.
- **Link**: the account row of the Edit page opens the file browser on `achievements/ra/`, titled "Account for <Mii>",
  listing the accounts by name, plus "None: earn Local" first and "This Wii's account" last.
  A links, B goes back. players.ini keeps the links in an `[Accounts]` section, one
  `<Mii key> = "<name>"` line per linked Mii (written by the same code as the seats).
- **Who plays Official**: one Mii per game (RetroAchievements sessions are one user). By default
  port 1's Mii (1A with a multitap, P1 with Co-Op). A linked Mii's card shows a small button
  labeled "Official" (no check boxes in WiiStation, and no On/Off text: the button's highlight shows
  it is on; the owner's calls). Every linked Mii's button can be pressed; pressing one turns it on
  and the others off, so exactly one is on (`PlayersOfficial = "<Mii key>"`, empty = port 1). Every
  other seated Mii earns Local (phase 3). No linked Mii on that seat: today's `ra_account.ini`
  if there is one, else Local only. A change applies at the next game load.
- **Seen**: a linked Mii's coin wears a small "RA" tag; the toast says which side an unlock went
  to ("RetroAchievements" over the Official Mii's line, "Local" over the others').
- **Failures**: a link to a missing file, or a failed login, makes that Mii earn Local for the
  game, with the reason on the Achievements page (as today's login errors). Nothing retries a
  password by itself. (Built 5.19.0, a decision where the plan was open: a Local set needs the
  set's definitions, and those only come from a session. So this Wii's `ra_account.ini`, when
  there is one, loads the set in spectator mode, nothing sent to it, and every seated Mii, that
  one too, earns Local. With no `ra_account.ini` there is no set to earn: the page says why.)
- **Never**: a token or password on screen, in a log or in players.ini; the browser lists file
  names only.

| Step | Work | Check |
| --- | --- | --- |
| 4a | `ra/` accounts read by name; login with a chosen file; `ra_setup.py --ra-name` | **Built 5.19.0, hardware check open**: `tests/ach_account_test.c` (link to path, bad names, the digest and journal names); Dolphin: a Mii linked to a name with no file (`scripts/chains/ach_official_acct.txt`) says so in the log and on the page; bench Wii: `ra_setup.py --ra-name dev` put `ra/dev.ini` (job 20261028-204440-0db05c); `ach_official_ra.txt` (job 20261028-205349-f2c8c7) read it for a linked seat and tried the login, but that DOL had no HTTPS stack, so the login itself is not yet proven on hardware: rerun with `scripts/bench_ra_mii.py` |
| 4b | The Edit slot and page, the account browser, `[Accounts]` and `PlayersOfficial` in players.ini, RA tag, Official button | **Done 5.19.0**: `wsx.sh menu ... --sd tests/fixtures/players_accounts` walks (link a file, link This Wii's account, Official moved to port 2's Mii; players.ini read back) |
| 4c | Official session for the chosen Mii, Local for the others; toasts | **Built 5.19.0, hardware check open**: `tests/achievements_ra_test.c` (who gets what, the journal, restore, the page, a failed account, the cap); Dolphin: `ach_players.txt` unchanged (Local mode), `ach_official_acct.txt`; hardware: the same bench job, not yet with a login, so no Local player has earned on a real set. The design, the rc_client facts it rests on and what was not tested: `Docs/ACHIEVEMENTS.md`, "Official mode with Local players" |

## Planned

Decisions of the project owner, 2026-10-23.

### Modes

| Mode | Definitions | Unlocks |
|---|---|---|
| Test | A definition file, to try it out | Not kept. The journal is not read or written. |
| Custom | The user's own definition file | Kept in the journal. Custom mode never sends an unlock to RetroAchievements. |
| Local | The RetroAchievements set of the game, downloaded once and saved | Kept in the Local journal of the game. Local mode never sends anything to RetroAchievements. |
| Official | RetroAchievements sets | Casual only. Hardcore needs RetroAchievements to approve WiiStation as a client. |

The mode switch is in the product (setting `Achievements`, 775ae73a). Test and Custom work, and since 5.19.0 Local.
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

- The account file is a Mii's ([Players and Miis](#players-and-miis), step 4a): the Mii that plays Official
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

One RetroAchievements session is one user, so one Mii plays Official ([Players and Miis](#players-and-miis), "Official
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

### Local mode

The owner's request (2026-10-29): Local uses the official achievements, but only locally, and
another mode uses custom achievements (Custom, the former Local). Setting `Achievements = 4`.

| Part | What happens |
|---|---|
| Who earns | Every seated Mii on a connected pad, or Guest when there is none (`achievements_ra_plan_all`). Nobody is the Official Mii: there is no account side. The journal is Official mode's Local side: `achievements/ra_local/<game id>.unlocks`, lines `<digest> <id> <player key> <title>`, the digest made from the game id. So a Mii that earned Spyro's set as a Local Mii of Official mode keeps it in Local mode, and the other way round. Guest's lines have no key. |
| Rules | Casual; Hardcore is not implemented. `AchievementUnofficial` applies. Encore is on (nothing is the account's). The toast has no side tag (all earn Local) and names the players as in Custom mode. |
| Never sent | Three locks. (1) rc_client is put in spectator mode before the game loads: it makes no session start, no ping (so no rich presence on the profile), no award, no leaderboard entry. (2) `server_call` (`achievements_official.c`) refuses every request that is not the login, the game id or the set (`ach_req_kind`, `ach_local_may_send` in `Gamecube/ach_ra_cache.h`) with an answer made on the console (`ra_transport_complete`): a refusal shows in the log as "a request of kind X is refused". (3) The test is the log: only "a login download goes to the server", "a gameid ...", "a set ..." lines can appear, or none (a game load served from the card). Leaderboards and rich presence are Official only (the book shows no Leaderboards tab in Local mode). |
| The download | The patch API needs a login. The account is any one: the Official Mii's linked file if it is there, else `achievements/ra_account.ini`, else the first `*.ini` in `achievements/ra/`. A password login stores the token as in Official mode. Nothing else about the account is used: no unlock list is fetched (spectator mode). |
| The card's copy | `sd:/wiistation/achievements/ra_cache/`: `gameid-<hash>.json` (the answer to a disc's hash, only when the game is known) and `sets-<hash>.json` (the set), each written only after a successful answer, through a temp file and a rename; never the login (it holds the token), never a request. A game load whose set file is on the card and younger than 30 days (`ACH_CACHE_REFRESH_DAYS`; a wrong clock counts as young) makes no request at all: the login is made up on the console (`ach_synth_login`: the user "Local", the token "offline", never stored), so Local also works with no account and no network. An older copy is asked for again; if the server cannot be reached (no answer or a 5xx) or the login fails, the old copy is used. Badges have their own cache (`achievements/badges/`, as before): a badge not yet cached shows as the title's letter until it can be downloaded. |
| No network, nothing saved | The status line says "No network, no saved set" and a black banner on the game card says "Local: no network, no set / Go online once to save it". With a network but no account: "Local: no account / Link one in Players". A disc the server does not know says "No set for this disc" as in Official mode. |
| Seats change | `achievements_players_changed()` (the game goes on after the menu) builds the Local side again for the new players (`achievements_ra_replan`, `achievements_official_players_changed`); the old players' unlocks are written first. Official mode with Local players still plans at the game load only. |
| Disc swap | rc_client's check as in Official mode. In an offline load the second disc of a game is known only when its hash was downloaded and saved before (a swap while online saves it). |
| Save states | Official's `WAO1` (rc_client's progress); the unlocks come from the journal, as in Official mode with Local players. |
| Not here | Hardcore, leaderboards, rich presence, the account's own unlocks. |

Checks (2026-10-29, `try/modes`): host tests `tests/achievements_local_test.c` and `tests/ach_ra_cache_test.c`
(`scripts/test-achievements.sh`); Dolphin `scripts/chains/ach_local.txt` with the made-up set of
`scripts/ra_cache_fixture.py` on the card (`--card tests/fixtures/ach_local/achievements`): runs `modes_local_nocache`
(no saved set, no network: "no network, none saved"), `modes_local_card` (Guest earns Alpha and Beta, comes back
with 2 earned, two Miis earn their own, no request on the wire) and `modes_local_book` (the book: coin, 2 / 3,
rarity from the set); bench Wii `scripts/bench_ra_local.py --fixture`, job 20261029-144405-854165 (the same on
hardware, the DOL without the HTTPS stack). **Not yet run:** a real download with an account (needs a DOL built
with `build_ogc3/deps/ra_https` and `ra/<name>.ini` on the card): `bench_ra_local.py NAME` without `--fixture`;
the refusal of a non-download request is host-tested (names) but no real rc_client request was refused, because
spectator mode makes none.

Decisions, 2026-10-29: Test stays its own mode (it keeps nothing and reads no journal, a different
promise from Custom; folding it in would need a "keep nothing" switch on Custom for no gain).
The old numbers keep their meaning, Local is 4. The menu cycles in the owner's order (Off, Local,
Official, Custom, Test) through a small map in the cycle of `OptionsFrame.cpp` (`optCycle`).

### Multi-disc games

- Official mode keeps the set through a disc swap (2026-10-23). `loadISOSwap` calls
  `achievements_change_media` once the new disc is open: the disc is hashed then, from the
  menu, and `rc_client_begin_change_media` checks it. Until rc_client answers, nothing is
  evaluated (`rc_client_idle` only). The same game's disc goes on. A disc of another game, or
  one RetroAchievements does not know, unloads the set until the next game load (the pinned
  rc_client's `rc_client_find_game_hash` gives the disc's game).
- A swap before the login is done needs no check: the set then loads from the new disc.
- Test: `scripts/chains/ach_discswap.txt` (the `discswap` script line).
- Custom and Test mode keep the set through a swap to a disc its `Game=` lines name
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
| HTTPS stack (Mbed TLS 3.6.4 + curl 8.16.0) | Proven on the bench Wii on 2026-10-11. Built for libogc2 and devkitPPC r41-2. It must be rebuilt for libogc v3, the default since WiiStation 5.13.0. **Status (2026-11-03):** rebuilt for libogc v3 (c03f43e6, c6847e35) and in the product since 5.17.0 (dc3674fc). |
| Strong TLS entropy | The proven probe uses a public dummy NV seed. A secret seed for each Wii needs confidential provisioning. See [TLS entropy](ACHIEVEMENTS_RESEARCH.md#tls-entropy). |
| Correct UTC | Needs a reviewed UTC source. See [UTC clock](ACHIEVEMENTS_RESEARCH.md#utc-clock). |
| `rc_client` connected to the emulator | **Done** (Status 2026-11-03). `rc_client` in the Wii build (825df461), HTTPS and login on the bench Wii (54dfd3f4, 2ef5cce3), the PS1 hash through WiiStation's own disc reader in every format incl. CHD and PBP (efe265b4), disc swaps (2b7b6c73); in 5.17.0 (dc3674fc). The frame and idle pump and the nonblocking transport (worker thread, `ra_transport.c`) are in the build; a separate proof of each is unverified. |

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
- **Status (2026-11-03), replaces the paragraph above, which is the 2026-10-23 text:**
  Phases 0 and 1 are done: the hash adapter is in (efe265b4, 825df461). Phase 2 (Casual client:
  login, set load, events, game list, popups) and phase 3 (progress indicators, badges, the
  state section, multi-disc) are in the product since 5.17.0 (dc3674fc, ef64c4bd, 34af5057,
  2b7b6c73). Reconnect and offline policy: unverified. Phase 4 (Hardcore) is **not
  implemented**: it needs RetroAchievements' approval of WiiStation as a client. Phase 5
  (Local mode) is built in 5.19.0 (8f06d2d5, merged 07cd4797; the book 8e9a4638; per-Mii
  932fad3b and 6c4c17f7). Hardware proof of an Official login with a Mii's own account is open
  ([Players and Miis](#players-and-miis), step 4a).
- Do not add these during phases 0 to 3: a multi-provider layer, an authoring toolkit, a new
  backend server, a libretro rewrite or a mandatory hardware adapter.
- If native HTTPS fails, write a measured comparison of the ESP32 adapter and a companion
  transport, and choose one. Never fall back silently to unencrypted credentials.
