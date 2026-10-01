# hw_gte_new_20260930

- game: GTE chain (Spyro, Crash 3, CTR, Crash Bash)
- purpose: hardware: GTE A/B, B = upstream gte.c port 8c387b8+8950ac8+77b4dd3+3778ed3 (deep build); +12-16% GTE time, identical VRAM
- date: 2026-09-30
- build: a7c329a
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 61.4 |
| vblanks | 3600 |
| speed | 0.977 |
| idle_pct | 56.1 |
| hw_gpu_pct | 13.0 |
| spu_pct | 4.75 |
| out_us | 383482 |
| conv_us | 12219 |
| tile_us | 9226 |
| mdec_us | 0 |
| cd_reads | 1493 |
| cd_worst_us | 21522 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_gte_new_20260930/perf.log <run>/perf.log`
