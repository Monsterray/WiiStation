# Contributing to WiiStation

<!-- From wii-homebrew-kit (the wii-homebrew-kit repository), adopted 2026-10-24. -->

## Set up

- **devkitPro** with devkitPPC r50 (GCC 16) and libogc v3.1.0, installed at `C:\devkitPro` on Windows
  (`scripts/setup-devkitpro-windows.sh`). The libogc2 + devkitPPC r41-2 pair still builds with
  `LIBOGC=2` for one release.
- **A POSIX shell:** devkitPro's MSYS2 bash (`C:\devkitPro\msys2\usr\bin\bash.exe`) or Git Bash.
  Never PowerShell with hand-set `DEVKITPRO`: Windows-style paths silently drop OpenGX
  functions, with no build error.
- **Python 3.10+:** `python` on the dev machine. The devkitPro bash has no
  `python` of its own.
- **Dolphin** for unattended runs: `$DOLPHIN` (see `python scripts/wii_paths.py dolphin`). The game library is `$WSX_LIBRARY`
  (`scripts/profile.sh`).

## Build

```sh
bash scripts/wsx.sh build release          # Gamecube/WiiSXRX_Release.dol
bash scripts/wsx.sh build debug [PRESET]   # Gamecube/WiiSXRX_debug.dol; speed for any benchmark
```

`scripts/build.sh` pins the toolchain and rebuilds the dependencies in `build_ogc3/`.

## Test

| Check | Command | Needs |
| --- | --- | --- |
| Line endings and `.gitattributes` coverage | `python -m unittest tests/test_line_endings.py` | Python |
| Host tests | the `cc ...` line at the top of each `tests/*_test.c` (e.g. `cc -O2 -o dither_test tests/dither_test.c && ./dither_test`) | a C compiler |
| Bench script tests | `python -m unittest discover -s tests -p "*_test.py"` | Python |
| Dolphin chain (many games, one boot) | `bash scripts/wsx.sh chain NAME scripts/chains/smoke.txt --secs 400` | Dolphin |
| Frames / audio unchanged | `bash scripts/wsx.sh frames A B`, `bash scripts/wsx.sh audio A B` | two runs |
| PS1 accuracy | `bash scripts/wsx.sh chain NAME scripts/chains/ps1tests.txt` then `python scripts/ps1tests_check.py .runs/NAME` | Dolphin |
| Bench Wii | `python "$WII_BENCH" add --cwd . -- python scripts/wii_lab.py NAME scripts/chains/FILE.txt --dol X.dol` | the bench Wii |
| Benchmark | `scripts/chains/library20.txt` on the bench Wii with `build debug speed`, then `python scripts/benchdb.py latest --platform wii --preset speed` | the bench Wii |

A test marked `known_bug` (`tests/host/hostcheck.h`) describes behaviour that is still broken:
it is reported as `known` and doesn't fail the run. When you fix the bug, remove the mark; a
`known_bug` test that passes fails the run (`FIXED?`) as a reminder.

## Conventions

- **Line endings.** Every text file is LF, in the repository and in the checkout; `.bat` files
  are checked out with CRLF. Merge an older branch with `git merge -X renormalize`. For a checkout
  made before this rule: `python tools/fix_line_endings.py`.
- **Evidence.** Say what a change was checked with: a host test, a build, Dolphin, or a Wii, with
  the numbers. Record findings and ideas in `REVIEW.md` and open bugs in `Docs/PARKED_BUGS.md`.
- **Release builds carry no probe cost.** Probes live in the debug build behind `PERF_PROF_*`.
- **Wiis.** Bench jobs only through the queue (`scripts/wii_targets.py` refuses anything else and
  refuses the protected Wiis). Never a test on a protected Wii.
- **No keys, console data or credentials.** `keys/` and `examples/ra_account.ini` are ignored;
  check `git status` before you commit.
- **AI agents** read `.agents/skills/wiistation-build-and-test/SKILL.md`, and on the dev
  workstation its local `AGENTS.md` (not in the repository: it names machines and paths).
