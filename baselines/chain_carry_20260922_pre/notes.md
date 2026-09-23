# chain_carry_20260922_pre

- game: FF7, Medievil, FF7, FF7 (carry.txt)
- purpose: state carried between chained games, before the fix: later FF7s rcnt 67870 desync 1
- date: 2026-09-22
- build: 627af35+probes
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 50.8 |
| vblanks | 2400 |
| speed | 0.787 |
| idle_pct | 44.0 |
| hw_gpu_pct | 15.8 |
| spu_pct | 2.34 |
| out_us | 400861 |
| conv_us | 781074 |
| tile_us | 631794 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 10819 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/chain_carry_20260922_pre/perf.log <run>/perf.log`
