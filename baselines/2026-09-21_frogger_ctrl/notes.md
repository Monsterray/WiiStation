# 2026-09-21_frogger_ctrl

- game: Frogger (1997)
- purpose: storage CONTROL: defaults (16 KB, read-ahead off); split-track cue with CD-DA; 170 s idle (title then demo levels)
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21 (read-ahead path fix)
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 106.4 |
| vblanks | 5775 |
| speed | 0.905 |
| idle_pct | 29.9 |
| hw_gpu_pct | 37.8 |
| spu_pct | 1.42 |
| out_us | 616347 |
| conv_us | 8512959 |
| tile_us | 7770173 |
| mdec_us | 3614052 |
| cd_reads | 13229 |
| cd_worst_us | 3178 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_frogger_ctrl/perf.log <run>/perf.log`
