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
