# 2026-09-21_spyro_cdpf_v1

- game: Spyro the Dragon
- purpose: storage: read-ahead ON but thread opened the .cue (reads=0); 64 KB buffers; frames on. Superseded by the fixed run
- date: 2026-09-21
- build: 05b1372 + uncommitted storage/probe changes of 2026-09-21
- settings: settings_cdpf.cfg
- DSP LLE: 0, frames dumped: 1

| metric | value |
|---|---|
| guest_s | 88.9 |
| vblanks | 3793 |
| speed | 0.711 |
| idle_pct | 13.2 |
| hw_gpu_pct | 64.9 |
| spu_pct | 1.21 |
| out_us | 634799 |
| conv_us | 421888 |
| tile_us | 371994 |
| mdec_us | 0 |
| cd_reads | 6603 |
| cd_worst_us | 5029 |
| cdpf_hit | 0 |
| cdpf_miss | 6603 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/2026-09-21_spyro_cdpf_v1/perf.log <run>/perf.log`
