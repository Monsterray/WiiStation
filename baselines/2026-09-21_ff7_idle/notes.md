# 2026-09-21_ff7_idle

- game: Final Fantasy VII
- purpose: demo check: 170 s idle with frames; title at ~15 s then a staff-credits roll over the logo from ~20 s on (no FMV, no gameplay); texture-streaming heavy
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 1

| metric | value |
|---|---|
| guest_s | 143.9 |
| vblanks | 7030 |
| speed | 0.814 |
| idle_pct | 51.1 |
| hw_gpu_pct | 11.5 |
| spu_pct | 5.48 |
| out_us | 749702 |
| conv_us | 5894425 |
| tile_us | 4774834 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 5213 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_ff7_idle/perf.log <run>/perf.log`
