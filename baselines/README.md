# Baselines

Reference runs kept so that later runs can be compared for regressions and improvements
instead of judged from memory. One directory per run, filed by `scripts/baseline_add.py`.

```
baselines/index.csv            one row per baseline with its key metrics (open it in a spreadsheet)
baselines/<id>/perf.log        the debug build's counters, last report block is what matters
baselines/<id>/run.log         dolphin_run.sh's log: build timestamp, every Dolphin option used
baselines/<id>/settings.cfg    the settings the run was staged with
baselines/<id>/notes.md        game, purpose, build, and the numbers in one page
baselines/media/<id>/          audio.flac (the Dolphin audio dump, lossless) and sheet.png
                               (contact sheet of the kept frames). NOT in git: see .gitignore
```

The text side is small (a perf.log is ~100 KB) and committed; the media side is a few MB per
run and lives only on this machine. If a media file matters beyond this workstation, copy it
somewhere durable and say where in the notes.

## Filing a run

```
python scripts/baseline_add.py <run dir> --id 2026-09-21_spyro_cdpf --game "Spyro the Dragon" \
    --purpose "read-ahead on, 64 KB buffers" --settings <staged settings file> [--build REV]
```

The run directory is what `scripts/dolphin_run.sh <outdir>` produced; the script picks up
`perf.log`, `<outdir>.log`, the single `*_dspdump1.wav` and `sheet.png` by itself. Audio is
converted with ffmpeg; `--no-audio` skips it. `--force` replaces an existing id.

## Comparing against one

```
python scripts/perf_compare.py baselines/<id>/perf.log <new run>/perf.log     # counters, A vs B vs B/A
python scripts/wav_compare.py  baselines/media/<id>/audio.flac <new>.wav        # level, silence, short gaps, skew
python scripts/wav_spectrum.py baselines/media/<id>/audio.flac <new>.wav        # band energies
```

Both audio tools read FLAC (decoded through ffmpeg on the fly). Two runs are never
sample-aligned, so judge audio by level, silence structure, gap count and spectrum, not by
correlation.

## What the numbers are, and are not

Everything here was measured under Dolphin unless the notes say otherwise. Dolphin's guest
time is an instruction-count estimate: it ranks costs and detects changes in the counters
reliably, it does not model cache misses or memory stalls, and its SD card never blocks. So a
baseline says "this change moved this counter by this much", not "this is how long it takes
on a Wii". A hardware `perf.log` (same debug build, `sd:/wiisxrx/perf.log`) files the same way
and should be marked `hardware` in the purpose.

Frame-dumping and DSP LLE both slow Dolphin's host side; neither changes the guest-side
counters (verified: identical `wall_us`/`vblanks` with and without), so a baseline with frames
compares fine against one without.

## The `speed` column

`speed` is emulated PSX vblanks / 60 per second of guest time. Under Dolphin it reads ~0.72 for
Spyro's gameplay script and ~0.95 for Medievil's intro, with the limiter idling 13-29% at the
same time, so it is not a frame-rate: Dolphin's guest clock and its vblank count do not stand
in the hardware ratio. It IS stable to three digits across runs of the same script (0.721,
0.724, 0.719 for three builds), which is what makes it a regression detector: a real change in
emulated throughput moves it. On hardware it should read 1.00 for a full-speed game.

## Naming

`<date>_<game>_<variant>`: `2026-09-20_spyro_cpu_hold`, `2026-09-21_ff7_idle`. Known-bad
runs kept as references for a detector carry `knownbad` in the id and say what the fault was
in the purpose.
