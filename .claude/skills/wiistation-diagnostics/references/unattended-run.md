# Unattended runs in Dolphin

## Layout on this machine

A run uses its OWN Dolphin user directory, `<repo>\.dolphin\` (gitignored, about 1.8 GB).
Nothing is shared with the user's Dolphin except the executable and the CPU, so a run can go
while they play something else, and while another project runs its own tests. `WSX_PROFILE=`
moves it.

| Thing | Where |
|---|---|
| Dolphin | `C:\tools\Dolphin-2609\Dolphin.exe` (2609, since 2026-10-01). Its `User\` is a junction to `C:\tools\Dolphin-x64\User` (16 GB, one copy), so paths below that name `Dolphin-x64\User` are the same folder. The old build in `Dolphin-x64` is the fallback: `DOLPHIN_DIR=/c/tools/Dolphin-x64`. |
| This project's user dir (`-u`) | `<repo>\.dolphin\` |
| SD sync folder (master; synced INTO the image at launch, never back on a kill) | `.dolphin\Load\WiiSDSync\wiistation\` |
| SD image (locked while Dolphin runs) | `.dolphin\Load\WiiSD.raw` |
| Frame dumps | `.dolphin\Dump\Frames\framedump_<n>.png` (824x480) |
| Dolphin log | `.dolphin\Logs\dolphin.log`, timestamps are `MM:SS:mmm`, not hours |
| Where the USER drops games, and where the card is copied FROM | `C:\tools\Dolphin-x64\User\Load\WiiSDSync\wiistation\` |
| Staged test files kept out of the way | `C:\tools\Dolphin-x64\User\Load\WiiSDSync_paused_by_claude\` (autoinput*.txt, settings*.cfg, autoboot_*.txt) |
| Builds | `Gamecube\WiiSXRX_debug.dol` (run this), `Gamecube\WiiSXRX_Release.dol` (never boot the release .elf under Dolphin: unzeroed BSS crashes) |

## The short loop: `scripts/wsx.sh`

Use this first; it exists so that a build-run-read cycle costs three short commands and
three short outputs instead of long paths, log tails and three analysis tools.

```bash
scripts/wsx.sh build debug light          # or: build release. Prints the DOL and time, or the first errors.
scripts/wsx.sh run rc_cpu                 # debug DOL, Spyro speech script, audio dump, then the summary
scripts/wsx.sh run rc_dsp --dsp           # DSP sound path: SoundHwAccel=1 + Dolphin DSP LLE
scripts/wsx.sh run legacy --dol release --set SoundRateControl=0,SoundTempo=1
scripts/wsx.sh run title --input title --secs 120 --nodump
scripts/wsx.sh summary rc_cpu rc_dsp legacy   # one block per run and a comparison table
scripts/wsx.sh runs                       # what is in .runs/
```

- Each run directory (`.runs/NAME`, gitignored) holds `boot.dol` (a copy, so a rebuild
  between queued runs cannot change what boots), `settings.cfg`, `autoinput.txt`,
  `run.info` (dol, settings, env, commit), `run.log` and everything dolphin_run.sh collects.
- `build` refuses while Dolphin is running (a compile skews the run's timing; `FORCE=1`).
- `run_summary.py` reads `perf.log` (last `xa:`/`xamix:`/`rate:` lines), `atrace.log`
  (core speed, XA gaps, rate-control nudge and queue per 10 s) and the largest
  `*dspdump*.wav` (runs of exact digital silence 3..400 ms: count, total, histogram, when;
  longer ones are the game's own silences). `--detail` lists each silence run.
- Input scripts live in `scripts/autoinput/` (`spyro_speech.txt`: title, new game, skip the
  intro, stay in the level with the dragon speech, audio timeline from vblank 2500;
  `spyro_title.txt`: the same with the timeline from vblank 700).
- Queue several runs in one shell line (`run a ...; run b ...; summary a b`) and read one
  table; a Dolphin run cannot overlap another (SD image lock), and nothing else should run
  alongside it if timing is being measured.
- Editing sources from a tool: `scripts/patch_text.py SPEC.py` applies exact-match edits
  from a Python spec (`EDITS = {path: [(old, new), ...]}`) with the file's own line ending
  and refuses anything that does not match exactly once. Write the spec with the Write
  tool: shell heredocs lose backslashes.

## The loop: `scripts/dolphin_run.sh`

```
KEEP=3200 SHEET_STEP=150 bash scripts/dolphin_run.sh <outdir> <seconds> [autoinput] [settings] [autoboot]
```

It: backs up and edits `Dolphin.ini` (`[Movie] DumpFrames = True`), `GFX.ini`
(`[Settings] DumpFramesAsImages = True`, `PNGCompressionLevel = 1`), `Logger.ini`
(`[Logs] FRAMEDUMP = True`); stages the three files into the sync folder (autoboot only if
given, restoring the original after); runs `Dolphin.exe -b -e <debug .dol>`; sleeps; kills
Dolphin; restores everything; keeps the last `KEEP` frames in `<outdir>/frames/`; extracts
`wiistation/ptrace.log` and `vram.bin` with 7-Zip; reads `perf.log` with
`scripts/sdimage_read.py` (7-Zip reports "Data Error" on a file whose directory entry was
updated after the last cluster flush, which a kill always leaves behind); prints the last
`sio:`/`offsoft:` counter lines; builds `sheet.png` with `scripts/sheet.py`.

Timing: boot to menu ≈ 15 s, autoboot into a game ≈ 20 s; a PSX vblank is 1/60 s of emulated
time and the emulation runs near real time, so vblank N happens ≈ 20 + N/60 s after launch;
frame dumping slows Dolphin (allow 1.5x).

**Frame index is the PRESENT index, not the vblank.** Dolphin writes one PNG per XFB it is
given, so a game that swaps buffers every second vblank produces one dump per two emulated
frames and no duplicates at all (measured on Crash 3's title: 1215 presents over 2652
vblanks, 0 of 118 consecutive dumps identical; `perf.log`'s `--- perf frames=N ---` is the
present count and `wall: vblanks=` the vblank count). Treating a dump index as a vblank
silently doubles every animation period you measure. The debug build's EC trace entries
carry the vblank of each present, which is also how you tell an even cadence from a
stutter. Two runs are comparable frame-for-frame only while both present at the same rate;
a debug DOL reaches a given scene at a similar present index but a much later wall time.

Only one Dolphin may run. `dolphin_run.sh` refuses to start while a `Dolphin.exe` exists and
kills only the PID it launched; earlier versions killed by image name and closed the user's own
session twice. The symptom of a collision is `Rename failed ... Access is denied` in dolphin.log
and a boot without SD card (no autoboot, no frames). The frames of a previous run must be moved
out before the next.

## Files that drive the run

**`autoboot.txt`** (SD sync folder, read after settings load): line 1 the ISO folder, line 2
the file name (matched with `strcasestr`, so it must be unique); or just `BIOS` to run the
BIOS shell via the menu's Execute Bios path. Delete it (or pass an empty file as the run
script's autoboot argument, `WiiSDSync_paused_by_claude/autoboot_none.txt`) to boot to the
WiiStation menu itself — that is how menu rendering (fonts, buttons) gets frame-dumped. The
menu cannot be navigated by autoinput.txt (it reads the host pads, not the PSX port), so only
the first screen is reachable unattended. A savestate-
anchored Dolphin movie (`.dtm` with `from_savestate=1`) cannot replace it: it restores the
recorded binary, so code changes are invisible under it.

**`settings.ini` (was settingsRX2022.cfg)** (bare `key = value`, unlisted keys keep compiled defaults — NOT the
user's values; per-game files in `wiistation/settings/<CdromId>.cfg` override it when the game
has one). Keys that unattended runs need:

```
gpuPlugin = 2        # 0 Soft Fast (ground truth), 1 Soft Timed, 2 OpenGX
FPS = 1              # the user wants the FPS overlay visible while runs play; it lands in the frames too
PadType1 = 1         # sio.c polls a port only when padType[0] != 0
PadAutoAssign = 0    # otherwise auto-assign resets PadType1 to 0 when no host pad exists
BiosDevice = 1       # for BIOS runs; SCPH1001.BIN in wiistation/bios/
```

All keys are documented in `SETTINGS.md`.

**`autoinput.txt`** lines `<vblank> <hex PSX mask>` (Start 0008, Cross 4000, Circle 2000,
Triangle 1000, Square 8000, Up 0010, Right 0020, Down 0040, Left 0080, Select 0001), plus
`trace <vblank>` (up to 8; arms a 16-present primitive-trace episode) and `dump <vblank>`
(one VRAM dump + a perf report at that moment). Parsed when the pad plugin opens
(`autoinput_load()` in `Gamecube/PadWiiSX.c`); a script with presses also makes port 1 report
a connected digital pad. Known timelines: Spyro title Start ~1400-1500, level pause ~2100 or
~3634 with the recorded movie; BIOS shell menu visible from ~600, X at 1500 opens Memory
Card, Triangle back, Down, X opens CD Player.

**Movies → scripts:** `scripts/dtm2autoinput.py MOVIE.dtm --offset N [--trace-after-start]`
decodes a Dolphin `.dtm` (8-byte pad polls interleaved with Wiimote records, 2 polls per VI)
into autoinput lines; find the offset once from the first Start press.

## Keeping a run as a baseline

Runs worth comparing against later go into `baselines/` with `scripts/baseline_add.py <run dir>
--id <date>_<game>_<variant> --game ... --purpose ... --settings <staged cfg>` (see
`baselines/README.md`). It keeps perf.log, the run log, the settings and a notes page in git,
the audio (as FLAC) and the contact sheet under `baselines/media/` outside git, and appends
the key metrics to `baselines/index.csv`. Compare with `perf_compare.py`, `wav_compare.py`,
`wav_spectrum.py` (both audio tools read FLAC). The scratchpad is per session and was 9 GB of
frame PNGs before this existed: file what matters, then delete `frames/` directories.

## Reading the results

- `python scripts/ptrace_summary.py <outdir>/ptrace.log [--big AREA] [--show N]` — per-present
  entry counts, control commands (fill / draw area / display), big primitives, semi-transparent
  ones, one line per VRAM→CPU read with mapping, capture result, merged pixels, state bits,
  and the screen re-uploads (EA/EB) and presents (EC).
- `python scripts/frame_cycle.py <frames dir> FIRST LAST [--period N] [--grid] [--map OUT.png]`
  — what changes between consecutive dumps and where: changed pixels, bright→dark against
  dark→bright (equal both ways is motion, one-sided is content being lost), the same count
  at six thresholds so a threshold artefact is obvious, a per-cell map of which part of the
  picture moves, and an image with the changing pixels in red. Reach for it before
  believing any "N things vanish per frame" count taken at one brightness threshold.
- `python scripts/dot_flicker.py <frames dir> FIRST LAST --exclude x0,y0,x1,y1` — tracks
  the small bright dots (a starfield of GP0 68 one-pixel rectangles) frame to frame and
  reports moved / dimmed / lost / new per third of the picture. This is what aggregate
  change counts cannot do: see ~90 dots blinking under ~250 pixels of animation. Compare
  plugins over the same present range with the same exclusion rectangle.
- `python scripts/vram2png.py vram.bin out.png [--crop X,Y,W,H] [--scale N]` — the 1024x512
  VRAM; display buffers at x<640 (or 512), texture pages and off-screen scratch to the right.
- `python scripts/sheet.py out.png frames_dir START STEP END [cols] [w]` — contact sheet with
  frame numbers burned in; then open single frames with the image reader.
- `python scripts/sdimage_read.py WiiSD.raw wiistation/<file> out` — any file from the image.
- Compare plugins by running the same script under `gpuPlugin = 0` and `2` and diffing
  `vram.bin` (texture pages identical ⇒ CPU side fine; display buffers differ by design).

## Restoring the user's setup

The user's play window (`scripts/wiistation_play.sh`, the shared profile without `-u`) starts
with folder sync OFF when `scripts/sd_sync.py` finds none of the user's SD files changed (it
copies WiiStation's own files from `WiiSD.raw` back into the folder first): WiiStation's writes
there live in the image until the next start through the script. Never start that Dolphin
another way after a session, or its folder sync repacks over them.


The script restores INIs and removes the staged files; `autoboot.txt` stays because the
user's own movies depend on it. Leave `WiiSDSync_paused_by_claude/` as the parking place.
Two permanent GFX.ini changes are deliberate (see dolphin-pitfalls.md).

## The bench Wii (real hardware, unattended)

The dev Wii is at 192.168.8.213 in the Homebrew Channel. `python scripts/wii_lab.py NAME
CHAINFILE` sends the DOL with wiiload and `lab=<PC>:4300`; `Gamecube/lab_net.c` fetches the
chain and its input scripts, runs it, sends the logs back into `.runs/NAME/` and exits to
HBC, ready for the next one. A crash returns to HBC after 10 s with no results (exit 3).
It needs an inbound firewall allow for TCP 4300 (the network is Public). Speed numbers
from it are the real ones: Dolphin cannot show cache or locked-cache effects.

Rules and fixes learned on the bench (2026-09-29/30):
- **Always through the queue**, never wiiload directly:
  `python C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation --name "..." --timeout S -- CMD`,
  then wait on `C:/tools/wii-bench/queue/done/<id>.json` (its `.log` next to it). Other
  workstations share the Wii **through the lease server `http://homeserver.local:4310`**
  (the user's rule, 2026-09-30): the dispatcher takes the lease before each job.
  `status` must show a `lease http://homeserver.local:4310: ...` line; if it does not
  (no `C:/tools/wii-bench/server`), run
  `python C:/projects/hbc-reborn/tools/wii-bench/wiibench.py setup --server http://homeserver.local:4310`.
  Never talk to the Wii (wiiload, hbc.py) outside a queue job.
- **Queue commands with the full python path** (`C:/Python312/python.exe ...`). The
  dispatcher's `bash` is WSL's `C:\Windows\System32\bash.exe`, which has no python: a
  `bash job.sh` job fails at once with `python: command not found`.
- **Never queue `hbc.py get` without the user's go-ahead**, and never a large file: it crashed HBC 1.4.1
  and 1.5.0. On HBC 1.8.6 files up to 28 KB worked (2026-09-30, `scripts/wii_getfiles.py`: size guard,
  smallest first, waits for HBC).
  Logs come back only through wii_lab.py's upload. `hbc.py crash [--clear]` is a small
  status request and is safe.
- **A DSI used to leave no crash report**: Gamecube/vm/vm.c hooks EX_DSI after the HBC
  agent and its dsihandler.s passed every foreign DSI to libogc's default entry, skipping
  the agent. Fixed 2026-09-30 (vm_dsi_next: the entry VM_Init found). Proved on the Wii with
  the debug input command `crashtest <vblank>` (stores to 0xFFFFFFF4, a DSI like the
  2026-09-29 crash): crash screen, HBC after 10 s, `hbc.py crash` shows DSI + backtrace;
  resolve with `powerpc-eabi-addr2line -f -C -e Gamecube/WiiSXRX_debug.elf ADDR...` (the ELF
  of the DOL that ran). Re-run a crashtest after touching exception or VM code.
- **Crash hunts:** `scripts/wii_crash_job.py NAME CHAIN DOL SECS` clears HBC's crash
  report, runs the chain, then reads the report. A crash leaves libogc2's crash screen up
  until someone presses RESET, so ask the user to be at the Wii first. One game per job
  tells which game crashed; if every game alone is clean, run them in one boot.
- **"No crash reported" but the Wii went back to HBC** (2026-10-01): the death was not one the
  agent hooked. Since agent 1.9 (deps/hbc_agent = hbc-reborn 52eeebd, HBC 1.9.1):
  - a hang: `ws_alive()` (= `hbc_agent_alive()`) per PS1 vblank, menu frame and lab chunk;
    the agent's watchdog reports `HBC_CRASH_HANG` after 60 s and pauses during its HOME
    overlay. Test with the input line `hangtest <vblank>` (`scripts/chains/hangtest.txt`);
  - Lightrec's guest segfault (code 0x82), no sound driver (0x83) and a CPU core that could
    not start (0x84, Lightrec's state not allocated): `ws_fatal(code, reason)` sends the lab
    results, then `hbc_agent_fatal()`;
  - an exception with MSR[RI] clear still goes past the agent: libogc's vector code jumps
    straight to `default_exceptionhandler`. WiiStation wraps `c_default_exceptionhandler`
    (`-Wl,--wrap` in Makefile_Wii*) and writes the block itself (reason "past the agent's
    hook");
  - the agent keeps the app's last 4 KiB of output: `hbc.py lastlog`; wii_crash_job.py saves
    it to `.runs/NAME/lastlog.txt` when the run failed. The kept log and the version-2
    block need HBC 1.9 on the Wii.
  `wii_crash_job.py` passes `--elf` when the DOL copy has its ELF beside it.
- `wii_lab.py` keeps the files that arrived when the Wii stops sending mid-upload, and writes
  `incomplete:` in run.info (exit 1).
- A job's output is buffered until wii_lab.py exits: an empty `running/<id>.log` does not
  mean it is stuck.
- Before the queue fix of 2026-09-30 (hbc-reborn's dispatcher now reports an agent app as
  "busy or off"), `wiibench.py status` said "in HBC (free)" whenever TCP 4299 answered, and
  an app with the HBC agent in it (WiiStation since 7a38145) answers there too: WiiStation
  still running looked free. A job queued behind it then fails with `HBC did not take the DOL` (wiiload
  refused). Ask the user what the TV shows before trusting "free" after a job that did
  not return. Worse, another workstation's queue took a running WiiStation chain for idle
  HBC and its `hbc.py run` made the agent exit it mid-chain: games "played as normal", then
  HBC, no results, perf.log cut off mid-game (2026-09-30, twice). Fix: lab mode starts the
  agent with `no_network` (Gamecube/hbc_home.c), so a running chain does not answer 4299.
  The queue-side fix (treat an agent app as busy) belongs to hbc-reborn's wiibench.py.
  A chain that stops mid-game with no crash report: suspect an outside exit first.
- Other agents and workstations can take the Wii between two of our jobs. A small request
  job (ls, crash, a tiny get) should retry for minutes rather than fail on the first
  timeout (pattern: `scripts/wii_getfiles.py`, `ls` every 20 s for 15 min).
- When a job returns no results, read what the card has (`scripts/wii_getfiles.py`, with the
  user's go-ahead): `lab.log` names the last lab step (a chain that ends logs nothing more
  than "starting the chain", then connects to send results; `lab_report()` gives up
  silently if that connect fails), `perf.log` has a block per 1800 presents and per game end,
  so its last `time:` line dates the stop, and the size of each `vram_NN.bin` (0 = emptied,
  never rewritten) says which chained game did not finish.
- Deep builds for A/Bs: `FORCE=1 bash scripts/wsx.sh build debug deep` (the build refuses
  while a Dolphin run is going), then copy the DOL to the scratchpad per variant; two DOLs
  from one tree differing in one file make a clean hardware A/B (`wsx.sh compare A B`).

## Installing on the bench Wii (2026-10-01)

`scripts/wii_install.py` (as a queue job) syncs the release DOL to `sd:/apps/WiiStation/boot.dol`
with meta.xml, icon.png and the `wiistation/controllers`, `lang` and `fonts` folders; it deletes
nothing but `autoboot.txt` with `--remove-autoboot`. Before 5.2.0 a lab run left its chain in
`sd:/wiistation/autoboot.txt`, so the next start from HBC ran that chain and switched the Wii off;
lab mode now deletes the file once it has read it. A lab run still writes the lab's base settings
over `sd:/wiistation/settings.ini`.
