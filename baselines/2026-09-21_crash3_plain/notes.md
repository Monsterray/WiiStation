# 2026-09-21_crash3_plain

- game: Crash Bandicoot 3
- purpose: perf: plain 120 s boot-and-idle; SPU 7.8% with reverb, hw_gpu 43%
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 75.2 |
| vblanks | 3822 |
| speed | 0.847 |
| idle_pct | 19.7 |
| hw_gpu_pct | 43.4 |
| spu_pct | 7.76 |
| out_us | 408088 |
| conv_us | 112608 |
| tile_us | 99147 |
| mdec_us | 0 |
| cd_reads | 1231 |
| cd_worst_us | 782 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_crash3_plain/perf.log <run>/perf.log`
