# Test regimen

Three tiers. The first two use games already in the test library (`C:/wiistation-lib/games`)
and run unattended (`scripts/wsx.sh chain`, the bench Wii through `wii_lab.py`). The third is
the list of games to add for major releases: each is there for something the others do not
cover. Every game is the owner's own disc, dumped by the owner.

## Tier 1 -- every build (about 30 minutes, Dolphin or bench Wii)

| Game | Covers | Chain |
|---|---|---|
| Spyro the Dragon | 3D, Lightrec compile spikes (the elevator), SPU reverb, recording replay | `spyro_tornado.txt` |
| Crash Bandicoot 3 | variable frame rate, pad timing, the starfield (1x1 tiles) | `replay3.txt` |
| Crash Team Racing | EFB readback (rain), mask bit, texture-cache churn, 4-player multitap | `tiger_temple_diag.txt`, `audit_tiger.txt` |
| Final Fantasy VII | MDEC FMV (15-bit), the opening present path, battle swirl | `replay_rest.txt` |
| Ape Escape | analog sticks (required), rumble, fades | `replay_rest.txt` |
| Crash Bash | CPU-bound, tiling pass, multitap | `replay_rest.txt` |
| ps1-tests (JaCzekanski) | GPU/DMA/timer accuracy against a real PS1 | `ps1tests.txt`, `ps1tests_check.py` |

Proof bars as in AGENTS.md: frames 0-different outside the overlay (pair kept frames by
position), guest identical (`vsig_cmp.py`) for timing-neutral changes, speed from the bench Wii.

## Tier 2 -- every release (adds the rest of the library, about 2 hours)

| Game | Covers |
|---|---|
| Dino Crisis 2 | 24-bit FMV, EFB readback (`dino2_readback.txt`) |
| Resident Evil 3 | 24-bit/15-bit FMV, pre-rendered backgrounds, CD streaming |
| MediEvil (CUE, CBN, CHD, CHD-LZ, PBP) | MDEC (`LockedCache=4`), every disc format |
| Frogger (9 formats) | the disc loaders (`Docs`: disc formats) |
| Gex, Gex 2, Gex 3 | HLE BIOS paths, XA streaming, the ghost-cursor upload |
| Micro Machines V3 | 8-player multitap, a 704 MB track (too big for the bench card agent's put) |
| Point Blank | GunCon |
| PadTest DX | the 12-cell controller matrix (`padtest_dx.sh`) |
| Medievil / any game, `CpuTiming`/`GpuTiming` Accurate | the Accurate options |
| Two of the above with save states and memory cards | `wsx.sh statecheck`, card writes |

## Tier 3 -- major releases and extensive testing: games to add

Grouped by what each one adds. Priority 1 = add first.

| P | Game | Why it is on the list |
|---|---|---|
| 1 | WipEout 2097 / XL | CD-DA (Red Book) music during gameplay; fast 3D with heavy semi-transparency |
| 1 | Ridge Racer (or Rayman) | CD-DA music; Rayman adds a 2D sprite-heavy game |
| 1 | Metal Gear Solid | two discs with a swap mid-game; codec screens; FMV; thermal-goggle framebuffer effects |
| 1 | Final Fantasy VIII | FMV with real-time polygons drawn over it (MDEC and GPU together); four discs |
| 1 | Silent Hill | fog and noise effects, dithering, heavy semi-transparency, SPU reverb |
| 1 | Castlevania: Symphony of the Night | 2D with many transparent sprites, XA audio |
| 1 | Tekken 3 | 60 fps 3D fighting: GTE and GPU load, frame pacing |
| 1 | Gran Turismo 2 | hi-res interlaced menus, analog and rumble, long play sessions |
| 1 | A PAL LibCrypt game (e.g. Spyro 2 or Spyro: Year of the Dragon, PAL) | 50 Hz timing, the SBI/LibCrypt path |
| 1 | Spyro: Year of the Dragon (NTSC) | anti-piracy checks that trip on wrong timing |
| 2 | Legacy of Kain: Soul Reaver | seamless streaming (no load screens): CD read timing under load |
| 2 | Vagrant Story | complex 3D and effects; timing-sensitive |
| 2 | Chrono Cross | framebuffer effects, two discs |
| 2 | Final Fantasy IX | battle transitions (framebuffer), four discs, long RPG sessions |
| 2 | Tobal 2 (or another 640x480 game) | high-resolution interlaced 3D at 60 fps |
| 2 | Parasite Eve | MDEC backgrounds with 3D over them |
| 2 | PaRappa the Rapper (or Dance Dance Revolution) | audio/video sync is the gameplay |
| 2 | Driver | open-city streaming, CPU load |
| 2 | Tony Hawk's Pro Skater 2 | CPU and GPU heavy at once |
| 2 | Resident Evil 2 | two discs, DualShock, FMV (pairs with RE3 already in the library) |
| 2 | Bomberman (Party Edition or World) | 5-player multitap |
| 2 | Time Crisis | a second GunCon game (pairs with Point Blank) |
| 3 | Command & Conquer | PlayStation Mouse |
| 3 | Twisted Metal 2 | split-screen 2-player |
| 3 | Xenogears | mode-7-like world map, two discs |
| 3 | Street Fighter Alpha 3 / Mega Man X4 | 2D fighters/platformers: sprite counts, frame-exact input |
| 3 | Suikoden II or Wild Arms | 2D RPGs: menus, text, long sessions with saves |
| 3 | Harvest Moon: Back to Nature | a light game: the floor for speed and audio |

How a new game enters the regimen: add it to the library only (`scripts/profile.sh`,
`library_check.py` checks the .cue), record one play session with `scripts/movie_capture.sh`
(it becomes `scripts/autoinput/NAME_play.txt`), add a chain line, and put the game on the
bench Wii's card (`wii_check.py card` first; the agent's put limit is 512 MB per file).

## The games added 2026-10-21 (`C:/projects/@Shared/ROMs/PS1`, copied into the library)

Boot test: `scripts/chains/newgames_boot.txt`, 40 s each, no input, Dolphin, HLE BIOS.

| Game | Format | Boot test | What it tests | Planned chain |
|---|---|---|---|---|
| Bomberman: Party Edition | CUE/BIN | ok | 2D, many sprites, 5-player multitap | multitap |
| Final Fantasy VIII disc 1 | CCD/IMG/SUB (+ a CUE) | **broken**: "Published by" tiny in a corner, then black with a garbled line; 446 CD reads, then stalls | 24-bit MDEC FMV, FMV with 3D over it, subchannel | fmv |
| Final Fantasy VIII discs 2, 3 | IMG.ECM | **not loadable**: no ECM reader | ECM format | formats |
| Final Fantasy VIII disc 4 | CUE/BIN | same as disc 1 (every disc boots the intro) | CUE against CCD of the same game | formats |
| Gran Turismo 2 (Simulation) | CUE/BIN | ok (title) | hi-res interlaced menus, analog, long races | gpu, input |
| Soul Reaver | CUE/BIN (its .cue named a missing .bin: fixed in the library copy) | ok (title) | seamless streaming, realm-shift warp effect | cd, gpu |
| PaRappa the Rapper | CUE/BIN | ok | audio/video sync, XA, input timing | audio |
| Rayman | CUE + 51 tracks | ok | **CD-DA music** (the only CD-DA game in the library) | audio, formats |
| Silent Hill | CUE/BIN | ok (warning screen, FMV) | fog, noise, dithering, semi-transparency, reverb | gpu, fmv |
| Spyro 2 | CCD/IMG/SUB | ok (title) | the Spyro engine, subchannel | replay |
| Spyro 3 (CCD/IMG/SUB copy) | CCD/IMG/SUB | runs but presents nothing: **a modified image**, not a WiiStation bug -- same size as v1.0, 388,031 bytes differ from sector 4 (the licence area) on | what the protection does with an altered disc | none (keep as a negative case only) |
| Spyro 3 v1.0 (USA) | CUE/BIN | ok: intro, title, demo mode | anti-piracy checks on a clean disc | replay, protection |
| Spyro 3 v1.1 (USA, Rev 1) | CUE/BIN | ok: intro, title, demo mode | the revision (different code and size: 664.8 MB) | replay, protection |
| Tobal 2 (Japan) | CUE/BIN | ok | 640x480 interlaced 3D at 60 fps | gpu |
| Vagrant Story | CUE/BIN | ok (opening FMV) | heavy 3D, battle-sphere overlay, timing | gpu, fmv |

## Movies to record (`scripts/movie_capture.sh GAME NAME --mins N`)

Each one plays a fixed route from power-on, so the replay reaches the same moments every time.
New game, no memory card unless the route says so. Keep the route and the length.

| Game | Route (from power-on) | Length | Settings |
|---|---|---|---|
| Rayman | skip the intro, start a new game, play the first level of the Dream Forest to the first cage, open the pause menu once | 5 min | digital pad |
| PaRappa the Rapper | start, play stage 1 (Chop Chop Master Onion) to the end of the song | 4 min | digital pad |
| Silent Hill | new game, the opening FMV, the alley chase until Harry falls, then the first café scene | 6 min | analog off |
| Vagrant Story | new game, the opening cutscene (skip nothing), the first battle using the targeting sphere, open the menu once | 6 min | digital pad |
| Soul Reaver | new game, the opening, play through the first area including one shift to the spectral realm and back | 6 min | analog |
| Gran Turismo 2 | Simulation mode menus (open a few, the hi-res screens), then one license test (B-1) and one Arcade race | 6 min | analog, rumble on |
| Tobal 2 | 1P versus, pick any fighter, fight two rounds | 3 min | digital pad |
| Spyro 2 | new game, the intro, Glimmer: run, glide, collect gems, talk to one character | 5 min | analog |
| Bomberman: Party Edition | battle mode, you against CPU players (as many as it allows), two rounds | 4 min | multitap if you have one |
| Final Fantasy VIII disc 1 | after the boot bug is fixed: the opening FMV, then walk from the infirmary to the classroom (FMV backgrounds with 3D) | 8 min | digital pad |
| Spyro 3 v1.1 | new game, the intro, Sunrise Spring to the first level (v1.0 replays it as a second line: the two differ) | 5 min | analog |

## Do disc formats change anything? (the study)

What a format can change: (1) audio tracks -- an ISO or a data-only image has no CD-DA, so the
game plays silence; (2) subchannel data -- .sub/.sbi carry the Q channel that LibCrypt and the
GetlocP/GetlocL commands read, without it the CD code fakes it; (3) the cost of reading a
sector on the Wii -- CHD, PBP and CBN decompress, ECM rebuilds the error-correction bytes; (4)
packaging -- PBP holds several discs. The guest's data reads are the same bytes in every
format, so the guest must run identically (`vsig_cmp.py`: ram, cycle, pc and the CD hash)
wherever the data is there; only the Wii's time per read differs.

The runs, one game per question:
- Rayman CUE (51 tracks) against Rayman CHD (`chdman createcd` keeps the audio): CD-DA, guest identical, the cost (`cd:`) on the bench Wii.
- Spyro 2 CCD+SUB against the same image with no .sub (and a CHD): what the subchannel changes.
- Final Fantasy VIII disc 1 CCD against its CUE (both are in its folder).
- One game as PBP (`psxpackager`) against its CUE: decompression cost.
- ECM: add a reader or decompress the two discs to .img; then ECM against IMG.
**Result in Dolphin (2026-10-21, `scripts/chains/format_study.txt`):** guest RAM, CPU cycles and PC
identical on every vblank for Rayman CUE (51 tracks) vs CHD, Spyro 2 CCD+SUB vs plain CUE, and
FF8 disc 1 CCD vs CUE. Spyro 2 does not read the subchannel in its first 50 s. With a .ccd and a
.cue in one folder the loader takes the CCD (+sub) even when the .cue is named, so a CUE-only
comparison needs its own folder (`Formats/... [CUE]`, a hard link to the same .img). The `cd`
and `jitb` lines of vsig_cmp count across a chain, so they differ between chained games anyway.
The cost on the Wii (bench, `scripts/chains/format_cost_rayman.txt`, speed preset, 3000 vblanks,
5745 sector reads each): CUE 761 ms of CD time on the emulator thread (1.5%, worst read 16.8 ms),
CHD 225 ms (0.45%, 68 ms of it decompressing, 5028 hunk hits / 717 misses, worst 17.0 ms). A CHD
read brings 8 sectors from the card at once; the plain image takes many small reads through the
read-ahead thread. Both are small; CHD is the cheaper of the two on a Wii.

**PBP and maximum compression (2026-10-21).** PBPs are made with PSXPackager v1.6.3 built from
source (`C:/tools/psxpackager-src`, `dotnet build PSXPackager/PSXPackager-windows.csproj`, run with
`DOTNET_ROLL_FORWARD=Major` and Windows paths; local patch: the console-cursor calls guarded, it
crashed with its output redirected). The release zip is not used: 0/19 AV engines and ClamAV clean,
but MetaDefender's sandbox said "Likely Malicious" with 0 indicators. Inputs are staged in
`C:/wiistation-lib/work/pbp_stage` (hard links, a .cue for FF8 discs 2-3, an .m3u for multi-disc).
- Guest identical to the original (vsig_cmp, every vblank): Frogger PBP level 0/5/9, Rayman (51
  tracks), Spyro 2, Spyro 3 Rev 1, MediEvil, and the 2.2 GB 4-disc FF8 PBP at `Disc=1..4`
  (`scripts/chains/pbp_study_a.txt`, `pbp_study_ff8.txt`). Frames identical outside the overlay.
- Fixed on the way (8ea05e11): discs 2+ of a PBP were unreachable (Swap CD on the same PBP now
  moves to the next disc; chain lines take `Disc=N`); level-0 PBPs (stored blocks) never booted.
- Open: CD-DA from PBP, CHD and CBN starts 24 ms (1152 samples at 48 kHz) later than from the CUE
  in Dolphin, the same data (`pbp_frogger_*.txt`); check on hardware (a click at music start?).
- Size (MB) -- BIN / PBP level 5 / CHD default / CHD `-c cdlz,cdzl,cdfl,cdzs` 8 frames a hunk /
  the same at 32 frames (`-hs 78336`): Frogger 254 / 160 / 151 / 150 / 139; Rayman 617 / 509 /
  323 / 323 / 320; MediEvil 562 / 363 / 334 / 333 / 323. PBP is poor on CD audio (deflate);
  CHD's FLAC is not. All CHDs decode to the identical guest (`chd_max.txt`).
- Cost on the bench Wii (speed preset, Rayman 3000 vblanks, `format_cost_rayman_pbp.txt`): CD time
  CUE 764 ms, CHD 224 ms, PBP 152 ms. In Dolphin, 32-frame hunks cost 2.7 s of CD time in 30 s of
  Frogger against 25 ms at 8 frames (about 21 ms a miss): the smallest files may stutter.

Earlier result (Frogger, 9 formats, and MediEvil, 5): the loaders all boot the same game; this
adds audio, subchannel and cost.

## Before the SD card is pulled

Every new game except Rayman is over the HBC agent's 512 MB put limit, so they go onto the bench
Wii's card by hand: `sd:/wiistation/isos/<the folder name above>/`, the same folder names as
the library. Rayman can also go over the network. FF8 discs 2 and 3 need converting first.

## The plan for the new games

1. **Boot (done, Dolphin):** 14 discs. Two left broken: FF8 (the opening goes black after the
   publisher text: a WiiStation bug, next to investigate) and the modified Spyro 3 copy (the
   image, not WiiStation; v1.0 and v1.1 boot).
2. **Movies (you, about an hour):** the routes above with `scripts/movie_capture.sh`. Each becomes
   `scripts/autoinput/NAME_play.txt`; FF8 after its fix.
3. **Chains, by what each game tests** (one line per recording, the settings it was recorded with):
   - `replay_new.txt`: every recording, for determinism (`vsig_cmp.py`) and the tier 2 sweep;
   - audio: Rayman (CD-DA), PaRappa (sync), Silent Hill (reverb) -- `wsx.sh audio` with
     SoundRateControl=0 SoundTempo=0 on both sides;
   - GPU/effects: Silent Hill, Vagrant Story, Soul Reaver (realm shift), Tobal 2 (640x480i), GT2
     menus -- frames 0-different outside the overlay;
   - FMV/MDEC and CD streaming: FF8, Silent Hill, Vagrant Story, Soul Reaver, Bomberman;
   - input: GT2 (analog, rumble), Bomberman (multitap), PaRappa (timing);
   - protection: Spyro 3 v1.0 and v1.1 (the modified copy as the negative case);
   - formats: Rayman CUE vs CHD, Spyro 2 CCD with and without .sub, FF8 disc 1 CCD vs CUE, one
     game as PBP -- guest identical (`vsig_cmp.py`), the read cost from the bench Wii.
4. **Tiers:** tier 1 (every build) gains Spyro 3 v1.1, Rayman and Silent Hill; tier 2 (every
   release) takes all of them.
5. **Hardware baselines:** one `speed`-preset chain of every recording on the bench Wii, filed
   with `scripts/baseline_add.py`, so a later regression is a diff of two rows.
