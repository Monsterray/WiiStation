# Debug-build probes

Everything here is compiled only with `-DPERF_PROF` (Makefile_Wii debug target and the
`libOpengx_prof.a` archive). In the release build `PERF_INC()` and friends are no-ops and
the timers are inline zero, so probes cost nothing on hardware. Source: `Gamecube/perf_prof.c`
and `.h`; the GX plugin's hooks are in `GlesGpu/gpuPlugin.c`, `gpuPrim.c`, `gpuVramReadback.inc`
and `deps/opengx/gc_gl.c`.

## perf.log (sd:/wiisxrx/perf.log, read it with scripts/sdimage_read.py)

Truncated once per boot by `perf_reset()` (called from `go()` for games and from
`Func_ExecuteBios()` for the BIOS). A report block is appended every 1800 presents and at the
scheduled `dump` vblank. Lines and what they mean:

| Line | Meaning |
|---|---|
| `--- perf frames=N ---` | presents so far (flipEGL count) |
| `fixes: dwActFixes=.. cdrom=..` | active per-game hack bits (database.c) and the disc id |
| `cpu:`, `wall:`, `inside:`, `slicecost:`, `slice:` | CPU core timing; `wall: vblanks=` is the emulated vblank count. `inside: spu_us` is the whole audio mixer per report, and `out_us` the part of it spent in the 44100→48000 output conversion (`dfsound/resample.c`; zero on the DSP path in Hold mode, where the microcode converts) |
| `irq: cdr=.. gpudma=.. rcnt=..` | interrupt histogram; only non-zero sources are printed — a missing `sio=` means the SIO never interrupted (no pad traffic) |
| `ram:` | MEM1/MEM2 usage, `heap_ok`, `null_read` |
| `gpu: tex_hit/miss/resets/loads` | texture cache activity |
| `cdpf: hit miss reads` | the CD read-ahead thread (`CdPrefetch`): sectors served from its ring, sectors it did not have, sectors it fetched. With the setting off all three stay 0 |
| `texk: conv_us conv texels tile_us tile mdec_us mdec` | CPU kernels sized as locked-cache candidates: the CLUT expansion (`LoadSubTexturePageSort`, with texel count), opengx's RGB5A3 tiling pass (`glTexSubImage2D`), and the MDEC decode per DMA (`psxDma1`) |
| `ogx: gc sub_new sub_hit skip unaligned oob` | opengx sub-texture uploads and skips |
| `ogxskip`, `ogxupl` | why uploads were skipped / semi vs opaque uploads, `mismatch` must be 0 |
| `ogxoff: prims tex off_roi fills fills_off va va_roi` | primitives whose destination misses both display buffers (render-to-texture), fills, VRAM-area invalidations; `ogxva:` the first rects |
| `efbloss: upl_calls upl_done \| pres: total clear inflight_skip` | the two ways already-drawn EFB content can go away again: `UploadScreen` calls (the PSX VRAM rect blitted over the EFB, which has no GX-drawn primitives in it) and those that reached the EFB; then presents, presents that cleared the EFB (`GX_CopyDisp(.., GX_TRUE)`, armed only by a GP0 02 fill covering the next screen) and presents refused because a copy was still in flight. Crash 3's title reads `upl_calls=0 ... total=1215 clear=2 inflight_skip=0`: the EFB is never re-uploaded, essentially never cleared, and no present is dropped |
| `ogxeq: n mism hole` | texel-equivalence detector: GX texel at the UV centroid vs PSX texel from VRAM; `mism=0` means the CPU side (converter, cache, placement) is right |
| `ogxud: gt nv mode page clut tex semi texel uv1024` | ring of draws that sampled a uniform texture (mode 2 = 15-bit direct, `texel=0000` = black) |
| `offsoft: prims rejected` | off-screen primitives handed to the software rasterizer |
| `pad: startpoll update ai_calls` | SSS pad plugin start-polls, state refreshes, scripted-input mask calls |
| `sio: write8 start ctrl16 read8 irq padtype0` | SIO data writes, pad-select starts that reached the plugin, control writes, reads, SIO interrupts, and `padType[0]` as the SIO sees it |
| `ai_ev: vblank mask` | every change of the scripted button mask |
| `ptrace:` + `pt:` (older builds) | trace episode, now in ptrace.log |
| `xa: sectors starts fed trunc fill_min fill_max freq stereo dt_min dt_max filtered` | CD-XA sectors queued into the SPU (`FeedXA`), stream starts, 44.1 kHz samples written, feeds cut short by a full ring, ring fill at feed time, the stream's format, PSX cycles between sectors (37.8 kHz stereo at 2x = 1806336), sectors dropped by the file/channel filter |
| `rate: ppm i min max occ_min occ_max occ_avg updates sat changes prefill \| limit: debt_max debt_drops` | output-stage rate control (`dfsound/ratectl.c`): the playback-rate nudge now applied (ppm; + = faster), its integral term's share, the extremes it reached (must stay within +-5000), the driver's queue occupancy in 44.1 kHz frames at each update (min, max, mean; the targets are 5512 for SDL and 5760 for the DSP path), controller updates, updates whose demand exceeded the cap, AESND voice-frequency writes, output frames of silence played before the queue first reached its target; frame limiter: the largest schedule debt carried (100 us ticks, bound 1250) and stalls longer than the bound |
| `xamix: mix hold gaps gap_calls gap_samples \| spu: ns pulls busy desync aev \| out: dry drop` | `MixCD` calls that consumed XA, calls that repeated the last sample (ring empty), silence episodes and their length in calls/samples while a stream was active; mixer samples produced, tempo pull-backs (`SoundTempo`), calls that found the output driver busy, `do_samples` clock resets, audio-timeline records; output frames the driver had no data for (SDL zero-fills, AESND repeats its last buffer) and feeds dropped because the driver was full |

Typical readings: a game polls the pad ~2x per vblank (`start` ≈ 2×vblanks); `padtype0=0`
means no pad will ever be seen; `ogxeq mism=0` with wrong pictures means the fault is after
the draw (GX state, Dolphin); `offsoft prims` > 0 means the title renders off-screen.

## Primitive trace (sd:/wiisxrx/ptrace.log)

`perf_prim_trace(cmd, flags, abr, color, x0, y0, x1, y1)` appends to a 2600-entry ring;
an episode is 16 presents, armed by `trace <vblank>` lines in autoinput.txt (or the legacy
automatic trigger), flushed to `ptrace.log` (rewritten per episode — the file holds the LAST
episode only; schedule the interesting one last or read between runs).

**Two traps that cost four runs on the Crash 3 starfield.** (1) The automatic trigger is a
large *semi-transparent, untextured* primitive, which is what a fade-out quad looks like; a
scene that has none — an ordinary title screen — never re-arms the trace, so the file keeps
whatever fade came before it, with a header pointing at a vblank hundreds of frames earlier.
Always read the header's `vblank=` before believing an episode is the scene you wanted, and
put an explicit `trace <vblank>` in autoinput.txt for scenes that are visually quiet.
(2) A scene drawing hundreds of small semi-transparent primitives per present (that title:
2370 prims, 478 of them semi, every frame) fills the 2600-entry ring inside five presents,
so the whole-screen primitive at the *end* of each present never reaches the log. Past half
full the ring now keeps only primitives larger than 60000 PSX pixels² whatever their blend.
Header:
`ptrace: armed_at_present=P vblank=V prims/semi/fills per present: a/b/c ...`.
Entry: `pt: +<present> cmd=<hex> <S><T><Q><G> abr=<n> col=<rrggbb> (x0,y0)-(x1,y1)` with
S semi-transparent, T textured, Q quad, G gouraud; abr = blend mode 0..3.

Synthetic commands emitted by the read path and the off-screen path (not GP0 commands):

| cmd | Meaning | Fields |
|---|---|---|
| `c0` | a VRAM→CPU read (GP0 C0) was issued | rect |
| `c1` | readback outcome | flags S=previous-display mapping, T=unknown, none=current; `abr` = capture result + 8 (3 = captured back buffer, 1 = live, -5 = previous with no capture, -6 = unknown mapping); `col` = merged pixel count |
| `c2` | readback state | `col` bits: 1 pendingPresented 2 contaminated 4 mixed 8 untracked 16 prevSnap 32 liveSnap 64 mapValid 128 contentValid 256 contentDirty 512 asyncInFlight; x0 map id, y0 previous map id, x1/y1 = FULL tiles in prev/live snapshot |
| `c3` | content the CPU will read | `col` = FNV hash of the rect's first 8 rows in psxVuw, x1 = non-black words, y1 = words hashed |
| `c8` | off-screen primitive rasterized in software | flags T=textured, `col` = GP0 command byte, rect |
| `ea` | a screen re-upload from PSX VRAM was asked for | flags S=`Position`, T=RGB24; `col` = map id; rect = `xrUploadArea` |
| `eb` | that re-upload actually reached the EFB | same fields |
| `ec` | one entry per present | flags S=cleared the EFB, T=submitted, Q=`uploadedScreen`, G=`needFlipEGL`; `abr` = `iDrawnSomething`; **`col` = the emulated vblank**; rect = previous display position → new one |

EA/EB/EC are numbered above 0xE0 on purpose: `perf_prim_trace` counts anything below that
as a primitive in the per-present `prims/semi/fills` header, and a probe that fires every
present would inflate it.

`scripts/ptrace_summary.py` decodes all of this; read its output, not the raw file.

## Audio timeline (sd:/wiisxrx/atrace.log)

`perf_audio_event(kind, cycle, a, b, c)` appends to a 3072-record ring that starts at
`atrace <vblank>` in autoinput.txt (without the line: at the first event), fills once and is
written whole from the present path (and again with every perf report). `dolphin_run.sh`
extracts it as `<outdir>/atrace.log`; `scripts/atrace_summary.py` turns it into: emulated-vs-
wall speed between XA sectors, sector intervals in both clocks, ring fill, gap lengths, and
the fraction of mixer calls that found the output driver busy. Records:

| kind | When | a | b | f |
|---|---|---|---|---|
| `F` / `R` | an XA sector was queued (`R` = stream start, ring reset) | ring fill before the feed (samples) | `cycle - cycles_played`, the mixer's lag | bit0 driver busy, bits1-3 `XARepeat`, bit4 this sector ends a gap |
| `H` | `MixCD` started repeating the last sample: ring empty | `ns_to` | `XARepeat` left | |
| `G` | `MixCD` started contributing silence: repeats used up | `ns_to` | `cdClearSamples` | |
| `S` | heartbeat, every 16th `SPU_async` | ring fill | `cycle - cycles_played` after any pull-back | driver busy |

Lines are `ae: <kind> w=<wall_us> c=<psx cycle> a=.. b=.. f=..`. Emulated ms = cycles / 33868.8.
How this settled the Spyro speech gaps is in case-studies.md.

## Broadway performance counters (PERF_PROF_PMC) -- the wrap below is FIXED

`perf_pmc_read()` (`Gamecube/perf_prof.c`) reads PMC1..4 once, at report time, over the
whole run. The counters are 32 bits: at 729 MHz a cycle count wraps every ~5.9 s, so on a
Wii any run longer than that reports the total modulo 2^32 -- meaningless, and it will not
look wrong. (Found while porting the probe to Wii64, September 2026; Dolphin's numbers hide
it because a Dolphin run's cycle count is not the Wii's.) The fix Wii64 uses
(`main/perf_prof.c` there): read the four counters at every periodic sample (well under
5.9 s apart) and add `(u32)(now - last)` into 64-bit totals. See the wii-homebrew skill's
"Standards shared by the projects here".

**Fixed 2026-09-22:** `perf_pmc_read()` now runs at every present and sums 32-bit deltas into
64-bit totals (`g_perf.pmc[]`, `pmc_last[]`). Checked under Dolphin: 7.6 billion cycles in
one 10 s game, 728.6 per microsecond. Build it with `PROBES=pmc`.

## Probes added 2026-09-22 (perf.log lines)

`gte:` (PERF_PROF_GTE), `gpusplit:`/`gpuprim:`/`gpudeep:`/`gpudraw:`/`gpuregs:`/`gpuflip:`/
`gpupres:`/`gxcache:` (PERF_PROF_GPUSPLIT), `nested:` (slices entered re-entrantly by HLE),
`softcall:` (HLE soft calls: runs/steps/escapes), `lc:` (locked-cache regions and DMA). All
are in the `deep` preset except `lc:`, which is always written. `scripts/chain_table.py
--detail` reads them; the reference is references/measuring.md.

## VRAM dump

`dump <vblank>` (autoinput.txt) or the fallback at vblank 6000 writes the whole 1 MB `psxVuw`
to `sd:/wiisxrx/vram.bin` and appends a perf report. Render with `scripts/vram2png.py`.
Because `psxVuw` is one global shared by all three plugins, dumps from Old Soft and OpenGX at
the same vblank are directly comparable: identical texture pages ⇒ the GX plugin's CPU side is
right; the display buffers legitimately differ (GX draws to the EFB, not to psxVuw).

## Adding a probe

1. Counter: add a `uint32_t` to `struct perf` in `perf_prof.h`, `PERF_INC(name)` at the site,
   and an `fprintf` in `perf_report()`. Print related counters on one line so a single grep
   shows the whole picture.
2. Event with geometry: `perf_prim_trace(0xC9, flags, aux, colour_or_count, x0, y0, x1, y1)`
   under `#ifdef PERF_PROF`; pick an unused synthetic code and document it in the table above
   and in `ptrace_summary.py`.
3. One-shot data: follow `perf_vram_dump()` (fopen "wb" on sd:/) — small files are fine, the
   SD image survives; extract with `sdimage_read.py`.
4. Keep the release build clean: run `bash scripts/build.sh release` before committing.

Files in the GX plugin also carry `DISP_DEBUG` logging (`writeLogFile`) — that is a separate,
noisier facility writing `sd:/wiisxrx/log.txt`; prefer perf counters and traces.
