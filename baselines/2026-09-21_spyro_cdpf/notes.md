# 2026-09-21_spyro_cdpf

- game: Spyro the Dragon
- purpose: storage: read-ahead ON (fixed), 64 KB buffers, 4 CHD hunks; 6592 hits / 11 misses; wall+vblanks identical to control
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21 (read-ahead path fix)
- settings: settings_cdpf.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 88.8 |
| vblanks | 3793 |
| speed | 0.712 |
| idle_pct | 13.2 |
| hw_gpu_pct | 64.9 |
| spu_pct | 1.21 |
| out_us | 635453 |
| conv_us | 421727 |
| tile_us | 371871 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 9564 |
| cdpf_hit | 6592 |
| cdpf_miss | 11 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_spyro_cdpf/perf.log <run>/perf.log`
