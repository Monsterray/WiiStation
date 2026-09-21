# 2026-09-21_medievil_intro

- game: Medievil
- purpose: perf: texk probes, idle through intro FMV; MDEC 4.4%
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 57.6 |
| vblanks | 3304 |
| speed | 0.955 |
| idle_pct | 29.2 |
| hw_gpu_pct | 37.1 |
| spu_pct | 1.16 |
| out_us | 352387 |
| conv_us | 12833 |
| tile_us | 9175 |
| mdec_us | 2516241 |
| cd_reads | 7343 |
| cd_worst_us | 9111 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_medievil_intro/perf.log <run>/perf.log`
