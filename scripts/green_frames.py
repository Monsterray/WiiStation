"""green_frames.py RUN [--min PCT]: frames of a run whose picture is more than PCT% (default 1)
green -- g > 100, g > r + 60, g > b + 60, below the 80-pixel overlay. The FF7 opening's green
frame (Docs/PARKED_BUGS.md) is the case it was written for."""
import os
import sys
from PIL import Image

run = sys.argv[1]
mn = float(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 1.0
d = os.path.join(".runs", run, "frames") if not os.path.isdir(run) else os.path.join(run, "frames")
hits = []
for f in sorted(os.listdir(d), key=lambda f: int(f.split("_")[1].split(".")[0])):
    im = Image.open(os.path.join(d, f)).convert("RGB")
    w, h = im.size
    px = im.crop((0, 80, w, h)).getdata()
    g = sum(1 for r, gg, b in px if gg > 100 and gg > r + 60 and gg > b + 60)
    pct = 100.0 * g / (w * (h - 80))
    if pct > mn:
        hits.append((f, round(pct, 1)))
print(f"{run}: {len(hits)} green frames" + (f": {hits[:10]}" if hits else ""))
