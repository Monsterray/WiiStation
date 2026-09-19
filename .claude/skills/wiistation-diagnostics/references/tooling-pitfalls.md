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
- Never boot `WiiSXRX_Release.elf` in Dolphin (BSS not zeroed → crash); test with the debug .dol.

## Shell and editing

- Bash-tool heredocs mangle backslash escapes: a `\\n` inside a quoted heredoc reaches Python as
  a real newline and silently writes broken C or Python. Use the Edit/Write tools for anything
  containing backslashes, or build the backslash from `chr(92)`.
- The PowerShell tool refuses `Remove-Item` when the same command line mentions a
  `C:\Program Files` path; do file removals in Bash.
- Windows Python does not understand `/c/...` paths passed as arguments; give it `C:/...`.
- Files in this repo are CRLF; Python patches should read bytes, normalise, and write back the
  original line ending (see the pattern in the session scripts) or Git shows whole-file diffs.
- Background Bash commands whose output goes through `grep -v` buffer everything until exit;
  wait for the "run done" marker file line instead of polling partial output.

## Bisecting

Old commits do not build under GCC 16/libogc2. Bisect by transplant: check the subsystem's
old files (e.g. all of `GlesGpu/`) out of a past commit over a current worktree and build that.
Valid because the plugin's interface to the core is stable.

## Local models and the code graph

- Codebase MCP project `wiistation` (re-index after large commits with
  `index_repository(mode=full)`); it cannot parse `.inc` files — grep those. Project `dolphin`
  holds Dolphin's source.
- `llm_fetch_summarize`: pass `model=qwen3-coder:30b`; works on raw GitHub and psx-spx.
- `llm_court`: read every dissent; verify disputed claims against the code or a run.
- `llm_vision`: broken routing to the vLLM vision instance as of 2026-09-18; the image reader
  on PNGs is the fallback. Base64 uploads cost the caller their size in output tokens.
- Do not ask a local model to review controlled-language docs (ASD-STE100) without the rules
  and examples in context; it produced only false findings.

## Working with the user

- They test on a much slower real Wii: keep release-path cost flat, gate everything under
  `PERF_PROF` or `DISP_DEBUG`.
- Confirm before touching their Dolphin config beyond the two documented GFX.ini options, and
  say what was changed and where the backup is.
- Commit with the evidence (counters, frame numbers, run names) in the message and the
  attribution line the session requires; push only when asked.
- Write memory notes as you go (`memory/` index): a mechanism that took a session to find must
  not be rediscovered.
