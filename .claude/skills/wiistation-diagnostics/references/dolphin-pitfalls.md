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
Dumps land in `User\Dump\Frames\framedump_<n>.png` at 824x480, one per emulated frame, so equal
ordinals across runs are the same game frame. Zero frames with the switches set means Dolphin
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
