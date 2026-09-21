# 2026-09-21_frogger_cdpf

- game: Frogger (1997)
- purpose: storage: read-ahead ON (fixed), 64 KB per-handle buffers; 13211 hits / 18 misses; wall+vblanks identical to control; read path total_us 216 ms vs 343. Replaces the v1 run whose thread had opened the .cue
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21 (read-ahead path fix)
- settings: settings_cdpf.cfg
- DSP LLE: 0, frames dumped: 0

| metric | value |
|---|---|
| guest_s | 106.4 |
| vblanks | 5775 |
| speed | 0.905 |
| idle_pct | 29.7 |
| hw_gpu_pct | 37.8 |
| spu_pct | 1.76 |
| out_us | 968069 |
| conv_us | 8512919 |
| tile_us | 7769849 |
| mdec_us | 3614889 |
| cd_reads | 13229 |
| cd_worst_us | 8825 |
| cdpf_hit | 13211 |
| cdpf_miss | 18 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_frogger_cdpf/perf.log <run>/perf.log`
