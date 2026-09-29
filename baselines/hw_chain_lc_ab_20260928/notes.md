# hw_chain_lc_ab_20260928

- game: Spyro + Crash Bash, LockedCache 0 vs 3 (chain)
- purpose: First hardware A/B of the locked-cache regions (spu-gauss + tex-tile). platform: hardware
- date: 2026-09-28
- build: e2c24bb
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 61.5 |
| vblanks | 3600 |
| speed | 0.976 |
| idle_pct | 56.4 |
| hw_gpu_pct | 13.4 |
| spu_pct | 4.41 |
| out_us | 388690 |
| conv_us | 562899 |
| tile_us | 527190 |
| mdec_us | 0 |
| cd_reads | 1493 |
| cd_worst_us | 21599 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_chain_lc_ab_20260928/perf.log <run>/perf.log`

## Hardware result

- No wall-time gain: every column moves by 0.0-0.25 points (noise).
- tex-tile: Spyro tile_us 525681 -> 465616 (-11%), Crash Bash 511241 -> 527190 (+3%).
  Tiling is under 1% of wall in both games.
- LC DMA wait: 5.3 ms (Spyro), 11.4 ms (Crash Bash) per minute.
- Verdict: keep every region off by default (as now).
