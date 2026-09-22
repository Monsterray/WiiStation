# chain_all_20260922_pre

- game: All 11 (chain)
- purpose: First eleven-game chained run; debug build before the OpenGX probe gate fix; OpenGX per-draw probes still on
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
| hw_gpu_pct | 23.0 |
| spu_pct | 1.67 |
| out_us | 601145 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 6770248 |
| cd_reads | 8728 |
| cd_worst_us | 4573 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/chain_all_20260922_pre/perf.log <run>/perf.log`
