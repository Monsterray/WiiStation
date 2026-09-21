# 2026-09-21_spyro_texk

- game: Spyro the Dragon
- purpose: perf: texk probes, gameplay script, no LLE/frames; CLUT+tiling 0.9%, hw_gpu 66%
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 87.9 |
| vblanks | 3793 |
| speed | 0.719 |
| idle_pct | 13.6 |
| hw_gpu_pct | 65.6 |
| spu_pct | 0.95 |
| out_us | 405025 |
| conv_us | 421793 |
| tile_us | 371928 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5355 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_spyro_texk/perf.log <run>/perf.log`
