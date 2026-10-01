# hw_gte_old_20260930

- game: GTE chain (Spyro, Crash 3, CTR, Crash Bash)
- purpose: hardware: GTE A/B, A = current gte.c (deep build, a7c329a+dirty)
- date: 2026-09-30
- build: a7c329a
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 61.4 |
| vblanks | 3600 |
| speed | 0.977 |
| idle_pct | 56.3 |
| hw_gpu_pct | 13.1 |
| spu_pct | 4.76 |
| out_us | 385961 |
| conv_us | 12235 |
| tile_us | 9239 |
| mdec_us | 0 |
| cd_reads | 1493 |
| cd_worst_us | 22855 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_gte_old_20260930/perf.log <run>/perf.log`
