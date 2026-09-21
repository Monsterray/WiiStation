# Case studies: symptom → probe → mechanism

Read these to calibrate how far a theory should go before a run is spent on it. Each one
lists the probe that actually decided it; the earlier probes are listed because they are the
tempting wrong turns.

## 1. Missing / swapped textures, popping (Spyro, OpenGX)

- Symptom: flat-shaded polygons, wrong "squares", textures popping in; Old Soft clean.
- Wrong turns (18 runs): stale texture cache (forced full reconvert → same counts), heap
  corruption (`heap_ok=1`), upload-state mismatch (`mismatch=0`), CLUT decoding, uniform
  textures (`ogxud` — real, but the game asks for them), render-to-texture (`ogxoff` real,
  but the ROI did not overlap), GX write hazards. `llm_court` endorsed several of these.
- Decisive: VRAM dumps identical between plugins (CPU side right), then `ogxeq mism=0`
  (draw bound to the right texel), then **Dolphin frame dumps with texture-cache accuracy
  Fast vs Safe**: Safe matches Old Soft. Mechanism: Dolphin's sampled texture hash misses
  in-place sub-texture uploads. Not a Wii bug.
- Lesson: when every in-emulator counter says "correct", change the host, not the code.

## 2. Garbage strips at top and bottom (Spyro loading screens)

- Symptom: 8-row bands of stale geometry above/below the picture.
- Decisive: primitive trace showed drawing area rows 248..471 inside a 240..479 display and
  fills covering only the drawing area; the plugin never clipped (scissor code commented out
  since the import, E3/E4 no longer re-armed it). Fix: restore the clip in top-origin GX
  coordinates, lift it around fills. Verified by edge crops of frame dumps.
- Lesson: read the control commands (E3/E4/E5/02/F5) in a trace before the polygons.

## 3. Pause screen flat green instead of a tinted frozen frame (Spyro)

- Symptom: pause menu over a uniform green field.
- Trace: game fills green, reads the just-drawn back buffer with four GP0 C0 strips, builds an
  8-bit texture at (512,0), redraws it tinted, dims with ABR 2. The read classified as
  `MAPPING_PREVIOUS` with no capture (`c1` result -5, merged 0).
- Two mechanisms: (a) the capture gate only accepted small Dino-Crisis-style reads — relaxed to
  "drawing area overlaps the previous display"; (b) even then the EFB copy was black
  (`c4/c5` probes: zero non-black words) because **Dolphin kept EFB copies on the GPU**
  (EFB-to-texture-only). With it off: 22528/32768 non-black, scene colours per strip, frames
  correct.
- Wrong turn: gating the capture on EFB tile coverage — fills of the next screen are not
  drawn, they become the clear colour, so tiles are empty although the EFB is full.
- Lesson: when a captured buffer is all zeros, suspect the copy never happened before
  suspecting the sampling.

## 4. BIOS shell without spheres, odd buttons (OpenGX)

- Symptom: user screenshots vs `examples/*.png`: no "bubbles" on main menu, memory card,
  CD player.
- Decisive in ONE run: `ogxud: mode=2 texel=0000 page=11 uv…` (15-bit textured quads sampling
  black) + `ogxoff: prims=170` (primitives off-screen) + `ogxva` rects at x≥640. Mechanism: the
  shell renders spheres off-screen and textures from them; the port dropped off-screen
  primitives. Fix: rasterize them in software into the shared VRAM. VRAM dump showed the
  sphere at (640,0); frame dumps matched the references.
- Lesson: perf.log's existing counters often already contain the answer — grep them before
  designing new probes.

## 5. "No controller" in the BIOS (unattended run only)

- Symptom: no cursor triangle, scripted presses ignored; games were fine.
- Probes, in order of one build each (should have been one build): pad plugin counters
  (`startpoll=0`), SIO counters (`write8` thousands, `start=0`, `irq=0`, **`padtype0=0`**).
  Mechanism: `sio.c` polls only when `padType[0] != 0`; the settings file said 1 but
  `auto_assign_controllers()` reset it to NONE because no host controller exists in a batch
  Dolphin run. Fix for test settings: `PadType1 = 1`, `PadAutoAssign = 0`; plus a loaded
  script makes port 1 report a connected pad.
- Lesson: instrument the whole path (SIO → plugin → script) in one build; print the state
  variable the gate reads (`padtype0`), not just the counts.

## 6. Black window, zero frame dumps (BIOS autoboot)

- Symptom: emulator running (perf.log, traces present), Dolphin window black, no dumps.
- Mechanism: `Func_ExecuteBios()` called straight from the autoboot path skipped the video
  preamble that `Func_PlayGame()` does (one GUI draw, `clearEFB`, `backFromMenu`,
  `glResetCacheRegion`). The user noticed the black window before the logs did.
- Lesson: "no frames dumped" is a symptom of the title, not of the dump switches, when the
  switches worked minutes earlier.

## 7. One-sector silent gaps in streamed speech (Spyro, every output path)

- Symptom: `AUDIO_DUMP=1` wavs of the level-intro dialogue held exact-zero gaps of ~53 ms
  (one 37.8 kHz stereo XA sector) alternating with ~20 ms ones, identical across the SDL and
  AESND drivers and all resampler modes, so upstream of the output stage. The waveform
  value after each gap matched the value before it: silence was *inserted*, nothing dropped.
- Probes in one build (`xa:` / `xamix:` lines, `atrace.log` timeline, `SoundTempo` setting so
  both behaviours run from one DOL): sectors arrived exactly on the emulated schedule
  (`dt_min/max` = 1806050..1806618 cycles, nominal 1806336), `trunc=0`, `filtered` = the other
  7 channels of the interleave. But the XA ring was empty at 45 of 49 feeds and the mixer had
  produced 27% more samples than emulated time: `pulls=2837`. The timeline gave the core's
  speed in that scene as a median 0.56x of wall time (debug build under Dolphin).
- Mechanism: `DF_SPUasync`'s tempo pull-back (`cycles_played -= 367*768` whenever the output
  driver is not busy; upstream pcsx-rearmed guards it with `iTempo`, default off) makes the
  mixer run at wall-clock rate while CD-XA is fed at emulated rate, so each sector is used
  up before the next one arrives. With the pull-back off (`SoundTempo = 0`) the gaps went
  (`gaps=0`, ring fill median 36 at feed) but the user heard an echo instead. Aligning that
  dump's speech against the release build's (`align.py`-style chunk cross-correlation)
  showed the content identical but jumping by exactly +250 ms every few hundred ms: the
  SDL driver's 250 ms ring was being replayed. Its callback tested `iReadPos != iWritePos`
  once per output frame and then read two shorts, so with the ring dry it could hop one
  short past the writer and lap the stale ring. (A first guess from raw autocorrelation, a
  32 ms repeat, was wrong: the same 32 ms peak is in the clean release dump, it is the
  speech itself.) The release DOL in the same scene ran near real time and had 2 gaps in
  110 s instead of 61. At 0.56x no setting is clean; the deficit has to land somewhere.
- Lesson: an audio gap that is exactly one source block long is a *rate* problem, not a data
  problem; measure emulated vs wall time around the feed before touching the decoder. The
  debug build's own slowness is a variable in audio experiments: compare with the release
  DOL (`DOL=` env of `dolphin_run.sh`) before attributing gaps to the code. And to identify
  a repeat, align the suspect dump against a clean one and read the offset steps; a bare
  autocorrelation peak may belong to the signal.

## 8. Replacing the tempo pull-back: rate control at the output stage (follow-up to 7)

- Design (2026-09-20, `dfsound/ratectl.c`): the mixer stays on emulated time; each output
  driver nudges its playback rate by at most +-0.5 % from its queue occupancy (PI, time
  constants in audio frames, slew-limited), the SDL callback by scaling its 16.16 step, the
  DSP driver with `AESND_SetVoiceFrequency`. Each driver plays silence until its queue first
  reaches the target. Stalls are the frame limiter's job: `FrameCap` now keeps a schedule
  and carries up to 125 ms of debt, so the core runs unthrottled after a disc-read stall
  and refills the queue with emulated-time-consistent audio; and it paces on the vblank
  rate the core emulates (`psxGetFps()`, 60.00 Hz) instead of the PEOPS table's 59.83 Hz
  rounded down to whole 100 us ticks, a built-in -0.26 % that would have eaten half the
  controller's authority. `SoundTempo` (the pull-back) stays as a file-only legacy switch,
  default off; `SoundRateControl` (default on) switches the new loop off for measurements.
- Probes: `rate:` line (ppm now/min/max, integral share, queue min/max/mean, saturated
  updates, voice-frequency writes, prefill silence, limiter debt max and drops) and `D`
  records in atrace.log; `scripts/run_summary.py` reads all of it plus the audio dump.
- Debug build (0.51x wall in the speech scene): the controller sits at its -0.5 % cap
  with the queue near empty and `out dry` ~1.17 M frames; XA gaps 0, holds 0 (the
  `xamix gaps=1 / gap_samples=88260` is the 2 s stream-end tail the detector counts, not
  a gap). No scheme is clean at half speed, and the debug build never has headroom, so it
  cannot show the steady state; judge the loop on the release DOL's audio dump.
- Release DOL, 160 s Spyro speech scene, runs of exact digital silence 3..400 ms in the
  dump (`run_summary.py`): rate control CPU path 14 runs / 209 ms, all at boot (10 s),
  title (20-30 s) and the level load (61 s), none in gameplay or speech; DSP path 4 / 203 ms,
  same places; legacy pull-back 22 / 266 ms, of which 20 are 3-4 ms holes spread through
  the speech (70-155 s): the XA gaps, shortened by MixCD's 3-call repeat of the last
  sample. The pull-back's one advantage remains: it refills the ring after a stall in a few
  frames regardless of headroom, so the boot phase (JIT warm-up, slower than real time)
  shows 5-6 chopped silences of 10-20 ms with rate control and none with the pull-back.
- Cadence trap found by the summary table, not by reading code: `SPU_async(cycle, 1, ...)`
  runs from rcnt 4 every 46080 cycles, 735 times per emulated second, not once per frame,
  and libSDL asks for 1024-frame halves, 47 times a second. A controller scaled per update
  had the DSP path's integral 16x too fast (`i=-1002` vs `-75`); scaling by the frames each
  update covers fixed it (`i=-83`, `sat 0`).
- Lesson: when a loop's constants are "per update", print the update count and derive the
  rate from it before trusting the design; and read all runs in one table -- the anomaly
  was visible only side by side.
- Verified after the merge of all sound branches (main 435bc21, 2026-09-20): release CPU Hold 13 zero-runs / 199 ms, DSP Hold 4 / 203, CPU Cubic 14 / 201, DSP Cubic 4 / 132, all at boot, title and level load, none in the speech, no channel skew; debug (0.50x) XA gaps 0, holds 4, pulls 0, `out dry` 1.22 M, controller median -4492 ppm. Same structure as before the merge, with the rate nudge now inside `resample.c`.

## 9. Right channel one sample behind the left (SDL driver, everything it plays)

- Symptom: in every `AUDIO_DUMP=1` wav of the CPU (SDL) sound path, Spyro's speech had
  `R[n] == L[n-1]` in 70-75% of non-silent frames and `R == L` in under 10%, with the L/R
  correlation 0.9999 at a shift of one frame and 0.98 at zero. Same in debug and release
  builds and in every resampler mode, so not the resampler's doing.
- Where it came from, by comparing paths instead of decoding the disc: the AESND (DSP) path's
  dumps of the same scene had `R == L` in 80% of frames at shift 0. Both drivers consume the
  same SPU buffer, so the SPU output (and the XA source behind it) is plain dual-mono; the
  SDL driver was adding the skew. Its ring writer pre-incremented (`++iWritePos` before the
  store), so the first sample of the run landed at index 1 and, the ring size being even,
  every frame the reader took at an even index straddled two source frames: `R_src[k-1]`
  came out as left and `L_src[k]` as right. Inherited from the original WiiSX import. The
  ring rewrite in ce10881 (frame-granular writer that stores at `wp` and `wp+1`) removed it
  as a side effect; the dumps made with that build peak at shift 0 (`R == L` 76%).
- Lesson: when two output drivers exist, a dump from each is the cheapest source check
  there is: a fault the source carries appears in both, a fault one driver adds appears in
  one. `scripts/wav_compare.py` now prints the shift -1/0/+1 equality fractions and flags a
  winner other than 0, so this is a one-line check on any future dump.
