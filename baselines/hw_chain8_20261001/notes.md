# hw_chain8_20261001

- game: 8-game chain (thread_check)
- purpose: hardware: after the fill (dcbz), VRAM transfer word copy and tracking words, XA filter, map cache (ae2399a): mean load 38.9 -> 35.8 vs 814e989; FF7 65.3 -> 53.9
- date: 2026-10-01
- build: ae2399a
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 10.5 |
| vblanks | 600 |
| speed | 0.950 |
| idle_pct | 73.3 |
| hw_gpu_pct | 2.3 |
| spu_pct | 0.90 |
| out_us | 62400 |
| conv_us | 16696 |
| tile_us | 7307 |
| mdec_us | 217394 |
| cd_reads | 579 |
| cd_worst_us | 24771 |
| cdpf_hit | 575 |
| cdpf_miss | 4 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_chain8_20261001/perf.log <run>/perf.log`
