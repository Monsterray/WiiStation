#!/usr/bin/env python3
"""frame_compare.py RUN_A RUN_B [--from N] [--no-mask] [--mask X0,Y0,X1,Y1] [--crop OUT.png]

Compare the frame dumps of two runs pixel by pixel, frame N of A against frame N of B, to
prove a change renders the same (or show where it does not). The runs must dump every frame
from the start: `wsx.sh run ... --env KEEP=100000`, or a chain with
`--env FRAMES_DUMP=True --env KEEP=100000`. The emulation is deterministic, so frame N is
the same guest frame in both.

The debug overlay (FPS, memory figures) depends on host timing and always differs, so the
rectangle it occupies, 0,0-430,80 in the 824x480 dump, is masked out; --no-mask keeps it,
--mask adds another rectangle. The last frame of each run is often half written when Dolphin
is stopped, so unreadable frames are skipped, not counted.

Prints how many frames differ, the first few with their differing-pixel count and largest
channel step, and a summary. --crop writes A | B | differing pixels, magnified, around the
largest difference: a step of 1-2 is rounding, a step of 100+ is a different texel or object.
"""
import os
import sys

import numpy as np
from PIL import Image


def frames(run):
    d = os.path.join(run, 'frames')
    return d, sorted(int(f.split('_')[1].split('.')[0]) for f in os.listdir(d)
                     if f.startswith('framedump_'))


def load(d, i):
    return np.asarray(Image.open('%s/framedump_%d.png' % (d, i)).convert('RGB'), dtype=np.int16)


def main():
    args = sys.argv[1:]
    opt = lambda k: args[args.index(k) + 1] if k in args else None
    a_run, b_run = [x for x in args if not x.startswith('--') and x not in
                    (opt('--from'), opt('--mask'), opt('--crop'))][:2]
    start = int(opt('--from') or 0)
    masks = [] if '--no-mask' in args else [(0, 0, 430, 80)]
    if opt('--mask'):
        masks.append(tuple(int(v) for v in opt('--mask').split(',')))
    ad, a = frames(a_run)
    bd, b = frames(b_run)
    n = min(len(a), len(b))
    compared, diffs, worst = 0, [], None
    for k in range(start, n):
        try:
            x, y = load(ad, a[k]), load(bd, b[k])
        except OSError:
            continue
        compared += 1
        if x.shape != y.shape:
            diffs.append((k, -1, -1))
            continue
        m = np.abs(x - y).max(axis=2)
        for x0, y0, x1, y1 in masks:
            m[y0:y1, x0:x1] = 0
        c = int((m > 0).sum())
        if c:
            diffs.append((k, c, int(m.max())))
            if worst is None or m.max() > worst[1].max():
                worst = (k, m, x, y)
    print('frames: A %d, B %d; compared %d from frame %d; differing %d'
          % (len(a), len(b), compared, start, len(diffs)))
    for k, c, s in diffs[:6]:
        print('   frame %4d  %s' % (k, 'different size' if c < 0 else
                                   '%6d pixels, largest step %d' % (c, s)))
    if diffs:
        px = [c for _, c, _ in diffs if c > 0]
        st = [s for _, _, s in diffs if s > 0]
        if px:
            print('   median %d pixels a differing frame (max %d of %d), median largest step %d'
                  % (int(np.median(px)), max(px), 480 * 824, int(np.median(st))))
    if worst is not None and opt('--crop'):
        k, m, x, y = worst
        ys, xs = np.nonzero(m == m.max())
        cy, cx = int(ys[0]), int(xs[0])
        y0, x0 = max(0, cy - 40), max(0, cx - 60)
        cut = lambda im: im[y0:y0 + 80, x0:x0 + 120]
        strip = np.concatenate([cut(x), cut(y), cut(((m > 0)[..., None] * 255).repeat(3, 2))],
                               axis=1).astype(np.uint8)
        Image.fromarray(strip).resize((strip.shape[1] * 4, strip.shape[0] * 4),
                                      Image.NEAREST).save(opt('--crop'))
        print('   crop of frame %d at (%d,%d): %s' % (k, cx, cy, opt('--crop')))
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
