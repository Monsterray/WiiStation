# Game-specific hacks in WiiStation

Inventory of 2026-09-28 (HEAD 01f5ec7): every place that behaves differently for particular
games, by disc ID, by a per-game flag, or by a check that exists for one game's data. Each
line says what it does and what a proper fix would be, so they can be retired one by one.
**[DEAD]** = never set, never tested, or its code is commented out: safe to delete.
**Update 2026-09-28:** the dead flags and tests are removed, and the two bugs below are fixed
(each line says so). A removed test kept the path the code always took, so behaviour did
not change: soft-GPU fingerprints identical, the six-game OpenGX chain identical.
Line numbers drift; the function names do not.

## How the values are set

`database.c` `Apply_Hacks_Cdrom()` (called from `misc.c` CheckCdrom) clears `Config.hacks`
and matches `CdromId` against each table. `Config.hacks.dwActFixes` goes to the one shared
global `dwActFixes` (defined in `SoftGPU/prim.c`), which OpenGX, the soft GPU and the old
soft GPU all read.

**Bit collision.** The `AUTO_FIX_*` values (0x100-0x10000) reuse P.E.Op.S. fix-bit numbers
that meant something else. The old low bits (1, 2, 4, 8, 0x10, 0x20, 0x80) were never set;
their tests are removed. 0x100 meant two things; fixed (see `AUTO_FIX_GPU_BUSY`).

## database.c tables

| table | games (IDs) | effect | proper fix |
|---|---|---|---|
| `MemorycardHack_db` | Lifeforce Tenka / Codename Tenka (SLES00613, SLED00690, SLES00614-00617, SCUS94409) | disables memory card 2 (`McdDisable[1]`, read in sio.c, psxbios.c). The bug (never reset, so card 2 stayed off for every later game until reboot) is **FIXED**: the next game restores both | find why the game hangs with two cards |
| `cdr_read_hack_db` | T'ai Fu (SLUS00787) | `cdrAlignTimingHack` aligns CD read IRQs to vblank (cdrom.c) | accurate CD IRQ / BIOS handler timing |
| `gpu_slow_llist_db` | Bomberman Fantasy Race, Crash Bash (SCES02834, SCUS94570/94616/94654), EA F1 2000, FF IV, Point Blank, Simple 1500 Vol.57, Spot Goes to Hollywood, Tiny Tank, Vampire Hunter D, Alice in Cyberland, NHL FaceOff 97/98 | emulates GPU DMA chain-walk progress (`psxdma.c`). Only gpulib honours `progress_addr`; OpenGX and the old soft GPU only get the timing override. Tiny Tank lists SCES02072 four times (typo); `Config.GpuListWalking` is never read | accurate GPU DMA timing |
| `gpu_busy_hack_db` | ToHeart (SLPS01919/01920), Hot Wheels Turbo Racing (SLUS00964, SLES02198), FIFA RtWC98 (SLPS01383, SLPS91150, SLUS00520, SLES00914-00918), Ishin no Arashi (SLPS01158, SLPM86861, SLPM86235), Dukes of Hazzard RfH (SLUS00859, SLES02343) | `psxHwReadGpuSRbusyHack`: GPUSTAT reads busy 3 times in 4 (psxhw.c) | model real GPU draw time |
| `gpu_centering_hack_db` | Gradius Gaiden (SLPM86042/86103/87323), Sexy Parodius (SLPM86009), Salamander DX Pack Plus (SLPM86037) | screen centering type 1; soft GPU (gpulib) only, the OpenGX side is commented out | automatic centering from the display range |
| `fractional_Framerate_hack_db` | DDR 3rd/4th/TBGO/Extra/Konamix (SLPM86503/86752/86266/86831, SLUS01446), Dancing Stage Fever/Fusion (SLES04097/04163), Spyro 2 (SCUS94425, SCES02104), Contra LoW (SLUS00288, SLES00608) | 59.81 / 49.76 Hz instead of 60 / 50 (psxcounters.c), unless the `FractionalFramerate` setting says otherwise | make accurate video timing the default |
| `special_game_hack_need_soft_title` | Brave Fencer Musashi, Vagrant Story, Pro Pinball Timeshock | **REMOVED** (was commented out; `ShouldDoSoftTitleFill` applies to all games) | - |
| `gpu_game_fixes` | see the flags below | sets `dwActFixes` bits | per flag |
| `cycle_multiplier_overrides` | Internal Section 202; Super Robot Taisen Alpha 190; Colin McRae PAL 174; **CTR 200 (a WiiStation speed workaround, "sometimes slow")**; Brave Fencer Musashi 170; Eagle One 153; Vandal Hearts I/II 125; Parasite Eve II 125; Discworld Noir 222; Digimon World 153; Power Rangers LR 310; Psychic Detective 200; Sol Divide 200; Syphon Filter 169; Vib-Ribbon 200; Zero Divide 200 (upstream uses 222 on Lightrec); Legend of Legaia 160; Tunguska 232; Riichi Mahjong 200; NBA Jam TE 161 | CPU cycles per instruction, used by Lightrec and the interpreter when the setting is at its default; on the old PPC dynarec it only rescales root counter 2 | accurate CPU / memory / cache timing |
| `gpu_timing_hack_db` | Judge Dredd (SLUS00630, SLES00755) 1024; EA F1 2000 (SLUS01120, SLES02722-02724, SLPS02758, SLPM80564) 300K; Soul Blade (SLUS00240, SCES00577) 512K | forced DMA2 chain time (psxdma.c) | accurate GPU command timing |
| `lightrec_hacks_db` | F1 Arcade (SCES03886), F1 '99 (SLUS00870, SCPS10101, SCES01979, SLES01979), F1 2000 (SLUS01134, SCES02777-02779), F1 2001 (SCES03404/03423/03424/03524) | `LIGHTREC_OPT_INV_DMA_ONLY`; any clear invalidates the whole JIT (lightrec.c) | proper self-modifying-code detection |
| `libcrypt_ids` | ~200 PAL SCES/SLES IDs | only a warning in the ROM info when the SBI file is missing | none needed |
| `CheckGameR3000AutoFix` (FileBrowserFrame.cpp) | Alone in the Dark JiB, NFS V-Rally (1); Supercross 2000 (2); Hot Wheels Turbo Racing (3); Blast Chamber (4) | old PPC dynarec only: those instructions fall back to the interpreter (see CPU) | fix the dynarec code generation |
| `CheckPsxType` (misc.c) | Wild Arms SCUS94608, DTLS3035, PBPX95001/95007/95008 | forces PAL; strips Wild Arms' "EXE\\" prefix | none: region data |

## dwActFixes flags (database.h)

| flag | games | effect (where) | proper fix |
|---|---|---|---|
| `AUTO_FIX_GPU_BUSY` 0x100 | Star Wars Dark Forces (SLUS00297, SLPS00685, SLES00585/00640/00646) | OpenGX and the old soft GPU fake busy/idle on status reads (`iFakePrimBusy`) | GPU busy timing |
| `AUTO_FIX_FLAT_TEX_WRAP` 0x20000 | Dark Forces | soft GPU `drawPoly3FT`: flat-textured triangles through the texture-window rasterizers, which mask U/V. It shared 0x100 with GPU_BUSY, so any game given one got both; **FIXED** by the split (Dark Forces keeps both, unchanged) | likely: wrap U/V at 8 bits in the fast rasterizer, as the hardware's 8-bit coordinates do; then drop the flag |
| `AUTO_FIX_QUADS_AS_2TRIANGLES` 0x200 | all games (forced on in FileBrowserFrame.cpp) | `SoftGPU/soft.c` IsNoRect | make it the unconditional path |
| `AUTO_FIX_DINO_CRISIS2` 0x400 | Dino Crisis 2 (SLUS01279, SLPM86627, SLES03221-03225) | `GlesGpu/gpuPrim.c` primSprtS drops every 64x48 textured sprite ("VRAM work data, not a sprite") | draw off-display targets into VRAM in software (the off-screen path already does this for other primitives) |
| `AUTO_FIX_FF9` 0x800 | none | **REMOVED** with `bCheckFF9G4` (never set) | - |
| `AUTO_FIX_NEED_SOFT_TITLE` 0x1000 | none | **REMOVED** (the only test was commented) | - |
| `AUTO_FIX_CHRONO_CROSS` 0x2000 | Chrono Cross (SCPS45447/48, SLPS02364/65, SLPS91464/65, SLPS02777/78, SLPM87395/96, SLUS01041/01080) | OpenGX only: GPUSTAT bit 31 toggles every 3rd status read instead of every vsync | derive bit 31 from the scanline / field, as gpulib does |
| `AUTO_FIX_NO_SWAP_BUF` 0x4000 | Tomb Raider series (41 IDs), Vagrant Story | **REMOVED** (set, but every test was commented out); the Tomb Raider entries went with it | re-derive if Tomb Raider shows a problem |
| `AUTO_FIX_VRAM_READBACK` 0x8000 | Dino Crisis 2, Vagrant Story (SLPS02377, SCPS45486, SLPS91457, SLPM87393, SLUS01040), Spyro (SCUS94228, SCES01438, SCPS10083), Ape Escape (SCUS94423, SCES01564, SCPS10091) | `GlesGpu/efbSync.inc`: EFB snapshots at every present from the start (EFB_SYNC.md) | an auto mode that learns earlier, or cheaper snapshots |
| `AUTO_FIX_EFB_SYNC_OFF` 0x10000 | none | **[DEAD]** so far: tested, never set; kept on purpose as the switch for a game EFB sync breaks | keep until needed |
| old bits 0x1 (FF7 battle cursor), 0x2 (Capcom 384 px), 0x4 (Lunar brightness), 0x8 (skip coord adjust / swap-front detection), 0x10 (no coord check), 0x20 (Legend of Dragoon palette, PC fps), 0x80 (old frame skip) | none | **REMOVED**: nothing set bits below 0x100; each test now takes the path it always took | - |

## OpenGX (GlesGpu), keyed on a shape of data

- `gpuPrim.c` TitleFillArea and `SoftGPU/soft.c` FillSoftwareArea: a 1x1 fill at (1020,511)
  alternates its colour by +1 each call ("pinball ... emu protection???", Pro Pinball). Proper
  fix unknown; probably the timing or readback of a protection probe.
- `gpuPrim.c` primTileS: a tile of size 0 becomes the screen width/height, and 1023 -> 1024,
  511 -> 512. Not keyed; origin unknown. Hardware draws nothing for size 0.
- `gpuPrim.c` Gradius "lClearOnSwap" (ly0 == -6 && ly2 == 10): commented out.
- `gpuPlugin.c` and `gpulib/oldGpu.c` CheckForEndlessLoop ("Tekken 3"): all games; a guard
  against display-list loops. Proper fix: gpulib-style loop detection with a count limit.

## CPU

- `ppc/pR3000A.c` (old PPC dynarec only), `Config.pR3000Fix` from the table above: recJR (1),
  recLW (2), recMULT-by-shift and recSRA (3), recSLL (4) fall back to the interpreter. Proper
  fix: the code generation bugs. `r3000a.c` suspends it while the BIOS runs.
- `gte.c` limH clamp 0x1000 ("Valkyrie Profile world map"): all games, an upstream value
  marked unverified. Verify on hardware.

## CD (cdrom.c)

- T'ai Fu: the only disc-ID-keyed CD hack (above).
- "F1 2000 timing hack": the next read is pushed back by cdReadTime/10 after a missed IRQ; all
  games. Proper fix: accurate CD IRQ / acknowledge timing.
- Game-motivated but general: Syphon Filter 2 (skips the spin-up), `errorRetryhack` (ignores
  more than 100 bad Setlocs), RetryDetected (delay to break retry races), Rayman / Twisted
  Metal 2 / Wild 9 (track and SubQ), Gundam / DDR Konamix (7000-cycle pause response),
  MediEvil / Rockman X5 (seek time), Croc / Shadow Tower vs Discworld Noir (acknowledge
  delay). Each is a timing approximation to replace with measured hardware timing.

## BIOS, SIO, hardware registers

- `psxbios.c` writes to RAM address 0 so that R-Type, CTR and Fade to Black read what they
  expect: all games, matches the hardware BIOS. Keep.
- `psxhw.c` sio1ReadStat16 returns 0xa0 (Armored Core, F1 link-cable misdetection): all
  games. Proper fix: implement SIO1.
- `psxbios.c` card_io_delay: general.

## SPU

No game-keyed code. `dfsound/registers.c` volume sweep is a general approximation.

## Generic, data-driven per-game mechanisms (not hacks)

`settings/<CdromId>.cfg` per-game settings (GamecubeMain.cpp), PPF patches by CdromId
(ppf.c), one memory-card file per game (sio.c).

## Suggested order

1. **DONE** Delete the dead ones.
2. **DONE** Fix the two bugs: `McdDisable[1]` after Tenka, the 0x100 collision.
3. Dino Crisis 2's 64x48 sprite drop: route it through the off-screen software path.
4. Chrono Cross's bit 31: derive it from the scanline as gpulib does, then drop the flag.
5. The timing tables (cycle overrides, GPU busy, DMA timing, CD) wait for accurate timing
   models; the bench Wii can measure the real values.
