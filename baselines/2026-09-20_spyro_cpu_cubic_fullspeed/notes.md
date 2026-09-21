# 2026-09-20_spyro_cpu_cubic_fullspeed

- game: Spyro the Dragon
- purpose: audio+perf: CPU path Cubic, no LLE, no frames; the reference the user heard as clean
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_res2.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 87.6 |
| vblanks | 3793 |
| speed | 0.721 |
| idle_pct | 12.8 |
| hw_gpu_pct | 65.6 |
| spu_pct | 1.66 |
| out_us | 934534 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5171 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 100.2 |
| audio_rms_db | -23.7 |
| audio_gaps | 112 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_cpu_cubic_fullspeed/perf.log <run>/perf.log`
