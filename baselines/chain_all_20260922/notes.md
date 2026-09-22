# chain_all_20260922

- game: All 11 (chain)
- purpose: Eleven-game chained run, debug build with every probe on and the OpenGX probe gate fixed: the reference for the GPU and CPU plans
- date: 2026-09-22
- build: 3864b6c
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 60.3 |
| vblanks | 3600 |
| speed | 0.996 |
| idle_pct | 44.6 |
| hw_gpu_pct | 22.9 |
| spu_pct | 1.67 |
| out_us | 601421 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 6771740 |
| cd_reads | 8728 |
| cd_worst_us | 4581 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/chain_all_20260922/perf.log <run>/perf.log`
