# WiiStation backends: where each one plugs in

WiiStation is PCSX-ReARMed's core with Wii front ends. Every emulated device is a
"backend" behind a table of function pointers, but the tables were added at different
times and are wired three different ways. This page says, per backend, what the table
is, where the choice is made, and what a new implementation has to provide. Read it
before touching any of them; the traps at the end cost real sessions.

| Backend | Vtable | Chosen by | Implementations |
|---|---|---|---|
| CPU core | `R3000Acpu` (`r3000a.h`) | `psxCpuSelect()` in `r3000a.c`, from `Config.Cpu` (`Core` setting) | interpreter (`psxinterpreter.c`), Lightrec (`deps/lightrec/` + Wii adapter `lightrec.c`), old PPC dynarec (`ppc/`) |
| GPU | `gpu_t` (`gpu.h`) | `setGpuPlugin()` in `Gamecube/GamecubeMain.cpp`, from `gpuPlugin` setting | Soft Fast (`gpulib/oldGpu.c`), Soft Timed (`SoftGPU/`), OpenGX (`GlesGpu/`, renders through `deps/opengx`). Both soft front ends draw through one of two pixel pipelines, chosen per command list by `SoftGpuMode`: `primTableAcc` (`SoftGPU/acc.c`, Accurate / Hi-Fi, Mednafen's rules, PS1-identical on ps1-tests) or `primTableJ` (`soft.c`/`prim.c`, P.E.Op.S., Fast) |
| SPU | PCSX plugin API (`SPU_*` pointers in `plugins.h`) | fixed: `DFSOUND_PLUGIN` slot in `Gamecube/GamecubePlugins.h`, resolved by name in `Gamecube/plugins.c` `LoadPlugins()` | dfsound (`dfsound/`) |
| CD-ROM | PCSX plugin API (`CDR_*`) | fixed: `CDR_ISO_PLUGIN` slot | cdriso (`cdriso.c`: .cue/.bin, .ccd/.img/.sub, .toc, .mds/.mdf, .iso or bare .bin, .pbp, .cbn, .chd, PS-X .exe; the formats and their limits are in the user README). One read-ahead thread (`pf_*`, `CdPrefetch`, priority below the emulator) reads raw images ahead and, since 5.13.0, decodes the next PBP/CBN blocks and CHD hunks ahead with its own FILE, z_stream and chd_file; never for `smb:` or `dvd:` |
| Pad | PCSX plugin API (`PAD1_*`, `PAD2_*`) | fixed: `SSS_PAD1_PLUGIN`/`SSS_PAD2_PLUGIN` slots | SSSPSX (`Gamecube/PadSSSPSX.c`); `PadWiiSX.c` keeps the scripted-input parser both use |
| Host input | `controller_t` (`Gamecube/gc_input/controller.h`) | auto-assign or the `PadType*`/`PadAssign*` settings | GameCube pad, Wiimote variants, Classic, Wii U Pro/GamePad, HID |
| Menu font | `.dat` glyph files (`Gamecube/libgui/IPLFont.cpp`) | `MenuFont` setting, else per-language file, else built-in `fonts/En.dat` | `fonts/menu/*.dat`, made by `scripts/genfont.py` |
| Network share | newlib device `smb:` (`Gamecube/fileBrowser/smb2dev.c`) | `smbipaddr`/`smbsharename` settings; mounted by the network thread (`fileBrowser-SMB.c`) | libsmb2 v6.0.0 (`deps/libsmb2`, SMB2/3), built by `deps/libsmb2/Makefile.wiistation`; patches below |
| DVD | newlib device `dvd:` (`Gamecube/fileBrowser/fileBrowser-DVD.c`) | Load from DVD mounts the disc in the drive, again each time unless a game from it is loaded | libiso9660 (vendored: `Gamecube/fileBrowser/iso9660.c`, libogc2 2026, patches marked `WiiStation`) over WiiStation's own drive reader in `fileBrowser-DVD.c` (direct DI registers with AHBPROT, 0xD0 then 0xA8, every wait bounded; libdi only for DI_Init/DI_Mount/DI_Identify); a spin-up that times out is broken off and the drive reset (HW_RESETS bit 10), as IOS otherwise stalls every request behind it; no CD read-ahead thread (libiso9660 has no lock) |
| Settings | `OPTIONS[]` table in `GamecubeMain.cpp` | `settings.ini` (was settingsRX2022.cfg) (see `SETTINGS.md`) | one table, integers and quoted strings |

## libsmb2 patches (deps/libsmb2)

The tarball of tag v6.0.0, unpacked, with these changes (each marked `WiiStation patch`). An
update must keep them: libsmb2's own Wii port had never run.

- `include/portable-endian.h`: the Wii, GameCube and Wii U use the big-endian branch (they
  were in the little-endian list, so every SMB field went out byte-swapped).
- `lib/compat.h`, `lib/compat.c`: every socket call goes through a wrapper with POSIX
  results (libogc's `net_*` return -errno; its sockets are IOS handles, not newlib file
  descriptors, so `close`/`fcntl`/`read`/`write` on them did nothing). `connect` blocks
  (IOS has no `getsockopt(SO_ERROR)`); `poll` calls `net_poll` directly; `readv` reads
  straight into the caller's buffers, at most 16 KB per call (libogc copies each through
  its 64 KB network heap, and a 128 KB read failed there with -22; plain TCP is 30% faster
  at 16 KB than at 4 KB and no faster above, hbc-reborn's tests/netblock), and drains
  while IOS has data. Counters for the perf report (`smb2_wii_count`).
- `lib/sync.c`: the poll error says its errno.

## libchdr patches (deps/libchdr)

- `include/libchdr/wii_alloc.h` (new), forced into every source by `Makefile.wii`
  (`-include`): `malloc`/`calloc`/`realloc`/`free` become `chdr_malloc` etc., defined in
  `cdriso.c` on the MEM2 heap (MEM1 only if MEM2 is full; `chdr_free` tells them apart by
  address). A CHD's map, hunk cache and codec state are 0.4-0.8 MB per open file, and the
  decode-ahead thread opens a second one (2026-10-13: 300 KB of MEM1 left on the bench Wii,
  save states failed).
- `src/libchdr_chd.c` `zstd_codec_init`: `ZSTD_createDStream_advanced(chdr_zstd_mem)`, as zstd
  allocates with its own malloc otherwise.
- `Makefile.wii`: the objects depend on `wii_alloc.h` and the makefile too, so a change to
  either rebuilds them (a stale archive links silently).

## How each choice is made

**CPU core.** `psxCpuSelect()` is the only mapping from the setting to a core; `psxInit()`,
`psxCpuInit()` and the netplay path call it. A core implements `Init/Reset/Execute/
ExecuteBlock/Clear/Notify/ApplyConfig/Shutdown`. Lightrec's adapter (`lightrec.c`) is the
reference: it owns the MEM2 code buffer, the memory map handed to the core, the HW
read/write callbacks, the interrupt scheduling glue and the register sync in both directions.
Updating the vendored core means replacing `deps/lightrec/*` only; the adapter's contract is
`lightrec_init/lightrec_execute/lightrec_invalidate/lightrec_set_*` and has not changed
between the 2021 snapshot and upstream HEAD a7464cc (2026-09-15).

The core is synced to a7464cc since 5.2.0 (merge 87d77c4). WiiStation's own changes in
`deps/lightrec/`, which an update must keep (each is marked `WiiStation` in the source):
- `lightrec_get_map_idx`: the last matching map is tried first (hprof: 1.3% of busy time).
- `lightrec_emit_code`: code checksums for perf.log `jitcode:` (`lightrec_code_sum/bytes/shape`),
  and the compile-time probes (`lightrec_cprof`, `lightrec_jit_*_ticks`).
- Segfault exits record a site and an address (`lightrec_segv_*`) for the `fatal:` line.
- `lightrec_rw`: `META_LWU`/`META_SWU` fall back to the LWR/LWL (SWR/SWL) pair when a map's
  ops have no `lwu`/`swu` (the HW-register ops), instead of calling address 0.
- `lightrec-config.h`: `OPT_DETECT_IDLE 0` (PS1 loop timing; Docs/ACCURACY.md).
- `CpuTiming` Accurate: per-opcode GTE and multiply/divide wait cycles (`optimizer.c`,
  `disassembler.h` flags, `stall_cycles` in `lightrec-private.h`, `lightrec.h`).
- `emitter.c`: each opcode's code is named (`_jit_name`) with its source line, for the
  profiler.
- `lightrec_invalidate`: the clear stops at the end of RAM's part of the code LUT (it had no
  bound: a clear near the top of RAM or a long one zeroed past the LUT into the heap);
  `lightrec_inv_clamped` counts the cut ones (perf.log `cpu: inv_clamped=`, 2026-10-24).
- `lightrec_init`/`lightrec_destroy`: the state (2.5 MB with its code LUT) is allocated once
  and kept (`lightrec_alloc_state`). Freed and asked for again at every game switch, it found
  the heap too fragmented for one 2.5 MB block (5.7.1). The adapter stops with
  `fatal: code=0x84` if `lightrec_init` still returns NULL.
- A full code buffer (5.9.3; CTR crashed at a race's end). `get_next_block_func`: a compile
  that fails no longer exits with `LIGHTREC_EXIT_NOMEM` (that return left `curr_pc` before a
  block the first pass had already interpreted, so the PS1 ran it twice); the block is
  interpreted and compiled on a later visit. `lightrec_emit_code`: the clean frees blocks
  older than 4 s, 1 s, 0.25 s, then all but the one compiling (`lightrec_free_all_blocks_except`),
  until the code fits; a failed `jit_emit` frees its chunk (it leaked). Block ages use
  `lightrec_age_now()` (`age_base` in the state, kept by `lightrec_reset_cycle_count`): the
  adapter resets `current_cycle` (1/1024 cycles) every slice, so the old 1<<27 test freed
  blocks at random. A recompile clears the block's LUT range before freeing the old code.
  Counters: perf.log `jitcode: cleans= fails= reruns=` and the per-vblank signature (VSG2).
- Bounded memory (2026-10-24): `memmanager.c` allocates through the adapter's
  `lightrec_host_malloc/calloc/free` (a 4 MB TLSF pool in MEM2, `lightrec.c`) and counts
  atomically (jitq's thread allocates too); Lightning gets the same pool through
  `jit_set_memory_functions`. `lightrec_bound_memory()` keeps IR + MIPS + Lightrec allocations
  (state excluded) under `LIGHTREC_DYN_MAX` (3 MB), freeing outdated blocks by age; it runs
  before each block lookup in `get_next_block_func` and in `jq_make_room`. perf.log `lrmem:`.
- The background compiler, "jitq" (try/jit-thread, 2026-10-19; `lightrec.c`, all marked
  `jitq`). A block is interpreted on its first `JitQueueAt` (2) visits; the second queues
  its compile on a thread at priority 20 (the emulator is 64), which compiles a copy of the
  block and its opcode list (`struct jitq_job`); the block is interpreted, or keeps its old
  code for a recompile, until exactly `JitDelay` (16) visits later, where the new code is
  switched in, after waiting for the thread if it is not done (a stall, perf.log
  `jitq: stalls= stall_us=`). Guest-visible = fixed by the guest: which code runs at which
  visit, which blocks a new one covers (`lightrec_install_code`, emulator only), when a
  block that stopped running gets its code anyway (1 s of guest time, `JQ_STALE`), and the
  code buffer's cleans: they follow the bytes of code in use (`MEM_FOR_CODE`, counted at
  installs and frees, limit `JQ_CODE_MAX` 1664 KB), not whether a TLSF allocation failed,
  since the thread's allocations land in any order. `realfull=` counts allocations that
  failed anyway (not guest-predictable; 0 so far). At most 768 compiles wait
  (`JQ_MAX_PENDING`); past that a block compiles on the emulator as before. Other parts:
  `lightrec_compile_block` is split into `lightrec_compile_code` (no shared state touched)
  and `lightrec_install_code`; `emitter.c` bakes `cstate->live_block` (not the copy) into
  jumps to the interpreter; `lightrec_free_block` cancels a pending compile; a tag found
  while a block waited re-arms its recompile (the LUT entry the install wrote hid it: Spyro
  spent a quarter more time in the core in Dolphin before that); a compile that finds only small pieces free tries
  `JQ_CODE_FIT` bytes before the estimate (Lightning's estimate is about 3 times the code);
  the `jitcode:` checksum is a sum over installed blocks (order-free). Lightning
  (`deps/lightning/lib/lightning.c` `_jit_emit`): a user buffer that is too small returns
  NULL instead of emitting again into it forever (`HAVE_MMAP=0`). Off: `JitDelay = 0`
  compiles on the emulator after the first pass, as before (guest identical to 5.13).
  Test only: `JitThreadTest = N` sleeps the thread N ms before each compile.
Compiling ahead (try/jit-ahead, 2026-10-19; replaced by jitq above). The Spyro elevator spike
(`scripts/chains/spyro_tornado.txt`, vblanks 2730-2735) is about 245 ms of compiling code that
first runs in those same frames (vblank 2731: 136 ms compiling, 10 ms running): there is no
idle time before it, so nothing done "ahead" can hide it without interpreting the new code.
- Lightrec's interpreter is not cycle-exact with its compiled code. Interpreting a block on
  its first 31 visits instead of its first only moved Crash 3 by one cycle at vblank 1
  (`replay3.txt`). The guest stays the same only if every block runs interpreted on its first
  visit and compiled from the second, as now.
- Upstream's threaded compiler (`ENABLE_THREADED_COMPILER 1`, libogc v3 pthreads) stopped
  about 30 s into every Dolphin run (the HBC agent's crash path; not root-caused, and no
  longer built: jitq replaces it). Its design cannot be predictable: the thread writes the
  LUT, reaps covered blocks and allocates code whenever it gets the CPU, and a block runs
  interpreted until the thread finishes.
- Compiling in the frame limiter's sleep between a block's first and second visit (guest
  identical: same vsig in all 3 games of replay3 + spyro_tornado, same ps1-tests output):
  on the bench Wii, compiling on the emulator's time 1.45 -> 1.31 s, but the spike was
  unchanged and slow vblanks went 13 -> 17 (510-512: 21 -> 40 ms lost).
jitq interprets new code on purpose and switches at a visit count, which keeps the guest
predictable (Lightrec's interpreter is not cycle-exact with its code, so the count is part
of the timing). Measured on the bench Wii, `spyro_tornado.txt`, JitDelay 0 against 8/16/32
(`.chains` in the try worktree): the elevator spike (2731-2815) 107 ms lost against
42/7/9, compiling on the emulator's time 1.41 s against 0.58/0.53/0.49 (of which 0.3 s is
decoding, which stays), load 57.8 against 57.1-57.2%, no cleans. Some short boot runs
(7-17, 510-522) get more slow vblanks but no lost time (the frames after catch up).

Check an update with `scripts/chains/ps1tests.txt` + `scripts/ps1tests_check.py` (same lines
as before) and with the recordings (`scripts/chains/replay3.txt`, `replay_rest.txt`).

**GPU.** `gpuPtr` points at one of three `gpu_t` tables. All three share the SAME global
VRAM and display state (`psxVuw`, `psxVub`, `PSXDisplay`, `PreviousPSXDisplay`,
`lGPUstatusRet`, defined in `SoftGPU/gpulib_if.c`), which is what lets OpenGX hand
off-screen primitives to the software rasterizer and lets a VRAM dump from one plugin be
compared with another. A fourth renderer would reuse those globals and provide the fourteen
`gpu_t` entries; `GamecubeMain.cpp` still special-cases `gpuPtr == &newSoftGpu` in two places
(rearmed callbacks, frame-limit refresh) that a new renderer must audit.

**SPU, CD-ROM, pad.** These still go through PCSX's dynamic-plugin machinery with the
dynamic part removed: `GamecubePlugins.h` declares each plugin as a table of
`{"SymbolName", function}` pairs, `plugins.c` `LoadPlugins()` looks the symbols up by string
in that table and stores them in the `SPU_*`/`CDR_*`/`PAD1_*` pointers. There is exactly one
implementation of each and no setting selects them. Adding a second one today means a new
macro block in `GamecubePlugins.h` and a slot swap; a cleaner step, when a second
implementation actually exists, is a `spu_t`/`cdr_t`/`pad_t` struct like `gpu_t` chosen by
a setting, and deleting the string lookup.

**Host input.** `controller_t` implementations register themselves in `controller_ts[]`;
`auto_assign_controllers()` (`Gamecube/PlugPAD.c`) probes them and sets `padType[]`. Note
that auto-assign overwrites `PadType1` with "none" when no host controller is present, which
is why unattended runs set `PadAutoAssign = 0`.

## Adding a backend, minimal recipe

1. Implement the vtable for that backend type; keep it in its own directory.
2. Add one case to the selector (`psxCpuSelect`, `setGpuPlugin`, or a slot in
   `GamecubePlugins.h`) and, if the user should be able to pick it, one row in `OPTIONS[]`
   plus one line in `SETTINGS.md`.
3. Verify with the unattended loop (`scripts/dolphin_run.sh`) under the debug build: the
   `perf.log` counters and frame dumps are the acceptance test, see
   `.claude/skills/wiistation-diagnostics/`.
4. Keep hardware cost in mind: everything diagnostic goes under `PERF_PROF` or
   `DISP_DEBUG`; the release build runs on a 729 MHz PowerPC.

## Traps

- The GPU plugins share globals by design; a renderer that allocates its own VRAM breaks
  the readback, off-screen rasterization and VRAM-dump tooling.
- `sio.c` only polls a pad port whose `padType[]` is non-zero; the pad plugin never sees a
  poll otherwise.
- `Config.Cpu` values: 0 Lightrec, 1 interpreter, 2 old dynarec (`DYNACORE_*` in
  `Gamecube/wiiSXconfig.h`), not the order the menu shows them in.
- The old dynarec and Lightrec both live in MEM2 with fixed regions (`Gamecube/MEM2.h`);
  moving the JIT buffer to MEM1 crashed CHD titles before.
- The legacy plugin string lookup fails silently if a symbol name in a macro block is
  misspelled: the pointer stays NULL until first use.
