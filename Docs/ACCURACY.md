# PS1 accuracy: measured gaps, and the Fast / Accurate plan

WiiStation must offer two options:

- **Fast**: the current emulation. Timing is an average tuned for games. It is the default.
- **Accurate**: timing and register behaviour as a real PlayStation shows them, at a cost in
  speed.

This file records what a real PS1 does that WiiStation does not, as measured on 2026-10-01
(main 87d77c4, Lightrec core synced to upstream a7464cc). It is the work list for the
Accurate option.

## How the gaps were measured

- Test programs: JaCzekanski's ps1-tests, release build-158, in `C:/tools/ps1-tests/bin/`.
  Each test has `psx.log`, its console output on a real PlayStation, in
  `C:/tools/ps1-tests/ref/`.
- Run: `bash scripts/wsx.sh chain NAME scripts/chains/ps1tests.txt --dol <dol>` (10 tests,
  2400 vblanks each, about 8 minutes on Lightrec in Dolphin). Use the HLE BIOS: only its
  printf reaches `tty.log`.
- Compare: `python scripts/ps1tests_check.py .runs/A [.runs/B]`. Each line is matched to the
  PS1 line with the same label. The result per test is SAME, or the lines that differ.
- Runs kept: `.runs/ps1t_main2` (main) and `.runs/ps1t_p2b` (phase 2 before the merge). The
  two give the same output on every line. These gaps are in the shared code, not in one core.
- The procedure is also in `.claude/skills/wiistation-diagnostics/references/test-roms.md`.

## Result per test

| Test | PS1 lines that differ | What is wrong |
|---|---|---|
| dma/dpcr | 0 of 6 | Nothing: SAME |
| cpu/code-in-io | 3 of 10 | Code that runs from the scratchpad, the MDEC or the IRQ registers must cause a bus error (exception 6). WiiStation runs it. |
| cpu/cop | 15 of 19 | The test stops after `testCop0InvalidOpcode`. `SWC0` with COP0 disabled must cause an exception; the test does not continue. |
| cpu/access-time | 17 of 22 | No bus wait states (see "CPU and bus timing"). |
| cpu/io-access-bitwidth | 42 of 64 | Register width and mask behaviour (see "Memory map and registers"). |
| timers | 82 of 111 | CPU cycle count, frame length, timer sync modes (see "Timers"). |
| dma/chain-looping | 4 of 10 | DMA takes no CPU time; a self-referencing linked list hangs the test. |
| dma/chopping | 130 of 132 | DMA chopping and block size have no effect on time. |
| dma/otc-test | 9 of 15 | The test stops after `testOtcWontStartWithoutStartFlag`; the 9 later tests (OTC/DMA6 direction, sync modes, control bits) do not run. |
| gte/test-all | 2 of 5 | 51 of 1150 cases pass, then the test stops at the first failure. |

## CPU and bus timing

- **Cycles per instruction.** Lightrec and the interpreter charge 1.75 cycles for each
  instruction (`cycle_multiplier` 175, as in PCSX-ReARMed). A busy loop that takes 1000
  cycles on a PS1 measures 1769 (`timers`: "1000 cycles delay").
  - The PS1 executes from the instruction cache at about 1 cycle per instruction. A cache
    miss costs a RAM burst. 1.75 is an average over game code.
- **Bus wait states.** WiiStation charges the same time for every load (about 1.77 cycles,
  the CPI above). A PS1 charges per region and width (`access-time`, cycles per access):

  | Region | PS1 8/16/32-bit | WiiStation |
  |---|---|---|
  | RAM | 5.2 / 5.3 / 5.1 | 1.77 |
  | BIOS ROM | 7.6 / 12.9 / 24.9 | 1.77 |
  | Scratchpad | 1.5 / 1.1 / 0.9 | 1.73 |
  | Expansion 1 | 6.9 / 13.7 / 25.7 | 1.77 |
  | Expansion 2 | 11.0 / 26.0 / 56.0 | 1.0 |
  | Expansion 3 | 6.7 / 6.1 / 10.0 | 1.77 |
  | I/O registers (DMA, pad, SIO, IRQ, timers, GPU, MDEC) | about 3 | 1.0 |
  | CD-ROM registers | 8.0 / 14.0 / 25.9 | 1.0 |
  | SPU registers | 18.0 / 18.0 / 38.9 | 1.0 / 1.0 / 3.0 |
  | Cache control | 1.0 / 1.9 / 1.9 | 1.77 |

- **DMA time.** A DMA does not take CPU time (`chain-looping`: work with no DMA took 16040
  ticks on a PS1 and 12264 in WiiStation, the same as with a DMA). A GPU DMA of 8192 bytes
  takes 549 cycles at any block size; a PS1 takes 5000-22800, by block size (`chopping`).

## Timers

- **Frame length.** Timer 0/1/2 on the system clock count 105,695 per frame; a PS1 counts
  about 112,530 (6% more). The dot clock counts 112,888 (PS1 112,030) and hblank counts 262
  per frame (PS1 263).
- **Jitter.** A PS1's frame readings vary by about ±20 ticks; WiiStation's by ±6.
  Lightrec's idle-loop skip (`OPT_DETECT_IDLE`) made them constant: it is off for this
  reason (deps/lightrec/lightrec-config.h).
- **Sync modes.** Timer 0 sync modes 0-3 (pause or reset at hblank) give other counts than a
  PS1 at every resolution (`timers`, "Testing Timer0 sync modes").
- **Hblank-clocked Timer 1.** Over 5000 cycles it counts 4; a PS1 counts 2-3 (this follows
  from the CPI above).

## Memory map and registers

From `io-access-bitwidth` (write 8/16/32 bits, read back 32/16/8 bits):

- **BIOS ROM is writable.** A PS1 ignores writes to 0xBFC00000 and reads the ROM word
  (`0x3c080013`); WiiStation reads back the value written.
- **Expansion 2 (0x1F802000)** must read `0xffffffff`. Expansion 3 (0x1FA00000) keeps only
  some byte lanes (`0xff785678`). WiiStation reads back what was written, or 0.
- **Register masks.** DMA0 address keeps 24 bits (`0x345678`). DICR (0x1F8010F4) and I_MASK
  keep only their defined bits. Timer target, MDEC status, SPU control and CD-ROM status
  read back other values than a PS1.
- **Narrow I/O writes.** A PS1 widens an 8/16-bit write to some registers (pad, SIO
  control); some 32-bit writes crash a PS1 (`--CRASH--` in the log). WiiStation stores them.

## Exceptions

- Instruction fetch from the scratchpad and from I/O must cause a bus error (`code-in-io`).
- Coprocessor-unusable and invalid-opcode behaviour for COP0-COP3 and SWC0-SWC3 (`cop`).

## GTE

`gte/test-all` passes 51 of 1150 cases, then stops. WiiStation's `gte.c` is the
paired-single rewrite (fast, less exact). The upstream PCSX-ReARMed GTE port passes the
AmiDog GTE opcode tests (`references/test-roms.md`, "AmiDog psxtest_gte") but costs
+12-16% per GTE command on the Wii. It is the Accurate option's GTE.

## The Accurate option: plan

One setting selects the option; each item below is a part of it. The Fast option keeps
today's code paths, so its speed does not change.

Three parts exist already, each a Fast/Accurate setting of its own (SETTINGS.md, section 2):
- `CpuTiming` (Lightrec): charges the cycles a PS1 CPU waits for the GTE and the
  multiply/divide unit.
- `GpuTiming` (OpenGX, Soft Fast): keeps the GPU busy for each command and transfer, as
  gpulib measures it.
- `SioTiming` (2026-10-02, sio.c): a controller byte takes its 8 bits at the baud rate (32 us
  at 250 kHz) and the /ACK comes 450 cycles later (DuckStation's measurement, 6.8..13.7 us).
  PadTest DX measures 33.0 us and 13.0..13.6 us with it (Docs/CONTROLLER_TESTING.md); Crash
  Bash plays its recording to the same screen. Memory card bytes keep the Fast timing: their
  state machine reads the reply buffer at the write, and a card bug costs saves.
The controller protocol itself (multitap long reads, empty slots, addresses) is not an
option: it was wrong, and is now as on a PS1 in both (PadTest DX's matrix, 12 of 12 cells).
The measurements above are with both at Fast (the defaults). With both at Accurate
(`.runs/ps1t_520_acc`, 5.2.0) every test line is the same: these tests hardly wait for the GTE
or the multiply/divide unit, and `dma/chopping` still never sees the "GPU busy" state that a
PS1 reports. The Accurate items below are new work, not the two settings switched on.

1. **Setting.** `Accuracy = 0` (Fast, default) / `1` (Accurate) in `settings.ini` (was settingsRX2022.cfg), the
   per-game files and the chain lines, with a menu item. Accurate sets `CpuTiming = 1` and
   `GpuTiming = 1` and turns on the items below as they are done. A change resets the game,
   as a CPU core change does.
2. **GTE.** Accurate uses the upstream GTE port (tests/gte_ab shows the differences).
3. **Register behaviour** (`psxhw.c`, the Lightrec map table in `lightrec.c`): ROM write
   protect, expansion 2/3 read values, register masks. These cost almost nothing; most can go
   into both options once each one is checked against games.
4. **Exceptions.** Bus error on instruction fetch from scratchpad/I/O; COPn unusable.
   Lightrec must end a block on such a fetch.
5. **Timer frame length and sync modes** (`psxcounters.c`): compare with the hardware
   numbers above. A fix here changes every game's timing, so check the game recordings
   (`scripts/chains/replay3.txt`, `replay_rest.txt`) and re-record those that drift.
6. **Bus timing.** A per-region wait-state charge in the I/O callbacks and for RAM/BIOS
   accesses that Lightrec makes direct. The largest cost: direct accesses would need a
   cycle charge in the generated code.
7. **CPI.** A cycle cost per instruction class and an instruction-cache model in place of the
   flat 1.75. This is the most expensive item; do it last and only for Accurate.
8. **DMA time.** Charge the CPU for DMA by block size and sync mode (`psxdma.c`).

Check each item with `ps1tests_check.py` (the lines it fixes go to SAME) and with the game
chains (no game breaks). Record the result in this file.
