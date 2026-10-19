"""vram_ref_cmp.py VRAM.BIN REF.PNG [OUT.PNG] [--box X0,Y0,X1,Y1] -- a run's VRAM against a real-PS1 VRAM picture.

VRAM.BIN is a 1024x512 dump of 16-bit little-endian PS1 pixels (a run's vram_NN.bin); REF.PNG is
a 1024x512 picture of real-PS1 VRAM with 5-bit colour shifted up by 3 (ps1-tests' vram.png, e.g.
C:/tools/ps1-tests/bin/gpu/transparency/vram.png). Bit 15 is not in the picture, so only colour
is compared. Prints the pixels that differ (in the box, default all of VRAM) and writes OUT.PNG:
the run's VRAM, with differing pixels in magenta. Exit 1 when any pixel differs.
"""
import struct
import sys

from PIL import Image

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from cliutil import parse_opts, req_file  # noqa: E402

W, H = 1024, 512


def vram_rgb(path):
    d = open(path, "rb").read()
    if len(d) < W * H * 2:
        sys.exit(f"{path}: {len(d)} bytes, need {W * H * 2}")
    px = struct.unpack_from(f"<{W * H}H", d)
    return [((p & 31) << 3, ((p >> 5) & 31) << 3, ((p >> 10) & 31) << 3) for p in px]


def main():
    pos, opts = parse_opts(sys.argv, valued=("--box",), bools=("-h", "--help"))
    if "-h" in opts or "--help" in opts or len(pos) < 2:
        sys.exit(__doc__.strip())
    vram, ref = req_file(pos[0], "VRAM dump"), req_file(pos[1], "reference picture")
    x0, y0, x1, y1 = (int(v) for v in opts.get("--box", f"0,0,{W},{H}").split(","))
    got = vram_rgb(vram)
    want = Image.open(ref).convert("RGB")
    if want.size != (W, H):
        sys.exit(f"{ref}: {want.size}, need {W}x{H}")
    wp = want.load()
    out = Image.new("RGB", (W, H))
    op = out.load()
    bad = 0
    for y in range(H):
        for x in range(W):
            g = got[y * W + x]
            w = tuple(c & 0xF8 for c in wp[x, y])
            diff = g != w and x0 <= x < x1 and y0 <= y < y1
            bad += diff
            op[x, y] = (255, 0, 255) if diff else g
    if len(pos) > 2:
        out.save(pos[2])
    print(f"{vram}: {bad} pixels differ from {ref} in box {x0},{y0}-{x1},{y1}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
