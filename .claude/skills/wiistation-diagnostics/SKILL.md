---
name: wiistation-diagnostics
description: How to triage and fix WiiStation (Wii/GameCube PlayStation 1 emulator) rendering, input and timing bugs without a human at the controls — boot a game or the BIOS shell unattended in Dolphin, drive it with a scripted input file, instrument the debug build with counters and primitive traces, dump frames and VRAM, read the logs back out of the SD image, and decide from evidence instead of guessing. Use this skill whenever the task touches the OpenGX/GlesGpu plugin, the soft GPU plugins, "artifacts", "black screen", "missing textures", "transparent/opaque", "pause screen", the PSX BIOS shell, controller/pad input not working, a Dolphin frame dump or movie, perf.log or ptrace.log, autoboot.txt or autoinput.txt, or any request to reproduce, bisect or verify emulator behaviour on this machine — even if the user just says "the game looks wrong" or "can you check what the emulator does".
---

# WiiStation diagnostics

WiiStation runs a PS1 game on Wii hardware; here it runs inside Dolphin on the user's PC.
Bugs in it are almost never solvable by reading code alone: the same command stream goes
through three GPU plugins, two emulators (WiiStation's PSX, Dolphin's Wii) and a cache of
half-documented P.E.Op.S. state. The way through is a tight loop of **hypothesis → probe
in the debug build → unattended run → measured answer**, with every run carrying every
probe you can think of, because a build plus run costs about five minutes.

## The method

1. **Get a reproduction you can run without touching the keyboard.** A game: `autoboot.txt`
   in the SD sync folder. The BIOS shell: `autoboot.txt` containing `BIOS`. Button presses
   at known emulated vblanks: `autoinput.txt`. Details, timings and the settings keys that
   unattended runs need are in [references/unattended-run.md](references/unattended-run.md).
   Never drive the user's real mouse/keyboard (`DolphinControl.ps1`); the scripted route
   replaces it. `dolphin_run.sh` refuses to start while a Dolphin that uses this install's
   user directory is running (the user's own session, or a stale run: same SD image), and it
   kills only the instance it started, found by the `-u` it puts on the command line. An
   instance from another project with its own `-u` profile (Wii64's `.dev/dolphin_test.sh`
   works the same way) is left alone; the run goes ahead beside it and shares the CPU.

2. **Decide what would distinguish the hypotheses, then instrument for all of them at once.**
   The debug build (`bash scripts/build.sh debug`, only through the devkitPro bash — see
   [references/tooling-pitfalls.md](references/tooling-pitfalls.md)) has `PERF_INC`
   counters, a per-present report in `perf.log`, a primitive trace (`ptrace.log`) that can be
   armed at a scheduled vblank, a one-shot VRAM dump, and custom trace lines you can emit
   from anywhere (`perf_prim_trace(0xC?, ...)`). Catalogue and how to extend it:
   [references/probes.md](references/probes.md). One question per build wastes a cycle;
   the user asked explicitly for many diagnostics per run.

3. **Run it with `scripts/wsx.sh`** (`build debug|release`, `run NAME [--dol release]
   [--dsp] [--set K=V] [--input speech|title|FILE]`, `summary NAME...`): one command per
   step, one screen of output per step, runs kept under `.runs/` with the DOL they booted.
   It wraps `scripts/dolphin_run.sh`, which stages files, turns on Dolphin frame dumping,
   boots, kills, restores the INIs, keeps the frames you asked for, extracts
   `ptrace.log`/`vram.bin`/`atrace.log` and reads `perf.log` straight from the FAT image
   (7-Zip refuses the file after a kill), and ends with `scripts/run_summary.py`: counters,
   audio timeline and audio-dump silence runs in ten lines, plus a table when given
   several runs. Details: [references/unattended-run.md](references/unattended-run.md).
   Summarise traces with `scripts/ptrace_summary.py`, render VRAM with
   `scripts/vram2png.py`, make contact sheets with `scripts/sheet.py`, and look at single
   frames with the image reader. Frame index ≈ emulated vblank, so runs are comparable.

4. **Read the numbers before the pictures.** A counter that is zero where you expected
   thousands (pad polls, SIO starts, merged pixels) points at the mechanism faster than any
   screenshot. Then confirm on frames, and compare against ground truth: the Old Soft plugin
   (`gpuPlugin = 0`) renders the same command stream correctly but slowly, and the user's
   reference screenshots live in `examples/`.

5. **Separate the Wii-side bug from the Dolphin-side artefact.** Two Dolphin defaults have
   already masqueraded as emulator bugs (texture-cache hashing, EFB copies never reaching
   RAM). Before blaming the plugin for something that only happens under Dolphin, check
   [references/dolphin-pitfalls.md](references/dolphin-pitfalls.md).

6. **Fix, verify with the same scripted run, commit with the evidence in the message,** and
   leave the machine as you found it: test files out of the SD sync folder, INIs restored,
   no Dolphin running. Record any new mechanism in the reference files here.

## What you already know about the code (don't rediscover it)

[references/psx-gpu-notes.md](references/psx-gpu-notes.md) holds the architecture facts that
took whole sessions to establish: which globals the GX and soft plugins share, why the GX
draw origin is `PreviousPSXDisplay`, how clear-on-swap hides fills, how the VRAM readback
machinery decides what it may capture, why off-screen primitives go to the software
rasterizer, how the SIO decides a pad exists, and a GP0 command cheat sheet for reading
traces. Read it before forming a theory about display buffers, textures or input.

[references/case-studies.md](references/case-studies.md) walks through eight solved bugs as
symptom → probe → mechanism, including the wrong turns; skim it to calibrate how much theory
a run should be spent on.

The generic Wii side of this — what the GP, DSP, DMA engines, locked cache and paired-single
FPU are for, and which questions Dolphin answers honestly — is the user-level `wii-homebrew`
skill. Use it for hardware behaviour and Dolphin's own capabilities; use this one for
WiiStation's own probes, scripts and history.

`Docs/BACKENDS.md` maps every backend (CPU core, GPU, SPU, CD, pad, host input, fonts,
settings) to its vtable, its selector and its traps; start there before adding or swapping one.

## Reading, asking, checking

- **Code structure:** use the codebase MCP (`search_graph`, `trace_path`, `get_code_snippet`,
  project `wiistation`) for callers, callees and globals before grepping. Caveat: it cannot
  parse `GlesGpu/gpuVramReadback.inc` (unity-build include); grep that file. The Dolphin
  source is indexed too, as project `dolphin` (clone at `C:\projects\dolphin-src`): answer
  "what does Dolphin do with this GX command" from the source, not from memory. Any other
  source worth asking structural questions about (a vendored dependency, libogc2, another
  emulator) can be added as its own project in minutes -- recipe in
  [references/tooling-pitfalls.md](references/tooling-pitfalls.md#indexing-another-project-in-the-codebase-mcp).
- **Specs:** `llm_fetch_summarize` works on `raw.githubusercontent.com` and psx-spx pages
  (pass `model=qwen3-coder:30b`); dolphin-emu.org returns 403 to it. Use it instead of
  guessing GP0/GP1 semantics or a Dolphin option's meaning.
- **Second opinions:** `llm_court` before applying a fix derived from reasoning rather than
  measurement; read dissents, verify disputed points yourself. It confirmed several theories
  that data later killed — a panel is not an experiment.
- **Vision:** `llm_vision` was broken (bad served-model id) on 2026-09-18; the image reader
  on contact sheets and single frames is the reliable path. Base64 uploads cost the caller
  their size in tokens, so downscale to ~400px JPEG if you must upload.

## Evidence hygiene

- Every run directory gets the INIs used, `ptrace.log`, `perf.log`, `vram.bin`, kept frames
  and `sheet.png`; name directories by hypothesis (`run58/bios_visible`), not by number only.
- Quote counters and frame numbers in commit messages and in memory; a future agent should
  be able to re-run the exact script and compare.
- When a probe answers its question, remove it or leave it under `PERF_PROF` with a comment;
  the release build must stay free of debug cost (it runs on real hardware).
