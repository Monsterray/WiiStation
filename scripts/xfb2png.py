#!/usr/bin/env python3
"""xfb2png.py XFB.BIN OUT.PNG

Convert the front XFB that a debug build writes at 'dump <vblank>' (sd:/wiisxrx/xfb.bin,
Gamecube/perf_prof.c) to a picture: what the TV shows, independent of Dolphin's XFB cache.
Under Dolphin run with XFB_RAM=1, or the XFB in memory never receives the copies.
Format: width and height (big-endian u32), then YUYV 4:2:2, two bytes a pixel.
"""
import struct
import sys

import numpy as np
from PIL import Image


def main():
    b = open(sys.argv[1], 'rb').read()
    w, h = struct.unpack('>II', b[:8])
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
