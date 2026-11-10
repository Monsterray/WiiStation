#!/usr/bin/env python3
"""menu_text_center.py [--fix] : where the ink of a centered menu label lands.

--old: the layout before 6.0.0's fix (advance box centered); default: ink centered, as
IplFont::drawString does now (inkSpan). Keep this model in step with IPLFont.cpp.
Reads the menu glyph atlases (fonts/En.dat, fonts/menu/*.dat) and lays each label out
exactly as Gamecube/libgui/IPLFont.cpp does (getStringWidth = (sum(adv+1)+5)*scale,
drawString(centered) starts at x - width/2, each glyph at the rounded pen, ink at its
column in the 24x24 cell). Prints, per font and label, the empty margin left and right of
the ink inside the width the code centers with, and the ink centre's offset from the
requested centre (px, + = right), horizontally and vertically (vertical: ink of the
label vs the 24-row line box, centre of the box is where the code centers).
Exit code 1 if any label is off by more than --tol px (default 1)."""
import os, sys, struct, argparse
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
from genfont import read_dat, detile, CELL

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FONTS = ["fonts/En.dat", "fonts/menu/Sans.dat", "fonts/menu/SansBold.dat", "fonts/menu/Serif.dat", "fonts/menu/Classic.dat"]
LABELS = ["Load ISO", "Players", "Off", "On", "Video", "Achievements", "1 of 2", "Settings", "Back", "OK",
          "0", "1", "2", "10", "Yes", "No", "Quit", "Play Game", "gjpqy"]

def load(path):
    g = {}
    for code, adv, tex in read_dat(open(path, "rb").read()):
        img = detile(tex); bb = img.point(lambda v: 255 if v > 64 else 0).getbbox()
        g[code] = (adv, bb)          # bb = (l, t, r, b) of the ink in the cell, or None (blank)
    return g

def measure(g, text, scale, extra=5):
    """returns (width the code uses, ink l, ink r, ink t, ink b) relative to the draw origin x0,y0"""
    adv = sum(g[ord(c)][0] + 1 for c in text)
    width = int((adv + extra) * scale)
    pen = 0.0; l = r = None; t = b = None
    for c in text:
        a, bb = g[ord(c)]
        if bb:
            x = int(pen + 0.5)
            gl, gr = x + bb[0] * scale, x + bb[2] * scale
            l = gl if l is None else min(l, gl); r = gr if r is None else max(r, gr)
            t = bb[1] * scale if t is None else min(t, bb[1] * scale); b = bb[3] * scale if b is None else max(b, bb[3] * scale)
        pen += (a + 1) * scale
    return width, l, r, t, b

def report(tol, scales, extra=5, quiet=False, old=False):
    bad = 0
    for f in FONTS:
        p = os.path.join(ROOT, f)
        if not os.path.exists(p): continue
        g = load(p)
        for scale in scales:
            rows = []
            for text in LABELS:
                if any(ord(c) not in g for c in text): continue
                w, l, r, t, b = measure(g, text, scale, extra)
                x0 = -(w // 2) if old else int(320 - (l + r) / 2 + 0.5) - 320   # old: x - strWidth/2; new: ink centre on x
                dx = (x0 + (l + r) / 2)            # ink centre minus requested centre
                h = int(CELL * scale); y0 = -(h // 2)
                dy = y0 + (t + b) / 2
                rows.append((text, l, w - r, dx, t, h - b, dy))
                if abs(dx) > tol: bad += 1
            if not quiet:
                print(f"{f} scale {scale}: left/right empty margin in the centered width, ink centre offset")
                for text, l, rm, dx, t, bm, dy in rows:
                    print(f"  {text:14s} L {l:5.1f} R {rm:5.1f}  dx {dx:+5.1f}{' !' if abs(dx) > tol else ''}   top {t:4.1f} bot {bm:4.1f}  dy {dy:+5.1f}")
                if rows:
                    print(f"  mean dx {sum(r[3] for r in rows)/len(rows):+.2f}  max|dx| {max(abs(r[3]) for r in rows):.2f}")
    return bad

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--tol", type=float, default=1.0)
    ap.add_argument("--scales", default="1.0,0.75,0.5"); ap.add_argument("--extra", type=int, default=5); ap.add_argument("--old", action="store_true")
    a = ap.parse_args()
    bad = report(a.tol, [float(s) for s in a.scales.split(",")], a.extra, old=a.old)
    print("labels off by more than %.1f px: %d" % (a.tol, bad)); sys.exit(1 if bad else 0)
