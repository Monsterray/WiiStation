# Games to obtain for the game-specific hacks

Written 2026-10-19 (main 59f1348 = 5.14.0). Each hack in WiiStation must be tested with the
game it was made for before it can be replaced by a proper fix. This file lists those games:
first the ones that are **not** in the library (`C:/wiistation-lib/games`, `games.tsv`), then
the ones that are.

Sources: `database.c` (all tables), `Gamecube/menu/FileBrowserFrame.cpp`
(`CheckGameR3000AutoFix`), `misc.c` (`CheckPsxType`), `Docs/GAME_HACKS.md` (also the hacks
that are general code made for one game), `Docs/PARKED_BUGS.md`, `Docs/GPU_REVIEW_BRIEF.md`,
`Docs/STP_BIT15.md`. No other code compares `CdromId` with a serial (grep of the tree,
`deps/` excluded).

How to read the tables:

- **Serial to get**: the serial that the hack matches. A hack keyed by ID works only for the
  listed IDs, so the dump must have one of them. Where the table has several regions, the
  NTSC-U one is first: the library and the recordings are NTSC-U.
- **Any region**: the hack is general code (not keyed by ID). Any region of the game
  reproduces the problem. The serial is given for identification only.
- "look up" = the serial is not in the code and was not confirmed. Check redump.org.
- A dump needs its `.sbi` file if the game is a PAL LibCrypt title (`database.c`
  `libcrypt_ids`).

## 1. Games to obtain

### 1.1 CPU timing

`cycle_multiplier_overrides` (`database.c`): cycles per instruction, used by Lightrec and
the interpreter when the CPU Speed setting is at its default. Proper fix for all of them:
accurate CPU timing (instruction cost, memory wait states, I-cache), as `Docs/ACCURACY.md`
plans. Then each override must be removed and the game tested at the default.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Internal Section (J) | SLPS-01868 | cycles 202 ("fussy about timings") | CPU timing |
| Super Robot Taisen Alpha (J) | SLPS-02528 or SLPS-02636 | cycles 190 (memory-card settings break it at 175) | CPU timing |
| Colin McRae Rally (PAL) | SLES-00477 | cycles 174 (language menu fails at 175) | CPU timing |
| Brave Fencer Musashi (U) | SLUS-00726 (J: SLPS-01490) | cycles 170 ("CD sectors arrive too fast") | CPU and CD read timing |
| Eagle One: Harrier Attack (U) | SLUS-00943 | cycles 153 (hangs) | CPU timing |
| Vandal Hearts (U) | SLUS-00447 (PAL SLES-00204, J SCPS-45183, SLPM-86007/86067/87278) | cycles 125 | CPU timing |
| Vandal Hearts II (U) | SLUS-00940 (PAL SLES-02469/02496/02497, J SCPS-45415, SLPM-86251/86504/87279) | cycles 125 | CPU timing |
| Parasite Eve II (U) | SLUS-01042 (disc 1), SLUS-01055 (disc 2). PAL and J IDs in `database.c` | cycles 125 (the game's own timer checks) | CPU timing |
| Discworld Noir (PAL) | SLES-01549 (disc 1), SLES-02063, SLES-02064 | cycles 222 (audio skips when the CPU runs too fast). Also the CD acknowledge-delay comment in `cdrom.c` | CPU timing, CD acknowledge timing |
| Digimon World (U) | SLUS-01032 (PAL SLES-02914) | cycles 153 | CPU timing |
| Power Rangers: Lightspeed Rescue (U) | SLUS-01114 (PAL SLES-03286) | cycles 310 (jumps fail above 30 fps) | CPU timing |
| Psychic Detective (U) | SLUS-00165 / 00166 / 00167 (PAL SLES-00070 / 10070 / 20070) | cycles 200 (a race in the game's CD code) | CPU and CD timing |
| Sol Divide (U) | SLUS-01519 (J SCPS-45260, SLPS-01463) | cycles 200 (FMV timing) | CPU and MDEC timing |
| Syphon Filter (U) | SCUS-94240 | cycles 169 (hangs, conditions unknown) | CPU timing |
| Vib-Ribbon (PAL) | SCES-02873 | cycles 200 (CD timing) | CPU and CD timing |
| Zero Divide (U) | SLUS-00183 (PAL SLES-00159, J SLPS-00083, SLPM-80008) | cycles 200 (too fast) | CPU timing |
| Legend of Legaia (U) | SCUS-94254 (also SCUS-94366, PAL SCES-01752, SCES-01944..01947) | cycles 160 (attack moves lag, A/V desync) | CPU timing |
| Tunguska: Legend of Faith (PAL) | SLES-03298 | cycles 232 (runs 2x too fast) | CPU timing |
| Riichi Mahjong (J) | SLPS-03023 | cycles 200 (hangs) | CPU timing |
| NBA Jam Tournament Edition (U) | SLUS-00002 (J SLPS-00199, PAL SLES-00068) | cycles 161 (halftime stats) | CPU timing |

`fractional_Framerate_hack_db` (`database.c`, read in `psxcounters.c`): 59.81 / 49.76 Hz
instead of 60 / 50, unless the FrameRate setting overrides it. Proper fix: accurate PS1
video timing as the default, then remove the table.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Dance Dance Revolution Konamix (U) | SLUS-01446 | fractional frame rate. Also the 7000-cycle pause response in `cdrom.c` | video timing, CD pause timing |
| Dance Dance Revolution 3rd / 4th / 4th TBGO / Extra Mix (J) | SLPM-86503 / 86752 / 86266 / 86831 | fractional frame rate | video timing. One of them is enough |
| Dancing Stage Fever (PAL) | SLES-04097 | fractional frame rate | video timing |
| Dancing Stage Fusion (PAL) | SLES-04163 | fractional frame rate | video timing |
| Spyro 2: Ripto's Rage (U) | SCUS-94425 (PAL SCES-02104) | fractional frame rate | video timing |
| Contra: Legacy of War (U) | SLUS-00288 (PAL SLES-00608) | fractional frame rate | video timing |

`lightrec_hacks_db` (`database.c`): `LIGHTREC_OPT_INV_DMA_ONLY`, any code write invalidates
the whole JIT. Proper fix: correct self-modifying-code detection in Lightrec.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Formula One 99 (U) | SLUS-00870 (J SCPS-10101, PAL SCES-01979 / SLES-01979) | INV_DMA_ONLY | Lightrec SMC detection |
| Formula One 2000 (U, Psygnosis) | SLUS-01134 (PAL SCES-02777..02779) | INV_DMA_ONLY | Lightrec SMC detection |
| Formula One 2001 (PAL) | SCES-03404 (also 03423, 03424, 03524) | INV_DMA_ONLY | Lightrec SMC detection |
| Formula One Arcade (PAL) | SCES-03886 | INV_DMA_ONLY | Lightrec SMC detection |

Old PPC dynarec only (`Core = 2`, `Config.pR3000Fix`, `FileBrowserFrame.cpp`): the listed
instructions fall back to the interpreter. Proper fix: the code generation bugs in
`ppc/pR3000A.c`. Low priority: Core 2 is not the default and is parked (PARKED_BUGS.md).

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Alone in the Dark: One-Eyed Jack's Revenge / Jack is Back (U) | SLUS-00239 (PAL SLES-00037, J SLPS-00141) | recJR to the interpreter (fix 1) | dynarec JR code generation |
| Need for Speed: V-Rally (U) | SLUS-00590 (PAL SLES-00250, J SLPS-01149, SLPS-91099, SLPS-91430) | recJR (fix 1) | dynarec JR |
| Supercross 2000 (U) | SLUS-01005 (PAL SLES-02373) | recLW (fix 2) | dynarec LW |
| Blast Chamber (U) | SLUS-00219 (PAL SLES-00476) | recSLL (fix 4) | dynarec SLL |
| Hot Wheels Turbo Racing | see 1.2 | recMULT by shift, recSRA (fix 3) | dynarec MULT / SRA |

GTE (all games, made for one):

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Valkyrie Profile (U) | any region (SLUS-00795 disc 1) | `gte.c` limH clamp 0x1000 ("Valkyrie Profile world map"), an unverified upstream value | a real-PS1 GTE test of the limH clamp (ps1-tests gte-fuzz style), then the world map |

### 1.2 GPU

`gpu_slow_llist_db`: GPU DMA chain walked in slices of ~512 cycles (`psxdma.c`). Proper fix:
accurate GPU DMA and command timing.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| EA Sports F1 2000 (U) | SLUS-01120 (PAL SLES-02722..02724, J SLPS-02758, SLPM-80564) | slow list walking (menus abort / resume DMA) **and** `gpu_timing_hack_db` 300K cycles **and** the CD "F1 2000 timing hack" (`cdrom.c`, all games except during XA) | GPU DMA timing, GPU command timing, CD IRQ timing |
| Bomberman Fantasy Race (U) | SLUS-00823 (PAL SLES-01712, J SLPS-01525, SLPS-91138, SLPM-87102) | slow list walking | GPU DMA timing |
| Final Fantasy IV (Chronicles, U) | SLUS-01360 (PAL SCES-03840, J SLPM-86028) | slow list walking | GPU DMA timing |
| Simple 1500 Series Vol. 57: The Meiro (J) | SLPM-86715 | slow list walking | GPU DMA timing |
| Spot Goes to Hollywood (U) | SLUS-00014 (PAL SLES-00330, J SLPS-00394) | slow list walking | GPU DMA timing |
| Tiny Tank (U) | SCUS-94427 (PAL SCES-01338, SCES-02072) | slow list walking. `database.c` lists SCES02072 four times (typo) | GPU DMA timing |
| Vampire Hunter D (U) | SLUS-01138 (PAL SLES-02731, J SLPS-02477, SLPS-03198) | slow list walking | GPU DMA timing |
| Alice in Cyberland (J) | SLPS-00636 | slow list walking | GPU DMA timing |
| NHL FaceOff 97 / 98 (U) | SCUS-94550, SCUS-94174 (PAL SCES-00392, SCES-01022) | slow list walking | GPU DMA timing |

`gpu_busy_hack_db`: GPUSTAT reads busy 3 times in 4 (`psxhw.c`). `AUTO_FIX_GPU_BUSY`:
OpenGX fakes busy / idle (`iFakePrimBusy`). Proper fix: model the real GPU draw time (the
GpuTiming = Accurate setting is the start).

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Hot Wheels Turbo Racing (U) | SLUS-00964 (PAL SLES-02198) | GPU busy 3 in 4 **and** old-dynarec fix 3 (1.1) | GPU busy timing, dynarec MULT/SRA |
| Star Wars: Dark Forces (U) | SLUS-00297 (J SLPS-00685, PAL SLES-00585 / 00640 / 00646) | `AUTO_FIX_GPU_BUSY` (OpenGX) **and** `AUTO_FIX_FLAT_TEX_WRAP` (soft GPU `drawPoly3FT`) | GPU busy timing. wrap U/V at 8 bits in the fast soft rasterizer |
| FIFA: Road to World Cup 98 (U) | SLUS-00520 (J SLPS-01383, SLPS-91150, PAL SLES-00914..00918) | GPU busy 3 in 4 | GPU busy timing |
| The Dukes of Hazzard: Racing for Home (U) | SLUS-00859 (PAL SLES-02343) | GPU busy 3 in 4 | GPU busy timing |
| ToHeart (J) | SLPS-01919 / 01920 | GPU busy 3 in 4 | GPU busy timing |
| Ishin no Arashi (J) | SLPS-01158 (also SLPM-86861, SLPM-86235) | GPU busy 3 in 4 | GPU busy timing |

`gpu_timing_hack_db`: forced DMA2 chain time (`psxdma.c`). Proper fix: accurate GPU command
timing.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Judge Dredd (U) | SLUS-00630 (PAL SLES-00755) | 1024 cycles (CD + MDEC + DMA + GPU timing) | GPU, MDEC and CD timing |
| Soul Blade (U) | SLUS-00240 (PAL SCES-00577) | 512K cycles (floods the GPU in menus) | GPU command timing |
| EA Sports F1 2000 | see above | 300K cycles | see above |

`gpu_centering_hack_db`: screen centering type 1, soft GPU (gpulib) only. Proper fix:
automatic centering from the display range, in both renderers.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Gradius Gaiden (J) | SLPM-86042 (also SLPM-86103, SLPM-87323) | centering type 1. Also the commented-out "lClearOnSwap" test in `gpuPrim.c` | centering from the display range |
| Sexy Parodius (J) | SLPM-86009 | centering type 1 | same |
| Salamander Deluxe Pack Plus (J) | SLPM-86037 | centering type 1 | same |

`gpu_game_fixes[]` (`dwActFixes`) and OpenGX code keyed on one game's data:

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Chrono Cross (U) | SLUS-01041 (disc 1), SLUS-01080 (disc 2). J IDs in `database.c` | `AUTO_FIX_CHRONO_CROSS`: OpenGX GPUSTAT bit 31 toggles every 3rd status read, not per field | derive bit 31 from the scanline / field, as gpulib does |
| Vagrant Story (U) | SLUS-01040 (J SLPS-02377, SCPS-45486, SLPS-91457, SLPM-87393) | `AUTO_FIX_VRAM_READBACK` (EFB snapshots from boot). Never tested: not on the test card | an auto policy that learns early enough. bit 15 per pixel (STP_BIT15.md) |
| Pro Pinball (which title: look up. the code says "pinball") | any region | `TitleFillArea` / `FillSoftwareArea`: a 1x1 fill at (1020,511) changes colour by +1 per call ("emu protection") | find what the protection reads (timing or readback) |
| Tekken 3 (U) | any region (SCUS-94388) | `CheckForEndlessLoop` (all games, made for it) | gpulib-style display-list loop detection with a count limit |

Bit 15 and mask bit (no hack yet, but the proper bit-15 fix in `Docs/STP_BIT15.md` needs a
test for each). The source is DuckStation's commit messages for its mask-bit work.

| Game (region) | Serial to get | What uses bit 15 |
|---|---|---|
| Silent Hill (U) | SLUS-00707 | fog, boxes around trees (check mask) |
| Ghost in the Shell (J or U) | SCPS-10043 (J). U serial: look up | menu effect |
| Dragon Quest VII (J) | SLPM-86500 (disc 1, look up) | menu text colours |
| TwinBee RPG (J) | SCPS-45208 | fade effect |
| Duke Nukem: Land of the Babes (U) | look up | water |
| Ultraman: Fighting Evolution (J) | SLPS-01248 | shadows |

Also needed for bit 15, and free: the ps1-tests `gpu/mask-bit` and `gpu/transparency`
executables (`$PS1_TESTS/bin/gpu/`). They are not in the library yet.

### 1.3 CD

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| T'ai Fu: Wrath of the Tiger (U) | SLUS-00787 | `cdr_read_hack_db`: `cdrAlignTimingHack` aligns CD read IRQs to vblank | accurate CD IRQ and BIOS handler timing |
| Syphon Filter 2 (U) | any region (SCUS-94451 disc 1) | `cdrom.c`: commands work soon after spin-up | measured spin-up / command timing |
| Rayman (U) | any region (SLUS-00005) | `cdrom.c`: track-change detection (play / SubQ) | accurate CDDA play and SubQ |
| Twisted Metal 2 (U) | any region (SCUS-94306) | `cdrom.c`: skip pregap, accurate SubQ start | same |
| Wild 9 (U) | any region (look up) | `cdrom.c`: skip pregap, accurate SubQ start | same |
| Gundam Battle Assault 2 (U) | any region (look up) | `cdrom.c`: 7000-cycle pause response | measured Pause timing |
| Rockman X5 / Mega Man X5 | any region (look up) | `cdrom.c`: seek-time table | measured seek timing |
| Croc: Legend of the Gobbos (U) | any region (look up) | `cdrom.c`: acknowledge delay ("Croc, Shadow Tower vs Discworld Noir") | measured acknowledge timing |
| Shadow Tower (U) | any region (look up) | same acknowledge delay | same |
| DDR Konamix, Discworld Noir, EA F1 2000 | see 1.1 / 1.2 | pause response, acknowledge delay, F1 2000 read delay | see above |

`errorRetryhack` (more than 100 bad Setlocs) and `RetryDetected` are general and name no
game. They need no game to get.

### 1.4 SPU

No SPU code is keyed by game. Discworld Noir's audio skips and Legend of Legaia's A/V
desync are CPU-timing overrides (1.1).

### 1.5 Input and serial port

No input code is keyed by game ID. MediEvil's analog mode (5.14.0, `PadSSSPSX.c`) follows
the protocol, not the ID.

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Armored Core (U) | any region (SCUS-94182) | `psxhw.c` `sio1ReadStat16` returns 0xa0 (all games): link-cable misdetection | implement SIO1 |
| An F1 game with link-cable support (any of the F1 titles above) | any | same SIO1 stub | same |

### 1.6 Memory card

| Game (region) | Serial to get | Hack | What a proper fix needs |
|---|---|---|---|
| Lifeforce Tenka / Codename Tenka (U) | SCUS-94409 (PAL SLES-00613..00617, SLED-00690) | `MemorycardHack_db`: card 2 is off while the game runs (the leak to later games is fixed) | find why the game hangs with two cards (card protocol / timing) |

### 1.7 Not needed

- `CheckPsxType` (Wild Arms SCUS-94608, DTLS-3035, PBPX-95001/95007/95008): region data, no
  fix needed.
- `psxbios.c` RAM address 0 (R-Type, CTR, Fade to Black): matches the real BIOS. Keep.
- `libcrypt_ids`: only a warning when the `.sbi` is missing.

## 2. Already in the library (for reference)

| Game | Library folder / boot file | Serial (file name or redump) | Hacks and per-game workarounds | Test material |
|---|---|---|---|---|
| Crash Team Racing (U) | `Crash Team Racing` | SCUS-94426 | cycles 200 ("sometimes slow", a WiiStation workaround). bit-15 tile heuristic made for its transitions (STP_BIT15.md). RAM address 0 (keep) | `ctr_demo.txt`, `ctr_tiger_attract*.txt`, `ctr_tt_pause.txt` |
| Ape Escape (U) | `Ape Escape` | SCUS-94423 | `AUTO_FIX_VRAM_READBACK`. bit 15 of its paused frame | `ape_escape_play.txt` (`replay_rest.txt`) |
| Spyro the Dragon (U) | `Spyro the Dragon` | SCUS-94228 | `AUTO_FIX_VRAM_READBACK` (pause readback) | `replay3.txt`, `spyro_the_dragon_play.txt` |
| Gex 2 - Enter the Gecko (U) | `Gex 2 - Enter the Gecko` | SLUS-00598 | `AUTO_FIX_VRAM_READBACK` (NTSC-U only). re-uploads keep 0000h as a hole except CPU-written pixels (6cd8062). F1 2000 read delay skipped during XA (b8ceb1b) | `gex2_quest_pointer_play.txt` |
| Dino Crisis 2 (U) | `Dino Crisis 2 (USA)` | SLUS-01279 | `AUTO_FIX_DINO_CRISIS2` (OpenGX `primSprtS` drops every 64x48 textured sprite) and `AUTO_FIX_VRAM_READBACK` | no recording reaches the readback scenes: one is needed |
| Crash Bash (U) | `Crash Bash` | SCUS-94570 | `gpu_slow_llist_db` (pause and rules text) | `crash_bash_play.txt`, `crashbash_dump.txt` |
| Point Blank (U) | `Point Blank (USA)` | SLUS-00481 | `gpu_slow_llist_db` (calibration cursor) | `bios_pointblank*.txt`, `gun_reticle.txt` |
| MediEvil (U) | `Medievil` (5 formats) | SCUS-94227 | `cdrom.c` seek-time comment ("Medievil = ?-4x"). off-screen headstone text (general fix) | `fmv_me.txt`, `medievil_analog.txt` |
| Final Fantasy VII (U), disc 1 | `Final Fantasy VII/Final Fantasy VII 1` | SCUS-94163 (the file name says SLUS-94163) | no ID-keyed hack | `final_fantasy_vii_1_play.txt` |
| Crash Bandicoot 3 (U) | `Crash Bandicoot 3 - Warped` | SCUS-94244 (the file name says SLUS-94244) | no ID-keyed hack | `replay3.txt` |
| Gex, Gex 3, Frogger, Resident Evil 3, Micro Machines V3 | library | - | no ID-keyed hack | `library20.txt` |

## 3. Priority

Games to obtain: **76 titles** in section 1 (80 table rows less 4 rows that point to
another row. the four Japanese DDR mixes and NHL FaceOff 97 / 98 count as one row each). 70
of them have a hack or a game-motivated workaround, 6 are bit-15 test games. By number of
hacks keyed to the game:

1. **EA Sports F1 2000** (SLUS-01120): slow list walking, GPU timing 300K, the CD read
   delay. Three hacks, three different timing models.
2. **Hot Wheels Turbo Racing** (SLUS-00964): GPU busy, old-dynarec fix.
3. **Star Wars: Dark Forces** (SLUS-00297): GPU busy (OpenGX), flat-texture wrap (soft GPU).
4. **Discworld Noir** (SLES-01549): cycles 222, CD acknowledge delay.
5. **DDR Konamix** (SLUS-01446): fractional frame rate, CD pause response.

Then, by value for open work:

- **Vagrant Story** (SLUS-01040) and **Silent Hill** (SLUS-00707): the readback and bit-15
  work (STP_BIT15.md).
- **Chrono Cross** (SLUS-01041): one small fix retires its flag (GAME_HACKS.md order, item 4).
- **Spyro 2** (SCUS-94425): fractional frame rate on a game close to Spyro, which has
  recordings.
- **Brave Fencer Musashi**, **Parasite Eve II**, **Vandal Hearts I / II**: the cycle
  overrides with the most IDs, for the accurate-timing work.
- **Lifeforce Tenka** (SCUS-94409): the only memory-card hack.
- **T'ai Fu** (SLUS-00787): the only CD hack keyed by ID.
