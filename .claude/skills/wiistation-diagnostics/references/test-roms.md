# PS1 test programs (PS-X EXE) and host A/B harnesses

How to run a PS1 test program in WiiStation unattended and read its verdict, and how to
compare two versions of a core file on the PC. Every step below cost a run to find.

## Running a PS-X EXE

- Put the `.exe` in its own folder on the automated card:
  `.dolphin/Load/WiiSDSync/wiisxrx/isos/<name>/<file>.exe`. A chain entry is the same as for
  a game: `VBLANKS [input] KEY=V...` / `sd:/wiisxrx/isos/<name>` / `<file>.exe`.
- **Lightrec runs EXEs since 2303729.** The EXE path skipped `loadSeparatelySetting()`, which
  makes the CPU core (`psxCpuInit`): Lightrec ran on a NULL state and stopped at the EXE's
  first block (the "Dolphin shuts down 2 s after an EXE" of 2026-09-30). Now an EXE gets the
  same settings/CPU/GPU steps as a disc, with no disc ID, so chain-line settings apply too.
  The interpreter (`Core=1`) runs at about 0.29x with `FPS=0`.
- **A stop says why:** `ws_fatal` writes `fatal: code= pc= cycle= vblank= <reason>` to perf.log;
  a Lightrec fault names its site (1 load/store to no memory ... 6 no map for the pc).
- **Use the HLE BIOS for console output.** Only the HLE BIOS printf reaches tty.log; a real
  BIOS (`BiosDevice=1`) sends putchar to a TTY device a retail PS1 does not have (and
  `psxJumpTest` is commented out). HLE printf took no flags before 62bfbcf (`%-10s` printed
  `10s`). HLE calls cost no guest cycles: timing lines that include a BIOS call (per-frame
  delays) are not a CPU-core measure.
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

## PadTest DX: the controller test ROM

- github.com/Monsterray/padtest at C:\projects\padtest (PSn00bSDK at C:\PSn00bSDK), `bash
  build.sh`; WiiStation's `scripts/padtest_dx.sh` copies build/padtest.bin+cue to the test
  card and runs the matrix (measuring.md, "Controller and multitap tests"). What it sends
  and why: the fork's docs/PROTOCOL.md; status block layout: include/dx.h (version 4, in
  step with scripts/padtest_dx.py).
- It boots in under a vblank on the HLE BIOS, so a cell's vblanks are all ROM time.
- **A PSn00bSDK program that hangs before it draws** (VRAM all black, `gpu 0.0`, PC in
  DrawSync or `_default_vsync_halt`): read SR from a save state. PSn00bSDK waits for the GPU
  DMA IRQ to drain its draw queue, so interrupts left off hang it there. The 2026-10-02 case
  was the HLE BIOS's syscall return (epc + 4 done on byte-swapped words: a syscall at
  ...FCh went back 256 bytes); `BiosDevice=1` (real BIOS) ran the same ROM, which is the
  quick way to tell an HLE bug from a program bug. Both CPU cores hung, so not the JIT.
- psxRegs in a save state are big-endian (the Wii's order); RAM, BIOS and psxH are the
  PS1's little-endian bytes.

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
- **`statecheck` in a chain (fixed 2026-10-02).** It used to work only in the first chained
  game; every later one logged `statecheck: save at vblank 120 FAILED`. The cause was not the
  file or the chain: SaveStateFile returned -1 with ENOMEM. statecheck's comparison copies
  (`snap_ram` 2 MB, `snap_h` 64 KB, two psxRegs) were MEM1 `malloc`s that were never
  freed, so the next game had ~700 KB of MEM1 left (perf.log `ram: mem1_free_kb=711`) and
  SaveStateFile's ~540 KB SPU-freeze buffer did not fit. Older chains (Spyro, FF7 and Crash
  Bash, before 2026-09-30) passed only because MEM1 had more room before Lightrec's 1.5 MB
  code buffer moved there. Now the copies come from MEM2 and are freed when a check ends and in
  `statetool_reset`. A failed save names the reason: `FAILED (r, strerror)`, where r 0 = the
  file did not open and r -1 = a buffer did not allocate. Check: `wsx.sh chain NAME
  statecheck3.txt` (PadTest DX three times, `statecheck 120 60`): three `-> match`, and
  `mem1_free_kb` about 2840 after every game. Anything that holds a buffer across a chain's
  games costs every later game that much MEM1; free it in a reset that `autoinput_reset` runs.

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

## JaCzekanski ps1-tests: emulator vs a real PS1

`C:/tools/ps1-tests/` (release build-158, scanned 2026-10-01): `bin/` the EXEs, `ref/` each
test's `psx.log` -- its console output on a real PlayStation. The EXEs are staged as
`.dolphin/Load/WiiSDSync/wiisxrx/isos/ps1t_<name>/<name>.exe`.
- `scripts/wsx.sh chain NAME scripts/chains/ps1tests.txt --dol X` (10 tests x 2400 vblanks,
  about 8 min on Lightrec), then `python scripts/ps1tests_check.py .runs/A [.runs/B]`: per
  test SAME or the lines that differ, matched by label (numbers and hex taken out, spacing
  collapsed). Two runs = an A/B of two cores against the PS1.
- access-time and io-access-bitwidth touch expansion regions 1-3: keep them last in a chain.
- Results on main 2026-10-01 (both cores the same where both ran): code-in-io 3 fails (code
  in scratchpad/MDEC/IRQ registers must raise a bus error); io-access-bitwidth 42/64 lines
  (BIOS ROM writable, expansion 2/3 read back what was written, register masks).
