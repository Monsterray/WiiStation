# hw_spyro_pool_20261001

- game: Spyro the Dragon (demos 75 s)
- purpose: hardware: Lightning first node pool 128 and patches 64 (was 1024/1024): nodes 367 -> 141 ms, new_state 55 -> 21 ms, compile 870 -> 608 ms; world-entry stall 247 -> 176 ms lost
- date: 2026-10-01
- build: 5c5220a
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 76.3 |
| vblanks | 4499 |
| speed | 0.983 |
| idle_pct | 57.0 |
| hw_gpu_pct | 12.2 |
| spu_pct | 1.31 |
| out_us | 486442 |
| conv_us | 133988 |
| tile_us | 96285 |
| mdec_us | 0 |
| cd_reads | 10220 |
| cd_worst_us | 23530 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_spyro_pool_20261001/perf.log <run>/perf.log`
