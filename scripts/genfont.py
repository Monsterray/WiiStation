#!/usr/bin/env python3
"""genfont.py --ref OLD.dat --ttf FONT.ttf --out NEW.dat [--px 24] [--preview out.png]
                [--new-widths] [--text "Settings Play Game"]

Regenerate a WiiStation menu font file (fonts/*.dat, loaded by
Gamecube/libgui/IPLFont.cpp). Format, per glyph: uint16 big-endian character
code, uint16 big-endian advance width, then a 24x24 GX IA8 texture (4x4
tiles, texel = alpha byte then intensity byte, 1152 bytes).

The original English file put the baseline on row 19 of the 24-row cell, so
capitals got 17 rows and descenders 4; g, p, y, j, Q were baked already cut.
This script measures the whole character set first and places the baseline
so the deepest descender ends on row 23, shrinking the face by a pixel at a
time if an ascender would then leave the cell. Widths are kept from the
reference file (so menu layouts do not move) unless --new-widths is given.
Needs Pillow."""
import argparse, struct, sys, zipfile, io
from PIL import Image, ImageDraw, ImageFont

CELL, TILE = 24, 4
ENTRY = 4 + CELL * CELL * 2

def read_dat(data):
    out = []
    for i in range(len(data) // ENTRY):
        e = data[i * ENTRY:(i + 1) * ENTRY]
        code, width = struct.unpack(">HH", e[:4])
        out.append((code, width, e[4:]))
    return out

def detile(tex):
    img = Image.new("L", (CELL, CELL)); px = img.load(); p = 0
    for ty in range(CELL // TILE):
        for tx in range(CELL // TILE):
            for y in range(TILE):
                for x in range(TILE):
                    px[tx * TILE + x, ty * TILE + y] = tex[p]; p += 2
    return img

def tile(img):
    px = img.load(); out = bytearray()
    for ty in range(CELL // TILE):
        for tx in range(CELL // TILE):
            for y in range(TILE):
                for x in range(TILE):
                    a = px[tx * TILE + x, ty * TILE + y]
                    out += bytes((a, 255 if a else 0))
    return bytes(out)

def measure(font, chars):
    top, bottom = 0, 0
    for ch in chars:
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        top = min(top, y0); bottom = max(bottom, y1)
    return top, bottom

def build(ttf, px, glyphs, keep_widths):
    chars = [chr(c) for c, _, _ in glyphs]
    # Size and baseline come from the plain ASCII glyphs, so a translation's
    # accented capitals do not shrink the whole face; such a glyph is lowered
    # on its own instead (it has no descender, so there is room below).
    ascii_chars = [c for c in chars if ord(c) < 128] or chars
    while px > 8:
        font = ImageFont.truetype(ttf, px)
        top, bottom = measure(font, ascii_chars)
        baseline = CELL - 1 - bottom          # deepest descender on the last row
        if baseline + top >= 0:
            break
        px -= 1
    else:
        sys.exit("cannot fit the character set into 24 rows")
    print(f"{ttf}: {px}px, baseline row {baseline}, ascent {-top}, descent {bottom}", file=sys.stderr)
    out = bytearray(); over = []; lowered = []
    for code, old_w, _ in glyphs:
        ch = chr(code)
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        base = baseline
        if base + y0 < 0:                       # accent would leave the cell: drop the glyph
            base = min(-y0, CELL - 1 - y1)
            lowered.append((ch, base - baseline))
        img = Image.new("L", (CELL, CELL), 0)
        ImageDraw.Draw(img).text((1, base), ch, font=font, fill=255, anchor="ls")
        adv = old_w if keep_widths else max(1, round(font.getlength(ch)) + 1)
        bbox = img.getbbox()
        if bbox and bbox[2] > adv + 1:
            # keep layouts stable but never let a glyph run into the next one
            over.append((ch, bbox[2], adv)); adv = bbox[2]
        out += struct.pack(">HH", code, adv & 0xFFFF) + tile(img)
    if over:
        print("advance widened to the ink: " +
              " ".join(f"{c}:{a}->{w}" for c, w, a in over), file=sys.stderr)
    if lowered:
        print("lowered to keep the accent inside the cell: " +
              " ".join(f"{c}:+{d}" for c, d in lowered), file=sys.stderr)
    return bytes(out), px, baseline

def preview(path, old, new, text, scale=3):
    def row(glyphs, y, canvas):
        m = {c: (w, t) for c, w, t in glyphs}; x = 4
        for ch in text:
            if ord(ch) not in m: continue
            w, t = m[ord(ch)]
            canvas.paste(detile(t), (x, y), detile(t)); x += w + 1
    width = 4 + sum((dict((c, w) for c, w, _ in new).get(ord(ch), 0) + 1) for ch in text) + 8
    canvas = Image.new("L", (max(width, 64), CELL * 2 + 12), 40)
    row(old, 4, canvas); row(new, CELL + 8, canvas)
    canvas = canvas.resize((canvas.width * scale, canvas.height * scale), Image.NEAREST)
    canvas.save(path); print(f"preview: {path} (top = reference, bottom = new)", file=sys.stderr)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, help=".dat or .zip containing one .dat")
    ap.add_argument("--ttf", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--px", type=int, default=24); ap.add_argument("--preview")
    ap.add_argument("--new-widths", action="store_true")
    ap.add_argument("--text", default="Settings Play Game Quit gjpqy@Q")
    a = ap.parse_args()
    if a.ref.lower().endswith(".zip"):
        with zipfile.ZipFile(a.ref) as z:
            name = [n for n in z.namelist() if n.lower().endswith(".dat")][0]
            ref = z.read(name)
    else:
        name = None; ref = open(a.ref, "rb").read()
    old = read_dat(ref)
    data, px, baseline = build(a.ttf, a.px, old, not a.new_widths)
    if a.out.lower().endswith(".zip"):
        with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(name or "font.dat", data)
    else:
        open(a.out, "wb").write(data)
    print(f"wrote {a.out}: {len(old)} glyphs, {len(data)} bytes", file=sys.stderr)
    if a.preview:
        preview(a.preview, old, read_dat(data), a.text)

if __name__ == "__main__":
    main()
