# Achievements: evidence and open work

This file is the evidence log. It gives the test runs, their queue job IDs and dates, the
measured numbers, and the work that remains. The engine reference is
[ACHIEVEMENTS.md](ACHIEVEMENTS.md). Outside research is in
[ACHIEVEMENTS_RESEARCH.md](ACHIEVEMENTS_RESEARCH.md).

## Rules for every run

- All Wii runs used the shared bench Wii through the HBC-Reborn queue lease. See
  [bench testing](BENCH_TESTING.md). Host research, hash and mock tests need no lease.
- Each run froze its DOL, matching ELF, collector, HBC SDK client and payload, with SHA-256
  hashes in the bundle manifest. Raw evidence is under the ignored `.runs/bench/` directory.
- Each test wrapper used a new UUID directory on SD, returned to HBC and deleted only that
  directory. The collector compared crash records.
- Collectors back up the exact bytes of each settings or log file that they replace, restore
  them and read them back.
- No personal memory card and no original library image was touched. Test definitions were
  local. No run sent anything to a service.
- The future live-test user name is Monsterray. No credentials are staged.
- Stage only dummy secrets for fixtures. Real credentials need separate provisioning for an
  authorized run, and normal evidence collection must exclude them.

## Evidence by gate

| Gate | Evidence | Result |
|---|---|---|
| Local engine, host | `scripts/test-achievements.sh` | ASan/UBSan pass: memory, conditions, state identity, journals, malformed sets |
| Local engine, Wii | Jobs `20261010-174237-f70aa9`, `20261010-174517-b2d6c9`, `20261010-220131-c89a02` | Pass. See [Engine suite on the Wii](#engine-suite-on-the-wii). |
| Emulator matrix, Wii | Job `20261010-221302-bd65ed`, recovery `20261010-221913-de7a77` | 13 cases and 3 restart cases pass. See [Emulator matrix](#emulator-matrix). |
| Upstream runtime, API and client, host | `scripts/test-achievements-client.sh` | 2,397/2,397 pass with ASan. Upstream hash tests off. |
| Client mocks on big-endian, Wii | Job `20261011-014231-081451` | 180/180 client tests pass. Clean HBC return. |
| PS1 hash parity, Wii | Job `20261011-014646-ea19fc` | Three real raw BIN images match the host hashes exactly |
| Public HTTPS with the port, Wii | Job `20261011-013921-a0f079` | Valid certificates accepted. Wrong-host, untrusted-root and expired certificates rejected. |
| NV seed contract, host | `scripts/test-achievements-entropy.sh` | ASan/UBSan pass with a public dummy seed and mock storage. Not Wii security readiness. |
| File-backed NV seed, Wii and host | Job `20261011-025257-be84db` | Zero failures. See [TLS seed and UTC](#tls-seed-and-utc). |
| Source-built NV HTTPS, Wii | Job `20261011-032017-49eea1` | All five certificate cases and seed cleanup pass in 31 seconds. Public dummy seeds only. |
| UTC callback and certificate dates, Wii and host | Job `20261011-040024-514221` | Zero clock failures. All five HTTPS cases pass. Clean HBC return in 37 seconds. |
| Spyro capacity campaign, Wii | Five pairs, see [the campaign](#accepted-spyro-capacity-campaign) | 2.84% mean overhead with 128 conditions |
| Release smoke, Wii | Jobs `20261010-220901-deb05a` (30 seconds), `20261011-015705-283c84` (38 seconds) | Menu, SDK safety, HOME overlay and resume, HBC return |
| Release build | `.runs/achievement-build/production-release-build.log`, `.runs/achievement-build/utc-release.log` | Pinned r41-2 build passes |
| Collectors, host | `tests/bench_gameplay_test.py`, `tests/gameplay_results_test.py` and the other bench tests | Completion, bad paths, memory telemetry, saturation, retries, frozen identity and checkpoints, replay failure gates pass |

No login, presence ping, award, leaderboard submission or progress reset occurred. vWii,
commercial games other than the Spyro capacity pilot, other storage devices and online
services are not tested.

## Build checks, 2026-10-10

- Host checks passed.
- The engine and 18 vendored runtime and util sources cross-compiled with
  `-Wall -Wextra -Werror`.
- `psxcounters.c`, `r3000a.c` and `misc.c` cross-compiled against the installed libogc2
  headers.
- A missing `wupc/wupc.h` came from an incomplete or incompatible system library install.
- With the bundled libraries and the pinned r41-2 macOS compiler, the full release DOL build
  passed. It also passed from a checkout path with spaces.
- The local setup script, the library preflight regression test, the host checks and the
  settings check passed.
- Build logs are in the ignored `.runs/achievement-build/`. Outputs were
  `Gamecube/WiiSXRX_Release.dol` and `.elf`. README has the isolated setup instructions.

## Engine suite on the Wii

`tests/achievements_wii.c` runs the same engine and rcheevos tests as the host suite, with
Wii video, FAT and the HBC SDK crash handler. It covers logical RAM and scratchpad bounds,
single-byte and little-endian word conditions, hit counts, pause and reset, state progress
recovery, monotonic earned bits, corrupt, missing and wrong-set progress, journal reopen,
definition identity, disabled mode, duplicate IDs, bad conditions, size limits, IEEE float
conditions, and a failed journal open followed by a good retry.

### Big-endian bug

| Job | Date | Result |
|---|---|---|
| `20261010-173704-da93f2` | 2026-10-10 | Fail at the three-hit assertion |
| `20261010-173948-d44aef` | 2026-10-10 | Fail on a one-byte condition, without state save or load. HBC kept both assertion reports. The suite returned to the channel. |
| `20261010-174237-f70aa9` | 2026-10-10 | Full suite pass after the fix |
| `20261010-174517-b2d6c9` | 2026-10-10 | Pass with the journal-write retry tests added (24 seconds) |
| `20261010-220131-c89a02` | 2026-10-10 | Final suite pass with high-bit 32-bit and IEEE float conditions (30 seconds) |

Cause: the pinned `rc_read_memory` put guest bytes in big-endian order on a big-endian host,
then applied the operand-size mask. An 8-bit value of 1 became `0x01000000`, and the mask made
it zero. Wider values were wrong too. The local patch puts the bytes in little-endian order with
unsigned shifts. Its record is in `deps/rcheevos/README.wiistation.md`.

### Evaluation cost (microbenchmark)

128 definitions with conditions that stay false. 1,000 batches of 100 evaluations. Each
condition reads one byte.

| Per-frame runtime cost | Time |
|---|---:|
| Median | 51.29 µs |
| p95 | 51.82 µs |
| p99 | 51.82 µs |

These are batch-amortized runtime numbers. They are not emulator throughput or a guarantee for
real games. More complex definitions can cost more.

## Emulator matrix

`scripts/achievement_fixture.py` makes an original PS1 disc: an assembled MIPS program and a
minimal ISO file system. It contains no BIOS, commercial game, outside PS1 SDK or compiler. The
guest polls VBlank, increments a RAM counter, and writes a little-endian 32-bit word and a
scratchpad byte. Seven synthetic serials keep the runtime and persistence cases apart.

`scripts/bench_achievement_games.py` stages the fixture and reserved per-game settings under
the lease. Global settings do not change. Autosave is off. The fixture does not touch memory
cards. Per-game settings clear inherited SMB credentials before they write state metadata.
Cleanup removes only the staged fixture settings, the generated logs and the private run
directory.

The 13 cases:

- Lightrec, interpreter and the old PPC dynarec, each with a disabled, enabled and disabled
  timing run.
- State runs: save a state before the third achievement, load it after that achievement is
  earned, and save again. The `ACH1` section must keep the two earlier earned bits before the
  load and all three after it. Each journal must have exactly one record per ID.
- A last run with frameskip on.

Every case must run exactly 240 emulated frames with the expected loaded and earned counts.
Loading frame 119 at frame 150 rolls the frame counter back by 31. The state tool moves the
chain stop to match: those runs end at counter 209, and the independent profiler proves that
240 frames ran.

A second DOL boot runs only five frames per core, before the guest conditions become true. All
three achievements must come back from the SD journals with no new records.

### PPC dynarec stop bug

The first matrix, job `20261010-174904-67a4b4`, ran the six Lightrec and interpreter cases.
Then it timed out in the PPC dynarec case with achievements off. Its heartbeat showed more than
773,000 emulated frames after a 240-frame stop request. Compiled jump and branch blocks chained
to each other and never returned to the outer loop that checks `stop`. Both chaining paths now
check `stop` after `psxBranchTest` and return when it is set. Recovery job
`20261010-220105-cfca5d` collected the failed run and removed its staged files under a new
lease.

### Runs

| Job | Result |
|---|---|
| `20261010-220852-5de4b6` | First corrected-core run. All 13 cases ran, but the collector rejected the expected frame rollback. Its state sections and journals passed the corrected judge. |
| `20261010-221302-bd65ed` | Final frozen matrix. All 13 cases and the three five-frame restart cases ran. The collector missed the short agent status window of the second boot and kept waiting after the Wii was back in HBC. We stopped the collector process. |
| `20261010-221913-de7a77` | Recovery, 25 seconds, pass. It checked all matrix evidence, the three restart markers, the restored earned counts, unchanged journal bytes and no new crash. It restored staged files and left the Wii in HBC. |
| `20261010-220901-deb05a` | Corrected release build: menu, SDK safety, HOME overlay and resume, HBC return (30 seconds). Evidence: `.runs/bench/20261010-release-stop-fixed-smoke/`. |

With the final engine suite, these runs cover all implemented local behaviour that needs
Wii validation.

The collector now accepts changed, complete restart logs when it misses the short agent
window, with a bounded restart timeout. Evidence: 
`.runs/bench/20261010-achievements-integration-final/results/`. Frozen inputs did not change.

### Timing samples

| Core | Disabled before | Enabled | Disabled after | Change against control mean |
|---|---:|---:|---:|---:|
| Lightrec | 6.158 s | 6.252 s | 6.126 s | +1.79% |
| Interpreter | 10.762 s | 11.108 s | 10.980 s | +2.18% |
| PPC dynarec | 3.779 s | 3.918 s | 3.852 s | +2.70% |

Each sample ran 240 frames with three simple definitions, a synthetic polling loop and a
profiler build. These single A/B/A samples include run-to-run noise. They are not statistical
bounds, and they do not replace real scenes, vWii or complex sets.

## Client, hash and HTTPS probes, 2026-10-11

### PS1 hash parity

| Image | Host and Wii PS1 hash |
|---|---|
| Spyro the Dragon, SCUS94228 | `3e9eb765a7b2912e60c30f363f0a3574` |
| Crash Bandicoot 3, SCUS94244 | `8209150e48e8f9f0e727347472c7e123` |
| Final Fantasy VII disc 1, SCUS94163 | `665933b4dbbdfa04b328a2deccea2a30` |
| Synthetic fixture, raw MODE2/2352 CUE and cooked ISO | `f4cbe17fd29e842da2c299ea52508443` (both) |

- The Crash 3 and FF7 library CUE files name stale BIN file names. Their tests read the BIN files
  directly and did not change the originals.
- This proves reader parity. It is not a service lookup of a supported version.
- The standalone hasher uses its own file handles. CHD, PBP, multi-disc changes and the
  emulator's sector adapter are not tested.

### HTTPS with the port packages (job `20261011-013921-a0f079`)

- RetroAchievements returned HTTP 403 after a verified TLS handshake of about 1.61 s. The
  badssl control returned 200 after about 0.54 s of TLS. This proves certificate and transport
  feasibility. It does not prove Connect API access or login.
- The first assertion wrongly required a nonzero auxiliary verification field. The corrected
  probe passed.
- The probe reports `authenticated_transport_ready: false` and sends no account data, because
  the port's entropy is fake. Do not send passwords or tokens through this port. Do not upload
  seed secrets over plain HBC.

## TLS seed and UTC

### Host contract test

`scripts/test-achievements-entropy.sh` gets the official Mbed TLS 3.6.4 release archive and
checks its SHA-256
(`ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110`). It rebuilds the crypto
library with NV seed callbacks and no platform entropy. The ASan/UBSan harness
`tests/entropy_seed_test.c` confirms:

- exactly one entropy source,
- seed rotation before DRBG output,
- different output from successive fresh contexts,
- rejection of missing, truncated and failed reads,
- rejection of a negative commit error,
- rejection of a fresh context after repeated failures.

It always destroys a context after a failed update. The storage is a public in-memory fixture.
This proves the library and callback contract, not confidential provisioning, atomicity,
power-loss durability or owner exclusion on the Wii.

### File-backed seed (`tests/achievement_seed.c`)

- Exact 64-byte reads. An exclusive `.busy` directory marks the owner for the whole TLS
  session. Exclusive candidate creation. Checked write, flush, fsync, close, remove, rename and
  read-back.
- Any callback error keeps the marker. A new session refuses an existing marker. It never
  promotes a leftover candidate or restores an old seed. Recovery needs a new, privately
  provisioned seed.
- Only one serialized TLS owner may call the callbacks. A missing seed is never made from
  device IDs. A failed context must still be destroyed.
- `tests/entropy_storage_test.c` passed under host ASan/UBSan and on the Wii in job
  `20261011-025257-be84db` (20 seconds, zero failures). It checks fsync, rotation and reopen,
  distinct fresh-context outputs, owner exclusion, rejection of missing, empty, truncated and
  oversized seeds, an exclusive-write failure with unchanged output, and refusal of simulated
  interrupted-rotation files. It used only public dummy data in the collector's UUID directory.
- This covers file-system return values and simulated interruption. It does not cover a real
  power cut or a rolled-back or cloned SD card.

### Source-built native HTTPS

- `scripts/test-achievements-entropy.sh --wii OUTPUT` builds all three Mbed TLS libraries from
  the checked official release with matched headers. The upstream Unix socket convenience and
  timing modules are off. The console RTC does not change.
- `scripts/build-achievements-native-https-test.sh` rebuilds official curl 8.16.0 against these
  headers and the pinned r41-2 SDK. It does not apply the port's fake-entropy patch. Curl's
  configure turns off `realpath` and `fnmatch` correctly, so the old stubs are not needed. The
  builder keeps matching headers, libraries and a public build-info manifest.
- On r41-2 the only prebuilt part was the checked wiisocket adapter. libogc v3 does not need
  it (see below).

| Job | Result |
|---|---|
| `20261011-025637-91172e` | No result JSON |
| `20261011-030737-35f8c3` | No result JSON |
| `20261011-031354-6eae33` | Synced diagnostics: all five HTTPS checks passed. The failure came after socket teardown and before the result write. |
| `20261011-031629-8f3966` | Socket teardown moved after seed and result I/O. Pass, 36 seconds, clean HBC return. RA HTTP 403 and control HTTP 200 accepted after verified TLS. Wrong host, untrusted root and expired certificates rejected with curl 60. |
| `20261011-032017-49eea1` | Final reproducible build. Same five cases pass, seed owner released, fixture removed, 31 seconds, clean HBC return. ELF, build info, CA bundle and collector and client hashes in `.runs/bench/20261011-native-nv-https-final/`. |

Collector changes after these runs:

- On failure, it keeps only bounded public diagnostic names before it removes its UUID
  directory.
- A suite that returns to HBC with a fresh diagnostic and no result fails at once.
- Result and diagnostic names are reserved. A payload cannot use them, so supplied JSON cannot
  pose as a fresh hardware result.
- Checkpoints fsync the log before transfers that can fail. The first unsynced diagnostic files
  were empty after a forced exit.
- Three host regression tests cover this and the exclusion of seed-like files from failure
  collection. The temporary trace markers are removed.

### libogc v3 rebuild, 2026-10-23

Both builders now use libogc v3 and devkitPPC r50 (GCC 16). The r41-2 versions are these
scripts at commit 775ae73a. libogc v3 has BSD sockets. The probe calls `if_configex()` after
the HBC agent has started the network. This registers the `soc` devoptab; `net_init()` alone
does not. Three gaps in v3 need handling in the probe:

| Gap in libogc v3 | Handling | Job that showed it |
|---|---|---|
| `getsockopt()` always fails (`net_getsockopt` returns -EINVAL) | The probe answers SO_ERROR with 0. A connect error that `poll()` reports still fails. | `20261023-132939-bef4f9`: curl 7 on all five |
| IOS rejects `send()` with `MSG_NOSIGNAL` (EOPNOTSUPP) | curl is configured with `curl_cv_msg_nosignal=no` | `20261023-134229-629714`: curl 35 on all five |
| No `select()` | curl uses `poll()`. The probe supplies a `select()` that only sleeps. | - |

`__SYS_GetSystemTime()` is `SYS_Time()` in v3. The CA bundle was devkitPro MSYS2's
`ca-bundle.crt` (`WII_NETWORK_CA`), not the pinned 2024 snapshot.

Job `20261023-135603-294ade` passed in 28 seconds. RA gave HTTP 403 and the control gave HTTP
200, both after verified TLS. Wrong host, untrusted root and expired certificates were rejected
with curl 60. The clock checks passed (raw RTC + 420 minutes was inside the host window). This
is the same result as 2026-10-11. Evidence: `.runs/bench/20261023-ogc3-https{,-2,-3}/`.

The fixes are for the probe only. A product transport needs a real v3 socket layer:
- Only `poll()` catches a connect error. An error that it misses shows at the TLS handshake.
- v3's `fcntl()` gives flags to IOS without translation, so `O_NONBLOCK` probably has no
  effect. Then curl's timeouts may not stop a read that stalls.

### UTC (job `20261011-040024-514221`)

- The Wii passed the clock assertions and all five public HTTPS cases (37 seconds, clean HBC
  return).
- With the `420` fixture (`clock-offset.txt`), raw RTC `1791691271` became UTC `1791716471`.
  That is inside the before and after window that the host recorded. This bounds the offset
  correction. It does not measure clock synchronization or long-term drift.
- Valid RA and control TLS passed. Wrong-host, untrusted-root and expired certificates returned
  curl 60 without HTTP. RA still returned HTTP 403. This is transport and date validation, not
  login.
- Host ASan/UBSan checks run the real Mbed TLS date checks one second before, at and one second
  after a certificate limit, with different corrections, DST offsets, bad configuration,
  integer limits and counter rollback.
- Evidence: `.runs/bench/20261011-utc-https/` with the frozen DOL and ELF, matched
  configuration, library and source hashes, `results/probe.log` and `host-clock-window.json`.
- The pinned r41-2 release build and the settings check also passed.

## Spyro capacity pilot

`scripts/bench_gameplay.py` freezes a real-game profile, the recording, the CUE, the DOL and ELF,
and the collector. `AchievementBenchRoot` sends card reads and writes and chain cleanup to a
fresh UUID directory. All cards start empty. Capacity definitions stay unearned. Each arm uses a
fresh journal. The reset and swap correctness hooks are off in this mode.

The profile analyzes guest VBlankStart events 6,000 to 13,199 (7,200 intervals), with
Lightrec, OpenGX, HLE BIOS, limiter off and frameskip off. It compares disabled, enabled and
disabled arms with 128 synthetic compound conditions. It is a **capacity pilot**. It is not
official Spyro set coverage and not a capped player-experience test.

The first exploratory replay reached gameplay and enemies, and later a death animation. The
screenshots show gameplay, not a title-only replay. They do not prove that today's replay
matches the recorded session.

### Rejected and invalid runs

| Job or batch | Problem | Fix |
|---|---|---|
| `20261011-013921-515b2d` | The guest frame total includes one HLE boot frame before `go()` resets profiling. The profiled count is stop minus one. The timeline count is stop minus two. | The collector requires these exact counts. |
| `20261011-014739-eba0e6` | The profiler's automatic large VRAM write at frame 6,000 saturates the 16-bit timeline. | `prepare` moves only this debug dump past the stop frame. It keeps every controller timestamp and freezes the original and the derived playback hashes. Timing passes request no screenshots. Final VRAM and state evidence comes after the stop. |
| `20261011-015023-59f96c` | Three arms ran with matching final checkpoints and verified cleanup. But the cached debug DOL had an OpenGX archive older than the changed profiler structure. The direct Gamecube-only make did not rebuild it. Implausible peak and field values showed the mixed layout. | **All timing from that cached DOL is invalid.** Its completion and scene evidence stays. The rebuild uses the build script's clean source snapshot. |
| First repeated batch | Four pairs completed, one collection failed, one replacement was cancelled. The failed case returned safely and restored files. | The collector waits for HBC and retries only reads, three times at most. It never retries a launch or a delete. |

### Accepted Spyro capacity campaign

All dependencies were rebuilt through `PROBES=... bash scripts/build.sh debug` in a clean
source snapshot. Bundles `20261011-spyro-clean-pair-1` to `-5`. All five manifests must match:

- DOL SHA-256: `0a80182177f0e5a964bb62d11e6ce134d1be799eec950762921e9f0a3bca7554`
- ELF SHA-256: `1b3f7b0b199c498bcdf37b5f9c2254e6ae7a9604ef3d619d42494d0f5e963c24`
- Original recording SHA-256: `1a88a80997eb05e615ae246c2e414d30434cfe6b6f80aa7e375a2c1ada357ee2`

| Pair / order | Queue job | Disabled window (s) | Enabled window (s) | Added time |
|---|---|---:|---:|---:|
| 1 / AB | `20261011-021358-2c3750` | 39.299 | 40.342 | 2.653% |
| 2 / BA | `20261011-021358-99e1bc` | 39.170 | 40.439 | 3.240% |
| 3 / AB | `20261011-021358-4395d2` | 39.305 | 40.312 | 2.561% |
| 4 / BA | `20261011-021359-bc85df` | 39.225 | 40.475 | 3.187% |
| 5 / AB | `20261011-021359-bb796d` | 39.306 | 40.313 | 2.563% |

- Mean paired overhead **2.84%**, median 2.65%, range 2.56–3.24%. The approximate 95%
  Student-t interval for the paired mean is 2.42–3.27% (df=4, paired differences assumed
  about normal). This is above the provisional 1% target.
- BA pairs ran about 0.6 percentage points higher than AB pairs. Order matters. Five pairs do
  not support a precise general slowdown claim.
- Mean evaluation time in the enabled arms: 142.40 µs per profiled frame. Longest single call:
  544 µs.
- Disabled p99 intervals: 11.192–11.209 ms. Enabled p99: 11.352–11.386 ms.
- Intervals past the NTSC budget: 13 in each enabled arm, 13 in the disabled arms except one
  with 14.
- All pairs had identical final RAM, VRAM, PC and cycle fingerprints and verified restore. No
  new crash, allocation failure, unexpected unlock or heap failure.
- Ten arms, 72,000 analyzed intervals. Each run has 7,200 NTSC intervals. All 128 conditions
  stay unearned. Each enabled arm ran 13,199 evaluation calls, one for each profiled frame.
- A final checkpoint match is not equality of the full per-frame path.
- `scripts/gameplay_results.py` requires matching frozen build, media, profile, input, final
  checkpoints and cleanup. It computes an approximate Student-t interval across paired
  repetitions. It never treats correlated frames as independent samples.
- MEM1 arena free space is not a full malloc-heap measure. The MEM2 peak counter tracks the
  custom MEM2 heap only, not every allocator of standalone clients or TLS.
- This uncapped pass does not show capped audio or input behaviour, official-set cost or
  online transport cost. Only this campaign is accepted timing evidence.

```sh
python3 scripts/gameplay_results.py \
  .runs/bench/20261011-spyro-clean-pair-1 \
  .runs/bench/20261011-spyro-clean-pair-2 \
  .runs/bench/20261011-spyro-clean-pair-3 \
  .runs/bench/20261011-spyro-clean-pair-4 \
  .runs/bench/20261011-spyro-clean-pair-5
```

The JSON summary is `.runs/bench/gameplay-profiles/spyro-clean-summary.json`.

## Protocol: gameplay performance

Proposed on 2026-10-11. Only the Spyro capacity pilot above has run.

### Candidates

All five have CUE files in the library (`/Volumes/General/Modding Systems/PSX/ROMs`, read-only
inventory on 2026-10-11, macOS setup). That does not prove a supported RA hash or that the
recording still reaches gameplay on today's build.

| Game | Input | Scene to confirm or record again |
|---|---|---|
| Spyro the Dragon | `scripts/autoinput/spyro_long_play.txt` | Movement, camera, geometry and texture changes |
| Crash Bandicoot 3 | `scripts/autoinput/crash3_long_play.txt` | Level play, enemies, effects |
| Final Fantasy VII, disc 1 | `scripts/autoinput/final_fantasy_vii_1_play.txt` | Field movement and battle. Report FMV and loading apart. |
| Ape Escape | `scripts/autoinput/ape_escape_play.txt` | DualShock movement and camera |
| Crash Bash | `scripts/autoinput/crash_bash_play.txt` | A busy minigame with effects |

- The [replay chains](../scripts/chains/replay3.txt) and
  [remaining replay chains](../scripts/chains/replay_rest.txt) start from boot. Label the
  gameplay intervals of a current-build replay first. Record again only if a recording drifts or
  stays in menus or attract mode.
- [movie_capture.sh](../scripts/movie_capture.sh) records input and build identity. In
  2026-10-11 its launcher used Windows/Dolphin paths and did not run on the macOS bench setup.
  The input recording code is in [PadWiiSX.c](../Gamecube/PadWiiSX.c).

### Design

- First campaign: Spyro, Crash 3 and FF7 on Lightrec with the usual renderer. 120 seconds of
  verified gameplay per scene after a fixed warm-up. Replay the full input preamble. Analyze
  frozen start and end VBlank windows. Do not remove menu or loading inputs or move their
  timestamps.
- Then add Ape Escape and Crash Bash. Other cores and renderers are separate campaigns. A
  recording may differ across cores, BIOS choices and timing settings.

| Arm | Meaning |
|---|---|
| A | Achievements off |
| B | Realistic local conditions for that exact game version, fresh progress |
| C | Capacity: 128 active definitions, with representative compound conditions |
| D, later | Online client idle, fetching, submitting, retrying, showing notifications |

- A set larger than 128 achievements or 64 KiB is unsupported by the local engine. A subset is
  a subset benchmark. Official set coverage needs verified media identity and a pinned set
  snapshot.
- Keep all arms local and without submissions.
- Run at least five paired repetitions per scene and arm, in alternating order (AB, BA), or
  five A/B/A blocks if the lease allows. Report paired differences, median, spread and
  confidence intervals. Frames of one run are correlated. Add repetitions if the noise is near
  the effect. Do not merge scenes into one number.
- Capacity pass: limiter off, frameskip off, fixed frame count. Compare µs per VBlank and
  emulation speed.
- Player-experience pass: normal limiter and presentation. Compare long-frame count, p95 and
  p99 frame time, audio disruption and input response. A limiter can hide added work in idle
  time.
- Use disposable cards and a fresh journal for each arm. Personal-card autosave off. The test
  build sends all card reads, writes and deletes to a fresh private directory. Its AutoSave
  setting loads only those cards.
- The engine restores earned entries as inactive ([achievements.c](../achievements.c)), so
  earned entries change the workload. Freeze the earned mask and report the active count.
  Measure the first unlock, notification and journal write apart from the steady cost.
- A checkpoint with `ACH1` restores earned and hit progress. Reset progress for each arm and
  check that each arm starts from the same guest state. State-assisted replay is for local or
  Casual tests only. It must never become a Hardcore submission test.

### Measurements

Use [chain_table.py](../scripts/chain_table.py), [chain_compare.py](../scripts/chain_compare.py),
[perf_compare.py](../scripts/perf_compare.py) and [vtl_view.py](../scripts/vtl_view.py). Keep
the raw files. Record per scene:

- executed VBlanks and wall time, and a separate cold-load sample,
- per-VBlank median, p95 and p99 and the count past the region's frame budget, with load
  stalls kept apart, not trimmed,
- evaluation time, active definitions, unlock frame and event count (the
  `PERF_PROF_ACHIEVEMENTS` counters),
- MEM1 and MEM2 and allocator peaks, allocation failures, stack use and heap checks, with set
  and client storage apart from later TLS and badge peaks,
- JIT activity, CD and SMB stalls, presents, audio timeline and underruns, and screenshots or
  VRAM fingerprints at agreed frames.

Profiler caveats:

1. [VBlank timeline recording](../Gamecube/perf_prof.c) also computes a RAM/PC signature. Its
   source gives about 0.45 ms per frame, and it allocates timeline and signature buffers in
   MEM2. AGENTS.md records a measured 23.5% of a Wii frame for this probe (`PERF_PROF_VSIG`)
   in the FF7 intro. Use signatures for correctness. Turn them off for timing
   (`PERF_PROF_VSIG=0`) or state their cost.
2. [Profiler groups](../Gamecube/perf_prof.h) include costly per-slice and draw tracing. Use the
   same probe flags in A and B. Turn off heavy groups. Confirm a result with a release-like
   build. Do not use the reset and swap hooks in throughput runs.
3. The speed and slow-frame helpers use 59.94/60 Hz and a 17.5 ms default. PAL needs its own
   rate. Timeline values are quantized and saturate. Detect saturation and truncation. Do not
   publish quantized values as sub-microsecond numbers.

Validity checks for each timing pair: game and media hash, configuration, controller mapping,
start card or checkpoint hash, replay input hash, completion marker and crash record. Both runs
must reach the same scene. [replay_check.py](../scripts/replay_check.py) compares guest
counters at common VBlanks. No common point is not a pass. Its `--all` mode fails after
differences, covered by `tests/bench_gameplay_test.py`. Full RAM/PC signature equality is a
strong same-core check, but tell intended achievement changes and valid core differences apart
from a replay that enters another level or menu.

Proposed gates, to tune after the pilot: zero false or missed unlocks, no new crash, allocation
or heap failure, no repeatable added stall. The 1% median slowdown target stays provisional. A
result whose paired uncertainty crosses 1% is inconclusive. Track p99 per scene. Interpreter
and slow scenes matter for correctness and cost, but their FPS is not a Lightrec shipping
target. If the cost is too high, make reads, caching or I/O faster. Never skip evaluation
frames to hide cost.

### Bench execution

- Extend the [frozen HBC collector](../scripts/bench_achievement_games.py) with a
  manifest-driven real-game mode. Freeze DOL, ELF, inputs, settings, definitions, scene
  checkpoints and progress and card seeds. Record game hashes. Never commit or share the
  images. Do not hardcode an earned count of three.
- Run one scene as a pilot to learn the wall time. Then choose small jobs and timeouts with
  margin for upload, boot, collection and cleanup. Slow interpreter runs can exceed a
  six-minute A/B/A budget. A job must handle a fast HBC return with no app status.

## Protocol: online service

Proposed on 2026-10-11. None of these gates has run.

### Gate 1: deterministic host client tests

Compile the pinned `rc_client` and WiiStation code with ASan, and UBSan on our code. Use the
upstream fake `server_call`, canned JSON and controlled clock. Keep the relevant upstream tests
and add tests for our queue, lifetime and status limits.

| Case | Required evidence |
|---|---|
| Password and token login | Good login, bad credentials, expired token, special-character encoding, no password kept |
| Load and identify | Correct game and set, unknown hash, empty set, unsupported condition, malformed, truncated or oversized JSON |
| Ownership | Request URL, body and content type copied before `server_call` returns. Response storage lasts through its callback. Each request completes once. |
| Cancellation | Abort login or load, change games, logout, destroy. A late completion does not revive the old session or touch freed code. |
| Threads | Memory validation and client changes on the emulator thread. A late load never reads freed RAM. |
| Frame and idle | One evaluation per emulated frame through frameskip. Pause runs `rc_client_idle` and completions without earning frames. |
| Events | Trigger, challenge, progress, server error, disconnected, reconnected. Callbacks queue safe snapshots for drawing. |
| Retry | Timeout, connection loss, 429, server failure. Advance the fake clock through upstream retries. Permanent errors stop. |
| Confirmation | The trigger event comes before server success. A rejected or pending award never shows as confirmed. |
| Identity | A late response for game or account A does not become progress for B. An older Casual state cannot revoke a confirmed unlock. |
| State | Sized progress round trip, missing, corrupt or wrong blob. Pending requests are not in a state. |

For device transport tests, put a small fixture service on the bench host with the same canned
responses. The Wii connects to the host's LAN address (`localhost` on the Wii is the Wii). Use
dummy identities. The fixture can count synthetic `awardachievement` requests. Allow this
override only in the test build. An HTTP fixture tests sockets and HTTP. A TLS fixture with a
trusted test CA also tests TLS. Neither proves RA interoperability. These are our controls, not
an RA staging service.

Hash tests: start with the synthetic disc and the upstream disc-reader fixtures, then the
user's supported games. Cover cooked ISO and raw BIN/CUE, pregaps, track offsets, a truncated or
missing boot executable, region, revision and patch mismatch, and each multi-disc image. Add CHD
and PBP after their adapters exist. A supported game must match its RA-linked hash. An unknown
image stays unidentified. Freeze hash, format, disc index and executable identity in results.
Do not upload disc images, cards or RAM dumps.

### Gate 2: Wii HTTPS, then spectator

- Before credentials: DNS, connection and HTTPS to RA with a bounded public GET. Check host
  name, chain, roots and clock. Test wrong host, untrusted CA and expired certificate against
  the fixture. Reject HTTPS-to-HTTP redirects for requests with credentials. Record handshake
  time, MEM1, MEM2 and stack peaks, body limits, timeout, keepalive and reconnect, and the effect
  while SMB streams a disc. (The 2026-10-11 probes did most of this. Memory peaks and SMB
  contention are not measured.)
- First authenticated run: spectator on before any game loads, Hardcore off. Assert at the
  transport with a fail-closed allowlist that no session, ping, award or leaderboard request
  leaves.

### Gate 3: one real Casual award

- Needs the user's authorization, a test account, an exact supported image and hash, one
  legitimate early achievement, and any upstream test-account procedure. A spare account does
  not authorize fake achievements. See the
  [player rules](https://docs.retroachievements.org/guidelines/users/global-leaderboard-and-achievement-hunting-rules.html).
- Normal gameplay from a clean boot and a clean copied card. No RAM injection, changed promoted
  definitions, canned trace or imported late-game save in production. The synthetic fixture
  stays on the mock service. No leaderboard submits. Hardcore off. Do not reset anyone's
  progress to repeat a test.
- Record the unlock status before the run, the image hash, the truthful incrementing
  `WiiStation/<numeric-version>` user agent, the event time, the redacted request kind, the
  response and the Casual unlock read back afterwards. Restart and check that the service
  restores the unlock. A popup or a sent request is not enough.
- One real award plus mock fault tests is better than many test awards on a real set. Do rate
  limit and failure tests on the fixture, not on production.

Hardcore needs its own review and RA validation. Test the restrictions against the mock first:
every state-load, statetool and autoboot path, cheat, debug and input playback, resume,
slowdown and frame advance, configuration changes, and the full reset from Casual to Hardcore.
Keep scripted gameplay and performance automation away from production Hardcore.

### Outages and storage

- Test a connection drop after login, several pending awards, reconnect while playing and while
  paused, terminal rejection, logout, account change and exit with pending work. Use the
  fixture first, then only a normal short outage in an authorized live session. Do not cut the
  shared bench network outside a leased, recoverable test.
- Test an interrupted shutdown as a known limit. Never replay local journal lines as fake
  service awards.
- Test logout removal, failed or corrupt storage, and loss of SD or USB.

### Run report

Each report states its gate (mock, TLS, spectator or confirmed Casual award), platform, build
hashes, rcheevos revision, game hash, format and disc, CPU and GPU, request counts by kind,
event order, TLS results, memory, latency and frame cost, cleanup checks and remaining limits.
Check all three CPU cores for memory and evaluation, and all three renderers (Soft Fast, Soft
Timed, OpenGX) for notifications. Measure network activity apart from frame evaluation. Gate 1
does not prove TLS. Gate 2 does not prove awards. Gate 3 does not prove Hardcore or durable
offline support.

## Validation matrix for Official mode

1. **Correctness:** synthetic RAM and scratchpad reads, unaligned and boundary requests,
   invalid addresses, little-endian multi-byte conditions on PPC, pointer chains, measured
   progress, ResetIf, PauseIf and hit counts. Compare frame traces with rcheevos on a host
   reference without uploading synthetic awards.
2. **Games and images:** supported hashes from the user's library. A small set, a large set, a
   multi-disc RPG, NTSC and PAL, BIN/CUE, CHD, ISO and PBP as each adapter is done. Modified and
   unknown discs and ordinary imported cards. Get supported versions from RA, not from the
   serial database.
3. **Execution and rendering:** Lightrec, PPC dynarec, interpreter. OpenGX and the software GPU.
   Frameskip and allowed fast-forward. Reset, restart, game change, multi-disc swap, state load
   and save, old state files.
4. **Network and storage:** no SMB share, SMB streaming with award traffic, Wi-Fi reconnect,
   failed TLS trust or date, server errors and rate limits, expired token, logout, login and
   account change, cached session offline, cold start offline, SD or USB missing, unexpected
   shutdown. Every request has a known owner and cancellation.
5. **Performance:** existing profiler and chain baselines on the Wii and vWii, same builds and
   scenes, and Dolphin for function checks. Per-frame p50, p95 and p99, emulated speed,
   stutters, MEM1 and MEM2 peaks, allocations, TLS stack and handshake cost, SMB disc stalls.
   Measure evaluation, hashing, image decode and network queues apart. Set the notification and
   TLS memory budgets from these measurements.
6. **Service:** test account and upstream test procedure, truthful user agent and version,
   Casual and Hardcore kept apart, no duplicate or invented awards, reset transitions,
   leaderboard cancellation and approved rejection behaviour.

## What remains

In order:

1. The HTTPS probe passes on libogc v3 (job `20261023-135603-294ade`). Still to do: run the
   seed storage test on the bench Wii (its v3 DOL builds), write a v3 socket layer for the
   product (nonblocking, connect errors, send flags), and move the other Wii test builders off
   r41-2. The manual UTC offset changes from 420 to 480 when daylight time ends (2026-11-01).
2. Provide confidential seed provisioning for each Wii and a reviewed UTC source. Test real
   power-cut durability.
3. Connect `rc_client` to the emulator: the account file login
   ([Official login](ACHIEVEMENTS.md#official-login)), the PS1 hash through WiiStation's own disc
   reader (BIN/CUE, ISO, then CHD and PBP), the frame and idle pump, the nonblocking transport
   and lifecycle cancellation. Keep online off by default, Hardcore unavailable and the status
   text explicit. Prove that one BIN/CUE image and the same game as CHD give identical hashes
   before other formats. Run Gate 1.
4. Run Gate 2: a spectator session for Monsterray.
5. Run Gate 3: one legitimate Casual award with a restart check.
6. Multi-disc: keep the Official session through a disc swap. Let a Local set name every disc.
7. Lower the 2.84% capacity overhead toward the 1% target. Then run the gameplay protocol on
   more scenes, with balanced repetitions, a capped pass, real set conditions, other cores and
   renderers, PAL, SD, USB and SMB, and vWii. Check full per-frame scene equality, not only the
   final checkpoint.
8. Add the unlock notification. The menu panel and the list are done (ab58936a); the user chose
   from the toast mockups (round 2, 2026-10-23).
9. Hardcore, only after RetroAchievements approves WiiStation.
10. Clean up: nothing open.
