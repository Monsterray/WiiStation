# 2026-09-20_spyro_cpu_hold

- game: Spyro the Dragon
- purpose: audio: CPU path, Hold resampler (the pre-resampler output, byte-identical); LLE+frames on
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_res0.cfg
- DSP LLE: 1, frames dumped: 1

| metric | value |
|---|---|
| guest_s | 87.3 |
| vblanks | 3793 |
| speed | 0.724 |
| idle_pct | 13.0 |
| hw_gpu_pct | 66.2 |
| spu_pct | 1.15 |
| out_us | 0 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5151 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 100.9 |
| audio_rms_db | -23.8 |
| audio_gaps | 112 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_cpu_hold/perf.log <run>/perf.log`
