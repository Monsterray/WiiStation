# chain_carry_20260922

- game: FF7, Medievil, FF7, FF7 (carry.txt)
- purpose: after 372a470: every FF7 row equal (rcnt 60359, desync 0, avail G=1000)
- date: 2026-09-22
- build: 372a470
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 50.8 |
| vblanks | 2400 |
| speed | 0.787 |
| idle_pct | 44.0 |
| hw_gpu_pct | 15.8 |
| spu_pct | 2.33 |
| out_us | 398448 |
| conv_us | 780387 |
| tile_us | 631503 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 10225 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/chain_carry_20260922/perf.log <run>/perf.log`
