#!/usr/bin/env python3
"""screenshot_check.py PNG VRAM_BIN SRC_X SRC_Y WIDTH HEIGHT

Checks a WiiStation screenshot (Gamecube/screenshot.c) against the VRAM of the same vblank.
VRAM_BIN is the debug build's raw VRAM dump (`dump <vblank>` in an input script): 1024 x 512
16-bit words, each stored little-endian as the PSX stores them (the Wii is big-endian, so this
is a byte swap of the host's u16 values). SRC_X, SRC_Y, WIDTH and HEIGHT are the
display rectangle the save logged ("screenshot: saved ... at X,Y" in perf.log).

Exit status 0 when every pixel matches the 15-bit colours of the rectangle. The PNG must be
8-bit RGB with filter type 0 on every row, which is what screenshot.c writes. 24-bit display
modes are not checked (the rows would need their own byte order).

Stdlib only. Example:
  python scripts/screenshot_check.py shot.png vram.bin 0 240 512 240
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG: " + path)
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if (depth, ctype) != (8, 2):
                raise SystemExit("expected 8-bit RGB, got depth %d colour type %d" % (depth, ctype))
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = 3 * w + 1
    if len(raw) != h * stride:
        raise SystemExit("bad IDAT length")
    rows = []
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        if row[0] != 0:
            raise SystemExit("filter type %d on row %d" % (row[0], y))
        rows.append(row[1:])
    return w, h, rows


def c5(v):
    v &= 31
    return (v << 3) | (v >> 2)


def main(argv):
    if len(argv) != 7:
        print(__doc__)
        return 2
    png, vram_path = argv[1], argv[2]
    sx, sy, want_w, want_h = (int(v) for v in argv[3:7])
    w, h, rows = read_png(png)
    if (w, h) != (want_w, want_h):
        print("size %dx%d, expected %dx%d" % (w, h, want_w, want_h))
        return 1
    vram = open(vram_path, "rb").read()
    words = struct.unpack("<%dH" % (len(vram) // 2), vram)
    bad, first = 0, []
    for y in range(h):
        row = rows[y]
        for x in range(w):
            v = words[((sy + y) & 511) * 1024 + ((sx + x) & 1023)]
            want = bytes((c5(v), c5(v >> 5), c5(v >> 10)))
            got = row[3 * x:3 * x + 3]
            if got != want:
                bad += 1
                if len(first) < 5:
                    first.append((x, y, got.hex(), want.hex()))
    print("pixels checked: %d, differing: %d" % (w * h, bad))
    for x, y, got, want in first:
        print("  x=%d y=%d png=%s vram=%s" % (x, y, got, want))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
