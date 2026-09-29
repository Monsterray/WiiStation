# hw_cbash_ref_20260928

- game: Crash Bash (chain)
- purpose: Crash Bash to vblank 3450 with an XFB dump at 3400, before the semi-scratch fix. platform: hardware
- date: 2026-09-28
- build: c48bb7d
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 60.2 |
| vblanks | 3450 |
| speed | 0.955 |
| idle_pct | 54.1 |
| hw_gpu_pct | 13.0 |
| spu_pct | 4.58 |
| out_us | 369371 |
| conv_us | 507000 |
| tile_us | 473177 |
| mdec_us | 0 |
| cd_reads | 1493 |
| cd_worst_us | 21693 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_cbash_ref_20260928/perf.log <run>/perf.log`
