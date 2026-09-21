# 2026-09-20_spyro_cpu_linear

- game: Spyro the Dragon
- purpose: audio: CPU path, Linear resampler; LLE+frames on
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_res1.cfg
- DSP LLE: 1, frames dumped: 1

| metric | value |
|---|---|
| guest_s | 87.4 |
| vblanks | 3793 |
| speed | 0.724 |
| idle_pct | 12.9 |
| hw_gpu_pct | 66.2 |
| spu_pct | 1.26 |
| out_us | 0 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5165 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 99.8 |
| audio_rms_db | -23.7 |
| audio_gaps | 113 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_cpu_linear/perf.log <run>/perf.log`
