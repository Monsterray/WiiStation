# 2026-09-21_crash3_idle

- game: Crash Bandicoot 3
- purpose: demo check: 170 s idle with frames; title at ~15 s, gameplay attract demo ~35-53 s, back to title; sheet in media
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 1

| metric | value |
|---|---|
| guest_s | 153.0 |
| vblanks | 8117 |
| speed | 0.884 |
| idle_pct | 16.9 |
| hw_gpu_pct | 44.0 |
| spu_pct | 8.04 |
| out_us | 866539 |
| conv_us | 1658299 |
| tile_us | 1305216 |
| mdec_us | 0 |
| cd_reads | 3195 |
| cd_worst_us | 1295 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_crash3_idle/perf.log <run>/perf.log`
