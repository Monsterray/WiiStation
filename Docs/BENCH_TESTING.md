# Bench Wii testing

WiiStation uses the same shared bench queue as the local Wii64 project. The
reference workflow is Wii64's `scripts/wii_release_check.py`,
`scripts/hardware_collect.py`, `doc/hardware-session.md`, and
`.agents/skills/wii64-build-and-test/SKILL.md`. WiiStation keeps its own pinned
devkitPPC r41 toolchain; Wii64's compiler setup is not interchangeable.

## Release smoke test

`scripts/bench_smoke.py` freezes the DOL, accompanying ELF, HBC client, and test
script into a new bundle. Its manifest records SHA256 hashes and source paths.
Run the frozen script through the installed queue, which provides the shared
lease and `WII_BENCH_JOB_START` / `WII_BENCH_IP`. Never manufacture those variables
to bypass the lease, or send directly when the lease service is unavailable.

From the repository root, substitute a fresh bundle name for each run:

```sh
python3 scripts/bench_smoke.py prepare \
  --dol Gamecube/WiiSXRX_Release.dol \
  --client '../hbc-reborn/tools/hbc.py' \
  --bundle .runs/bench/my-release-smoke

python3 "$HOME/.wii-bench/wiibench.py" status
python3 "$HOME/.wii-bench/wiibench.py" add \
  --name wiistation-release-smoke --agent wiistation --timeout 300 \
  --cwd "$PWD" -- \
  python3 .runs/bench/my-release-smoke/bench_smoke.py run \
  --bundle .runs/bench/my-release-smoke

# Use the job ID returned by add. If the dispatcher is already running,
# do not start another. Otherwise:
python3 "$HOME/.wii-bench/wiibench.py" run --idle 5
python3 "$HOME/.wii-bench/wiibench.py" wait JOB_ID
```

The smoke test checks idle HBC and refuses an existing SD
`wiistation/autoboot.txt` or incomplete directory listing. It passes exactly
one loader argument, `AutoSave=0`: WiiStation currently interprets two arguments
as an autoboot directory and filename. It never stages card files. All HBC
connections are outbound; no inbound log listener is needed.

Checks: WiiStation agent startup, advancing SDK retraces, active stack guard,
menu screenshot, HOME open/close and resumed retraces, HBC return, and no new
crash record. Cleanup exits only a WiiStation app younger than the queue job.
The bundle retains statuses and screenshot in `results/`; failures retain
evidence too. Use its matching `boot.elf` to symbolize a crash. Do not edit a
submitted bundle. A new test requires a new bundle, not overwritten results.

Host checks:

```sh
python3 tests/bench_smoke_test.py
bash scripts/test-achievements.sh
python3 scripts/settings_check.py
```

## Achievement engine hardware coverage

The menu smoke verifies the integrated release boots safely. It does **not**
load a game, evaluate achievements, validate journals on SD, or measure runtime
overhead. The controlled fixture workflow is now implemented by
`scripts/bench_achievements.py` and `scripts/bench_achievement_games.py`. See
[hardware validation](ACHIEVEMENTS_HARDWARE_TESTS.md) for results and commands.
The collectors follow these requirements:

1. Freeze a test DOL/ELF, configuration, definitions, replay inputs and expected
   results. Use a legal deterministic PS1 fixture with a known disc ID and RAM
   transitions. Use disposable cards and output paths; disable autosave. Test
   local definitions only, without online submissions.
2. Give each game run a unique tag in its collected logs. Reject stale logs,
   missing completion markers, wrong disc IDs, new crashes, and incomplete
   chains. Pull files through HBC after return and retain raw evidence.
3. Verify unlock timing, hit counts, duplicate suppression, restart journal
   persistence, reset, save/load state, mismatched definition state, and disc
   swap behavior against the host fixtures. Include interpreter and Lightrec.
4. Compare disabled / enabled / disabled runs with the same frozen fixture and
   replay (A/B/A). Record emulated frames, wall time, memory, and evaluation
   cost. Rendering FPS alone does not measure achievement overhead.
5. If settings must be staged, back up exact bytes under the lease and restore
   them in cleanup; do not print credential-bearing settings. The old
   `scripts/wii_lab.py` uses an inbound listener and disables app networking in
   lab mode, so it cannot simply be reused as a live HBC pull collector.

## First hardware result

On 2026-10-10, queue job `20261010-172222-8fd253` passed in 30 seconds on the
shared Wii with HBC/SDK 1.10.0. Frozen release DOL SHA256:

`3a6cef6c7e30c68a5cb0241027e64e763906e1fd895caf98bcc1938dd4557697`

The observed WiiStation build string was `Oct 10 2026 17:13:22`. The captured
screen showed the main menu with no ISO loaded. Evidence remains in the ignored
`.runs/bench/20261010-achievements-release-smoke/` bundle. No game or personal
memory card was loaded by this test.
