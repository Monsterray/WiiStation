#!/usr/bin/env python3
"""frame_cycle.py FRAMES_DIR FIRST LAST [--map OUT.png] [--period N] [--grid]

What changes between consecutive frame dumps, and where -- the measurements that
tell "the emulator is losing what it drew" apart from "the game is animating".

A report of the form "N bright dots vanish and none come back" is almost always a
brightness threshold sitting in the middle of a population that is being modulated
smoothly: count the same dots at a different threshold and the one-sidedness goes
away. So this tool never counts at a single threshold. It prints:

  per frame   how many pixels changed at all, how many went from bright to near
              black and how many came back. Roughly equal both ways is motion;
              a large one-sided number is content actually being lost.
  --period N  the count above several thresholds for each phase of an N-frame
              cycle, so a threshold artefact is visible as a swing that only
              exists at one threshold.
  --grid      the fraction of bright pixels that swing, per grid cell, which says
              whether the change is confined to one moving object or is all over
              the background.
  --map       an image of the picture with the changing pixels picked out in red.

Frame numbering note: a frame dump is one image per WiiStation PRESENT, not one
per emulated vblank. A game presenting every second vblank gives 30 dumps a second
with no duplicates, so consecutive dumps can be 2 emulated frames apart. Read the
EC entries of a primitive trace (ptrace_summary.py) for the vblank of each present.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

FPS_OVERLAY = (0, 60, 0, 200)      # the on-screen FPS text, never part of the picture


def load(d, n):
    a = np.asarray(Image.open(os.path.join(d, 'framedump_%d.png' % n)).convert('L'),
                   dtype=np.float32)
    y0, y1, x0, x1 = FPS_OVERLAY
    a[y0:y1, x0:x1] = 0
    return a


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('first', type=int)
    ap.add_argument('last', type=int)
    ap.add_argument('--map')
    ap.add_argument('--period', type=int, default=0)
    ap.add_argument('--grid', action='store_true')
    ap.add_argument('--bright', type=int, default=180)
    args = ap.parse_args()

    nums = list(range(args.first, args.last + 1))
    try:
        L = np.stack([load(args.frames, n) for n in nums])
    except OSError as e:
        sys.exit('cannot read frames: %s' % e)

    print('frames %d..%d  (%d images, %dx%d)'
          % (args.first, args.last, len(nums), L.shape[2], L.shape[1]))
    print('\n  pair        changed   bright->dark   dark->bright')
    for i in range(1, len(nums)):
        a, b = L[i - 1], L[i]
        ch = (np.abs(a - b) > 0.5).sum()
        out = ((a > args.bright) & (b < 60)).sum()
        back = ((a < 60) & (b > args.bright)).sum()
        print('  %d->%d  %9d %12d %14d' % (nums[i - 1], nums[i], ch, out, back))

    if args.period:
        p = args.period
        print('\n  count of pixels above a threshold, by phase of the %d-frame cycle' % p)
        print('  threshold   ' + ' '.join('%6d' % k for k in range(p)) + '     swing')
        for t in (120, 160, 200, 225, 240, 250):
            c = [int(np.mean([(L[i] >= t).sum() for i in range(k, len(nums), p)]))
                 for k in range(p)]
            sw = max(c) - min(c)
            print('     %3d      ' % t + ' '.join('%6d' % v for v in c)
                  + '   %6d (%.0f%%)' % (sw, 100 * sw / max(np.mean(c), 1)))
        print('  A swing that exists at every threshold is real change; one that appears')
        print('  only near a single threshold is that threshold crossing a smooth ramp.')

    rng = L.max(axis=0) - L.min(axis=0)
    mean = L.mean(axis=0)

    if args.grid:
        H, W = mean.shape
        gy, gx = 6, 8
        print('\n  fraction of bright pixels (mean>%d) swinging >10, by grid cell'
              % args.bright)
        for i in range(gy):
            row = []
            for j in range(gx):
                y0, y1 = i * H // gy, (i + 1) * H // gy
                x0, x1 = j * W // gx, (j + 1) * W // gx
                b = mean[y0:y1, x0:x1] > args.bright
                row.append('   -   ' if b.sum() < 8 else
                           '%3.0f%%/%-3d' % (100 * (rng[y0:y1, x0:x1][b] > 10).mean(), b.sum()))
            print('   y%3d-%3d  ' % (i * H // gy, (i + 1) * H // gy) + ' '.join(row))

    if args.map:
        base = (mean * 0.45).astype(np.uint8)
        img = np.dstack([base, base, base])
        img[:, :, 0] = np.maximum(img[:, :, 0], np.clip(rng * 4, 0, 255).astype(np.uint8))
        Image.fromarray(img).save(args.map)
        print('\n  %s: red is how much each pixel changes across these frames' % args.map)


if __name__ == '__main__':
    main()
