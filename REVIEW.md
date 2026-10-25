# WiiStation review

## Plan

Each finding records its trigger, its effect, the smallest safe fix, and the check that shows it (a test where one can be written).

| Subsystem | What to trace | Check |
| --- | --- | --- |
| CPU (Lightrec, `lightrec.c`, `deps/lightrec/`) | PS1 code -> compiled block -> IRQs at block ends | `vsig_cmp.py`, `replay_check.py --all`, ps1-tests |
| GPU (OpenGX `GlesGpu/`, soft `SoftGPU/`) | GP0 command -> GX draw or software raster -> XFB | `wsx.sh frames A B` = 0 different, ptrace/vramio |
| SPU / XA / CDDA (`dfsound/`, `spu.c`) | sector -> XA ring -> mixer -> resampler -> AI/DSP | `wsx.sh audio A B` bit-identical (rate control off) |
| CD (`Gamecube/fileBrowser`, CHD/PBP loaders) | file -> sector cache -> CD-ROM controller | `cd_*` chains, `cd_hash_check.txt` |
| Memory | MEM1 / MEM2 per game, between chained games | perf.log `gameclose:`, `chain_table.py` memory line |
| Menus (`Gamecube/menu`, `libgui`) | input -> frame -> XFB | `wsx.sh menu A --dol X --dol2 Y` |
| Achievements | rcheevos client -> HTTPS -> toast | `ach_*` chains, `achdump` |

## Findings

### 5.17.0: tools

| Kind | Change | Check |
| --- | --- | --- |
| Tools | wii-homebrew-kit adopted: line endings LF, `.editorconfig`, `tests/host/hostcheck.h`, the kit's address comparison in `scripts/wii_targets.py` (protected Wiis can be added by `$WII_PROTECTED` or `.dev/wiis.json`, never removed) | `python scripts/wii_targets.py`, `python -m unittest tests/test_line_endings.py` |
| Tools | `scripts/benchdb.py`: every chained run in `baselines/bench.sqlite` (gitignored); perf.log line 2 names the version and probe gates | `benchdb.py backfill`: 1113 runs, 3336 game results |

### 5.17.0: memory

| Kind | Change | Check |
| --- | --- | --- |
| Fix | CTR after Spyro in one chain ran MEM1 out (libogc v3 malloc has no MEM2 fallback; Lightning uses a NULL `jit_alloc`): Lightrec/Lightning in a 4 MB MEM2 TLSF pool, capped; XA ring, chain list, overlay buffer in MEM2 (dbf0054a, b5d837a6) | bench Wii: malloc 5904 KB after every game, lowest MEM1 1600 KB |

### Next

- The three XFBs in MEM2 (+1.8 MB MEM1): Dolphin frames and menus identical; bench A/B on `library20.txt` decides.
