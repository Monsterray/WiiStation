#!/usr/bin/env python3
"""dot_flicker.py FRAMES_DIR FIRST LAST [--exclude x0,y0,x1,y1] [--bright N] [--csv OUT]

Track the small bright dots of a frame-dump sequence (a starfield drawn as 1x1 PSX
rectangles) and say, dot by dot, whether they move and whether they flicker.

A dot is a connected blob of pixels >= --bright (default 170) with at most 6 pixels,
outside the --exclude rectangle (the logo). Dots are matched between consecutive
frames by nearest position within 3 px. For every frame pair it prints, split into
left / middle / right thirds of the picture:

  dots        dots found in the first frame of the pair
  moved       matched dots whose position changed
  flicker     matched dots whose peak brightness changed by more than 40 (a dot that
              blinks, dims or is drawn 1 px instead of 2)
  lost        dots with no match in the next frame (went out)
  new         dots in the next frame with no match in this one (came in)

Then per-third totals over the whole range, and the mean |dx|,|dy| of moved dots, so
"the right side flickers" and "the right side moves" can be told apart.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

FPS_OVERLAY = (0, 60, 0, 200)


def load(d, n):
    a = np.asarray(Image.open(os.path.join(d, 'framedump_%d.png' % n)).convert('L'),
                   dtype=np.int16)
    y0, y1, x0, x1 = FPS_OVERLAY
    a[y0:y1, x0:x1] = 0
    return a


def dots(a, bright, excl):
    """Connected blobs of >= bright pixels, at most 6 px; returns (cx, cy, peak, npix)."""
    m = a >= bright
    if excl:
        x0, y0, x1, y1 = excl
        m[y0:y1, x0:x1] = False
    H, W = m.shape
    seen = np.zeros_like(m)
    out = []
    ys, xs = np.nonzero(m)
    for y, x in zip(ys, xs):
        if seen[y, x]:
            continue
        stack = [(y, x)]
        seen[y, x] = True
        pts = []
        while stack and len(pts) <= 8:
            cy, cx = stack.pop()
            pts.append((cy, cx))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < H and 0 <= nx < W and m[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        if len(pts) <= 6:
            py = [p[0] for p in pts]
            px = [p[1] for p in pts]
            peak = max(int(a[p]) for p in pts)
            out.append((float(np.mean(px)), float(np.mean(py)), peak, len(pts)))
    return out


def match(d0, d1):
    """Nearest-neighbour match within 3 px; returns list of (i0, i1|None) and unmatched i1."""
    used = set()
    pairs = []
    p1 = np.array([(d[0], d[1]) for d in d1]) if d1 else np.zeros((0, 2))
    for i0, d in enumerate(d0):
        if len(p1) == 0:
            pairs.append((i0, None))
            continue
        dist = np.hypot(p1[:, 0] - d[0], p1[:, 1] - d[1])
        j = int(np.argmin(dist))
        if dist[j] <= 3.0 and j not in used:
            used.add(j)
            pairs.append((i0, j))
        else:
            pairs.append((i0, None))
    new = [j for j in range(len(d1)) if j not in used]
    return pairs, new


def third(x, W):
    return 0 if x < W / 3 else (1 if x < 2 * W / 3 else 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames')
    ap.add_argument('first', type=int)
    ap.add_argument('last', type=int)
    ap.add_argument('--exclude', help='x0,y0,x1,y1 of the region to ignore (the logo)')
    ap.add_argument('--bright', type=int, default=170)
    ap.add_argument('--csv')
    args = ap.parse_args()
    excl = tuple(int(v) for v in args.exclude.split(',')) if args.exclude else None

    nums = list(range(args.first, args.last + 1))
    try:
        frames = [load(args.frames, n) for n in nums]
    except OSError as e:
        sys.exit('cannot read frames: %s' % e)
    W = frames[0].shape[1]
    D = [dots(a, args.bright, excl) for a in frames]

    tot = np.zeros((3, 5), int)
    mv = [[], [], []]
    out = open(args.csv, 'w') if args.csv else None
    if out:
        out.write('frame,third,dots,moved,flicker,lost,new\n')
    print('%s: frames %d..%d, dots >= %d, %s' % (args.frames, args.first, args.last,
          args.bright, 'excluding %s' % (excl,) if excl else 'whole picture'))
    print('\n  pair        third   dots moved flick  lost   new')
    for i in range(1, len(nums)):
        d0, d1 = D[i - 1], D[i]
        pairs, new = match(d0, d1)
        row = np.zeros((3, 5), int)
        for i0, j in pairs:
            t = third(d0[i0][0], W)
            row[t, 0] += 1
            if j is None:
                row[t, 3] += 1
                continue
            dx, dy = d1[j][0] - d0[i0][0], d1[j][1] - d0[i0][1]
            if abs(dx) > 0.25 or abs(dy) > 0.25:
                row[t, 1] += 1
                mv[t].append((abs(dx), abs(dy)))
            if abs(d1[j][2] - d0[i0][2]) > 40:
                row[t, 2] += 1
        for j in new:
            row[third(d1[j][0], W), 4] += 1
        tot += row
        for t, nm in enumerate(('left', 'mid', 'right')):
            print('  %d->%d  %-6s %5d %5d %5d %5d %5d' % (nums[i - 1], nums[i], nm, *row[t]))
            if out:
                out.write('%d,%s,%d,%d,%d,%d,%d\n' % (nums[i], nm, *row[t]))
    print('\n  totals over %d pairs:' % (len(nums) - 1))
    print('  third   dots moved flick  lost   new   mean|dx| mean|dy| (of moved)')
    for t, nm in enumerate(('left', 'mid', 'right')):
        mdx = np.mean([v[0] for v in mv[t]]) if mv[t] else 0
        mdy = np.mean([v[1] for v in mv[t]]) if mv[t] else 0
        print('  %-6s %5d %5d %5d %5d %5d   %6.2f   %6.2f' % (nm, *tot[t], mdx, mdy))
    if out:
        out.close()


if __name__ == '__main__':
    main()
