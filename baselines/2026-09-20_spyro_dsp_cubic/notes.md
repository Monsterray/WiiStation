# 2026-09-20_spyro_dsp_cubic

- game: Spyro the Dragon
- purpose: audio: DSP path Cubic after the buffer fix (voice at 48 kHz)
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_dsp_res2.cfg
- DSP LLE: 1, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 88.3 |
| vblanks | 3793 |
| speed | 0.716 |
| idle_pct | 12.7 |
| hw_gpu_pct | 65.9 |
| spu_pct | 1.51 |
| out_us | 800179 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5199 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 95.5 |
| audio_rms_db | -23.5 |
| audio_gaps | 110 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_dsp_cubic/perf.log <run>/perf.log`
