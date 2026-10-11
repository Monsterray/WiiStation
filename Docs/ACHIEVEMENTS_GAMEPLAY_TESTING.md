# Representative gameplay performance testing

Research and proposed protocol — 2026-10-11. No new Wii runs were performed for
this investigation. The previous [hardware suite](ACHIEVEMENTS_HARDWARE_TESTS.md)
proved correctness and measured a synthetic workload; it does not establish
representative gameplay overhead.

## What is already available

The repository has human-recorded, emulated-VBlank input scripts and a chained
runner. Scripts declare their original build, controller configuration and game
path. All five candidate games below have CUE files in the user's mounted
`/Volumes/General/Modding Systems/PSX/ROMs` library (read-only inventory checked
2026-10-11). Presence does not establish a matching supported RA hash or that
the recording still reaches gameplay on today's build.

| Candidate | Existing input | Scene to confirm or recapture |
| --- | --- | --- |
| Spyro the Dragon | `scripts/autoinput/spyro_long_play.txt` | Traversal, camera movement, geometry and texture changes |
| Crash Bandicoot 3 | `scripts/autoinput/crash3_long_play.txt` | Active level play, enemies and effects |
| Final Fantasy VII, disc 1 | `scripts/autoinput/final_fantasy_vii_1_play.txt` | Field movement and battle; report FMV/loading separately |
| Ape Escape | `scripts/autoinput/ape_escape_play.txt` | DualShock movement and camera controls |
| Crash Bash | `scripts/autoinput/crash_bash_play.txt` | A minigame with effects and busy scenes |

These are workload candidates, not assertions about what each old recording
currently does. Existing [replay chains](../scripts/chains/replay3.txt) and
[remaining replay chains](../scripts/chains/replay_rest.txt) start from boot.
First inspect a current-build replay and label its gameplay intervals. Re-record
only recordings that drift or remain in menus/attract modes. The capture helper
[describes input and build identity](../scripts/movie_capture.sh), but its
current launcher uses Windows/Dolphin paths and is not directly runnable on
this macOS bench setup. The input recording code itself is in
[PadWiiSX.c](../Gamecube/PadWiiSX.c).

## Small first campaign

Start with Spyro, Crash 3 and FF7 on Lightrec and the usual renderer/settings.
Use 120 seconds of verified gameplay per scene after a fixed warm-up. Replay
the complete input preamble and analyze frozen start/end VBlank windows; do not
remove menu/loading inputs or shift their timestamps to manufacture a gameplay
window. Add Ape
Escape and Crash Bash after that campaign is repeatable. Expand to the other
CPU cores and renderers as separate campaigns; a recording is not assumed to
remain identical across cores, BIOS choices or timing settings.

For each game, compare the same frozen DOL and inputs with:

| Arm | Meaning |
| --- | --- |
| A | Achievements disabled |
| B | Realistic local conditions for that exact game version, fresh unearned progress |
| C | Capacity test with 128 active definitions, including representative compound conditions |
| D, later | Online client idle / fetching / submitting / retrying / displaying notifications |

The current engine has a 128-achievement and 64 KiB definition limit. An
upstream set beyond either limit must be reported as unsupported for the current
local engine. A deliberate subset is a subset benchmark, not full-set coverage.
Use supported local fixtures for the first campaign; authentic official set
coverage needs verified media identification and a pinned set snapshot. Disc
serials and filename labels alone do not identify a supported RA version.

Keep all arms local and non-submitting in this performance campaign. The online
campaign has its own [implementation and service gates](ACHIEVEMENTS_ONLINE_TESTING.md).

Run at least five paired repetitions per scene/arm, alternating order (AB, BA),
or five A/B/A blocks if the lease duration permits. Report paired differences,
median, spread and confidence intervals across repetitions; frames from one
run are correlated and are not thousands of independent repetitions. Add more
repetitions if run-to-run noise is comparable to the effect. Do not combine
results from different scenes into one number that hides a regression.

Two complementary passes are necessary:

- **Capacity:** frame limiting off, frameskip off, fixed emulated-frame count.
  Compare microseconds per executed VBlank and emulation speed.
- **Player experience:** normal limiter and presentation settings. Compare
  long-frame frequency, p95/p99 frame time, audio disruption and input response.
  A limiter can hide added work in its idle time, so capped FPS alone is insufficient.

Use disposable memory cards and a fresh private achievement journal for every
measured arm. Autosave must be disabled, and benchmark card state must be
identical. Existing earned entries affect the evaluation workload: the engine
[restores earned entries as inactive](../achievements.c), and triggered
conditions no longer represent a fully active set. Freeze the initial earned
mask and report active definitions over the measurement window. Separate steady
locked-condition cost from the first unlock, notification and persistence cost.
Current local journals flush at stopped menu/state boundaries, not during normal
frames; do not present a gameplay measurement as an SD-write latency test.

Use an identical checkpoint where needed to reach late-game play, but validate
that each arm starts from identical guest state and deliberately resets its
achievement progress. A checkpoint containing `ACH1` can restore earned/hit
progress and contaminate the workload. State-assisted replay belongs to local
or softcore testing; it must never become a hardcore submission test.

## Measurements and validity gates

Reuse [chain_table.py](../scripts/chain_table.py),
[chain_compare.py](../scripts/chain_compare.py),
[perf_compare.py](../scripts/perf_compare.py) and
[vtl_view.py](../scripts/vtl_view.py) for existing wall, CPU, GPU, limiter,
JIT, storage and per-VBlank timeline data. Retain raw files, not just tables.

Record per scene:

- Executed VBlanks and measured wall time, including a separate cold-load sample.
- Per-VBlank median/p95/p99 and count beyond the region's frame budget;
  preserve load stalls separately instead of silently trimming them.
- Runtime evaluation aggregate time, active definition count, unlock frame and
  event count. These need new bounded achievement-specific counters; current
  profiler reports do not isolate achievement evaluation inside the CPU slice.
- MEM1/MEM2 and allocator peaks, allocation failures, stack usage and heap checks;
  separate persistent set/client storage from TLS and badge-buffer peaks later.
- JIT activity, CD/SMB stalls, presentation count, audio timeline/underrun evidence,
  and screenshots or VRAM fingerprints at agreed guest-frame checkpoints.

The current profiler needs three explicit caveats before this becomes a release
performance gate:

1. [VBlank timeline recording](../Gamecube/perf_prof.c) also computes a RAM/PC
   signature costing approximately 0.45 ms per frame according to its source.
   It allocates timeline and signature buffers in MEM2. Turn signatures on for
   correctness validation, then decouple them from the lightweight timing pass.
   Otherwise disclose that cost and memory footprint in every comparison.
2. [Profiler groups](../Gamecube/perf_prof.h) include expensive per-slice and
   draw tracing. Freeze identical probe flags in A and B, disable heavy groups,
   and confirm the finding with a minimally instrumented release-like build.
   Do not use the test build's boundary reset/swap hooks in throughput runs.
3. Existing speed/slow-frame helpers use 59.94/60 Hz and a 17.5 ms default.
   They need region-aware rates for PAL, and timeline values are quantized and
   saturated. Detect saturation/truncation; do not publish quantized values as
   precise sub-microsecond runtime measurements.

Before accepting any timing pair, check game/media hash, configuration, controller
mapping, starting card/checkpoint hash, replay input hash, expected completion
marker and crash record. Check that both runs actually reach the same scene.
[replay_check.py](../scripts/replay_check.py) compares guest counters at common
VBlanks; no common point is not a pass. Its `--all` mode currently prints a final
success message/returns zero after differences, so automation must fix that
behavior or use first-difference mode. Full RAM/PC signature equality is a strong
same-core check, but intentional achievement changes and valid core differences
must be distinguished from a replay entering a different level or menu.

Proposed gates, to be tuned from the pilot rather than claimed as current results:
zero false/missed unlocks; no new crashes, allocation failures or heap failures;
no reproducible added gameplay stall. Keep the previous plan's provisional 1%
median slowdown target, but classify a result as inconclusive when the paired
uncertainty crosses that target. Track p99 regressions per scene independently.
Interpreter and low-speed scenes still matter for correctness and overhead,
but do not treat their total FPS as a shipping Lightrec performance target.

## Bench execution and next changes

Extend the existing [frozen HBC pull collector](../scripts/bench_achievement_games.py)
with a manifest-driven real-game mode. Freeze DOL/ELF, inputs, relevant settings,
definitions, expected scene checkpoints and progress/card seeds. Record game
hashes in the manifest; use the user's existing images without committing or
redistributing them. Do not hardcode a game's expected earned count to three.

All Wii actions use the [shared bench queue](BENCH_TESTING.md). Stage only
private inputs/output and back up and restore exact bytes of any replaced
configuration or log under the lease. No personal card may be deleted by a
benchmark helper. Pilot one scene to estimate actual wall time, then choose
small queue jobs and timeouts with upload, boot, collection and cleanup margin;
slow interpreter runs can exceed a nominal six-minute A/B/A budget. A queued
job must handle a fast HBC return even if no app status was observed.

Suggested order of implementation:

1. Inventory/verify the five recordings on the current build, choose three
   actual gameplay windows, and freeze their card/input/configuration seeds.
2. Add private per-arm journals, manifest-driven game expectations and
   lightweight evaluation counters. Separate signature validation from timing.
3. Run one Spyro pilot and repeatability check, then the three-game paired
   campaign. Add remaining scenes, cores, renderers, PAL and SD/USB/SMB separately.
4. After the online client exists, repeat the same scenes with mocked transport
   and then verified live transport, including notification and reconnect spikes.

This investigation does not implement the runner extensions or claim new game
performance results. The existing hardware evidence remains the baseline.
