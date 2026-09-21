# 2026-09-21_ff7_plain

- game: Final Fantasy VII
- purpose: perf: plain 120 s boot-and-idle at the title; textures 4.4%, 26% sub-texture misses
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_res0.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 83.9 |
| vblanks | 3430 |
| speed | 0.681 |
| idle_pct | 40.7 |
| hw_gpu_pct | 13.8 |
| spu_pct | 2.27 |
| out_us | 366232 |
| conv_us | 2033360 |
| tile_us | 1657046 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 5479 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_ff7_plain/perf.log <run>/perf.log`
