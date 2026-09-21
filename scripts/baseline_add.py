#!/usr/bin/env python3
"""baseline_add.py <run_dir> --id ID --game "Name" --purpose "why this run is kept" [options]

Files one unattended run (a `scripts/dolphin_run.sh` output directory) as a baseline under
`baselines/`, so later runs can be compared against it for regressions and improvements:

  baselines/<ID>/perf.log        the debug build's counters (text, small, committed)
  baselines/<ID>/run.log         dolphin_run.sh's own log: build timestamp, Dolphin options
  baselines/<ID>/settings.cfg    the settings file the run was staged with (if given)
  baselines/<ID>/notes.md        game, purpose, build commit, and the key numbers
  baselines/media/<ID>/audio.flac   the Dolphin audio dump, lossless (needs ffmpeg), ignored by git
  baselines/media/<ID>/sheet.png    the contact sheet of the kept frames, ignored by git
  baselines/index.csv            one row per baseline with the key metrics (committed)

Options:
  --settings PATH   copy this settings file in
  --audio PATH      WAV to keep (default: the run's *_dspdump1.wav if there is exactly one)
  --no-audio        keep no audio even if the run has a dump
  --sheet PATH      contact sheet to keep (default: the run's sheet.png)
  --build REV       build commit (default: `git rev-parse --short HEAD`)
  --force           overwrite an existing baseline of that ID

The metrics come from the LAST report block of perf.log: guest seconds, emulated vblanks,
speed (vblanks/60 per guest second), limiter idle share, hw_gpu share, SPU share, resampler
us, texk conv/tile/mdec us, CD reads and worst stall, read-ahead hits, and, when audio is
kept, its duration, RMS and the short-gap count from wav_compare.py.

Compare later with:
  python scripts/perf_compare.py baselines/<ID>/perf.log <new run>/perf.log
  python scripts/wav_compare.py  baselines/media/<ID>/audio.flac <new dump>.wav
  python scripts/wav_spectrum.py baselines/media/<ID>/audio.flac <new dump>.wav
"""
import sys, os, re, csv, glob, shutil, subprocess, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASE = os.path.join(ROOT, "baselines")
INDEX_FIELDS = ["id", "date", "game", "purpose", "build", "settings", "dsp_lle", "frames", "guest_s", "vblanks",
                "speed", "idle_pct", "hw_gpu_pct", "spu_pct", "out_us", "conv_us", "tile_us", "mdec_us",
                "cd_reads", "cd_worst_us", "cdpf_hit", "cdpf_miss", "audio", "audio_s", "audio_rms_db", "audio_gaps"]


def last_block(perf_path):
    blocks, cur = [], None
    for line in open(perf_path, encoding="utf-8", errors="replace"):
        if line.startswith("--- perf frames="):
            cur = {}; blocks.append(cur)
        elif cur is not None and ":" in line:
            tag, rest = line.split(":", 1)
            d = cur.setdefault(tag.strip(), {})
            for k, v in re.findall(r"(\w+)=([-\d./]+)", rest):
                d[k] = v
    return blocks[-1] if blocks else {}


def num(block, tag, key, default=0.0):
    try:
        return float(block.get(tag, {}).get(key, default))
    except ValueError:
        return default


def audio_metrics(path):
    """Duration, RMS and short-gap count, via wav_compare's own functions."""
    sys.path.insert(0, HERE)
    import wav_compare  # noqa: E402
    w = wav_compare.read_wav(path)
    st = wav_compare.stats(w, 100)
    return st["seconds"], st["rms_db"], st["gaps"]


def main(argv):
    if len(argv) < 1 or argv[0].startswith("-"):
        sys.exit(__doc__)
    run_dir = argv[0]
    opts = {"--id": None, "--game": None, "--purpose": None, "--settings": None, "--audio": None,
            "--sheet": None, "--build": None}
    flags = {"--no-audio": False, "--force": False}
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in flags:
            flags[a] = True; i += 1
        elif a in opts:
            opts[a] = argv[i + 1]; i += 2
        else:
            sys.exit("unknown option %s\n%s" % (a, __doc__))
    for req in ("--id", "--game", "--purpose"):
        if not opts[req]:
            sys.exit("%s is required\n%s" % (req, __doc__))
    bid = opts["--id"]
    if not re.match(r"^[A-Za-z0-9._-]+$", bid):
        sys.exit("--id: letters, digits, . _ - only")
    perf = os.path.join(run_dir, "perf.log")
    if not os.path.exists(perf):
        sys.exit("no perf.log in %s" % run_dir)

    dest = os.path.join(BASE, bid)
    media = os.path.join(BASE, "media", bid)
    if os.path.exists(dest) and not flags["--force"]:
        sys.exit("%s exists; use --force to replace it" % dest)
    os.makedirs(dest, exist_ok=True)
    os.makedirs(media, exist_ok=True)

    shutil.copy2(perf, os.path.join(dest, "perf.log"))
    run_log = run_dir.rstrip("/\\") + ".log"
    dsp_lle = frames = "?"
    if os.path.exists(run_log):
        shutil.copy2(run_log, os.path.join(dest, "run.log"))
        txt = open(run_log, encoding="utf-8", errors="replace").read()
        dsp_lle = "1" if "DSPHLE=False" in txt else "0"
        frames = "0" if "DumpFrames=False" in txt else "1"
    if opts["--settings"]:
        shutil.copy2(opts["--settings"], os.path.join(dest, "settings.cfg"))
    build = opts["--build"] or subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                               capture_output=True, text=True).stdout.strip()

    # media
    audio_name = ""
    audio_s = audio_rms = audio_gaps = ""
    if not flags["--no-audio"]:
        wav = opts["--audio"]
        if wav is None:
            cands = glob.glob(os.path.join(run_dir, "*_dspdump1.wav"))
            wav = cands[0] if len(cands) == 1 else None
        if wav and os.path.exists(wav):
            try:
                s, r, g = audio_metrics(wav)
                audio_s, audio_rms, audio_gaps = "%.1f" % s, "%.1f" % r, str(g)
            except Exception as e:  # metrics are a convenience; the file still gets kept
                print("audio metrics failed:", e)
            out = os.path.join(media, "audio.flac")
            ff = shutil.which("ffmpeg") or r"C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe"
            if os.path.exists(ff) or shutil.which("ffmpeg"):
                r = subprocess.run([ff, "-y", "-loglevel", "error", "-i", wav, "-c:a", "flac", "-compression_level", "8", out])
                if r.returncode == 0:
                    audio_name = "audio.flac"
            if not audio_name:
                shutil.copy2(wav, os.path.join(media, "audio.wav"))
                audio_name = "audio.wav"
    sheet = opts["--sheet"] or os.path.join(run_dir, "sheet.png")
    if os.path.exists(sheet):
        shutil.copy2(sheet, os.path.join(media, "sheet.png"))

    # metrics
    b = last_block(perf)
    wall = num(b, "wall", "wall_us")
    guest_s = wall / 1e6
    vbl = num(b, "wall", "vblanks")
    speed = (vbl / 60.0) / guest_s if guest_s else 0
    row = {
        "id": bid, "date": datetime.date.today().isoformat(), "game": opts["--game"], "purpose": opts["--purpose"],
        "build": build, "settings": os.path.basename(opts["--settings"]) if opts["--settings"] else "",
        "dsp_lle": dsp_lle, "frames": frames,
        "guest_s": "%.1f" % guest_s, "vblanks": "%d" % vbl, "speed": "%.3f" % speed,
        "idle_pct": "%.1f" % (100 * num(b, "inside", "limit_us") / wall if wall else 0),
        "hw_gpu_pct": "%.1f" % (100 * num(b, "inside", "hw_gpu_us") / wall if wall else 0),
        "spu_pct": "%.2f" % (100 * num(b, "inside", "spu_us") / wall if wall else 0),
        "out_us": "%d" % num(b, "inside", "out_us"),
        "conv_us": "%d" % num(b, "texk", "conv_us"), "tile_us": "%d" % num(b, "texk", "tile_us"),
        "mdec_us": "%d" % num(b, "texk", "mdec_us"),
        "cd_reads": "%d" % num(b, "cd", "reads"), "cd_worst_us": "%d" % num(b, "cd", "worst_us"),
        "cdpf_hit": "%d" % num(b, "cdpf", "hit"), "cdpf_miss": "%d" % num(b, "cdpf", "miss"),
        "audio": audio_name, "audio_s": audio_s, "audio_rms_db": audio_rms, "audio_gaps": audio_gaps,
    }

    # notes
    with open(os.path.join(dest, "notes.md"), "w", encoding="utf-8") as f:
        f.write("# %s\n\n" % bid)
        f.write("- game: %s\n- purpose: %s\n- date: %s\n- build: %s\n- settings: %s\n- DSP LLE: %s, frames dumped: %s\n\n"
                % (row["game"], row["purpose"], row["date"], build, row["settings"] or "(defaults)", dsp_lle, frames))
        f.write("| metric | value |\n|---|---|\n")
        for k in INDEX_FIELDS[8:]:
            f.write("| %s | %s |\n" % (k, row[k]))
        f.write("\nCompare: `python scripts/perf_compare.py baselines/%s/perf.log <run>/perf.log`\n" % bid)

    # index
    index = os.path.join(BASE, "index.csv")
    rows = []
    if os.path.exists(index):
        with open(index, newline="", encoding="utf-8") as f:
            rows = [r for r in csv.DictReader(f) if r.get("id") != bid]
    rows.append(row)
    with open(index, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=INDEX_FIELDS)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in INDEX_FIELDS})
    print("filed %s: %s (%s), speed %s, idle %s%%, hw_gpu %s%%, audio %s" %
          (bid, row["game"], row["purpose"], row["speed"], row["idle_pct"], row["hw_gpu_pct"], audio_name or "none"))


if __name__ == "__main__":
    main(sys.argv[1:])
