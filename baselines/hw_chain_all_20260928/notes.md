# hw_chain_all_20260928

- game: All 11 (chain)
- purpose: First hardware run of the eleven-game chain (bench Wii, PMC build: PMC1 cycles, PMC2 instructions). 10 of 11 games at full speed; FF7 0.86. platform: hardware
- date: 2026-09-28
- build: e2c24bb
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 60.8 |
| vblanks | 3600 |
| speed | 0.986 |
| idle_pct | 59.8 |
| hw_gpu_pct | 8.7 |
| spu_pct | 1.22 |
| out_us | 407455 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 4376453 |
| cd_reads | 8728 |
| cd_worst_us | 15643 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_chain_all_20260928/perf.log <run>/perf.log`

## Hardware notes

- IPC (PMC2/PMC1): Spyro 0.789, Crash 3 0.754, CTR 0.663, Crash Bash 0.735, Ape 0.815,
  MediEvil 0.722, FF7 0.726, Gex 0.728, Frogger 0.725, Micro Machines 0.734, Point Blank 0.738.
- FF7: 70.2 s wall for 3600 vblanks (0.86x) with 39 s of limiter sleep, so the CPU is not the
  limit. Its scheduler time is 51.6 s (the others 32-46 s). A deep-probe FF7 run follows
  (hw_ff7_deep).
- MediEvil: texture conversion 8.0 s and tiling 5.8 s of 61 s, the largest of the chain;
  still 0.98x.
- The Wii's clock was 12 h ahead of the PC (perf.log says 2026-09-29 07:57; the PC started
  the run at 2026-09-28 19:56). run.info has the PC times.
