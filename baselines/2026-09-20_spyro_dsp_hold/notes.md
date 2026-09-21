# 2026-09-20_spyro_dsp_hold

- game: Spyro the Dragon
- purpose: audio: DSP path Hold after the whole-chunk buffer fix; gaps back to the game's own
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_dsp_res0.cfg
- DSP LLE: 1, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 87.6 |
| vblanks | 3793 |
| speed | 0.721 |
| idle_pct | 12.9 |
| hw_gpu_pct | 66.3 |
| spu_pct | 0.63 |
| out_us | 0 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5192 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 100.5 |
| audio_rms_db | -23.7 |
| audio_gaps | 108 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_dsp_hold/perf.log <run>/perf.log`
