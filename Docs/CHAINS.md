# Test chains

`scripts/chains/` holds the chain files that `wsx.sh chain`, `wii_lab.py` and `wii_run.py` run: one file, one or more
games in one boot, each with its own settings and input script (format: `.claude/skills/wiistation-diagnostics/references/measuring.md`).
This page sorts the 201 files by what they are for. It came out of an audit on 2026-11-09 (git history, citations
in docs and scripts, use counts in `baselines/bench.sqlite`, every game checked against the library).

Nothing here is to be deleted: the investigation records are kept as the exact setup behind a past result.

## Rules for a new chain

- Line 2 is a `#` comment that says what the chain is for, which build and which Wii (if not Dolphin).
- A chain that pairs with an input script uses the same name (`scripts/autoinput/NAME.txt`).
- A chain that becomes a standing test goes in the first table below, and the doc that uses it names it.
- `python scripts/library_check.py` checks that every game a chain names is in the library.

## Standing suites and regression checks (30)

Run these when a change touches their area; the skill and AGENTS.md name most of them.

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `all` | 11 | Every game in the shared sync folder, 3600 vblanks (one emulated minute) each. | Docs/PERFORMANCE_PLAN.md, measuring.md, scripts/wsx.sh +1 |
| `ape_wii_demo` | 1 | Ape title demo on any Wii (dev or LRWii): same pad on port 1 everywhere (scripted) |  |
| `bios_formats` | 11 | Every disc format with the real BIOS (BootThruBios), formats.txt's nine Frogger copies plus | case-studies.md |
| `carry` | 4 | Emulator state carried from one game into the next (root counters, SPU, SIO, pads). | baselines/chain_carry_20260922/notes.md, baselines/chain_carry_20260922_pre/notes.md, case-studies.md +1 |
| `cd_ab` | 16 | The CD settings on a Wii: four games that read the disc hard, each with a 16, 64 and | SETTINGS.md, measuring.md |
| `crashtest` | 1 | Crash-report check: a DSI on purpose at vblank 600 (debug build), as the 2026-09-29 Lightrec crash. | scripts/wii_clean.py |
| `ctr_demo` | 1 | CTR left alone: intro, title, then the attract demos. 2.5 minutes. | Docs/GAMES_TO_GET.md, Docs/STP_BIT15.md, case-studies.md |
| `ctr_tiger_attract` | 1 | CTR left alone (no input): intro, menu, then the attract demos Crash Cove, Roo's Tubes and | Docs/GX_QUALITY.md, Docs/STP_BIT15.md, unattended-run.md |
| `ff7x3` | 3 | FF7 three times in one boot: the three rows must match (state carried between games). | baselines/chain_ff7x3_20260922/notes.md, case-studies.md, measuring.md |
| `formats` | 8 | Frogger in every disc format WiiStation loads (scripts/disc_convert.py), 1800 vblanks each, no input | Docs/ACHIEVEMENTS.md, case-studies.md |
| `gpu_carry` | 3 | GPU plugin state carried from one game into the next. FF7 booted after Medievil made no | case-studies.md, measuring.md |
| `gte_testall` | 1 | ps1-tests gte/test-all alone (real-PS1 log: $PS1_TESTS/ref/gte_test-all.psx.log). | test-roms.md |
| `gte_testall_int` | 1 | ps1-tests gte/test-all on the interpreter (Core=1); real-PS1 log $PS1_TESTS/ref/gte_test-all.psx.log. | test-roms.md |
| `hangtest` | 1 | The hang watchdog (Gamecube/ws_crash.c): the input script spins the emulation thread at | scripts/wii_clean.py, unattended-run.md |
| `lab_check` | 1 | lab connection check | Docs/PARKED_BUGS.md |
| `lc_ab` | 6 | The locked cache on and off, same game, same boot: a hardware session runs this file | Docs/LOCKED_CACHE.md, measuring.md, scripts/chain_compare.py |
| `library20` | 17 | every game of the library (games.tsv), 1200 vblanks each, no input: intros and demos (regression and anomaly sweeps) | CONTRIBUTING.md, Docs/GAMES_TO_GET.md, Docs/MEMORY_MAP.md +3 |
| `long_play` | 2 | The user's 10-minute sessions (2026-09-30), one boot: a soak of real play. | Docs/ACHIEVEMENTS.md, Docs/ACHIEVEMENTS_NOTES.md, scripts/autoinput/bit15_spyro.txt +3 |
| `menu_hw` | 0 | No game: the menu comes up (chainLoad finds none). The input script goes to sd:/wiistation/autoinput.txt: sd:/wiistation/autoinput.txt |  |
| `play` | 5 | The user's five recordings (scripts/movie_capture.sh), each with its own chain line: real play. | Docs/ACHIEVEMENTS.md, Docs/ACHIEVEMENTS_NOTES.md, Docs/GAMES_TO_GET.md +52 |
| `ps1tests` | 10 | JaCzekanski ps1-tests ($PS1_TESTS, build-158): CPU, timer, DMA, GTE tests whose | CONTRIBUTING.md, Docs/ACCURACY.md, Docs/BACKENDS.md +4 |
| `ps1tests_gpu` | 44 | JaCzekanski ps1-tests GPU tests that have a real-PS1 result ($PS1_TESTS/bin/gpu/<t>/): | Docs/ACCURACY.md, SETTINGS.md, scripts/ps1tests_gpu_check.py +1 |
| `ps1tests_gpu_modes` | 56 | The ps1tests_gpu.txt tests under the other two SoftGpuMode values (1 Hi-Fi, 2 Fast) of | test-roms.md |
| `replay3` | 2 | The user's 2026-09-30 sessions, first 3 minutes each: a replay-exactness check | Docs/ACCURACY.md, Docs/ACHIEVEMENTS_NOTES.md, Docs/BACKENDS.md +4 |
| `replay_rest` | 3 | FF7, Ape Escape, Crash Bash recordings (from play.txt): main vs phase 2 replay A/B | Docs/ACCURACY.md, Docs/ACHIEVEMENTS_NOTES.md, Docs/BACKENDS.md +3 |
| `smoke` | 2 | Two short games: proves a chain boots its second game and powers off at the end. | CONTRIBUTING.md, measuring.md |
| `spyro_speech` | 1 | Spyro through the level-intro speech (scripts/autoinput/spyro_speech.txt) to vblank 5400, rate | measuring.md, scripts/autoinput/spyro_mouth_trace.txt, scripts/autoinput/spyro_mouth_xfb.txt +2 |
| `statecheck3` | 3 | statecheck in every game of a chain: PadTest DX three times, "statecheck 120 60" each. | test-roms.md |
| `thread_check` | 8 | Thread leak check: eight games of 10 s, read-ahead on (2026-09-30). |  |
| `three` | 3 | One game of each kind, 3600 vblanks each, about 3.5 minutes: 3D (Spyro, with its | Docs/PERFORMANCE_PLAN.md, Docs/done/LIBOGC3_PLAN.md, measuring.md |

## Reusable checks no doc names yet (27)

Short A/B or feature checks worth running again. `ctr_vs4`, `ctr_1p_4p` and `ctr_vs4_rep` (4-player CTR on a multitap,
with `scripts/autoinput/ctr_vs4.txt`, `ctr_a1.txt` and `ctr_vs4_rep.txt`) came back from the deleted `ws-ctr-4p` and
`ws-ctr-perf` worktrees on 2026-11-09; `ctr_1p_4p` was never run as a pair.

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `ach_official_spyro` | 1 | Official mode, Spyro only (spectator): the set, presence, boards, board 0's scores and the first badge (achdump) |  |
| `analog_regress` | 4 | AnalogAuto check (Analog pad): Ape Escape (locks analog itself), Spyro (44h itself), MediEvil (never 44h: the new switch), Gex 3 (same). |  |
| `ape_attract` | 1 | Ape Escape idle attract demo (Spike chases a monkey): grass tearing (user 2026-11-01) |  |
| `ape_finish_lvl1` | 1 | finish_lvl1 (user, 2026-11-01) with the user's picture settings. FrameRate=1 (TV) and GpuTiming=1 change the timing: the replay parts fro... |  |
| `ape_finish_lvl1_wii` | 1 | finish_lvl1_wii (user, 2026-11-01, recorded with GpuTiming=1 FrameRate=1) with the user's picture settings |  |
| `ctr_1p_4p` | 2 | Crash Team Racing, Crash Cove: 1P Arcade then 4P VS (multitap), same chain. Race windows ~7000-11000 vblanks. Untested as a pair. |  |
| `ctr_mirror_vs4` | 1 | PadMirror: the 4P VS script with p1 lines only. Mirror on: 4P selectable, all four karts on slot A's input. |  |
| `ctr_speed_ab` | 2 | CTR 4P race, one boot: CpuSpeed Auto (no CTR underclock since try/ctr-clock = 100%) then 150%. p1-only script, PadMirror; race ~6300-11000. |  |
| `ctr_vs4` | 1 | Crash Team Racing 4-player VS on a multitap, Crash Cove, 3 laps (needs ctr_vs4.txt). Race lights at ~vblank 6300; race window 6800-11000. | scripts/autoinput/ctr_vs4_p1.txt |
| `ctr_vs4_rep` | 1 | Monty's 4P VS movie (ctr_vs4_play.txt, recorded 2026-11-05) with PadMirror on and 61 report lines (ctr_vs4_rep.txt): per-window speed on... |  |
| `decode_state` | 9 | Decode-ahead (5.13.0) and save states: MediEvil's opening video from every compressed |  |
| `decode_state_off` | 9 | Decode-ahead (5.13.0) and save states: MediEvil's opening video from every compressed |  |
| `decode_switch` | 12 | Decode-ahead (5.13.0) under game switches: MediEvil's opening video (streamed, XA) from a |  |
| `decode_switch_off` | 12 | Decode-ahead (5.13.0) under game switches: MediEvil's opening video (streamed, XA) from a |  |
| `dsp_ab` | 8 | The CPU sound path (SoundDriver=0, SDL) and the DSP path (1, AESND) on the same game, |  |
| `ff7_dump450` | 1 | *(no comment in the file)* |  |
| `frameskip_ab` | 4 | Frame Skip (SkipFrames) on OpenGX: off and on, same game, same boot. Two heavy games: |  |
| `frameskip_off` | 2 | Frame Skip off must not change anything: run this under main's DOL and the Frame Skip DOL, |  |
| `gte_ab` | 4 | GTE-heavy games, for an A/B of two builds that differ only in gte.c (one job per DOL). |  |
| `hprof_games` | 4 | whole-game CPU profile on the bench Wii (hprof, no windows), 2026-10-21 |  |
| `limiter_ab` | 4 | The frame limiter spinning (0) against sleeping on a timer alarm (1), same game, same boot. |  |
| `smb_only` | 1 | SMB share only (libogc3 HAVE_POLL_H fix check). wii_lab.py --smb-from <settings.ini> |  |
| `spyro600` | 1 | Spyro to vblank 600 (the title), read-ahead on: game 1 of thread_check.txt alone. |  |
| `spyro_30s` | 1 | Spyro left alone for 30 s (title and the first demo), for quick hardware checks. |  |
| `spyro_demo75` | 1 | Spyro left alone: title, then the attract demos (the first enters a world). 75 s, past the first world's load (vblank ~4141). |  |
| `spyro_determinism` | 4 | Spyro to vblank 600 four times in one boot: twice with the CD read-ahead thread, twice |  |
| `usb_read` | 1 | USB storage still mounts and reads with the USB pad reader started before the usb: mount |  |

## Area tests cited by a doc (85)

Tests for one area (achievements, bit 15, GX quality, CD, formats, PBP, light gun, Tiger Temple, ...). The doc named in the
last column holds the result; keep the chain so that result can be reproduced.

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `ach_book_bench` | 1 | The unlock pictures on the bench Wii (Gamecube/ach_shot.c): Spyro's attract demo with the book's set (tests/fixtures/ach_book), | Docs/ACHIEVEMENTS.md, scripts/autoinput/ach_book_bench.txt, tests/fixtures/ach_book/Tests/AchBook/set.txt |
| `ach_cost` | 4 | Achievement costs on the bench Wii: Spyro's attract demo with achievements off, then with the | Docs/ACHIEVEMENTS_NOTES.md |
| `ach_cost_517` | 6 | What the achievement system costs, 5.17.0 (2026-10-24): Spyro's attract demo and CTR's | Docs/ACHIEVEMENTS_NOTES.md, scripts/autoinput/ach_banner.txt |
| `ach_cost_official` | 4 | Achievement costs on the bench Wii: Spyro's attract demo with achievements off, then with the | Docs/ACHIEVEMENTS_NOTES.md |
| `ach_discswap` | 1 | Official mode through a disc swap: Frogger (CUE), swapped to Frogger as CHD (the set goes on), then to | Docs/ACHIEVEMENTS.md, measuring.md, scripts/autoinput/ach_discswap.txt |
| `ach_discswap_local` | 1 | A Local multi-disc set through two swaps: FF8 disc 1 (CUE), at 900 disc 2 (CCD, named by the | Docs/ACHIEVEMENTS.md, scripts/autoinput/ach_discswap_local.txt, tests/fixtures/ach_multidisc/Tests/AchMultiDisc/set.txt |
| `ach_hash_formats` | 20 | Official mode's PS1 hash in every format WiiStation boots (scripts/chains/cd_audit.txt's 16 Frogger copies, Spyro 2 as CUE/PBP/TOC+RW, FF... | Docs/ACHIEVEMENTS.md |
| `ach_hud` | 1 | The achievement indicators (Gamecube/ach_toast.c) and the list page: the game card, the | measuring.md, scripts/autoinput/ach_hud.txt, tests/fixtures/ach_hud/Tests/AchHud/set.txt |
| `ach_hud_bench` | 1 | The achievement indicators on the bench Wii (the set: tests/fixtures/ach_hud, sent with --put), | scripts/autoinput/ach_hud_bench.txt |
| `ach_local` | 3 | Local mode (Achievements = 4, Docs/ACHIEVEMENTS.md "Local mode"): the official set, nothing sent, from the card. | Docs/ACHIEVEMENTS.md |
| `ach_local_bench` | 2 | Local mode on the bench Wii (scripts/bench_ra_local.py). Spyro, Guest alone, AchievementSpectator=1 as every bench run | scripts/bench_ra_local.py |
| `ach_official_acct` | 1 | Official mode, a Mii linked to an account file that is not on the card (Docs/ACHIEVEMENTS.md, Players and Miis step 4a): | Docs/ACHIEVEMENTS.md, scripts/autoinput/ach_official_acct.txt |
| `ach_official_bench` | 2 | Official mode on the bench Wii, everything the menus show, read into perf.log by achdump | Docs/PARKED_BUGS.md, measuring.md, scripts/autoinput/ach_official_ctr.txt +1 |
| `ach_official_formats` | 20 | Official mode on the bench Wii in every format on its card: hash, then the RetroAchievements set (Casual) | Docs/ACHIEVEMENTS.md |
| `ach_official_ra` | 1 | Official mode with Local players (Docs/ACHIEVEMENTS.md, Players and Miis step 4c), Spyro, spectator (nothing reaches an | Docs/ACHIEVEMENTS.md, scripts/autoinput/ach_official_ra.txt, scripts/bench_ra_mii.py |
| `ach_overlay` | 1 | Overlay placement and small text (Gamecube/ach_toast.c): Spyro's attract demo with the HUD set, the game card then the first | scripts/autoinput/ach_overlay.txt |
| `ach_overlay_bench` | 1 | Overlay placement and animation on the bench Wii: Spyro, the HUD set, TV pictures from the input script ("xfb <vblank>", a debug build: | scripts/autoinput/ach_overlay_bench.txt |
| `ach_players` | 5 | Per-Mii achievements (Local mode, Docs/ACHIEVEMENTS.md, Players and Miis phase 3), Spyro with a set whose hit counts fire | Docs/ACHIEVEMENTS.md, scripts/autoinput/ach_players_none.txt, scripts/autoinput/ach_players_p2.txt +1 |
| `ach_toast` | 2 | Achievement toast (Gamecube/ach_toast.c): every tier in one boot, over Spyro's attract demo. | Docs/ACHIEVEMENTS.md, scripts/ach_toast_fixture.sh |
| `ape_attract_texmin` | 4 | *(no comment in the file)* | Docs/GX_QUALITY.md |
| `ape_front_reg` | 6 | *(no comment in the file)* | Docs/EFB_SYNC.md |
| `ape_front_reg2` | 5 | *(no comment in the file)* | Docs/EFB_SYNC.md |
| `ape_hand_aniso` | 3 | Ape Escape start-screen hand cursor (frames ~229-257 of a dump; vblank ~1800-1860) with the user's settings, GxTexMin 1 (Linear), 2 (Anis... | Docs/GX_QUALITY.md |
| `ape_jit` | 4 | Ape Escape title demo: A/B of the jitq queue settings around the first-run compile burst (2026-11-02). Dolphin: burst at vblank 9631, ben... | Docs/BACKENDS.md |
| `ape_jit_wii_base` | 1 | the same line on the build before the jitq changes | Docs/BACKENDS.md |
| `ape_wii_burst` | 1 | Ape title demo on the Wii, hprof build: the compile burst at vbl 8774-8775 (111 ms, 78% jit); normal speed: compile timing changes at Unl... |  |
| `ape_wii_hprof` | 1 | Ape title demo on the Wii, hprof build: what the core does in the slow stretch (vbl 8741-8801, 72-77% core) and at 8951 |  |
| `ape_wii_memset` | 1 | Ape title demo on the Wii, hprof build: who calls memset (6% of the slow stretch) |  |
| `audit_spyro` | 1 | branch audit 2026-10-20: Spyro, the first 3200 vblanks of the user session (through the elevator) | Docs/PARKED_BUGS.md |
| `audit_tiger` | 1 | branch audit 2026-10-20: Tiger Temple to the end of the first rain stretch (4869-5486) | Docs/PARKED_BUGS.md, Docs/TEST_REGIMEN.md |
| `bios_chd` | 3 | Frogger (USA) [CHD] (zstd: cdzs/cdfl) through the real BIOS set in the base settings (as a | case-studies.md |
| `bios_ctr` | 1 | Crash Team Racing through the real BIOS (BootThruBios): the end of the boot sound. | case-studies.md |
| `bios_pointblank` | 4 | Point Blank (USA): HLE vs the real BIOS (BootThruBios), LightGun 0 vs 2 (Justifier). | case-studies.md |
| `bios_pointblank_wiimote` | 4 | bios_pointblank.txt with a Wii Remote on port 1 (PadAutoAssign=1): the Justifier needs one | case-studies.md |
| `bit15` | 4 | Bit 15 (STP / mask bit) tests, JaCzekanski ps1-tests with real-PS1 results: |  |
| `bit15_hw` | 4 | GxMask on the bench Wii (Docs/STP_BIT15.md): each game with GxMask=0 (Accurate: bit 15 in the EFB | Docs/STP_BIT15.md |
| `bit15_oracle` | 8 | The bit-15 oracle (Docs/STP_BIT15.md section 11): each game to the same vblank under Soft Fast | Docs/STP_BIT15.md, case-studies.md, scripts/bit15_cmp.py +1 |
| `bit15_pics` | 9 | Bit-15 pictures (Docs/STP_BIT15.md): CTR's intro blur, Ape Escape's pause and a fade, Spyro's | Docs/STP_BIT15.md, scripts/autoinput/bit15_ctr.txt, test-roms.md |
| `bm_trace` | 1 | Bomberman explosions under OpenGX, traced (bm_trace.txt = the user movie + trace lines) | scripts/build.sh |
| `cd_audit` | 16 | CD audit (2026-10-21): every Frogger format, 1500 vblanks each; per game the cdpf: line must show the read/decode-ahead working (reads>0,... | Docs/TEST_REGIMEN.md |
| `cd_hash_check` | 5 | CD hash check: the same disc as CUE, PBP (level 10), 32-frame CHD, CBN and MDS; vsig cd/cdn must now match too | REVIEW.md |
| `cd_settings_ab` | 8 | CD settings A/B (2026-10-21): CdBuffer 16/64/256 KB and CdPrefetch off on the CUE; CdChdHunks 2/4/8 on the 32-frame CHD; fastLoad on. Eac... | Docs/TEST_REGIMEN.md |
| `chain_memory` | 6 | Memory between games: each game closes back to the same memory (perf.log gameclose:), the same game twice included | Docs/PARKED_BUGS.md |
| `chd_max` | 8 | Max-compression CHDs (chdman 0.289: default; cdlz,cdzl,cdfl,cdzs at 8 and 32 frames a hunk) against the original image; vsig_cmp pairs | Docs/TEST_REGIMEN.md |
| `crash_pause` | 1 | the user's Crash Bash session with two pauses and a minigame (2026-10-01) |  |
| `crashbash_dump` | 1 | *(no comment in the file)* | Docs/GAMES_TO_GET.md |
| `ctr_tiger_attract_skip` | 1 | ctr_tiger_attract.txt with Frame Skip on: does skipping help Tiger Temple (GPU-bound, ~12 fps at vblanks 29340-29609 on the bench Wii, 5.... | Docs/STP_BIT15.md |
| `dino2_readback` | 1 | The user's Dino Crisis 2 session (a picture read back from VRAM somewhere in it) | Docs/TEST_REGIMEN.md |
| `ff7_green` | 1 | FF7 opening: the green frame at the start of the opening video (Docs/PARKED_BUGS.md); dump every frame | Docs/PARKED_BUGS.md |
| `ff8_boot` | 2 | FF8 disc 1 opening: OpenGX against Soft (the publisher screen, then black) | Docs/PARKED_BUGS.md |
| `ff8il_hw` | 2 | bench Wii: FF8 opening then FF7 opening, OpenGX (try/ff8-interlace-upload A/B) | Docs/PARKED_BUGS.md |
| `fmv_ab` | 4 | 24-bit video colour off and on, same game, same boot: two games whose opening 2400 | SETTINGS.md, measuring.md |
| `fmv_me` | 2 | MediEvil only, from fmv_ab.txt: the 24-bit FmvColour A/B on hardware. | Docs/GAMES_TO_GET.md |
| `format_cost_rayman` | 2 | Rayman CUE (51 tracks) vs CHD on the bench Wii (speed preset): the cost of reading each format | Docs/TEST_REGIMEN.md |
| `format_cost_rayman_pbp` | 3 | Rayman CUE (51 tracks) vs CHD vs PBP (PSXPackager level 5, built from source) on the bench Wii (speed preset): the cost of reading each f... | Docs/TEST_REGIMEN.md |
| `format_study` | 6 | format study (2026-10-21): the same game in two formats, guest compared with vsig_cmp.py | Docs/TEST_REGIMEN.md |
| `gun_reticle` | 4 | The light gun reticle and the Wii Remote LEDs (Docs/GUN_MOUSE_TESTING.md). Needs a profile | Docs/GAMES_TO_GET.md, Docs/GUN_MOUSE_TESTING.md |
| `gx_aa_ab` | 2 | 240p without and with GxColor=2 (AA), the same intro moment: xfb_01/02 base, xfb_03/04 AA. | Docs/GX_QUALITY.md, scripts/autoinput/gx_ctr_aa4.txt |
| `gx_aa_ctr3` | 4 | GxColor=2 after the same settings sequence as hw_gx_quality rows 3-6 (GxTexMin=3, GxColor=1 in | Docs/GX_QUALITY.md, scripts/autoinput/gx_ctr_aa3.txt |
| `gx_dol_ctr` | 5 | GX quality trials in Dolphin (Docs/GX_QUALITY.md): CTR Time Trial start line, TV pictures at | Docs/GX_QUALITY.md |
| `gx_dol_ctr240` | 1 | CTR Time Trial start line in 240p (TVMode=1), TV pictures at 5600/5900: is the 512x216 display's | Docs/GX_QUALITY.md |
| `gx_dol_spyro` | 3 | GX quality trials in Dolphin (Docs/GX_QUALITY.md): the user's Spyro play, TV pictures at | Docs/GX_QUALITY.md |
| `gx_quality_ab` | 9 | GX picture trials on the bench Wii (Docs/GX_QUALITY.md). Dolphin cannot show these: it ignores | Docs/GX_QUALITY.md |
| `io_ab` | 4 | Storage and state check (libogc3 move, 2026-10-11): the same game from SD and from the SMB | Docs/done/LIBOGC3_PLAN.md |
| `lc34` | 5 | The mdec region (LockedCache bit 4) on and off, same game, same boot, in Dolphin. |  |
| `lc_mdec_ab` | 2 | MDEC locked cache on the bench Wii: FF7 (opening FMV) with LockedCache 0 and 4 (try/lc-step34) | Docs/LOCKED_CACHE.md |
| `limiter_debt_ab` | 2 | The frame limiter's debt bound: 12.5 ms (0) against 125 ms (1), same boot. Spyro's attract | SETTINGS.md |
| `newgames_boot` | 12 | boot test of the games added 2026-10-21: 40 s each, no input (formats: cue, multi-track cue, ccd+sub) | Docs/TEST_REGIMEN.md |
| `pbp_frogger_cbn` | 1 | PBP study: Frogger CBN, idle 90 s, against the CUE (is the CD-DA delay a compressed-image thing?) | Docs/PARKED_BUGS.md |
| `pbp_frogger_chd` | 1 | PBP study: Frogger CHD, idle 90 s, against the CUE (is the CD-DA delay a compressed-image thing?) | Docs/PARKED_BUGS.md |
| `pbp_frogger_cue` | 1 | PBP study: Frogger CUE (CD-DA track 2), idle 90 s (demo/FMV), against its other format | Docs/PARKED_BUGS.md |
| `pbp_frogger_pbp` | 1 | PBP study: Frogger PBP, idle 90 s (demo/FMV), against its other format | Docs/PARKED_BUGS.md |
| `pbp_level10` | 2 | Frogger CUE, then its level-10 PBP (PSXPackager fork: zopfli + zlib 9, the smaller per block); vsig_cmp 01/02 | Docs/TEST_REGIMEN.md |
| `pbp_study_a` | 10 | PBP study (2026-10-21): each PBP made from source-built PSXPackager right after its original; guest compare with vsig_cmp (pairs 01/02, 0... | Docs/TEST_REGIMEN.md |
| `pbp_study_ff8` | 9 | PBP study: the 4-disc FF8 PBP (2.2 GB) booted at each disc (Disc=N), each right after that disc's own image | Docs/TEST_REGIMEN.md |
| `play_cost` | 7 | The play build's probe cost against release (Docs/DEBUG_PLAY.md): three games with the frame | Docs/DEBUG_PLAY.md, probes.md |
| `sd_speed` | 2 | SD card speed: io_ab.txt's SD games (Ape Escape; Spyro with a save-state round trip), no SMB/USB | Docs/done/LIBOGC3_PLAN.md |
| `shot_crash3` | 1 | *(no comment in the file)* | unattended-run.md |
| `shot_lvl1` | 1 | finish_lvl1_wii (user, 2026-11-01, recorded with GpuTiming=1 FrameRate=1) with the user's picture settings |  |
| `softmodes_play` | 12 | Soft Fast (gpuPlugin=0) under SoftGpuMode 0 Accurate, 1 Hi-Fi, 2 Fast: the user's recordings | scripts/softmode_cmp.py, test-roms.md |
| `spyro_tornado` | 1 | The user's Spyro session (new game, the whirlwind elevator, a level): the elevator lag spike | Docs/BACKENDS.md, Docs/TEST_REGIMEN.md |
| `stills_check` | 2 | logos and still pictures under OpenGX: Spyro (Sony/Universal logos), Gex 3 (LOADING) -- 900 vblanks each, every frame | Docs/PARKED_BUGS.md |
| `tiger_temple_diag` | 1 | Tiger Temple full race, deep+pmc probes (GPU split, GTE, SPU, Broadway counters): diagnosis 2026-10-20 | Docs/PARKED_BUGS.md, Docs/TEST_REGIMEN.md |
| `tiger_temple_spots` | 1 | Tiger Temple: the sampling profiler over the two short slow spots only (Dolphin: instructions) | dolphin-pitfalls.md |
| `tobal2_boot` | 1 | Tobal 2: 640x480i, must still render under OpenGX | Docs/PARKED_BUGS.md |

## Investigation records (59)

One-off runs from a finished investigation; no doc names them. Kept as the setup of the result, not for reuse.

### Ape Escape (JIT burst, locked cache, XFB colour, picture checks)

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `ape_attract_anicap` | 3 | *(no comment in the file)* |  |
| `ape_attract_xfb` | 1 | *(no comment in the file)* |  |
| `ape_crt_turnon` | 1 | *(no comment in the file)* |  |
| `ape_exit_user` | 1 | *(no comment in the file)* |  |
| `ape_fin1_xfbcol` | 2 | *(no comment in the file)* |  |
| `ape_hand_user` | 1 | *(no comment in the file)* |  |
| `ape_jit_hprof` | 1 | Ape title demo: CPU profile of the first-run compile burst (Dolphin vblank 9631; hprof build) |  |
| `ape_jit_short` | 1 | first 2100 vblanks of the Ape title demo: a perf report (jitcode sum/shape) |  |
| `ape_jit_wii` | 4 | Ape Escape title demo on the bench Wii: A/B of the jitq queue settings across the first-run compile burst at vblank 8773 (2026-11-02). Fr... |  |
| `ape_lab_rect` | 1 | lab rect repro |  |
| `ape_w_xfbcol` | 1 | finish_lvl1_wii (user, 2026-11-01, recorded with GpuTiming=1 FrameRate=1) with the user's picture settings |  |
| `ape_wii_scene` | 1 | Ape title demo on the Wii, hprof build: the scene change at vbl 8951 only (one 200 ms vblank, 95% core) |  |
| `ape_wii_scene_fast` | 1 | Ape title demo on the Wii, hprof build: the scene change at vbl 8951 only (one 200 ms vblank, 95% core); Unlimited speed (FrameRate=3) |  |
| `lc5_ape_def` | 1 | Ape Escape title demo: A/B of the jitq queue settings around the first-run compile burst (2026-11-02). Dolphin: burst at vblank 9631, ben... |  |
| `lc5_ape_verify` | 1 | Ape Escape title demo: A/B of the jitq queue settings around the first-run compile burst (2026-11-02). Dolphin: burst at vblank 9631, ben... |  |
| `lc5_fin_def` | 1 | finish_lvl1 (user, 2026-11-01) with the user's picture settings. FrameRate=1 (TV) and GpuTiming=1 change the timing: the replay parts fro... |  |
| `lc5_fin_verify` | 1 | finish_lvl1 (user, 2026-11-01) with the user's picture settings. FrameRate=1 (TV) and GpuTiming=1 change the timing: the replay parts fro... |  |
| `lc5_replay3_def` | 2 | The user's 2026-09-30 sessions, first 3 minutes each: a replay-exactness check |  |
| `lc5_replay3_verify` | 2 | The user's 2026-09-30 sessions, first 3 minutes each: a replay-exactness check |  |
| `lc_wii_hprof_new` | 1 | Ape title demo on the Wii, hprof build: what the core does in the slow stretch (vbl 8741-8801, 72-77% core) and at 8951 |  |
| `lc_wii_hprof_off` | 1 | Ape title demo on the Wii, hprof build: what the core does in the slow stretch (vbl 8741-8801, 72-77% core) and at 8951 |  |
| `lc_wii_speed_new` | 1 | the same line on the build before the jitq changes |  |
| `lc_wii_speed_off` | 1 | the same line on the build before the jitq changes |  |

### Bomberman, Vagrant Story, Spyro 3 (OpenGX picture bugs, 2026-10-21)

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `bm_gxmask` | 2 | Bomberman vertical-flame tearing: GxMask Accurate (0) vs Fast (1), the user movie to the first big explosion |  |
| `bomberman_movie_ab` | 2 | Bomberman: texture tearing on vertical explosions (user 2026-10-21): the movie under OpenGX, then Soft as the reference |  |
| `spyro3_boot` | 1 | Spyro 3 (CCD+SUB) boot: HLE BIOS against the real SCPH1001, 50 s each |  |
| `spyro3_open` | 2 | Spyro 3 v1.1 opening: OpenGX vs Soft, every frame (the user saw broken screens at the start) |  |
| `spyro3_open_gx` | 1 | Spyro 3 v1.1 opening: OpenGX vs Soft, every frame (the user saw broken screens at the start) |  |
| `spyro3_universal` | 1 | Spyro 3 Rev 1 Universal logo screen under OpenGX, traced (autoinput s3_universal_trace.txt) |  |
| `spyro3_versions` | 2 | Spyro 3 clean dumps: v1.0 (USA) and v1.1 (USA, Rev 1), 50 s each, no input |  |
| `vagrant_movie_ab` | 2 | Vagrant Story: start-game screen black, garbage in loading transitions (user 2026-10-21): the movie under OpenGX, then Soft as the reference |  |
| `vagrant_movie_gx` | 1 | Vagrant Story: the movie under OpenGX only |  |
| `vs_reveal` | 1 | Vagrant Story title screen under OpenGX, traced (vs_reveal_trace.txt = the user movie + trace/dump lines) |  |
| `vs_reveal_soft` | 1 | Vagrant Story title screen under Soft (reference) (vs_reveal_trace.txt = the user movie + trace/dump lines) |  |
| `vs_title` | 1 | Vagrant Story title screen under OpenGX, traced (vs_title_trace.txt = the user movie + trace/dump lines) |  |

### CD and disc formats (2026-10-21)

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `cd_fixes` | 5 | CD fixes: Spyro 2 CCD (reference) vs the same disc as a cdrdao .toc with interleaved subchannel (read-ahead on, off), and Frogger picked... |  |
| `cdh_cbn` | 1 | CD hash, one format per run (the hash counts across a chain) |  |
| `cdh_chd` | 1 | CD hash, one format per run (the hash counts across a chain) |  |
| `cdh_cue` | 1 | CD hash, one format per run (the hash counts across a chain) |  |
| `cdh_pbp` | 1 | CD hash, one format per run (the hash counts across a chain) |  |
| `mds_only` | 1 | Frogger MDS only, read-ahead off (cd_audit: it stopped booting) |  |
| `mmv3_probe` | 1 | *(no comment in the file)* |  |
| `pbp_level0` | 2 | PBP level 0 (every block stored): Frogger CUE, then its level-0 PBP, then the level-9 one; guest compare 01/02, 01/03 (cdriso.c stored bl... |  |
| `pbp_medievil_cue` | 1 | PBP study: MediEvil CUE, idle 90 s (demo/FMV), against its other format |  |
| `pbp_medievil_pbp` | 1 | PBP study: MediEvil PBP, idle 90 s (demo/FMV), against its other format |  |

### FF7, FF8, Tiger Temple, others

| Chain | Games | What it is for | Cited by |
|---|---|---|---|
| `ab_20261020` | 6 | A/B of the 2026-10-20 changes on the bench Wii (light, no vsig): one job per DOL |  |
| `ach_banner` | 2 | Official mode's two notices at a game's start: the game card, then the Hardcore warning as a black banner |  |
| `ff7` | 1 | FF7 alone, deep probes, for the 0.86x on hardware |  |
| `ff7_play` | 1 | FF7 New Game from the user's recording: the opening video (green frame, Docs/PARKED_BUGS.md) |  |
| `ff7_video_dump` | 1 | FF7 New Game (the recording) with the XFB at vblank 2100, inside the opening video |  |
| `ff8_trace` | 1 | FF8 disc 1 opening under OpenGX: a primitive trace at vblank 1500 (credits on black) |  |
| `fmv_frames` | 1 | Gex's FMV intro, 15 s, for frame-dump comparisons of the 24-bit present path. |  |
| `gex3_analog` | 1 | *(no comment in the file)* |  |
| `hprof_fmv` | 2 | Two FMV-heavy intros for the sampling profiler (PROBES=hprof): where a 24-bit present |  |
| `library20b` | 8 | library20.txt games 10-17 in their own boot (written when Gex 3 seemed to stop the frame dump; that was a bad split, 2026-10-12) |  |
| `spyro_demo` | 1 | Spyro left alone: title, then the attract demos (the first enters a world). 4 minutes. |  |
| `tiger_temple_7k` | 2 | Tiger Temple race, first 7000 vblanks (the slow stretch 6173-6758), GxMask 0 vs 1: GPU profile |  |
| `tiger_temple_mask` | 2 | The user's Tiger Temple race (CTR), OpenGX: GxMask Accurate vs Fast on the bench Wii (speed, bit-15 cost) |  |
| `tiger_temple_trace` | 1 | Tiger Temple: primitive traces of a fast frame and the worst slow stretch (Dolphin) |  |

## Found in the 2026-11-09 audit

- **Identical copies:** `lc5_fin_def` = `ape_finish_lvl1`, `lc5_replay3_def` = `replay3` (same lines, other file name).
- **Games missing from the library** (`library_check.py`, FIXED 2026-11-09): `newgames_boot` and `pbp_study_ff8` named
  FF8 disc 4 by its old folder name (`Final Fantasy VIII (USA) (Disc 4)`); they now use the library's
  `Final Fantasy VIII [NTSC-U] [Disc4of4] [SLUS-00909]`. The library's FF8 disc 4 cue and all four FF9 cues named `.bin`
  files from before the folders were renamed; their `FILE` lines now name the `.bin` beside them (old cue kept as
  `.cue.orig`). `library_check.py`: 0 problems. Left as is: the FF9 folders all carry SLUS-01296 (the real serials are
  01251, 01295, 01296, 01297) and FF8 disc 4 carries 00909 (disc 3's); renaming them would move their saves and states.
- **No comment line:** `ape_attract_anicap`, `ape_attract_texmin`, `ape_attract_xfb`, `ape_crt_turnon`, `ape_exit_user`,
  `ape_fin1_xfbcol`, `ape_front_reg`, `ape_front_reg2`, `ape_hand_user`, `crashbash_dump`, `ff7_dump450`, `gex3_analog`,
  `mmv3_probe`, `shot_crash3`. Some others reuse a comment copied from the chain they came from (the `lc5_*` and `lc_wii_*`
  files say "A/B of the jitq queue settings").
- **Most used** (bench.sqlite runs): `thread_check` 54, `ff7_green` 40, `ff7_dump450` 33, `all` and `replay3` 21,
  `spyro_demo75` 18, `smoke`, `lab_check` and `tiger_temple_trace` 17.
