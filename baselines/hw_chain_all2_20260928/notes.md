# hw_chain_all2_20260928

- game: All 11 (chain)
- purpose: Eleven-game chain on the bench Wii after the day's GPU fixes (one-pass 16-bit upload, no movie memset, VRAM loop on locals, off-screen skip). FF7 0.86 -> 0.95x; GPU share down in all 11. PMC build. platform: hardware
- date: 2026-09-28
- build: c48bb7d
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 60.8 |
| vblanks | 3600 |
| speed | 0.986 |
| idle_pct | 61.7 |
| hw_gpu_pct | 6.9 |
| spu_pct | 1.22 |
| out_us | 402278 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 4357345 |
| cd_reads | 8728 |
| cd_worst_us | 15763 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_chain_all2_20260928/perf.log <run>/perf.log`
