# hw_ff7_mirror_20260929

- game: Final Fantasy VII (chain)
- purpose: FF7 alone on the bench Wii with partial screen uploads while the EFB mirrors VRAM (0fbe257): upload 6.48 -> 4.05 s, GPU share 11.9 -> 6.8%, 0.93 -> 0.95x. Deep build. platform: hardware
- date: 2026-09-29
- build: 0fbe257
- settings: (defaults)
- DSP LLE: ?, frames dumped: ?

| metric | value |
|---|---|
| guest_s | 63.1 |
| vblanks | 3600 |
| speed | 0.951 |
| idle_pct | 68.8 |
| hw_gpu_pct | 6.8 |
| spu_pct | 2.32 |
| out_us | 380289 |
| conv_us | 17260 |
| tile_us | 12396 |
| mdec_us | 0 |
| cd_reads | 462 |
| cd_worst_us | 24451 |
| cdpf_hit | 0 |
| cdpf_miss | 0 |
| audio |  |
| audio_s |  |
| audio_rms_db |  |
| audio_gaps |  |

Compare: `python scripts/perf_compare.py baselines/hw_ff7_mirror_20260929/perf.log <run>/perf.log`
