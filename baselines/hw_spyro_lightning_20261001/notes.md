# hw_spyro_lightning_20261001

- game: Spyro the Dragon (demos 75 s)
- purpose: hardware: Lightning optimizer skips (8610af3): optimize 317 -> 170 ms, compile 1008 -> 870 ms, world-entry stall 287 -> 247 ms lost (vblanks 4141-4176); emitted code identical to upstream
- date: 2026-10-01
- build: 8610af3
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 76.4 |
| vblanks | 4499 |
| speed | 0.981 |
| idle_pct | 56.7 |
| hw_gpu_pct | 12.1 |
| spu_pct | 1.31 |
| out_us | 485899 |
| conv_us | 135474 |
| tile_us | 97786 |
| mdec_us | 0 |
| cd_reads | 10220 |
| cd_worst_us | 23499 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_spyro_lightning_20261001/perf.log <run>/perf.log`
