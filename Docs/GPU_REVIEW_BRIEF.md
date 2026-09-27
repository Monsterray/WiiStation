# GPU review brief (2026-09-27)

Input for a focused session on the OpenGX GPU plugin (`GlesGpu/`, `deps/opengx/`). It holds
two open bugs, with what is measured and what is not, and the parts of the pipeline that
make them hard. Read `AGENTS.md` (GX/OpenGX rules) and `.claude/skills/wiistation-diagnostics`
first.

## Tools for this session (use these first, they are cheap)

- **`vramio.log`** (debug build, extracted to `.runs/NAME/vramio.log` by every run): every
  large VRAM transfer of the whole run, one line each, repeats merged into
  `first-last xN`. Kinds: `C0` CPU read of VRAM, `C1` readback outcome (a = capture result
  + 8, b = % taken from the EFB), `C2` readback state bits, `C4` snapshot at each present,
  `C5` display flip, `A0` image load, `80` VRAM move, `FF` a game starts. Header lines
  explain the fields. One run answers "where does the game move pictures".
- **Primitive trace** (`trace <vblank>` in the input script, 16 presents): needs a build with
  `bash scripts/wsx.sh build debug all` -- the default `light` preset compiles it out. The
  file is written when the NEXT trace fires, so schedule a second `trace` line after it.
  `scripts/ptrace_summary.py` decodes.
- New trace kinds: `80` VRAM move (col = source x << 12 | y), `A0` image load rect.

## Bug 1: white dots on the left and top edges (Crash games)

**Reproduce:** Crash 3 with the user's recording (`scripts/autoinput/crash_bandicoot_3_warped_play.txt`),
the space scene of the intro, vblank ~986 onward (trace `trace 985` + `trace 1060`).

**Measured (2026-09-27):**
- Frame dumps, isolated bright pixels in the 2-pixel left/top border, 1700 vblanks:
  OpenGX 89 frames / 177 dots, **software GPU (`gpuPlugin=1`) 88 frames / 176 dots**, first
  on the same frame. The GPU plugin draws what it is given: the dots are in the command
  stream.
- Trace of one 16-present episode: 313 one-pixel tiles (`cmd=68`, colour 7c7c7c, the
  stars); 17 at x = 0 (left edge of the drawing area), 13 at y = 12 (its top, draw offset
  (0,12)), 7 exactly at the corner (0,12). Positions pinned to the boundary: off-screen
  stars clamped to the edge instead of culled.

**Not yet known -- next steps, in order:**
1. Same trace on the interpreter core (`Core=1` on the chain line, allow `--secs 900`: it is
   slow). Same edge stars there = not Lightrec's GTE; different = a Lightrec GTE bug.
2. The GTE: screen-coordinate saturation (SX/SY limits and FLAG bits 13/14) and the divide
   overflow (FLAG bit 17). A game that culls on FLAG draws a clamped star when FLAG is not
   set. Compare our `gte.c` / Lightrec GTE with psx-spx "GTE Saturation".
3. Real footage of the same scene (yt-dlp, `C:\tools\yt-dlp`) to rule out authentic
   behaviour.
The earlier Crash 3 star bug (stars dropped on alternate frames, draw offset counted twice
in `primTile1`, fixed) is a different mechanism.

## Bug 2: Ape Escape pause screen blue; no CRT fade between scenes

**Reference:** video b21k8NFfVtk, 17:10-17:13 (fade: the picture folds in on itself, then a
white rectangle shrinks to a line and a dot) and 17:30-17:37 (pause over the frozen game).
Frames in `C:\tools\reference\ape_escape\` (`trans_sheet.png`, `pause_sheet.png`).

**Reproduce:** Ape Escape, `ControllerType=1`, input script: Start (0008) then Cross (4000)
every 150 vblanks from 600 (reaches gameplay and pauses at vblank 3305).

**What the game does (vramio.log, one run):**
```
3305 C0 0,256 384x240        CPU reads the frame it just drew
3305 C1 0,256 384x240 6 0    capture result -2, 0% from the EFB -> RAM gets the blue clear
3427-4199 x194 A0 0,256 384x240   then uploads that copy every paused frame, both buffers
3429-4197 x193 A0 0,0 384x240
```
On a PlayStation the read returns the frame; here only the EFB holds it, and the readback
refuses (`CanCaptureActiveEfb`: active map content not valid). Every paused frame then
darkens it with 836 subtractive 8x8 sprites and draws the menu.

**Why the capture is refused (measured):** `C4` (snapshot at each present) is refused on
every present of gameplay with only `mapValid` set: `content_valid` and `content_dirty` are 0
although the game draws every frame. The active map alternates between (0,0) and (0,256)
each present without passing `OnDisplayMappingWillChange` (no `C5` lines in gameplay), and
each rebuild of the active map clears `content_valid`. The tracker's model -- the EFB
belongs to the displayed map, draws are clipped to it or to a "pending presented" map --
does not follow a game that double-buffers in the ordinary way (draw into the back buffer,
flip). Spyro works through a separate special case (`TryCapturePreviousReadRect`: read of
the back buffer while the drawing area lies over the previous display).

Putting Ape Escape on the readback list (`database.c`) alone does not fix it (tried; reverted).
The fade is probably the same thing (the last frame drawn back into the new one); not traced
yet -- needs a recording that reaches a scene change (`scripts/movie_capture.sh ape --ct 1`).

**Direction for the fix:** make the tracker's notion of "what the EFB holds" follow the
drawing area (E3/E4), which is where P.E.Op.S. draws, instead of the displayed map; then a
read, move or texture sample of a buffer is served from the live EFB (if it is the one
being drawn) or from the snapshot taken when it was last presented. That replaces several
special cases rather than adding one.

## The pipeline's special cases (what a cleanup has to account for)

- Per-game lists and fixes: `database.c` `special_game_hack_vram_readback` (Dino Crisis 2,
  Vagrant Story, Spyro), `AUTO_FIX_*` flags in `dwActFixes`; `primMoveImage` materializes the
  EFB only for Dino Crisis 2's exact copy shape.
- Readback tracker (`gpuVramReadback.inc`, ~2300 lines): active / previous / pending-presented
  maps, EFB tile coverage, live and previous snapshots, rebuild candidates, async captures.
- Off-screen drawing: `OffscreenSoftDraw` rasterizes primitives outside both display buffers
  in software (`do_cmd_list`), clipped to the drawing area (fixed 2026-09-23 for MediEvil).
- Screen re-upload from VRAM (`UploadScreen`, trace `ea`/`eb`) after CPU image loads that
  overlap the display.
- Removed 2026-09-27: the FPS overlay's "draw only when the game redrew the top-left
  corner" guess and the `g_efbContaminated` readback gate. The overlay now saves the EFB,
  draws, copies to the XFB and restores (`OverlaySaveEfb`/`OverlayRestoreEfb`, gpuPlugin.c).
