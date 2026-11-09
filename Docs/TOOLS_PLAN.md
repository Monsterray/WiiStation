# Plan: set up WiiStation on a clean PC

Status: PLAN for Monty's approval (2026-11-09, second version). Nothing below is implemented yet.
The first version only changed how scripts find tools. This version also makes the repo
self-contained, so a person who has never seen WiiStation or Wii homebrew can set it up alone.

## The goal

A new person reads one file, `SETUP.md`, and gets as far as they want in three levels:

| Level | They get | They install | Command |
|---|---|---|---|
| 1. Build | `boot.dol` | devkitPro | `scripts/setup.sh`, then `scripts/wsx.sh build` |
| 2. Dolphin | WiiStation running on the PC | + Python, Dolphin | `python scripts/tools.py setup`, then `scripts/wsx.sh run` |
| 3. Real Wii | WiiStation sent to their Wii and run | + a Wii with the Homebrew Channel | `scripts/wsx.sh wii run` |

Each level works without the next one. Nothing else is needed for these three levels: no other
repository, no queue server, no capture card, no folder in a special place.

## What a new person cannot get from the repo (and never will)

- **devkitPro, Python and Dolphin.** Separate installs. `SETUP.md` links each one.
- **A PS1 BIOS and games.** These cannot be shipped. Level 2 boots with the built-in HLE BIOS;
  `SETUP.md` says which games need a real BIOS file and where to put it.
- **A Wii with the Homebrew Channel.** Level 3 only.

Everything else that is our own code goes into the repo.

## What is wrong now

**1. Our own code lives outside the repo.**

| Needed for | Lives in | Today |
|---|---|---|
| sending a DOL to a Wii (level 3) | `hbc.py`, in the hbc-reborn repo | `../hbc-reborn` beside the project. Breaks in every worktree. |
| every Wii run (level 3) | the wii-bench queue + lease server on homeserver | `wii_lab.py` refuses to run without a queue job. |
| movie recording | dolphin-movie repo | `../dolphin-movie` |
| PS1 accuracy tests | `C:/tools/ps1-tests` | env var only |
| game library | `C:/wiistation-lib` | hard-coded in 9 scripts |

**2. Scripts find tools in six different ways:** environment variables, `~/.wii-paths.json`,
`scripts/wii_paths.py` (214 lines), `find_python.sh` / `find_dolphin.sh`, `../sibling` paths,
and hard-coded `C:/` paths. That makes 28 environment variables. Hard-coded `ffmpeg`, 7-Zip and
clang paths too (`wav_compare.py:39`'s is corrupted).

**3. The docs disagree.** README says devkitPPC r41-2 + libogc2. CONTRIBUTING says current
devkitPPC + libogc v3. The truth: since 5.13.0 the default build is pacman's current devkitPPC
+ libogc v3. The r41-2 install and the prebuilt zips at the repo root (`lightrec+Libogc2.zip`,
`libogc_1.8.16_ipcfix.zip`, `libSDL.a`) exist only for the old `LIBOGC=2` build, "kept for one
release" in 5.13.0. We are at 5.19.

## The new way

**1. Our own tools come with the repo.**
- `hbc.py` (2273 lines, standard library only) is copied to `scripts/vendor/hbc.py`, with the
  hbc-reborn commit it came from in its first line. Same recipe as `deps/hbc_agent`: an update is
  a copy and a commit. A test fails if the file differs from that commit's copy, so nobody edits
  it here by mistake.
- The queue, the capture card, movie recording, ps1-tests and chdman become **extras**: one
  section of `SETUP.md` each, after the three levels. A script that needs an extra stops with one
  line that names the extra and its section.

**2. One file per PC for tool paths, `~/wii-tools.env`** (plain `NAME=path` lines, as in the first
version). Required names by level:

| Level | Names |
|---|---|
| 1 | `DEVKITPRO` |
| 2 | + `PYTHON`, `DOLPHIN`, `GAME_LIBRARY` |
| 3 | + `WII_IP` |
| extras | `WII_BENCH`, `WIICAP`, `DOLPHIN_MOVIE`, `PS1_TESTS`, `CHDMAN`, `FFMPEG`, `SEVENZIP` |

Bash reads it with `. ~/wii-tools.env`; Python with a 10-line function. An environment variable of
the same name wins, for a one-off. The file is in the home folder, so every worktree and every
project sees it.

**3. `scripts/tools.py` (about 80 lines)** replaces `wii_paths.py`, `find_python.sh` and
`find_dolphin.sh`.
- `tools.py setup` looks in the usual install places, writes the lines it finds, asks for the
  rest, and creates the `GAME_LIBRARY` folders (`bios/`, `games/`) with a short README in each.
- `tools.py check` prints each name, its path, whether it exists, and which levels work now.
- No script guesses a path. A missing name stops the script with one line, for example:
  `DOLPHIN is not set: run python scripts/tools.py setup (SETUP.md, level 2)`.

**4. Level 3 without a queue.** `wsx.sh wii run` sends the DOL with the vendored `hbc.py` to
`WII_IP` and reads the logs back.
- On a PC with `WII_BENCH` set (Monty's), Wii runs still go **only** through the queue. That rule
  does not change.
- Without `WII_BENCH`, the run goes straight to `WII_IP`.
- The protected-Wii guard stays in code (`wii_targets.py`): the production Wii and the LRWii are
  refused whatever the file says. A missing or wrong file can never unprotect them.

**5. One setup doc.** `SETUP.md` holds the three levels, the extras and the table of names.
README's "Compilation information" becomes one line pointing to it. CONTRIBUTING keeps only how to
work on the code (branches, tests, style) and points to it too.

**6. Tests guard it.**
- `tests/test_workstation_paths.py` is widened: it fails on any absolute path, `../sibling` path
  or LAN address in tracked scripts (today it misses `/c/wiistation-lib`, `/c/devkitPro`,
  `Program Files` and `192.168.x`). `wii_targets.py` is the one allowed place for addresses.
- The vendored `hbc.py` check from point 1.

**7. Proof: a clean-PC run.** Windows Sandbox (built into Windows 11 Pro) starts empty each time.
An agent follows only `SETUP.md`, from a fresh clone, through levels 1 and 2, and writes down
every place it had to guess. Each guess becomes a fix to the doc or a script. Level 3 is checked
on the dev Wii from a fresh worktree, with `WII_BENCH` unset in that one shell (the queue still
holds the lease; see question 2).

## What stays as it is

- Folder layout on Monty's PC. The tools file names the paths, so nothing moves.
- The queue's own variables (`WII_BENCH_IP` and the rest): the queue sets them per job.
- `deps/` as it is (already vendored).

## Steps (each one small and testable)

1. `scripts/tools.py` + `~/wii-tools.env` on this PC; `tools.py check` passes.
2. Vendor `hbc.py`; point `wii_lab.py`, `wsx.sh` and the bench scripts at it; add its check test.
3. Move the bash scripts to `. ~/wii-tools.env` (wsx.sh, dolphin_run.sh, movie_capture.sh,
   wiistation_play.sh, profile.sh, padtest_dx.sh, menu_pages_walk.sh, the build scripts). Delete
   `find_python.sh` and `find_dolphin.sh`.
4. Move the Python scripts to `tools.get("NAME")`. Delete `wii_paths.py` and `~/.wii-paths.json`.
   Fix the hard-coded ffmpeg, 7-Zip, clang and nm paths and the `wav_compare.py` literal.
5. `wsx.sh wii run`: the direct path when `WII_BENCH` is not set.
6. Widen the guard test. Write `SETUP.md`; trim README and CONTRIBUTING.
7. Check on this PC from a fresh worktree: one `wsx.sh run`, one menu walk, one dev-Wii queue job.
8. The Windows Sandbox run (point 7 above). Fix what it finds.
9. Later: the same `tools.py` and `~/wii-tools.env` in the wii-bench kit, for Wii64, hbc-reborn and
   d2x-cios.

Estimate: steps 1-7 are one focused session for one agent; step 8 is a second short one.

## Questions for Monty

1. Drop the old `LIBOGC=2` build now (the r41-2 install, the three prebuilt files at the repo root,
   and the `LIBOGC=2` branches in the Makefile)? It was kept "for one release" in 5.13.0. Without
   it, level 1 is one pacman command. Yes / keep it for now.
2. For the level-3 check, may the agent run one direct (no-queue) job on the dev Wii, holding the
   queue lease by hand so nothing else runs at the same time? Yes / test it only on a newcomer's
   Wii later.
3. `hbc.py`: a copy in the repo (recommended: one file, no network at clone time) or a git
   submodule of hbc-reborn (always exact, but needs `git clone --recursive`)?
