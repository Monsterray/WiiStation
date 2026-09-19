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
