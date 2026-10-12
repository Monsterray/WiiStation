# Achievement hardware validation

Tests run on the shared bench Wii through the HBC-Reborn queue. Each run freezes
its DOL, matching ELF, HBC client and collector, with SHA256 hashes. Raw evidence
is kept under the ignored `.runs/bench/` directory. No personal memory cards are
used. Test definitions are local; there are no service submissions.

## Big-endian bug found by hardware

The initial suite, job `20261010-173704-da93f2`, failed at the three-hit
achievement assertion. A smaller one-byte condition, without saving or loading
state, failed in job `20261010-173948-d44aef`. HBC retained both assertion reports
and the suite returned to the channel.

The pinned rcheevos `rc_read_memory` assembled guest bytes in big-endian order
on big-endian hosts, then applied the operand-size mask. An 8-bit value of 1
became `0x01000000` and was masked to zero. Wider values were also wrong. The
local patch assembles guest bytes in little-endian order, using unsigned shifts.
Its provenance is recorded in `deps/rcheevos/README.wiistation.md`.

After the fix, job `20261010-174237-f70aa9` passed the full engine suite on Wii.
The expanded suite including failed journal-write retry passed in job
`20261010-174517-b2d6c9` (24 seconds). The final suite added high-bit
32-bit and IEEE float conditions and passed in job `20261010-220131-c89a02`
(30 seconds).

## PPC dynarec stop bug found by integration

The first matrix, job `20261010-174904-67a4b4`, completed the six Lightrec and
interpreter cases, then timed out in the PPC dynarec case with achievements
disabled. Its retained heartbeat showed more than 773,000 emulated frames
despite the 240-frame stop request. Compiled jump and branch blocks chained
directly to one another without returning to the outer loop that checks `stop`.
Both chaining paths now test `stop` after `psxBranchTest` and return when set.
The recovery job `20261010-220105-cfca5d` collected the failed run and removed
its staged files under a new queue lease.

## Engine suite

`tests/achievements_wii.c` runs the same actual engine/rcheevos tests as the host
suite, with Wii video, FAT and the HBC SDK crash handler. A new UUID directory on
SD holds all temporary definitions, journals and results. The collector pulls
the result after HBC return, compares crash records and removes only that
directory.

Hardware coverage includes logical RAM/scratchpad boundaries, single-byte and
little-endian word conditions, hit counts, pause/reset, state progress recovery,
monotonic earned bits, corrupt/missing/wrong-set progress, journal reopening,
definition identity, disabled mode, duplicate IDs, bad conditions, size limits,
IEEE floating-point conditions, and journal open failure followed by successful retry.

The 128-definition active-false-condition microbenchmark ran 1,000 batches of
100 evaluations. The expanded run measured:

| Per-frame runtime cost | Time |
| --- | ---: |
| Median | 51.29 µs |
| p95 | 51.82 µs |
| p99 | 51.82 µs |

These are batch-amortized runtime measurements, not complete emulator throughput
or a general real-game overhead guarantee. The workload reads one byte per
condition; more complex definitions can cost more.

Reproduce from the repository root with a fresh output/bundle name:

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

Wait for the build to finish successfully before freezing. Use queue `status`,
`run` and `wait` as described in [bench testing](BENCH_TESTING.md).

## Completed hardware results

The final frozen matrix `20261010-221302-bd65ed` completed all 13 cases and the
three five-frame restart cases. Its collector missed the short second boot's
agent status window and kept waiting after the Wii had returned to HBC. The
waiting collector process was terminated; recovery job
`20261010-221913-de7a77` then passed in 25 seconds. It independently verified
all matrix evidence, the three completed restart markers, restored earned
counts, unchanged journal bytes and no new crash. It restored staged files and
left the Wii in HBC. The collector now accepts changed, complete restart logs
when it misses that short agent window, with a bounded restart timeout.

Raw matrix, restart, recovery and pass evidence is in
`.runs/bench/20261010-achievements-integration-final/results/`. Frozen inputs
remain unchanged. The corrected release build also passed menu, SDK safety,
HOME overlay/resume and HBC return in job `20261010-220901-deb05a` (30 seconds),
with evidence in `.runs/bench/20261010-release-stop-fixed-smoke/`.

Together with the final engine suite, hardware checks cover all implemented
local achievement behaviors requiring Wii validation. vWii, representative
commercial-game performance, other storage devices and online services have
not been tested.

## Emulator integration matrix

`scripts/achievement_fixture.py` generates an original PS1 disc with an
assembled MIPS program and minimal ISO filesystem. No BIOS, commercial game,
external PS1 SDK or compiler is included. The guest polls VBlank, increments a
RAM counter and writes a little-endian 32-bit word and a scratchpad byte. Seven
synthetic serials isolate the runtime and persistence cases.

`scripts/bench_achievement_games.py` stages the fixture and reserved per-game
settings under the shared lease. Existing logs are backed up as exact bytes and
restored with read-back verification. Global settings are not changed; autosave
is disabled and the fixture does not access memory cards. Per-game settings
clear inherited SMB credentials before writing state metadata. Cleanup removes
only the staged fixture settings, generated logs and the private run directory.

The 13-case matrix covers Lightrec, interpreter and old PPC dynarec, with
disabled/enabled/disabled timing runs for each core. Additional runs save a state
before the third achievement, load it after earning that achievement, and save
again: the WSXE `ACH1` section must retain the two earlier earned bits before
load and all three afterward. Journals must contain exactly one record per ID.
The final run enables frameskip. Every case must complete exactly 240 emulated
frames with the expected loaded/earned counts. Loading frame 119 at frame 150
rolls the saved frame counter back by 31. The existing state tool adjusts the
chain stop accordingly: those runs end at counter 209, while the independent
profiler proves 240 frames executed. The first corrected-core run
`20261010-220852-5de4b6` completed all 13 cases but the collector rejected this
expected rollback. Its retained state sections and journals passed the corrected
judge. The final matrix and recovery verified the restart checks as described above.

The `ACHIEVEMENTS_BENCH` hooks were removed from the product file. The build no longer
records stopped-boundary counts, runs the emulator reset and disc-swap checks, or
returns chains to HBC for collection. The collector gets no achievement-bench log lines. A second
DOL boot runs just five frames per core, before guest conditions become true;
all three achievements must be restored from SD journals with no new records.

```sh
PROBES='-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0' \
  bash scripts/build.sh debug
python3 scripts/achievement_fixture.py .runs/bench/fixture
python3 scripts/bench_achievement_games.py prepare \
  --dol Gamecube/WiiSXRX_debug.dol --client ../hbc-reborn/tools/hbc.py \
  --fixture .runs/bench/fixture --bundle .runs/bench/games-frozen
python3 "C:/tools/wii-bench/wiibench.py" add \
  --name wiistation-achievements-games --agent wiistation --timeout 600 \
  --cwd "$PWD" -- python3 .runs/bench/games-frozen/bench_achievement_games.py run \
  --bundle .runs/bench/games-frozen
```

The final matrix's focused timing samples were:

| Core | Disabled before | Enabled | Disabled after | Change against control mean |
| --- | ---: | ---: | ---: | ---: |
| Lightrec | 6.158 s | 6.252 s | 6.126 s | +1.79% |
| Interpreter | 10.762 s | 11.108 s | 10.980 s | +2.18% |
| PPC dynarec | 3.779 s | 3.918 s | 3.852 s | +2.70% |

Each sample executed 240 frames with three simple definitions. These single
A/B/A samples include run-to-run noise and are not statistical bounds.

The matrix's A/B/A samples use a simple synthetic polling loop and profiler
build. They establish behavior under all supported cores and provide a focused
timing comparison. They do not substitute for representative gameplay scenes,
vWii measurements, or worst-case complex achievement sets.

Online RetroAchievements authentication, disc hashing, HTTPS transport and
hardcore features remain unimplemented; this suite validates the current local
engine.

## Next validation campaigns

The 2026-10-11 research describes [representative gameplay testing](ACHIEVEMENTS_GAMEPLAY_TESTING.md)
and [online service testing](ACHIEVEMENTS_ONLINE_TESTING.md). These are proposed
protocols and implementation prerequisites, not additional passed hardware runs.
