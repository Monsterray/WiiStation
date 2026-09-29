# hw_cbash_semi_20260928

- game: Crash Bash (chain)
- purpose: Crash Bash to vblank 3450, XFB at 3400, with the semi-scratch fix (tiling 473 -> 74 ms, XFB identical). platform: hardware
- date: 2026-09-28
- build: 0b2876e
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 59.8 |
| vblanks | 3450 |
| speed | 0.961 |
| idle_pct | 55.1 |
| hw_gpu_pct | 12.4 |
| spu_pct | 4.61 |
| out_us | 369246 |
| conv_us | 106700 |
| tile_us | 73684 |
| mdec_us | 0 |
| cd_reads | 1493 |
| cd_worst_us | 21626 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_cbash_semi_20260928/perf.log <run>/perf.log`
