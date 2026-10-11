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
Wii clock offset; production needs an explicit UTC policy.
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
`scripts/gameplay_results.py` requires matching frozen build/media/profile/input,
final checkpoints and cleanup, then computes an approximate Student-t confidence
interval across paired repetitions. No inference treats individual correlated
frames as independent repetitions.

Full scene trajectory equivalence, realistic official conditions, capped
presentation/audio testing, other scenes/cores/renderers and vWii remain required.

## Remaining online work

The native mock suite proves upstream client behavior on Broadway, not emulator
callback ownership. Next integrate a nonblocking transport, frame/idle pumping,
lifecycle cancellation and settings controls after resolving secure entropy.
Then privately provision credentials and run a spectator session for Monsterray.
A legitimate user-approved Casual award needs matching supported media and its
own server-confirmation/restart check. Hardcore, leaderboards and durable official
offline award synchronization remain unavailable.
