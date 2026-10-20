"""bit15_cmp.py RUN [--png DIR] [--box X0,Y0,X1,Y1] -- the bit-15 oracle: OpenGX against the soft renderer.

RUN is a chain run in which each game runs to the same vblank once under a soft renderer
(gpuPlugin=0 or 1; use SoftGpuMode=0, Accurate: the PS1 rules on every write path, the
reference) and once or more under OpenGX (gpuPlugin=2). The chain's end-of-game VRAM dumps
(vram_NN.bin; OpenGX's EFB is synced into VRAM first) are compared per pixel:
  ref% / ogx%  share of pixels with bit 15 set (in the box)
  ref-only     bit 15 set in the reference, clear under OpenGX
  ogx-only     set under OpenGX, clear in the reference
  colour       pixels whose 15-bit colour differs (GX and the soft rasterizer differ at edges
               and in blends anyway: context, not the measure)
  parted       the first vblank where the two guests' vsig differ (guest RAM, cycles, PC):
               past it the two dumps are not the same guest moment
--png DIR writes DIR/bit15_NN.png per OpenGX entry: the reference's VRAM dimmed, bit 15 set in
both = green, ref-only = red, ogx-only = cyan. Chain lines: scripts/chains/bit15_oracle.txt.
Exit 1 when any OpenGX entry has a bit-15 difference.
"""
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import parse_opts  # noqa: E402

END = re.compile(r"=== chain (\d+)/\d+ end vblanks=(\d+)(?: set=(\S+))? at=\S+ rom=(.+?) ===")


def vram(run, n):
    p = os.path.join(run, "vram_%02d.bin" % n)
    if not os.path.isfile(p) or os.path.getsize(p) < 1024 * 512 * 2:
        return None
    return np.fromfile(p, dtype="<u2", count=1024 * 512).reshape(512, 1024)


def parted(run, a, b):
    """first vblank where the two entries' vsig files differ, '-' if never, '?' if missing"""
    try:
        from vsig_cmp import load
        va = load(os.path.join(run, "vsig_%02d.bin" % a))
        vb = load(os.path.join(run, "vsig_%02d.bin" % b))
    except (OSError, SystemExit, ImportError):
        return "?"
    n = min(len(va), len(vb))
    k = min(len(va[0]), len(vb[0]), 3) if n else 3      # ram, cycle, pc
    first = next((i for i in range(n) if va[i][:k] != vb[i][:k]), None)
    return "-" if first is None else str(first + 1)


def main():
    pos, opts = parse_opts(sys.argv, valued=("--png", "--box"), bools=("-h", "--help"))
    if "-h" in opts or "--help" in opts or not pos:
        sys.exit(__doc__.strip())
    run = pos[0]
    x0, y0, x1, y1 = (int(v) for v in opts.get("--box", "0,0,1024,512").split(","))
    games = {}
    for line in open(os.path.join(run, "perf.log"), errors="replace"):
        m = END.search(line)
        if m:
            sets = dict(kv.split("=", 1) for kv in (m.group(3) or "").split(",") if "=" in kv)
            games.setdefault((m.group(4), m.group(2)), []).append((int(m.group(1)), sets))
    png = opts.get("--png")
    if png:
        os.makedirs(png, exist_ok=True)
    bad = 0
    print(f"{'game':<30} {'vbl':>6} {'ogx set':<22} {'ref%':>6} {'ogx%':>6} {'ref-only':>9} "
          f"{'ogx-only':>9} {'colour':>8} {'parted':>7}")
    for (rom, vbl), ents in games.items():
        ref = next((e for e in ents if e[1].get("gpuPlugin") in ("0", "1")), None)
        if not ref:
            continue
        r = vram(run, ref[0])
        if r is None:
            print(f"{rom[:30]:<30} {vbl:>6} reference vram_{ref[0]:02d}.bin missing")
            continue
        r = r[y0:y1, x0:x1]
        rs = (r & 0x8000) != 0
        for n, sets in ents:
            if sets.get("gpuPlugin") != "2":
                continue
            g = vram(run, n)
            if g is None:
                print(f"{rom[:30]:<30} {vbl:>6} vram_{n:02d}.bin missing")
                continue
            g = g[y0:y1, x0:x1]
            gs = (g & 0x8000) != 0
            ro, go = int((rs & ~gs).sum()), int((gs & ~rs).sum())
            col = int(((r ^ g) & 0x7FFF != 0).sum())
            bad |= (ro + go) != 0
            label = ",".join(f"{k}={v}" for k, v in sets.items() if k not in ("gpuPlugin", "ChainDump"))
            print(f"{rom[:30]:<30} {vbl:>6} {(label or '-')[:22]:<22} {100 * rs.mean():6.2f} "
                  f"{100 * gs.mean():6.2f} {ro:>9} {go:>9} {col:>8} {parted(run, ref[0], n):>7}")
            if png:
                from PIL import Image
                rgb = np.stack([(r & 31) << 3, ((r >> 5) & 31) << 3, ((r >> 10) & 31) << 3], -1)
                img = (rgb // 3).astype(np.uint8)
                img[rs & gs] = (0, 160, 0)
                img[rs & ~gs] = (255, 0, 0)
                img[gs & ~rs] = (0, 255, 255)
                Image.fromarray(img).save(os.path.join(png, "bit15_%02d.png" % n))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
