# chain_ff7x3_20260922

- game: FF7 x3 (ff7x3.txt)
- purpose: after 372a470: three FF7 rows equal
- date: 2026-09-22
- build: 372a470
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 50.9 |
| vblanks | 2400 |
| speed | 0.787 |
| idle_pct | 44.0 |
| hw_gpu_pct | 15.8 |
| spu_pct | 2.33 |
| out_us | 398526 |
| conv_us | 780970 |
| tile_us | 631894 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 10697 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/chain_ff7x3_20260922/perf.log <run>/perf.log`
