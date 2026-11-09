# Plan: one simple way for scripts to find tools

Status: PLAN for Monty's approval (2026-11-09). Nothing below is implemented yet.

## What is wrong now

A script finds a tool in one of six ways:
1. an environment variable;
2. `~/.wii-paths.json`;
3. `scripts/wii_paths.py`, a 214-line resolver;
4. the shell helpers `find_python.sh` and `find_dolphin.sh`;
5. a path relative to the project (`../hbc-reborn`, `../dolphin-movie`);
6. a hard-coded `C:/` or `/c/` path.

The way changes from tool to tool and from script to script. Some examples:

| Tool | How scripts find it today |
|---|---|
| game library | env `WSX_LIBRARY`, else `/c/wiistation-lib` hard-coded in 4 bash scripts and 5 Python scripts |
| hbc-reborn | env, json, else `../hbc-reborn`. **Breaks in every worktree** (the cause of today's lab failure) |
| dolphin-movie | `../dolphin-movie`. Breaks in worktrees |
| devkitPro | hard-coded `/c/devkitPro` in 5 scripts; required as `$DEVKITPRO` in others |
| ffmpeg, 7-Zip, clang | hard-coded `Program Files` paths (wav_compare.py:39's is corrupted) |
| Python | `find_python.sh` for bash; Python scripts use whatever runs them |
| ps1-tests, wiicap | env only, no default |

That makes 28 environment variables and 7 resolver names. A new person cannot tell what to install or where to put it.

## The new way: one file per PC, one list of names

**1. One file per PC, `~/wii-tools.env`, written in plain `NAME=path` lines.**
```
# Wii homebrew tools on this PC. One line per tool. Every project and worktree reads this file.
PYTHON=C:/Python312/python.exe
DEVKITPRO=C:/devkitPro
DOLPHIN=C:/tools/Dolphin-x64/Dolphin.exe
HBC_PY=C:/projects/hbc-reborn/tools/hbc.py
WII_BENCH=C:/tools/wii-bench/wiibench.py
WIICAP=C:/tools/wii-capture/wiicap.py
GAME_LIBRARY=C:/wiistation-lib
DOLPHIN_MOVIE=C:/projects/dolphin-movie
PS1_TESTS=C:/tools/ps1-tests
CHDMAN=C:/tools/chdman/chdman.exe
FFMPEG=...
SEVENZIP=...
```
- It lives in the home folder, so a new worktree or a different project needs no setup.
- Bash reads it with `. ~/wii-tools.env`.
- Python reads it with a 10-line function.
- An environment variable of the same name still wins, for a one-off override.

**2. No guessing in scripts.**
- No relative `../x` defaults, no hard-coded `C:/` paths and no search logic in any script.
- A script asks for a name. If the name is missing it stops with one line, for example: `DOLPHIN is not set: add it to ~/wii-tools.env (python scripts/tools.py setup)`.

**3. One small helper, `scripts/tools.py` (about 60 lines, replacing wii_paths.py, find_python.sh and find_dolphin.sh).**
- `tools.py setup` looks in the usual places, writes the lines it finds, and lists what is still missing, with where to get each tool. A new person runs this once.
- `tools.py check` prints every name, its path, and whether the path exists.
- `tools.py NAME` prints one path (for bash: `DOLPHIN=$(... tools.py DOLPHIN)`; usually the `. ~/wii-tools.env` line is enough).

**4. One table in the docs:** each name, what it is, where to get it, and which scripts use it. It goes in CONTRIBUTING.md and replaces the scattered notes.

**5. A test guards it.** `tests/test_workstation_paths.py` is widened to fail on any absolute path or `../sibling` in tracked scripts. Today it misses `/c/wiistation-lib`, `/c/devkitPro`, `Program Files` and `192.168.x`.

**6. The same file for every project.** The helper and the test go into the wii-bench kit (`template/`), so Wii64, hbc-reborn and d2x-cios read the same `~/wii-tools.env`. One file per PC for all of them.

## What stays as it is

- **Folder layout.** Third-party programs stay in `C:/tools`, git checkouts in `C:/projects`, game data in `C:/wiistation-lib`. The file names the paths, so nothing has to move.
- **Wii addresses and the safety guards** stay in code (`wii_targets.py`). A missing config file must never unprotect the production Wii.
- **The queue's own settings** (`WII_BENCH_IP` and the others) stay. The queue sets them for each job.

## Steps (each one a small, testable change)

1. Write `scripts/tools.py`, write `~/wii-tools.env` on this PC with `setup`, and run `check`.
2. Move the bash scripts to `. ~/wii-tools.env`: wsx.sh, dolphin_run.sh, movie_capture.sh, wiistation_play.sh, profile.sh, padtest_dx.sh, menu_pages_walk.sh, the build scripts. Delete find_python.sh and find_dolphin.sh.
3. Move the Python scripts to `tools.get("NAME")`. Delete wii_paths.py and `~/.wii-paths.json`.
4. Fix the hard-coded ffmpeg, 7-Zip, clang and nm paths, and the broken wav_compare.py literal.
5. Widen the guard test. Write the docs table.
6. Check it all from a fresh worktree in the temp folder: one Dolphin run (`wsx.sh run`), one menu walk, one dev-Wii lab job. Then do the same on a second machine or a clean user if one is available.
7. Later: put the same files into the wii-bench kit for the other projects.

Estimate: steps 1-6 are one focused session for one agent.
