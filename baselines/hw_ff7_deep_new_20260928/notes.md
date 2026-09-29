# hw_ff7_deep_new_20260928

- game: Final Fantasy VII (chain)
- purpose: FF7 alone, deep probes, with the one-pass 16-bit upload (upload 10.9 -> 7.7 s, 0.86 -> 0.90x). platform: hardware
- date: 2026-09-28
- build: 34413f9
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 67.0 |
| vblanks | 3600 |
| speed | 0.896 |
| idle_pct | 58.4 |
| hw_gpu_pct | 12.7 |
| spu_pct | 2.21 |
| out_us | 383001 |
| conv_us | 2228958 |
| tile_us | 1704626 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 24807 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_ff7_deep_new_20260928/perf.log <run>/perf.log`
