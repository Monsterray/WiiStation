# Achievements

This is the reference for the WiiStation achievement engine. It tells what the engine does
today, how to set it up, how to test it and what is planned.

- What we learned from outside sources (rcheevos, RetroAchievements rules, PS1 hashing, TLS,
  DuckStation, other systems): [ACHIEVEMENTS_RESEARCH.md](ACHIEVEMENTS_RESEARCH.md).
- Test runs, queue job IDs, measured numbers and open work:
  [ACHIEVEMENTS_NOTES.md](ACHIEVEMENTS_NOTES.md).

## Status

| Part | Status on 2026-10-23 |
|---|---|
| Local evaluation of a definition file | In the product. Off by default. Merged into main from `codex/achievement-engine` (c29cafd9, 2026-10-12). |
| Modes Off, Local, Test, Official | In the product (775ae73a, 2026-10-23). Official loads nothing yet. |
| Unlock journal | In the product. An unlock is written when it triggers (f9ca407b). Records cut short by a power loss are ignored (2026-10-23). |
| Save-state progress (`ACH1` section) | In the product. |
| Menu controls | None in main yet. The settings file sets the mode. A General-tab button and a Current ISO list are in progress on `try/achievement-ui`. |
| RetroAchievements login, awards, leaderboards, Rich Presence, Hardcore | Not implemented. |
| `rc_client`, API and PS1 hash code | Vendored in `deps/rcheevos` and tested in standalone host and Wii programs. Not connected to the emulator. |
| HTTPS (Mbed TLS 3.6.4 + curl 8.16.0) | Test probes only. Proven on the bench Wii on 2026-10-11 with libogc2 and devkitPPC r41-2. Not in the product. |

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
| `Achievements` | 0 = Off, 1 = Local, 2 = Test, 3 = Official | 0 | Local evaluates the set and keeps unlocks in the journal. Test evaluates the set, but does not read or write the journal. Official loads nothing yet. Off stops all frame evaluation. A value 1 from before the modes is Local. |
| `AchievementFile` | `"path"` | empty | The definition file. Its `Game=` line must match the loaded disc. |
| `AchievementLog` | `"path"` | empty | The unlock journal. Empty uses the `AchievementFile` path with `.unlocks` added. The journal path must not be the definition path. |
| `AchievementBenchRoot` | `"path"` | empty | Bench builds only (`ACHIEVEMENTS_BENCH`). It sends card reads, writes and deletes, and chain cleanup, to a fresh private directory. Product builds do not read it. |

- Load the game again after you change a key, the file or the set.
- Make the parent folder yourself. The engine does not make it.
- The settings Save writes `Achievements`, `AchievementFile` and `AchievementLog`. No menu
  page shows them. The settings table gives `Achievements` a label and a help line, but no
  menu row uses it.
- There is no password key and no account key.
- The key `AchievementUTCOffsetMinutes` is removed. The UTC clock is test-only. No product code
  reads or links it. See [UTC clock](ACHIEVEMENTS_RESEARCH.md#utc-clock).

## Definition file

```text
# Synthetic fixture only; these addresses are not achievements for a real game.
Game=TEST12345
1|Three consecutive hits|0xH000010=1.3.
2|Little-endian 16-bit word|0x 000020=4660
3|Scratchpad byte|0xH200001=9
```

1. The first line that is not a comment must be `Game=<CdromId>`. The ID must match the loaded
   disc. If it does not match, the engine rejects the set.
2. Each line after it is `id|title|condition`. The condition uses the upstream rcheevos format.
3. The engine ignores blank lines and lines that start with `#` or `;`.

| Limit | Value |
|---|---|
| File size | 64 KiB. The file must not contain a NUL byte. |
| Achievements | 128 |
| ID | Decimal, 1 to 4294967295, unique in the file |
| Title | 79 bytes |
| Condition | 2048 bytes |

- One bad line rejects the whole set: a bad ID, a duplicate ID, a bad condition, an empty
  title or condition, or a value past a limit. An edited file thus cannot silently change scope.
- A set with no achievements is rejected.
- The IDs are local IDs. They are not official service awards.
- The `Game=` serial is a local safeguard. It is not the RetroAchievements content hash. See
  [PS1 hashing](ACHIEVEMENTS_RESEARCH.md#ps1-hashing).
- A condition that is true at the start does not award at once. The runtime stays in its
  WAITING state until the condition becomes false.
- Conditions on little-endian values, 32-bit values with the high bit set and IEEE floats work.

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
| Disc swap | `loadISOSwap` calls `achievements_close()` | Closes the set until the next full game load |
| Shutdown | `r3000a.c` calls `achievements_close()` | Writes pending unlocks and destroys the runtime |

- The frame hook does no allocation and no network calls. Its only file write is the journal
  append when an achievement triggers. That is rare: one short line.
- A disc swap closes the set because the engine cannot associate two discs of one game. See
  [Multi-disc games](#multi-disc-games).

## Unlock journal

Each line is `<definition MD5> <id> <title>`. The MD5 is the digest of the exact definition
file.

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
| Header, 36 bytes | `WAC1` version marker, 16-byte definition MD5, 128 earned bits |
| Body | Upstream sized runtime progress (`rc_runtime_serialize_progress_sized`) |
| Size limit | 256 KiB (`ACHIEVEMENTS_STATE_MAX`) |

- A state for a different set, an old state, a state with no section or a corrupt section
  resets the runtime progress.
- Loading an older state never revokes an earned local achievement.

## Source files

| File | Content |
|---|---|
| `achievements.c`, `achievements.h` | The engine. All calls belong to the emulator thread. |
| `deps/rcheevos/` | Pinned rcheevos `include/`, `src/runtime/`, `src/util/`, `src/client/`, `src/api/`, `src/hash/`. The emulator compiles only the runtime and util sources. |
| `Gamecube/achievements_bench.cpp` | Bench-only hooks (`ACHIEVEMENTS_BENCH`): stop-boundary counts, reset and disc-swap checks, card isolation, return to HBC after a chain. Product builds omit it. |
| `misc.c` | The `ACH1` state section |
| `tests/achievement_seed.c`, `tests/achievement_clock.c` | Test-only TLS seed storage and UTC clock. Not in the product. |

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
| `scripts/settings_check.py` | Settings consistency. Use `C:/Python312/python.exe` on this machine. |

### Wii tests

All Wii runs go through the bench queue (`C:/tools/wii-bench/wiibench.py`). Read
[bench testing](BENCH_TESTING.md) for the queue workflow. Do not start a hardware run directly.
Do not make lease environment variables yourself. Each run freezes its DOL, matching ELF,
collector and HBC client, with SHA-256 hashes, and keeps raw evidence under `.runs/bench/`.

The standalone Wii test builders (`build-achievements-wii-test.sh`,
`build-achievements-client-test.sh`, `build-achievements-hash-wii-test.sh`,
`build-achievements-https-test.sh`, `build-achievements-native-https-test.sh`) require
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
Official logs "not available yet" and loads nothing until the items below are done.

### Official login

- The account file is `sd:/wiistation/achievements/ra_account.ini`, with `User=` and
  `Password=`.
- WiiStation logs in once, writes `Token=` to the file and deletes the `Password=` line.
  (The research advised to keep a token only when the user opts in. This decision replaces
  that advice.)
- The password never goes in `settings.ini`. The password never goes in a log.
- Logout deletes the stored token. An account change keeps caches, pending awards and progress
  apart for each account.
- Do not call the token file encrypted. An SD card is readable storage. A key next to the file
  does not protect it.
- Keep the token out of frozen manifests, state metadata, crash collection and exported
  configurations. Redact the password, the token, the Web API key, POST bodies and
  authenticated URLs from all logs. Log the operation, the status and the time instead.

### Multi-disc games

Today a disc swap closes the set, and a definition file names one disc serial.

- Official mode must keep the session through a disc swap. Use rc_client's media-change API
  (`rc_client_begin_identify_and_change_media`, or `rc_client_begin_change_media` with a hash).
  Hash the new disc first. Then let rc_client decide if the disc belongs to the loaded game.
  Stop awards for an unknown or unrelated disc until identification is done.
- Local mode needs a set that names every disc of a game.

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
