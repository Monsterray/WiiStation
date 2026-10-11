# Achievement testing implementation — 2026-10-11

Work is on `codex/achievement-engine`. Production still evaluates local settings-file
sets. Matching pinned rcheevos client/API/hash sources are now vendored and tested
in standalone programs; they are not yet an authenticated emulator integration.
No login, presence ping, award, leaderboard submission or progress reset occurred.
The selected future live-test username is Monsterray; no credentials are staged.

## Verified host and Wii results

| Gate | Evidence | Outcome |
| --- | --- | --- |
| Local engine | `scripts/test-achievements.sh` | ASan/UBSan pass: memory, conditions, state identity, journals, malformed sets |
| Upstream runtime/API/client | `scripts/test-achievements-client.sh` | 2,397/2,397 pass with ASan; upstream hash tests disabled |
| Big-endian client mocks | Wii job `20261011-014231-081451` | 180/180 client tests pass; clean HBC return |
| PS1 hash parity | Wii job `20261011-014646-ea19fc` | Three real raw BIN images match host hashes exactly |
| Public HTTPS/certificates | Wii job `20261011-013921-a0f079` | Valid certificates accepted; wrong-host, untrusted-root and expired certificates rejected |
| NV seed contract, host only | `scripts/test-achievements-entropy.sh` | ASan/UBSan pass with public dummy seed and mock storage; no Wii security readiness |
| File-backed NV seed | Wii job `20261011-025257-be84db` and host sanitizer harness | Rotation/reopen, exact size, exclusive ownership, failed commit and simulated interrupted restart pass; zero failures |
| Source-built NV HTTPS | Wii job `20261011-032017-49eea1` | Final reproducible build: all five certificate cases and seed cleanup pass in 31 seconds; public dummy seeds only |
| UTC callback and certificate dates | Wii job `20261011-040024-514221`, host ASan/UBSan | Zero clock failures; all five HTTPS cases pass; explicit +420 correction falls within host time window; clean HBC return in 37 seconds |
| Production release | `.runs/achievement-build/production-release-build.log` | Pinned r41-2 build succeeds |
| Collectors | `tests/bench_gameplay_test.py`, `tests/gameplay_results_test.py`, existing bench tests | Completion, invalid paths, memory telemetry, saturation, retries, frozen identity/checkpoints and replay failure gates pass |

Wii evidence is under `.runs/bench/`; DOL, matching ELF, collector, SDK client and
payload SHA-256 hashes are frozen in each bundle's manifest. Every hardware action
uses the HBC-Reborn shared queue lease. Test wrappers return to HBC and delete only
their UUID directory. The gameplay collector restores replaced settings/log bytes
and verifies restoration. Personal cards and original library images are untouched.

| Image | Host and Wii canonical PS1 hash |
| --- | --- |
| Spyro the Dragon, SCUS94228 | `3e9eb765a7b2912e60c30f363f0a3574` |
| Crash Bandicoot 3, SCUS94244 | `8209150e48e8f9f0e727347472c7e123` |
| Final Fantasy VII disc 1, SCUS94163 | `665933b4dbbdfa04b328a2deccea2a30` |

The synthetic raw MODE2/2352 CUE and cooked ISO both produce
`f4cbe17fd29e842da2c299ea52508443`. Crash 3/FF7 library CUE files refer to stale
BIN filenames, so their parity tests read BIN files directly without modifying
originals. These results establish reader parity, not a service-supported version
lookup. CHD, PBP, multi-disc changes and the active emulator sector adapter remain
separate tests; the standalone hasher uses independent file handles.

## HTTPS boundary

The probe uses checked [Wii curl 8.16.0 / mbedTLS 3.6.4 packages](https://github.com/AndrewPiroli/wii-curl/releases/tag/c8.16.0%2Bm3.6.4),
strict CA and hostname checks, bounded bodies, HTTPS-only protocol, timeouts and
no redirects. Certificate rejection is determined by curl error 60, HTTP status
zero and the diagnostic: this backend's auxiliary verification field remains zero
even after failed verification. The first assertion wrongly required that field
to be nonzero; the corrected public probe passed.

RetroAchievements returned HTTP 403 after a successful verified TLS handshake
(~1.61 s). The valid badssl control returned 200 (~0.54 s TLS). Thus this proves
certificate/transport feasibility, not successful Connect API access or login.
The test's 2024 CA snapshot is pinned for reproducibility, not a production update
policy. `tests/https_wii_compat.c` supplies probe-only missing r41 newlib symbols;
rebuild curl against the chosen pinned SDK before product integration.

The port's entropy source is explicitly **fake**, derived from predictable Wii
state. Certificate validation does not establish secure session key generation.
The probe deliberately reports `authenticated_transport_ready: false` and sends
no account data. Production transport requires a reviewed source of strong
per-device entropy and confidential bootstrap/reseed provisioning. Do not send
passwords/tokens through this port or upload seed secrets over plaintext HBC.
The follow-up [entropy investigation](ACHIEVEMENTS_TLS_ENTROPY.md) identifies
Mbed TLS NV seeds as a conditional candidate and also records the observed
Wii clock offset. The source-built probe now has an explicit UTC correction policy;
production requires a verified RTC and confidential bootstrap.
See the pinned [port source](https://github.com/AndrewPiroli/wii-curl/tree/d9b21a24142c6bfbfff15cd4151ef154b9659c4e).

## Gameplay pilot and reproducibility

`scripts/bench_gameplay.py` freezes a real-game profile, recording, CUE, DOL/ELF
and collector. The test-only `AchievementBenchRoot` redirects card reads, writes
and chained cleanup into a fresh UUID directory. Capacity definitions remain
unearned; each arm uses a fresh local journal. All cards begin empty. Boundary
reset/swap correctness hooks are disabled for this real-game mode.

`PERF_PROF_VSIG=0` removes per-frame RAM signature work/allocation;
`PERF_PROF_ACHIEVEMENTS=1` measures evaluation time and calls. Those counters and
test-only paths have no production timer overhead. Build the pilot with:

```sh
DEVKITPRO=/tmp/wiistation-devkitpro-local \
DEVKITPPC=/tmp/wiistation-devkitpro-local/devkitPPC-r41-2 \
PROBES='-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0 -DPERF_PROF_VSIG=0 -DPERF_PROF_ACHIEVEMENTS=1 -DACHIEVEMENTS_BENCH' \
bash scripts/build.sh debug
```

Use `scripts/gameplay_profiles/spyro_capacity.json` with the existing
`scripts/autoinput/spyro_long_play.txt`; the profile specifies the user's SD media
path. `bench_gameplay.py prepare --help` describes frozen inputs. Submit its
frozen `bench_gameplay.py run --bundle ...` through `wiibench.py add`, never
invoke the hardware run directly or manufacture lease environment variables.

The original exploratory replay reached gameplay/enemies and later a death
animation. Its screenshots validate gameplay rather than a title-only replay;
they do not prove that today's replay matches the original recorded session.
The original profile analyzes guest VBlankStart events 6,000–13,199 (7,200
intervals), with Lightrec/OpenGX, HLE BIOS, limiter off and frameskip off. It
compares disabled/enabled/disabled with 128 synthetic compound conditions;
it is a **capacity pilot**, not official Spyro achievement-set coverage or
normal capped player-experience acceptance.

Two rejected measurement runs retained raw evidence and restored the bench:

- `20261011-013921-515b2d`: guest frame total includes one HLE boot frame before
  `go()` resets profiling. The independent profiled count is stop minus one;
  timeline count is stop minus two. The corrected collector requires exact
  counts instead of relaxing completion checks.
- `20261011-014739-eba0e6`: the profiler's automatic large VRAM write at frame
  6,000 saturates the 16-bit timeline. `prepare` now defers only this debug dump
  beyond the stopped frame, preserves every recorded controller timestamp, and
  freezes both original and derived playback hashes. Timing passes also omit
  screenshot requests; final VRAM/state evidence is collected after stopping.

The initial corrected collector pilot, job `20261011-015023-59f96c`, completed
three arms with matching final checkpoints and verified cleanup. However, all
subsequent timing evidence from the cached debug DOL was **invalidated**: its
OpenGX archive predated the changed profiler structure and was not rebuilt by
the direct Gamecube-only make invocation. Implausible peak/field values exposed
the mixed layout. Completion/scene evidence is retained, but its timing numbers
must not be used. The replacement uses the repository build script's clean
source snapshot so all profiling dependencies use matching flags and headers.

The first repeated batch retained four completed pairs, one failed collection
and a cancelled replacement. The failed collector case returned safely and
verified restoration; the collector now waits for HBC and retries only reads,
with a bounded three-attempt policy. No launch or delete operation is retried.
The replacement five-pair campaign uses the clean DOL and frozen bundles
`20261011-spyro-clean-pair-1` through `-5`; all dependencies were rebuilt through
`PROBES=... bash scripts/build.sh debug` in a clean source snapshot.
The independent production release smoke also passed, job
`20261011-015705-283c84` (menu/SDK/HOME/HBC, 38 seconds).
Each accepted run has 7,200 analyzed NTSC intervals; all 128 conditions remain
unearned and evaluation runs once for each of 13,199 profiled guest frames.
`PERF_PROF_VSIG=0` is required for these timing passes; end RAM/VRAM hashes remain
available. A final checkpoint match is not full per-frame trajectory equality.
`scripts/gameplay_results.py` requires matching frozen build/media/profile/input,
final checkpoints and cleanup, then computes an approximate Student-t confidence
interval across paired repetitions. No inference treats individual correlated
frames as independent repetitions.

MEM1 arena free space is not a complete malloc-heap measurement. The existing
MEM2 peak counter tracks the custom MEM2 heap, not every allocator used by
standalone clients/TLS. Retain raw counters without claiming complete client/TLS
memory peaks.

Full scene trajectory equivalence, realistic official conditions, capped
presentation/audio testing, other scenes/cores/renderers and vWii remain required.

Clean campaign build identity (all five manifests must match):

- DOL SHA-256: `0a80182177f0e5a964bb62d11e6ce134d1be799eec950762921e9f0a3bca7554`
- ELF SHA-256: `1b3f7b0b199c498bcdf37b5f9c2254e6ae7a9604ef3d619d42494d0f5e963c24`
- Original recording SHA-256: `1a88a80997eb05e615ae246c2e414d30434cfe6b6f80aa7e375a2c1ada357ee2`


## Accepted clean gameplay campaign

All five alternating pairs passed with the clean build, identical final
RAM/VRAM/PC/cycle fingerprints and verified restoration. There were no new
crashes, allocation failures, unexpected unlocks or heap failures. The ten arms
cover 72,000 analyzed intervals; each enabled arm ran 13,199 evaluation calls.

| Pair / order | Queue job | Disabled window (s) | Enabled window (s) | Added time |
| --- | --- | ---: | ---: | ---: |
| 1 / AB | `20261011-021358-2c3750` | 39.299 | 40.342 | 2.653% |
| 2 / BA | `20261011-021358-99e1bc` | 39.170 | 40.439 | 3.240% |
| 3 / AB | `20261011-021358-4395d2` | 39.305 | 40.312 | 2.561% |
| 4 / BA | `20261011-021359-bc85df` | 39.225 | 40.475 | 3.187% |
| 5 / AB | `20261011-021359-bb796d` | 39.306 | 40.313 | 2.563% |

Mean paired overhead: **2.84%**; median
2.65%; range
2.56–3.24%.
The approximate 95% Student-t interval for the paired mean is
2.42–3.27%
(df=4, approximate normality of paired differences). This is above the
provisional 1% capacity target. BA pairs ran about 0.6 percentage points higher
than AB pairs: order matters, and five pairs do not justify a precise universal
slowdown claim. More balanced repetitions and additional scenes are needed for
shipping acceptance.

Mean evaluation time across the enabled runs was
142.40 us per
profiled frame; the maximum observed single call was 544 us. Disabled p99
intervals were 11.192–11.209 ms; enabled p99 was 11.352–11.386 ms. Enabled arms
had 13 intervals each beyond the NTSC budget; disabled arms had 13, except one
with 14. This uncapped capacity pass does not establish capped audio/input
behavior, official-set overhead, or online transport overhead.

Reproduce the offline summary with:

```sh
python3 scripts/gameplay_results.py \
  .runs/bench/20261011-spyro-clean-pair-1 \
  .runs/bench/20261011-spyro-clean-pair-2 \
  .runs/bench/20261011-spyro-clean-pair-3 \
  .runs/bench/20261011-spyro-clean-pair-4 \
  .runs/bench/20261011-spyro-clean-pair-5
```

The JSON summary is `.runs/bench/gameplay-profiles/spyro-clean-summary.json`.
Only the clean campaign above is accepted timing evidence; all earlier cached
DOL measurements remain excluded.

## Remaining online work

The native mock suite proves upstream client behavior on Broadway, not emulator
callback ownership. Next integrate a nonblocking transport, frame/idle pumping,
lifecycle cancellation and settings controls after resolving secure entropy.
Then privately provision credentials and run a spectator session for Monsterray.
A legitimate user-approved Casual award needs matching supported media and its
own server-confirmation/restart check. Hardcore, leaderboards and durable official
offline award synchronization remain unavailable.

## Native entropy and transport implementation follow-up

The file-backed NV callbacks now live in `achievement_seed.c`; missing or
malformed seeds and failed rotations fail closed with a persistent `.busy`
marker. Only one serialized TLS owner may call them. The physical SD harness
passed in 20 seconds, and the same storage code passed with host ASan/UBSan.
This covers filesystem return values and simulated interrupted artifacts,
not a physical power-cut durability campaign or rollback/cloned-card protection.

`scripts/build-achievements-native-https-test.sh` rebuilds official Mbed TLS 3.6.4
and curl 8.16.0 with the pinned r41-2 SDK and identical NV configuration headers.
It does not use the Wii port's fake-entropy patch or the old `realpath`/`fnmatch`
stubs. The checked wiisocket binary adapter remains the socket bridge. The
builder retains matching headers/libraries and a public build-info manifest.

The first two native runs (`20261011-025637-91172e`,
`20261011-030737-35f8c3`) lacked result JSON. Synced diagnostics in
`20261011-031354-6eae33` showed all five HTTPS checks had already passed; failure
occurred after socket teardown and before writing the final result. Moving
socket teardown after seed/result I/O fixed the original failure in
`20261011-031629-8f3966` (36 seconds, clean HBC return). That run accepted RA
HTTP 403 and the HTTP 200 control after verified TLS, and rejected wrong host,
untrusted root and expired certificates with curl 60. Temporary trace markers
have been removed. The final reproducible build passed the same five cases in
`20261011-032017-49eea1` (31 seconds, clean HBC return); its matching ELF,
build-info, CA bundle and collector/client hashes are frozen under
`.runs/bench/20261011-native-nv-https-final/`.

The collector now preserves only bounded public diagnostic names on failure,
before removing its owned UUID directory. A suite returning to HBC with a
fresh diagnostic but no result fails immediately. Three host regression tests
cover that behavior and exclusion of seed-like files from failure collection.
Result/diagnostic output names are reserved and cannot be pre-staged as payloads,
preventing supplied JSON from masquerading as a fresh hardware result.
Initial unsynced diagnostic files were empty after forced exit; checkpoints
now fsync the log before potentially failing transfers.

Neither native probe accepts credentials: its seed is intentionally public.
Private device bootstrap and verified RTC-to-UTC configuration remain
necessary, along with production nonblocking rc_client ownership/frame/idle
integration, before a Monsterray spectator login. The production local engine
and the previously measured 2.84% capacity overhead are unchanged.


## UTC implementation follow-up

The settings schema now includes quoted `AchievementUTCOffsetMinutes`, empty
by default. The portable clock rejects unknown/malformed corrections and bad
RTC/counter ranges, snapshots corrected RTC, and advances from the monotonic
system counter. The source-built TLS probe installs both TIME and GMTIME_R
callbacks, so unknown UTC rejects both certificate-date predicates. No console
clock is modified. [Policy and evidence](ACHIEVEMENTS_TLS_ENTROPY.md#explicit-utc-correction)
include Pacific daylight/standard-time examples and the requirement to verify
RTC settings and maintain the correction manually. The local production engine
still has no authenticated transport; this fixes the probe's UTC boundary and
prepares its configuration. Clean release build: `.runs/achievement-build/utc-release.log`.
