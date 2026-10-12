#!/usr/bin/env python3
"""xfb2png.py XFB.BIN OUT.PNG

Convert the front XFB that a debug build writes at 'dump <vblank>' (sd:/wiistation/xfb.bin,
Gamecube/perf_prof.c) to a picture: what the TV shows, independent of Dolphin's XFB cache.
Under Dolphin run with XFB_RAM=1, or the XFB in memory never receives the copies.
Format: width and height (big-endian u32), then YUYV 4:2:2, two bytes a pixel.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_file  # noqa: E402

import numpy as np
from PIL import Image


def main():
    need(sys.argv, 3, "xfb2png.py XFB.BIN OUT.PNG")
    b = open(req_file(sys.argv[1], 'xfb dump'), 'rb').read()
    if len(b) < 8:
        sys.exit(f"{sys.argv[1]}: too short for a header (8 bytes needed)")
    w, h = struct.unpack('>II', b[:8])
    if not 0 < w <= 1920 or not 0 < h <= 1080 or w % 2:
        sys.exit(f"{sys.argv[1]}: implausible size {w}x{h} (not a TV frame)")
    if len(b) != 8 + w * h * 2:
        sys.exit(f"{sys.argv[1]}: {len(b)} bytes != {8 + w * h * 2} for {w}x{h} YUYV")
    px = np.frombuffer(b[8:8 + w * h * 2], dtype=np.uint8).reshape(h, w // 2, 4).astype(np.float32)
    y = np.stack([px[..., 0], px[..., 2]], -1).reshape(h, w)
    u = np.repeat(px[..., 1], 2, axis=1) - 128
    v = np.repeat(px[..., 3], 2, axis=1) - 128
    y = (y - 16) * 1.164
    rgb = np.stack([y + 1.596 * v, y - 0.392 * u - 0.813 * v, y + 2.017 * u], -1)
    Image.fromarray(rgb.clip(0, 255).astype(np.uint8)).save(sys.argv[2])
    print('%dx%d -> %s' % (w, h, sys.argv[2]))


if __name__ == '__main__':
    main()
