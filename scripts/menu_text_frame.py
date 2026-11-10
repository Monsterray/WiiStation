#!/usr/bin/env python3
"""menu_text_frame.py STILL.png [...]: how far off centre is the white label in each button of a menu still.

For each cluster of near-white pixels (a label), walks left/right/up/down from its ink box to
the button's edge (a jump of more than 25 in luminance) and prints the empty gap on each side.
dx = (left - right) / 2 is the label's horizontal offset (+ = right of centre), in still pixels
(a 824x480 still is 640 menu units wide: 1.29 px per unit). Flags |dx| over --tol (default 1.5).
Vertical gaps are printed for information: caps-only labels sit a little high by design (the
glyph cell keeps room for descenders). tests/menu_text_center.py is the host-side twin."""
import sys, argparse
from PIL import Image

def clusters(mask, w, h):
    seen = set(); out = []
    pts = [(x, y) for y in range(h) for x in range(w) if mask[y * w + x]]
    pset = set(pts)
    for p in pts:
        if p in seen: continue
        stack = [p]; seen.add(p); comp = []
        while stack:
            x, y = stack.pop(); comp.append((x, y))
            for dx in range(-9, 10):
                for dy in (-2, -1, 0, 1, 2):
                    q = (x + dx, y + dy)
                    if q in pset and q not in seen: seen.add(q); stack.append(q)
        out.append(comp)
    return out

def edge(img, x, y, dx, dy, limit, jump=20):
    prev = img.getpixel((x, y))
    for i in range(1, limit):
        px, py = x + dx * i, y + dy * i
        if not (0 <= px < img.width and 0 <= py < img.height): return None
        v = img.getpixel((px, py))
        if abs(v - prev) > jump: return (px, py)
        prev = v
    return None

def measure(path, tol):
    rgb = Image.open(path).convert("RGB"); g = rgb.convert("L"); w, h = rgb.size
    px = rgb.load()
    mask = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if min(px[x, y]) >= 225: mask[y * w + x] = 1
    rows = []
    for comp in clusters(mask, w, h):
        if len(comp) < 20: continue
        xs = [p[0] for p in comp]; ys = [p[1] for p in comp]
        l, r, t, b = min(xs), max(xs), min(ys), max(ys)
        if r - l > 400 or b - t > 40: continue
        cx = (l + r) // 2; y0 = t - 4
        if y0 < 0: continue
        # the button's side edges along a row above the label, from the label's centre outward
        el = edge(g, cx, y0, -1, 0, 170); er = edge(g, cx, y0, 1, 0, 170)
        if el is None or er is None: continue
        left, right = el[0] + 1, er[0] - 1
        et = edge(g, left + 6, (t + b) // 2, 0, -1, 60); eb = edge(g, left + 6, (t + b) // 2, 0, 1, 60)
        if et is None or eb is None: continue
        top, bot = et[1] + 1, eb[1] - 1
        # the ink: anything clearly brighter than the fill on its row, inside the rect (3 px in)
        il = ir = it = ib = None
        for y in range(top + 3, bot - 2):
            fill = g.getpixel((left + 6, y))
            for x in range(left + 3, right - 2):
                if g.getpixel((x, y)) >= fill + 45:
                    il = x if il is None or x < il else il; ir = x if ir is None or x > ir else ir
                    it = y if it is None or y < it else it; ib = y if ib is None or y > ib else ib
        if il is None: continue
        rows.append((il, it, ir, ib, il - left, right - ir, it - top, bot - ib))
    rows.sort(key=lambda r: (r[1] // 20, r[0]))
    bad = 0
    print(path)
    for l, t, r, b, el, er, et, eb in rows:
        dx = (el - er) / 2.0
        flag = " !" if abs(dx) > tol else ""
        bad += bool(flag)
        print(f"  ink x {l:3d}-{r:3d} y {t:3d}-{b:3d}  left {el:3d} right {er:3d}  dx {dx:+5.1f}{flag}   top {et} bottom {eb}")
    if rows:
        print(f"  {len(rows)} labels, mean dx {sum((r[4]-r[5])/2 for r in rows)/len(rows):+.2f}, off by more than {tol}: {bad}")
    return bad, [(r[4] - r[5]) / 2 for r in rows]

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("png", nargs="+"); ap.add_argument("--tol", type=float, default=1.5)
    a = ap.parse_args()
    for p in a.png: measure(p, a.tol)
