# WiiStation backends: where each one plugs in

WiiStation is PCSX-ReARMed's core with Wii front ends. Every emulated device is a
"backend" behind a table of function pointers, but the tables were added at different
times and are wired three different ways. This page says, per backend, what the table
is, where the choice is made, and what a new implementation has to provide. Read it
before touching any of them; the traps at the end cost real sessions.

| Backend | Vtable | Chosen by | Implementations |
|---|---|---|---|
| CPU core | `R3000Acpu` (`r3000a.h`) | `psxCpuSelect()` in `r3000a.c`, from `Config.Cpu` (`Core` setting) | interpreter (`psxinterpreter.c`), Lightrec (`deps/lightrec/` + Wii adapter `lightrec.c`), old PPC dynarec (`ppc/`) |
| GPU | `gpu_t` (`gpu.h`) | `setGpuPlugin()` in `Gamecube/GamecubeMain.cpp`, from `gpuPlugin` setting | Soft Fast (`gpulib/oldGpu.c`), Soft Timed (`SoftGPU/`), OpenGX (`GlesGpu/`, renders through `deps/opengx`) |
| SPU | PCSX plugin API (`SPU_*` pointers in `plugins.h`) | fixed: `DFSOUND_PLUGIN` slot in `Gamecube/GamecubePlugins.h`, resolved by name in `Gamecube/plugins.c` `LoadPlugins()` | dfsound (`dfsound/`) |
| CD-ROM | PCSX plugin API (`CDR_*`) | fixed: `CDR_ISO_PLUGIN` slot | cdriso (`cdriso.c`, ISO/BIN+CUE/CHD/CCD/PBP) |
| Pad | PCSX plugin API (`PAD1_*`, `PAD2_*`) | fixed: `SSS_PAD1_PLUGIN`/`SSS_PAD2_PLUGIN` slots | SSSPSX (`Gamecube/PadSSSPSX.c`); `PadWiiSX.c` keeps the scripted-input parser both use |
| Host input | `controller_t` (`Gamecube/gc_input/controller.h`) | auto-assign or the `PadType*`/`PadAssign*` settings | GameCube pad, Wiimote variants, Classic, Wii U Pro/GamePad, HID |
| Menu font | `.dat` glyph files (`Gamecube/libgui/IPLFont.cpp`) | `MenuFont` setting, else per-language file, else built-in `fonts/En.dat` | `fonts/menu/*.dat`, made by `scripts/genfont.py` |
| Network share | newlib device `smb:` (`Gamecube/fileBrowser/smb2dev.c`) | `smbipaddr`/`smbsharename` settings; mounted by the network thread (`fileBrowser-SMB.c`) | libsmb2 v6.0.0 (`deps/libsmb2`, SMB2/3), built by `deps/libsmb2/Makefile.wiistation`; patches below |
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
- `lightrec_init`/`lightrec_destroy`: the state (2.5 MB with its code LUT) is allocated once
  and kept (`lightrec_alloc_state`). Freed and asked for again at every game switch, it found
  the heap too fragmented for one 2.5 MB block (5.7.1). The adapter stops with
  `fatal: code=0x84` if `lightrec_init` still returns NULL.
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
