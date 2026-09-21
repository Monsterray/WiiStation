# Tooling pitfalls on this machine

## Building

- Build only with the pinned toolchain through the devkitPro MSYS2 bash:
  ```
  C:\devkitPro\msys2\usr\bin\bash.exe -lc "cd /c/projects/WiiStation && bash scripts/build.sh debug"
  ```
  (`release` for the release variant). `build.sh` pins `DEVKITPPC=/c/devkitPro/devkitPPC-r41-2`
  and libogc2. Running `make` in `deps/opengx` from another shell picks the tuxedo libogc
  headers and every link then fails with `PPCDCacheFlushAsync` undefined; fix with
  `make -C deps/opengx clean` and a normal build.
- The .elf depends only on its own objects: a change confined to `deps/opengx` rebuilds the
  archive but does not relink unless `build.sh`'s `relink_if_deps_newer()` sees it. Check the
  .dol timestamp AND size before running.
- Always build release before committing; the debug probes must compile out.
- `scripts/build.sh release` used to delete `WiiSXRX_debug.dol`: the probe-gate stamp check
  (PROBES=) ran in every mode and cleaned the debug tree whenever the last debug build had a
  non-default PROBES. Fixed 2026-09-20 (debug modes only). A run script pointed at the debug
  DOL then failed with "no such .dol"; keep a copy of the DOL you are measuring in the
  scratchpad if you build anything between runs.
- Never boot `WiiSXRX_Release.elf` in Dolphin (BSS not zeroed → crash); test with the debug .dol.

## Stale dependency archives

`deps/lightrec` and `deps/opengx` are built as archives by their own Makefiles, and the .elf
does not depend on them (see Building above). Both now emit `-MMD` dependency files, so a
header change rebuilds what it should. Before that fix (main, 2026-09-19) a worktree switched
between branches carrying different vendored Lightrec cores kept objects compiled against the
other core's headers in `libLightrecWithLog.a`; the build succeeded and the guest booted to
the PS logo and hung. If you work in an older tree, or see a hang that no source change
explains, `rm -rf deps/lightrec/obj deps/lightrec/lib deps/opengx/obj*` and rebuild before
believing the symptom.

## Shell and editing

- Bash-tool heredocs mangle backslash escapes: a `\\n` inside a quoted heredoc reaches Python as
  a real newline and silently writes broken C or Python. Use the Edit/Write tools for anything
  containing backslashes, or build the backslash from `chr(92)`. A Python `SyntaxWarning:
  invalid escape sequence` out of such a heredoc means a level of backslashes has already been
  eaten: stop and look at what landed in the file, because the damage can be invisible — a
  Windows path written as `C:\ai\...` becomes a BEL control character that greps will not match.
- The PowerShell tool refuses `Remove-Item` when the same command line mentions a
  `C:\Program Files` path; do file removals in Bash.
- Windows Python does not understand `/c/...` paths passed as arguments; give it `C:/...`.
- Files in this repo are CRLF; Python patches should read bytes, normalise, and write back the
  original line ending (see the pattern in the session scripts) or Git shows whole-file diffs.
- Background Bash commands whose output goes through `grep -v` buffer everything until exit;
  wait for the "run done" marker file line instead of polling partial output.

## Driving `scripts/dolphin_run.sh`

- **Never edit the script while a run is in progress.** bash reads a script lazily, so the
  sleeping instance resumes at a byte offset in the new text, dies with a syntax error, and
  leaves Dolphin running with the test files still staged (2026-09-19). Recover by killing that
  PID and deleting `autoinput.txt`/`settingsRX2022.cfg` (and any `autoboot.txt` you staged)
  from the SD sync folder. Since the run configures Dolphin with `-C` rather than by editing
  the INI files, there is nothing left to restore in `User/Config`. Copy the script to the
  scratchpad if you must change it mid-run.
- **Always pass the autoboot file** (fifth argument) for a game run: the user keeps their own
  `autoboot.txt` renamed to `.disabled`, so without it WiiStation sits in its menu for the
  whole run. The script prints a note when the argument is missing.
- The script sets `ConfirmStop = False` for the run so a guest crash or exit cannot leave a
  modal dialog waiting for the user, and it only ever kills Dolphin by Windows PID with
  `taskkill /F`. Keep both properties if you change it: a POSIX `kill` on the native process
  posts a window close, which raises that dialog instead of ending the run.
- Settings reach Dolphin as `-C <System>.<Section>.<Key>=<Value>` arguments, never by editing
  the INI files, so a run that dies half way cannot leave the user's Dolphin misconfigured.
  The system name for GFX.ini is `Graphics`; `-C GFX.*` is silently ignored.
- `CACHE=1` turns on `AccurateCPUCache`, which emulates the PPC data cache and so catches
  missing `DCFlushRange`/`DCInvalidateRange` -- the bugs that are invisible in a default
  Dolphin and corrupt memory on the real Wii. **Use it for release testing only** (the user's
  standing instruction, 2026-09-19): it costs roughly four times the wall clock, which is too
  slow for the iterate-on-a-hypothesis loop. Ordinary debug runs leave it off. WiiStation
  passed it on 2026-09-19, so a *new* divergence under `CACHE=1` means the change being
  released introduced a coherency bug.
- WiiStation's own FPS counter reads the emulated time base, so it still shows ~40 fps while
  Dolphin crawls. The script also turns on Dolphin's `ShowSpeed`/`ShowVPS` overlays, which
  are the host-side truth.
- `MMU=1` turns on address translation, so a wild pointer faults instead of landing somewhere
  harmless; `DOLPHIN_ARGS` appends anything else verbatim.
- `DSP_LLE=1` runs the DSP microcode for real. **Required to test WiiStation's AESND sound
  path at all**: Dolphin's default high-level DSP emulation matches microcode by hash, does
  not know libogc's AESND (CRC 8d527c50), instantiates AXWii instead and produces pure
  silence, with a modal panic dialog saying so. Dolphin ships replacement DSP ROMs in
  `Sys/GC`, so nothing else is needed.
- `AUDIO_DUMP=1` writes the mixed audio to the run directory; compare two runs with
  `scripts/wav_compare.py`. Judge by level and silence structure, not by the correlation:
  two separate emulator runs are never sample-aligned. Its `R==L at shift -1/0/+1` line
  should peak at 0 on dual-mono content (CD-XA speech is); a peak at +1 or -1 is an
  output-stage interleave fault, not a source property (case study 9). A dump from the other
  driver (`SoundHwAccel=1`, needs `DSP_LLE=1`) tells source faults from driver faults.
- `PROBES=all|light|min` on `scripts/build.sh` selects how much instrumentation the debug
  build carries (see the sub-gates in `Gamecube/perf_prof.h`). Worth about 2% of emulated
  CPU time, so reach for it only when profiling the guest; the big costs are host-side.
- `XFB_RAM=1` makes CPU-written framebuffers visible in the frame dump — the libogc exception
  screen after a guest crash, and the debug console. Resolve its addresses with
  `powerpc-eabi-addr2line -e Gamecube/WiiSXRX_debug.elf`.

## Bisecting

Old commits do not build under GCC 16/libogc2. Bisect by transplant: check the subsystem's
old files (e.g. all of `GlesGpu/`) out of a past commit over a current worktree and build that.
Valid because the plugin's interface to the core is stable.

## Local models and the code graph

- Codebase MCP project `wiistation` (re-index after large commits with
  `index_repository(mode=full)`); it cannot parse `.inc` files — grep those. Project `dolphin`
  holds Dolphin's source. To add a third source, see the next section.
- `llm_fetch_summarize`: pass `model=qwen3-coder:30b`; works on raw GitHub and psx-spx.
- `llm_court`: read every dissent; verify disputed claims against the code or a run.
- `llm_vision`: broken routing to the vLLM vision instance as of 2026-09-18; the image reader
  on PNGs is the fallback. Base64 uploads cost the caller their size in output tokens.
- Do not ask a local model to review controlled-language docs (ASD-STE100) without the rules
  and examples in context; it produced only false findings.

## Indexing another project in the codebase MCP

The server indexes a directory on disk, not a URL, and each index is a named *project* that
every query tool then takes as its `project` argument (`wiistation` and `dolphin` exist
already). Add one when a question about someone else's code would otherwise be answered from
memory or a web summary -- a vendored dependency (`deps/lightrec`, `deps/opengx` upstream),
libogc2 (what a `GX_*` call actually writes), another emulator to compare behaviour with.

1. Get the source on disk: `git clone --depth 1 <url> C:/projects/<name>-src` keeps a large
   tree small, and `git -C ... pull --depth 1` refreshes it when the version matters.
2. Index it: `index_repository(path="C:/projects/<name>-src", name="<name>", mode="fast",
   persistence=true)`. The `name` is what every later call passes as `project`, and
   `persistence` is what makes it survive a restart. Dolphin's tree (72k nodes) took a few
   minutes this way.
3. Check it: `list_projects` shows the name, `index_status` shows progress on a big tree, and
   `check_index_coverage` says whether the files you care about were parsed. Two known gaps:
   the indexer skips `docs/` directories by design, and unity-build includes (`*.inc`) are
   not parsed at all -- grep those.
4. Re-index after substantial edits to a project you own; a stale graph will confidently
   report a call site that no longer exists.

The `mcp__codebase-memory-local__*` tools may be deferred in a fresh session (load them
through the tool search first), but the same server is reachable from the shell straight
away, so indexing never has to wait for a session restart:

```
C:/ai/venvs/codebase-memory/Scripts/codebase-memory-mcp.exe cli --quiet index_repository '{"path":"C:/projects/foo-src","name":"foo","mode":"fast","persistence":true}'
```

WiiStation itself is registered project-scoped in the repo's `.mcp.json`; fuller notes on the
server live in `C:\ai\README.md`.

## Working with the user

- They test on a much slower real Wii: keep release-path cost flat, gate everything under
  `PERF_PROF` or `DISP_DEBUG`.
- Confirm before touching their Dolphin config beyond the two documented GFX.ini options, and
  say what was changed and where the backup is.
- Commit with the evidence (counters, frame numbers, run names) in the message and the
  attribution line the session requires; push only when asked.
- Write memory notes as you go (`memory/` index): a mechanism that took a session to find must
  not be rediscovered.
