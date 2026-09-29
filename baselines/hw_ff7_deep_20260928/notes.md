# hw_ff7_deep_20260928

- game: Final Fantasy VII (chain)
- purpose: FF7 alone, deep probes, before the one-pass 16-bit upload. platform: hardware
- date: 2026-09-28
- build: e2c24bb
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 70.1 |
| vblanks | 3600 |
| speed | 0.856 |
| idle_pct | 55.6 |
| hw_gpu_pct | 12.3 |
| spu_pct | 2.12 |
| out_us | 383446 |
| conv_us | 2304175 |
| tile_us | 1787585 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 24002 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_ff7_deep_20260928/perf.log <run>/perf.log`
