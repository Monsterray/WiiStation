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

## 10. "The starfield flickers" on Crash 3's title -- it was the logo's own glow

- Symptom as reported: on Crash 3's title screen the stars twinkle; on hardware they are
  steady. The evidence handed over was a pixel count: bright pixels (luminance >= 225) in
  the left and right background margins run an exact eight-frame cycle, 204 -> 441 -> 204,
  and the transitions are ONE-SIDED -- several frames where stars are added and none go,
  then a batch that goes with none added. That one-sidedness is what made it look like a
  buffer retaining part of a starfield and periodically losing it, and the suspects were
  the OpenGX back-buffer readback gate, the EFB clear on present, and screen re-uploads.
- Every one of those was wrong, and three cheap measurements said so before any code was read:
  1. **The two software renderers show the identical cycle.** Old Soft (`gpuPlugin = 0`,
     ground truth) and New Soft give the same numbers frame for frame, 188 -> 450 -> 188,
     same shape, same period. Whatever this is, it is upstream of the GPU plugin. This is
     step 3 of the method and it cost one run; it should have been the first run, not the
     third.
  2. **Nothing is lost.** Counting pixels that go from bright (>180) to near black (<60)
     between consecutive frames gives 218, 278, 242, 313... and the same number coming back
     the other way, every frame, in both plugins. Equal both ways is motion. The one-sided
     "gone/new" in the original count was the threshold 225 sitting in the middle of a
     population being modulated smoothly: at 120 and at 200 the swing is a flat 20-32%,
     at 240 and 250 it is 175-190%. A count taken at a single brightness threshold cannot
     tell "it vanished" from "it dimmed by 7%".
  3. **The stars that move are the ones under the logo.** A map of how much each pixel
     changes over the cycle (`scripts/frame_cycle.py --map`) draws the answer: the swirling
     energy ribbons and the halo around the WARPED logo, plus the NEW GAME menu highlight.
     Per grid cell, 53-100% of bright pixels swing in the band through the logo and 0-2%
     everywhere else; below y=400 and above y=40, nothing moves at all. The margins the
     original count sampled are crossed by the ribbons, which reach x~100 on the left and
     x~720 on the right.
- What the game is actually doing, from the trace: a rock-steady 30 fps -- one present every
  two vblanks, exactly, for all 16 presents of the episode -- with 2370 primitives, 478 of
  them semi-transparent, drawn every single frame, and the display alternating (0,0) <-> (512,0).
  `efbloss: upl_calls=0 | pres: total=1215 clear=2 inflight_skip=0`: the EFB is never
  re-uploaded from PSX VRAM, is cleared twice in 1215 presents, and no present is ever
  dropped. There is no mechanism losing anything, and nothing for a fix to attach to.
- Wrong turns worth naming. Diffing one pair of frames (the brightest against the dimmest)
  showed a difference that was *exactly zero* outside the logo and suggested the glow alone;
  diffing every consecutive pair showed scattered specks across the whole background too.
  One pair is not a cycle -- diff all of them. And the first three trace episodes came back
  from fades hundreds of frames away from the title, because the trace re-arms itself only on
  a large semi-transparent primitive and a title screen has none (see probes.md).
- **This conclusion was wrong** -- see case 11. The eight-frame pulse is real and is the glow,
  but underneath it the stars themselves WERE blinking, on OpenGX only, and the aggregate
  counts above could not see ~90 one-pixel dots under ~250 pixels of ribbon animation.
  "All three renderers agree" was true of the statistics, not of the picture.
- Lesson: when a report arrives with a pixel count attached, re-take the count at other
  thresholds and diff every consecutive pair before adopting the mechanism the count implies.
  A one-sided appear/disappear pattern is the classic signature of a threshold crossing a
  smooth ramp, not of a buffer losing its contents. And then -- case 11 -- still track the
  objects the report named, because a real defect can hide under the artefact.

## 11. Crash 3's starfield really did blink -- one-pixel rectangles dropped on alternate presents (OpenGX)

- Follow-up to 10, which was wrong in its conclusion. The user came back with a sharper
  observation than the pixel count: New Soft steady, OpenGX flickering, only the stars,
  only the right ~70% of the screen. Every clause of that was a measurement waiting to be
  taken, and case 10's statistics had missed it because they were dominated by the logo's
  ribbons: ~250 pixels changing each way per frame from the animation swamped ~90 one-pixel
  dots blinking underneath it.
- The instrument that saw it: track the dots themselves. `scripts/dot_flicker.py` finds every
  blob of at most 6 bright pixels outside the logo, matches each to the next frame within
  3 px, and reports moved / dimmed / lost / new per third of the picture. OpenGX, presents
  1300-1316, right third: 88 dots present, **88 lost, 88 new, 0 moved, 0 dimmed**. New Soft,
  same presents: 176 dots, 0 lost, 0 new. So the right-side stars were not moving or
  dimming; they were absent on every other present, and presents alternate between the two
  display buffers. A zoomed strip of the right margin shows it to the eye: stars in 1301 and
  1303, none in 1300 and 1302.
- What the trace had already said: the stars are 297 GP0 68 one-pixel rectangles (`cmd=68`,
  opaque), drawn right after the 67 gouraud nebula triangles; E5 alternates (0,12) and
  (512,12) with the buffers. They are not a texture, whatever they look like.
- Mechanism, in `primTile1` and nowhere else (the 8x8 and 16x16 tiles have no such line):
  `if ((lx0 + PSXDisplay.CumulOffset.x) > 640 || ...) return;` evaluated AFTER
  `offsetPSX4()` had added the draw offset to `lx0`, while `CumulOffset` already contains the
  draw offset. On the buffer at VRAM x=512 the game's draw offset is 512, so the sum
  double-counted it and crossed 640 for every dot right of screen x~300; on the buffer at
  x=0 the offset is 0 and nothing was dropped. Hence: only one-pixel tiles, only alternate
  presents, only from a certain x rightwards. The guard came in with the repo's initial
  import (7f067cd). The fix tests the SCREEN coordinates offsetST() had already put in
  `vertex[]`.
- Verified on the same presents with the same exclusion: fixed OpenGX right third 176 dots,
  0 lost, 0 new; mid 480, 0/0 (before: 432, 48/48 -- and 48 dots fewer, because they were
  missing half the time). Indistinguishable from New Soft.
- Lessons. (a) A user's "only X, only when Y, only on Z" is three measurements, not a
  hypothesis to be argued with; take them before theorising. (b) Aggregate change counts
  cannot see a small population blinking under a large one animating -- track the objects.
  (c) When a symptom follows the buffer alternation, look for anything computed from the
  draw offset or display position along the primitive's path; `offsetST()` then
  `offsetPSX4()` leave `vertex[]` in screen space and `lx0..ly3` in VRAM space, and mixing
  them with `CumulOffset` counts the offset twice.


## 12. FF7 after Medievil made no screen uploads -- display state carried from the last game (OpenGX)

- Symptom (2026-09-22, an A/B chain): FF7 booted after Medievil in one boot had
  `efbloss: upl_calls=132960 upl_done=0` and `gpupres: uploads=0`; FF7 booted first, or after
  another FF7, had 554/553. Same `irq:` counts, so the guest did the same thing both times.
- Probe: a counter per UploadScreen early return (`uplret:`) and the state those returns read
  at the first and latest call (`uplst0/1:`), read with `wsx.sh table NAME --detail upl`, on
  `scripts/chains/gpu_carry.txt` (FF7, Medievil, FF7). One run: every broken call was the
  1-row guard (`px1=132960`) with Position 0 (from CheckWriteUpdate), and the one differing
  field was `PSXDisplay.InterlacedTest`: 2 in the good FF7, 0 in the broken one.
- Mechanism: GP1(08) raises InterlacedTest only when it turns interlace on while
  `PSXDisplay.Interlaced` is still 0. `GL_GPUinit` (run again for every game) zeroed
  Interlaced but not InterlacedNew, and `updateDisplayIfChangedGl` copies InterlacedNew into
  Interlaced -- so FF7 started "already interlaced" from Medievil's value, its switch was not
  seen, and CheckWriteUpdate sent each line of its line-by-line screen writes to UploadScreen
  as a 1-row upload, which is skipped. Not explained: why FF7 after FF7 escaped (it also
  ends interlaced); the rest of the carried state differs.
- Fix: `GL_GPUinit` zeroes `PSXDisplay`, `PreviousPSXDisplay`, the upload rects and the
  upload bookkeeping statics before setting its own values. Verified: FF7 after Medievil
  554/553, uploads 554, and every GPU counter equal to FF7 first.
- Not GPU, still carried between games in one boot (the same in FF7 x3): the root-counter
  IRQ count (`rcnt=60359` first, 67870 after), CPU slice counts, and pad `avail G=1000`.
- Lesson: a plugin "init" that lists the fields it resets is a list of the fields it forgot.
  When a later game in a chain differs from the same game booted first, zero the structs.
- The non-GPU part is case 13.


## 13. A second game in one boot ran on the last game's timers, SPU clock and pad (core)

- Symptom (2026-09-22): FF7 booted second or third in a chain had `irq: rcnt=67870`, `slice:
  cycles=4233507259`, `desync=1` and `avail G=1000`; booted first, `rcnt=60359`, `cycles=
  4810858936`, `desync=0`, `avail G=0000`. After Medievil it also had `xamix: hold=3 gaps=1
  gap_samples=88260`. Same `cdr`/`gpudma` counts: the guest did the same work.
- Probe: `carry:` (probes.md) -- each init writes the state it is about to reset -- plus
  `rcntfire=` per root counter, on `scripts/chains/carry.txt` (FF7, Medievil, FF7, FF7). One
  run answered all of it:
  - root counter 2 arrived with the last game's mode and target (`rc2=e58/43d1` after FF7,
    `c00/43d1` after Medievil) and fired 21288 times instead of 13740: `psxRcntInit` set
    only `rate` and `irq`, and the new game ran on the old target until it reprogrammed it;
  - the SPU arrived with `cycles_played=1354752000` (so the first `do_samples` saw a 1.35 G
    cycle gap, counted a desync and resynced, 60 samples short) and, after Medievil,
    `XARepeat=3`, which is `hold=3`. `DF_SPUinit` never cleared `spu`, the channels, reverb
    or sound RAM. `gaps=1 gap_samples=88260` was the probe's own static (`xa_stream_on`) left
    on by Medievil's stream: 2 s of "gap" and then off;
  - port 1 was EMPTY in the first game (`pad1=0/-` with `gc=1`): with PadAutoAssign off the
    only code that fills a manual port is the menu's status bar, drawn once in an autoboot
    before the pad had answered. The later games found it filled by their own draw.
  - SIO arrived clean (`0005/0/0/0`) but had no reset at all.
- Direction: power-on is the reference. The first game is right for the counters and the
  SPU (BSS zero), the later games are right for the pad (the pad IS connected).
- Fix: `psxRcntInit` zeroes `rcnts` and `frame_counter`; `DF_SPUinit` zeroes `spu` (keeping
  `bSPUIsOpen`), `s_chan`, `rvb`, sound RAM and the XA decoder's history and probe statics;
  new `sioReset()` from `psxHwReset`; `manual_assign_port()` in PlugPAD.c is the status bar's
  manual assignment, now also run by go(). Verified on carry.txt, gpu_carry.txt, ff7x3.txt:
  every FF7 row equal in `slice`, `irq`, `rcntfire`, `spu ns/desync`, `xamix`, `ports`,
  GPU counts. What still differs is Wii-side: `*_us`, the frame limiter count, MEM1 free,
  menu glyphs and the output rate controller (`rate: prefill` is SDL callback timing).
- Lesson: when a later game differs, read what the inits did NOT touch, and decide the
  direction per field from power-on; "first is right" was true for three of four.
