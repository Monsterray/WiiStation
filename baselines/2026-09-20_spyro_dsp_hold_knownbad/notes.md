# 2026-09-20_spyro_dsp_hold_knownbad

- game: Spyro the Dragon
- purpose: KNOWN BAD: DSP path with 4096-byte AESND buffers -> one 2.7 ms hole per buffer (~1800 gaps); reference for the short-gap detector
- date: 2026-09-21
- build: 954be31 (WIP resampler tree, pre-merge)
- settings: settings_dsp_res0.cfg
- DSP LLE: 1, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 87.6 |
| vblanks | 3793 |
| speed | 0.722 |
| idle_pct | 12.9 |
| hw_gpu_pct | 66.4 |
| spu_pct | 0.60 |
| out_us | 0 |
| conv_us | 0 |
| tile_us | 0 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5199 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio | audio.flac |
| audio_s | 99.9 |
| audio_rms_db | -23.9 |
| audio_gaps | 1785 |

Compare: `python scripts/perf_compare.py baselines/2026-09-20_spyro_dsp_hold_knownbad/perf.log <run>/perf.log`
