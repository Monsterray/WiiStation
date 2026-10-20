# Dolphin pitfalls (things that looked like emulator bugs and were not)

## Two graphics options change what OpenGX shows

Both are set in the user's `GFX.ini` on purpose; put them back if a Dolphin reset loses them.

- **Texture Cache accuracy = Safe** (`[Settings] SafeTextureCacheColorSamples = 0`). Dolphin
  detects texture changes by hashing texture memory at 128 sample points ("Fast"). The GX
  plugin updates small sub-rectangles of 256x256 pages in place (`glTexSubImage2D`); the
  sampled hash misses most of them, so Dolphin keeps showing stale pages: striped grass,
  wrong plaza, missing faces, popping. Eighteen runs of in-emulator counters could not see it
  because the Wii side was right. Real hardware has TMEM and never had the problem.
- **Store EFB Copies to Texture Only = off** (`[Hacks] EFBToTextureEnable = False`). Default
  on keeps every `GX_CopyTex` on the host GPU, "bypassing system memory" (Dolphin's own
  tooltip in `Source/Core/DolphinQt/Config/Graphics/HacksWidget.cpp`). Anything the CPU reads
  back from an EFB copy — the VRAM readback machinery, hence Spyro's pause frame — reads the
  buffer's memset zeros. "Defer EFB Copies to RAM" (default on) is fine: `BPStructs.cpp`
  flushes deferred copies on PE finish/token, which `GX_DrawDone`/`GX_SetDrawSync` issue.

## Frame dumps

Master switch is `Dolphin.ini [Movie] DumpFrames = True` (a GFX key or `-C` override does
nothing), plus `GFX.ini [Settings] DumpFramesAsImages = True` and `PNGCompressionLevel = 1`.
Dumps land in `User\Dump\Frames\framedump_<n>.png` at 824x480, one per emulated frame. Equal
ordinals are the same game frame only for the same DOL: between two builds the numbering can
be one apart (5167.. vs 5168..) with the same present count (`efbloss: pres: total=`). Pair the
kept frames by position and try offsets -1/0/+1 before believing a whole-frame difference
(2026-10-20). Zero frames with the switches set means Dolphin
presented nothing: the emulated title has no video output (the BIOS autoboot without the
Func_PlayGame preamble did exactly that — black window, no dumps, logs otherwise normal).

## Movies

`.dtm` recorded from a running WiiStation is savestate-anchored (`from_savestate` at 0x0C,
companion `.dtm.sav`): it restores the whole RAM image including the emulator binary, so a
new .dol cannot be tested under it, and replaying with the flag cleared makes Dolphin demand a
disc change. Decode the presses with `scripts/dtm2autoinput.py` and use autoinput.txt instead.

## Process and files

- The SD image `User\Load\WiiSD.raw` is exclusively locked while Dolphin runs; the sync folder
  is copied INTO it at launch and never back on a kill. Deleting the image makes Dolphin
  rebuild it from the folder. The rebuild writes `WiiSD.raw.xxx` and renames it over the image;
  if anything holds the image open at that moment (an indexer, a reader you just ran) the log
  says `Rename failed ... Access denied` then `Could not create file ... aborting`, and the boot
  runs with NO SD card: no autoboot, no settings, nothing dumped. Just rerun.
- Killing Dolphin leaves the last-written file with a directory size ahead of its flushed
  clusters: 7-Zip says "Data Error". `scripts/sdimage_read.py` walks the FAT chain and returns
  what is there.
- `taskkill //IM Dolphin.exe //F` kills every instance, including the user's and other
  projects'. `dolphin_run.sh` therefore starts its Dolphin with an explicit `-u <User dir>`,
  finds instances by command line (PowerShell `Get-CimInstance Win32_Process`), refuses to
  start only when one shares its user directory, and kills only its own PID. A `-u` that
  points elsewhere is another project's profile with its own SD image: leave it alone.
- `dolphin.log` timestamps are minutes:seconds:ms; old FRAMEDUMP lines from an earlier session
  stay in the file — filter by frame numbers, not by hour.
- Dolphin's own source is indexed in the codebase MCP as project `dolphin`
  (`C:\projects\dolphin-src`, shallow clone). `raw.githubusercontent.com` files fetch fine with
  `llm_fetch_summarize`; `dolphin-emu.org/docs` returns 403 to server-side fetches.

## A crashed or exiting guest pops a dialog

When WiiStation crashes, libogc prints its exception screen and later reloads (`SYS_ResetSystem`); when it exits, the same call runs. Dolphin turns that into a host stop request and, with `[Interface] ConfirmStop = True` (its default), shows "Do you want to stop the current emulation?" and waits for a human, in batch mode too. `scripts/dolphin_run.sh` now writes `ConfirmStop = False` into `Dolphin.ini` for the run (restored afterwards) so the emulation just ends. If you drive Dolphin by hand for a run, set it yourself first.

The exception screen itself is invisible under Dolphin's default `XFBToTextureEnable = True`: only GPU-made XFB copies are presented, never a CPU-written framebuffer. Run with `XFB_RAM=1` (the script sets `[Hacks] XFBToTextureEnable = False`) and the crash dump with PC, LR and the stack trace appears in the frame dump; resolve the addresses with `powerpc-eabi-addr2line -e Gamecube/WiiSXRX_debug.elf`.

## Getting more out of Dolphin (what works unattended, what does not)

Checked against the source (`C:/projects/dolphin-src`: tag `2609` = the installed build;
`origin/master` is fetched too, read it with `git -C C:/projects/dolphin-src grep PATTERN
origin/master -- Source/Core` for anything newer). 2026-10-20.

- **Counts: Dolphin; times: the Wii.** Every guest-side counter in `perf.log` is the same in
  Dolphin and on the Wii (the guest runs the same command stream), so Dolphin answers "how many
  captures, texture loads, rects scanned, pixels merged" exactly and repeatably. It does not
  model GX time, so never rank costs by Dolphin's wall clock.
- **Windows: `report <vblank>` lines + `scripts/perf_windows.py`.** Put reports at the edges of
  the stretches `vtl_view.py` lists (from a bench run) and compare the slow windows' counters
  with the rest; time counters are only meaningful from a Wii run, event counters from either.
- **The Wii CPU's own profile: `hprof` under Dolphin.** Dolphin emulates the
  performance-monitor exception, so `wsx.sh build debug hprof` samples there too, by Dolphin's
  instruction-count estimate (no cache misses, ~1.9x a Wii's samples). `hprof <from> <to>`
  input lines (up to 8) restrict it to those vblanks, so one stretch can be profiled alone
  (`scripts/chains/tiger_temple_spots.txt`). Keep the ELF: `cp Gamecube/WiiSXRX_debug.elf` next
  to the DOL copy; `hprof_view.py hprof_01.bin --elf X.elf`.
  **Broken in libogc v3 builds (2026-10-20):** ~21 s into a `--fast` run the HBC agent's
  `lastlog_write` runs and its checksum loop runs away (PC 0x8016bb20, reads from 0x14000000
  upward): 100 million MMU warnings, dolphin.log 28.5 GB in 25 min, no hprof file. Open task
  "Fix hprof (libogc v3) runaway under Dolphin". Until it is fixed, do not run hprof builds,
  and check `stat -c %s .runs/NAME/dolphin.log` after any run that took far longer than usual.
- **JIT block profiling: not unattended.** Dolphin can record per guest block run counts and
  estimated cycles (`[Interface] DebugModeEnabled` + `[Debug] JitEnableProfiling`), but the
  only way to write them out is the debugger's menu item "Write JIT Block Log Dump"
  (`DolphinQt/MenuBar.cpp OnWriteJitBlockLogDump`), in 2609 and in master. No command-line or
  batch trigger: do not chase it; `hprof` gives the same answer.
- **Graphics statistics: on screen only.** `VideoCommon/Statistics.h` counts draw calls,
  primitives, rasterized pixels, DrawDones and EFB peeks per frame, but only the overlay
  (`Graphics.Settings.OverlayStats`) shows them; nothing writes them to a file. Count in
  WiiStation's own probes instead.
- `-P <dir>` (perf map, `Common/JitRegister.cpp`) is for a Linux host profiler: no use here.
