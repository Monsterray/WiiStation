# PS1 test programs (PS-X EXE) and host A/B harnesses

How to run a PS1 test program in WiiStation unattended and read its verdict, and how to
compare two versions of a core file on the PC. Every step below cost a run to find.

## Running a PS-X EXE

- Put the `.exe` in its own folder on the automated card:
  `.dolphin/Load/WiiSDSync/wiisxrx/isos/<name>/<file>.exe`. A chain entry is the same as for
  a game: `VBLANKS [input] KEY=V...` / `sd:/wiisxrx/isos/<name>` / `<file>.exe`.
- **Use the interpreter: `Core=1`.** Lightrec shuts Dolphin down about 2 s after booting an
  EXE (seen 2026-09-30, not traced; games are fine). The interpreter runs at about 0.29x with
  `FPS=0`, so 30000 vblanks take about 29 minutes of wall time.
- Loading an EXE needs `biosFileInit()` before `SysReset(); Load(file);`
  (`Gamecube/GamecubeMain.cpp`); without it the HLE BIOS jumps to 0 through a NULL
  `biosFile_readFile`.
- **The screen needs the soft GPU:** `--set gpuPlugin=0` on the `wsx.sh chain` command line.
  A `gpuPlugin=0` on the chain line itself does not apply (a plugin change resets the game;
  the run still uses OpenGX -- vramio.log `F1` lines prove it). Under OpenGX the AmiDog
  screens stay black: the program draws into the displayed buffer and OpenGX never presents
  (`iDrawnSomething=0`, vramio `F1 ... 0 8`), an open present-path bug.
- **See the screen:** input-script line `dump <vblank>` plus `--env XFB_RAM=1`, then
  `python scripts/xfb2png.py .runs/NAME/xfb.bin out.png`. Crop and enlarge with PIL to read
  the colours. A chain run turns frame dumps off; `dump` still works.
- Console text: debug builds copy SysPrintf (the HLE BIOS printf/puts) to
  `sd:/wiisxrx/tty.log`, and `dolphin_run.sh` extracts it into `.runs/NAME/tty.log`. Most test
  programs print only a banner there and put their results on screen.
- Reading a program: RAM from a save state (below), then `python scripts/mipsdis.py RAM OFF:PC:N` (hex file offset, guest PC, word count;
  a small MIPS disassembler) and `strings`. perf.log `pcs:` lists the hottest guest PCs:
  a hot delay loop next to a SIO pad read (`01 42 00 00 00` to 0x1F801040) is a menu
  waiting for input.

## Save states as a data source

- `state save NAME <vblank>` in the input script writes `sd:/wiisxrx/states/NAME.st`
  (perf.log `state: save NAME at vblank V ok`). Dolphin's folder sync-back can fail
  (dolphin.log `Failed to sync SD card with folder`): then the file is only inside the image.
  Read it with `python scripts/sdimage_read.py .dolphin/Load/WiiSD.raw wiisxrx/states/NAME.st OUT`.
- The file is gzip. Decompressed layout (`misc.c` SaveState): 32-byte header, 1-byte HLE
  flag, 36 KB of zeros, then RAM (2 MB) at offset 36897, BIOS (512 KB), scratchpad/HW
  (64 KB), psxRegs (to `gteBusyCycle`), GPUFreeze_t (1032 bytes; for a 2026-09-30 build it
  started 8988 bytes after the HW block), then VRAM (1024x512x2, little-endian 15-bit).
  Find GPUFreeze_t by its version word `00 00 00 01` followed by a sane GPUSTAT.

## AmiDog psxtest_gte (v1.4)

- Menu: `START = RUN ALL, X = RUN, ^ = TOGGLE, L1 = NONE, R1 = ALL`. **Select does nothing.**
  An 8-vblank press is missed (one menu loop is a long delay plus a redraw): hold Start
  about 300 vblanks. Input script: `300 0008` / `600 0000` / `dump 29900`
  (`scripts/autoinput/gtetest_full.txt`).
- Results are only on screen: a grid of test names, each with four letters E F V T
  (exception, flag, value, timing) coloured brown N/A, green OK, cyan INFO, yellow WARNING,
  red ERROR; the bottom line names the test running. The BIOS printf gets 4 banner lines.
- Timing (T) is red on the interpreter: it does not charge GTE cycles.
- `python scripts/amidog_grid.py xfb.png` reads the letter colours per row (G/R/Y/C/n), so two
  runs are compared as text, not by eye. A failing test gives up early ("too many errors"),
  a passing one runs every case: the a7c329a gte.c reaches ALL DONE within 30000 vblanks, the
  more accurate upstream port was only at RTPT by 29900 -- give an accurate GTE 100000.
- Result for gte.c at a7c329a: SQR, NCLIP, OP pass; GPL and MVMVA FLAG errors; every other
  command FLAG and VALUE errors (the test uses sf=0 and other non-SDK encodings, which that
  gte.c ignores -- tests/gte_ab shows the same).
- Upstream port (8c387b8+8950ac8+77b4dd3): every OPCODE command AVSZ3..DCPL passes (19,
  17 of them failed before); register tests unchanged. After 100000 vblanks (92 min) MVMVA
  was still running: to finish it, save a state near the end and continue from it.
- A full run needs more than 2500 vblanks; at 2400 it is at the timing section.

## Host A/B of gte.c: `tests/gte_ab/run.sh`

`bash tests/gte_ab/run.sh [A_GTE_C] [B_GTE_C] [CASES]` builds gte.c twice with clang on the
PC (A defaults to HEAD, B to the working file), runs the same seeded states and opcodes
through both, reads every register back through MFC2/CFC2, and prints per command the %
of cases whose values and FLAG differ, which registers, and ns per command.
- Split by encoding: the PsyQ SDK encodings (sf=1, fixed lm) are what games issue; random
  encodings exercise sf=0 and the MVMVA mx=3/cv=2 quirks. A 70% difference that vanishes
  under the SDK encodings is sf=0 handling, not a game-visible change.
- Host speed is not Wii speed: the upstream port measured +60% per command on x86 and
  +12-16% on the Wii (hw_gte_old/new_20260930 baselines). Time it on the Wii.
- `tests/gte_ab/shim/` has the few libogc/zlib headers `psxcommon.h` includes; gte.c needs
  none of them. The same pattern (shim headers, stub `psxMemRead32`, a global `psxRegs`)
  works for other plain-C core files.
