---
name: wiistation-build-and-test
description: Build, test, review and hardware-check WiiStation (PS1 emulator for Wii). Use for any code change, a failing build or test, adding a test, a Dolphin run or chain, a benchmark, and before any work that touches a Wii. Covers Lightrec, the OpenGX and soft GPU plugins, the SPU/XA/CD paths, the menus, achievements and memory.
---

# WiiStation build and test

This is the kit's entry point. The full method lives in two places; read them, don't copy them:

- `AGENTS.md` (dev workstation only, gitignored): building, the Wiis and their rules, the
  measuring tools, the benchmark database.
- `.claude/skills/wiistation-diagnostics/SKILL.md` and its `references/`: the hypothesis -> probe
  -> unattended run -> measured answer loop, the probes, Dolphin's pitfalls, the bench Wii.

## Build

```sh
bash scripts/wsx.sh build release
bash scripts/wsx.sh build debug [light|speed|deep|pmc|hprof|play|all|min]
```

devkitPPC r50 + libogc v3.1.0, through a POSIX shell only (`scripts/build.sh` pins the paths). `speed` for any
speed or load number: every other preset carries the vsig RAM hash (23.5% of a Wii frame).

## Host tests

Each `tests/*_test.c` names its build line at the top (`cc ... && ./NAME`); `tests/host/hostcheck.h`
is the kit's runner (`CHECK`, `known_bug`, `hc_hang()`) for new ones.

## Dolphin

`bash scripts/wsx.sh run|chain|frames|audio|menu ...` -- never raw Dolphin, never `tools/dolphin_run.py`
(WiiStation's `scripts/dolphin_run.sh` does frames, audio, chains and fresh profiles). Dolphin gives
exact counts and pictures, never GX times.

## Wii bench

Queue jobs only (`$WII_BENCH add ... -- python scripts/wii_lab.py ...`);
`scripts/wii_targets.py` refuses anything outside a job and the protected Wiis. Runs land in
`.runs/NAME` and in `baselines/bench.sqlite`.

## Review and release

Report whether a check covered a host test, a build, a Dolphin run, or the bench Wii, with numbers.
Release: `Gamecube/version.h`, `meta.xml`, `Gamecube/release/apps/README`; install on the bench Wii
with `scripts/wii_install.py` (queued) and `--play-build`.
