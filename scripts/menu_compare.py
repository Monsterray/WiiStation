#!/usr/bin/env python3
"""menu_compare.py -- the comparison half of `wsx.sh menu`: did two WiiStation menu runs draw
the same frames, and did a scripted walk move the menu? Stdlib + PIL.

  menu_compare.py compare RUN_A [RUN_B] --out DIR [--page | --input WALK]
      --page   one picture each: the LAST still picture of each run (the first frame of the
               last stretch of at least STILL frames that do not change, masks aside). The page
               opens a few frames after boot, so the boot screen and the main menu are earlier
               stills; the page is the last one. A against B.
      --input  a walk: frame N of A against frame N of B (aligned by frame index), and the
               movements of each run: changes above MOVE_PX pixels after its first still
               picture. A focus move repaints a button (thousands of pixels); flicker is a few
               pixels. With a menupage line the first movement is the page opening, not a press,
               and is not counted. A walk with a menukeys line and no movement exits 2: the
               presses were ignored.
      Writes DIR/before_after.png: the most different pair (A | B), or the first pair if the
      runs are identical.
  menu_compare.py ini-off WIIMOTE_INI
      sets Source = 0 on Wiimotes 1-4 (inserting the line where missing), in place.

Masks: the debug overlay (cliutil.FPS_OVERLAY) and the spinning logo. The logo's two places
(Gamecube/menu/MenuLayout.h: LOGO_MAIN_* and LOGO_PAGE_*, MENU_LOGO_W/H) are scaled from the
640x480 menu to the dump and padded by PAD pixels. The padding is generous on purpose: the
logo's drawn edge reaches a few pixels past its 72x72 box. A dump is 824 wide, so the masks
follow the dump's size, not a fixed box.

Exit: 0 identical (or a walk that moved, with one run), 1 a difference, 2 a walk that did not
move the menu, 3 nothing to compare or a usage error.
"""
import functools
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import FPS_OVERLAY, parse_opts  # noqa: E402

from PIL import Image, ImageChops, ImageDraw  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT = os.path.join(REPO, 'Gamecube', 'menu', 'MenuLayout.h')
PAD = 12                     # dump pixels of margin around each logo box
STILL = 3                    # frames that must stay unchanged to count as a still picture
MOVE_PX = 2000               # a change above this is a menu movement; flicker is under 20 px
WIIMOTES = ('Wiimote1', 'Wiimote2', 'Wiimote3', 'Wiimote4')
USAGE = ("menu_compare.py compare RUN_A [RUN_B] --out DIR [--page | --input WALK]  |  "
         "menu_compare.py ini-off WIIMOTE_INI")


def fail(msg, code=3):
    print(msg)
    return code


@functools.lru_cache(maxsize=None)
def masks_for(width, height):
    """Rectangles (x0, y0, x1, y1), x1/y1 exclusive, that a comparison ignores."""
    try:
        with open(LAYOUT, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError as e:
        sys.exit(f"{LAYOUT}: {e.strerror}")
    c = {m.group(1): float(m.group(2))
         for m in re.finditer(r'^\s*#define\s+(\w+)\s+(-?\d+(?:\.\d+)?)', text, re.M)}
    try:
        sx, sy = width / c['MENU_W'], height / c['MENU_H']
        hw, hh = c['MENU_LOGO_W'] / 2, c['MENU_LOGO_H'] / 2
        spots = [(c['LOGO_MAIN_X'], c['LOGO_MAIN_Y']), (c['LOGO_PAGE_X'], c['LOGO_PAGE_Y'])]
    except KeyError as e:
        sys.exit(f"MenuLayout.h: {e.args[0]} not found")
    logo = [(math.floor((x - hw) * sx) - PAD, math.floor((y - hh) * sy) - PAD,
             math.ceil((x + hw) * sx) + PAD, math.ceil((y + hh) * sy) + PAD) for x, y in spots]
    return tuple([FPS_OVERLAY] + logo)


def load(path):
    try:
        return Image.open(path).convert('RGB')
    except OSError:
        return None          # a half-written last frame: skipped, not counted


def diff(a, b):
    """(differing pixels, largest channel step) of two RGB frames, masks excluded.
    Frames of different size give (-1, -1)."""
    if a.size != b.size:
        return -1, -1
    r, g, bl = ImageChops.difference(a, b).split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), bl)   # per pixel: largest channel step
    draw = ImageDraw.Draw(m)
    for x0, y0, x1, y1 in masks_for(*a.size):
        draw.rectangle((x0, y0, x1 - 1, y1 - 1), fill=0)
    h = m.histogram()
    return sum(h[1:]), max((i for i, n in enumerate(h) if n), default=0)


def flat(im):
    lo, hi = im.convert('L').getextrema()
    return lo == hi


def frames(run):
    """{frame index: path} of a run's frame dumps."""
    d = os.path.join(run, 'frames')
    out = {}
    if os.path.isdir(d):
        for f in os.listdir(d):
            m = re.match(r'framedump_(\d+)\.png$', f)
            if m:
                out[int(m.group(1))] = os.path.join(d, f)
    return out


def still_stretches(paths):
    """[(first frame, length)] of every stretch of at least STILL frames that do not change
    (masks aside) and are not one flat colour. An unreadable frame ends a stretch."""
    out, start, n, prev = [], None, 0, None

    def close():
        if start is not None and n >= STILL and not flat(prev):
            out.append((start, n))

    for k in sorted(paths):
        im = load(paths[k])
        if im is None or prev is None or diff(prev, im)[0] != 0:
            close()
            start, n, prev = (k, 1, im) if im is not None else (None, 0, None)
        else:
            n += 1
    close()
    return out


def still_picture(paths):
    """(first frame, length) of the last still stretch, or None."""
    stills = still_stretches(paths)
    return stills[-1] if stills else None


def walk_moves(paths, has_page):
    """(movement frames, pairs compared, biggest movement (count, frame, before, after)).
    Movements are changes above MOVE_PX after the run's first still picture. With has_page the
    first one is the page opening, not a press, and is dropped."""
    stills = still_stretches(paths)
    after = stills[0][0] if stills else None
    prev = None
    moves, pairs, best = [], 0, None
    for k in sorted(paths):
        im = load(paths[k])
        if im is None:
            prev = None
            continue
        if prev is not None and after is not None and k > after:
            pairs += 1
            n, _ = diff(prev, im)
            if n > MOVE_PX:
                moves.append(k)
                if best is None or n > best[0]:
                    best = (n, k, prev, im)
        prev = im
    if has_page and moves:
        moves = moves[1:]
    return moves, pairs, best


def walk_pair(fa, fb):
    """Frame-by-frame A against B. Returns (compared, [(frame, count, step)], best)."""
    diffs, compared, best = [], 0, None
    for k in sorted(set(fa) & set(fb)):
        a, b = load(fa[k]), load(fb[k])
        if a is None or b is None:
            continue
        compared += 1
        n, step = diff(a, b)
        if n != 0:
            diffs.append((k, n, step))
        if best is None or n > best[0]:
            best = (n, k, a, b)
    return compared, diffs, best


def before_after(a, b, path):
    out = Image.new('RGB', (a.width + 8 + b.width, max(a.height, b.height)), (255, 0, 255))
    out.paste(a, (0, 0))
    out.paste(b, (a.width + 8, 0))
    out.save(path)


def page(fa, fb, out):
    sa = still_picture(fa)
    if sa is None:
        return fail(f"A has no still picture ({STILL} frames unchanged): the menu did not settle")
    ka = sa[0]
    if not fb:
        print(f"A's page is frame {ka} (still for {sa[1]} frames); no B to compare")
        return 0
    sb = still_picture(fb)
    if sb is None:
        return fail(f"B has no still picture ({STILL} frames unchanged): the menu did not settle")
    kb = sb[0]
    a, b = load(fa[ka]), load(fb[kb])
    if a is None or b is None:
        return fail("a still picture is unreadable")
    n, step = diff(a, b)
    print(f"  still picture      A frame {ka} (still {sa[1]}), B frame {kb} (still {sb[1]})")
    print("  frames compared    1")
    print(f"  frames differing   {1 if n else 0}")
    print(f"  max differing px   {max(n, 0)} (largest step {max(step, 0)})")
    print(f"  first differing    {'frame ' + str(ka) if n else 'none'}")
    before_after(a, b, os.path.join(out, 'before_after.png'))
    print(f"  before|after       {os.path.join(out, 'before_after.png')}")
    return 1 if n else 0


def walk(fa, fb, a_dir, b_dir, out, input_path):
    with open(input_path, 'rb') as f:
        text = f.read()
    has_keys = re.search(rb'^menukeys\s', text, re.M) is not None
    has_page = re.search(rb'^menupage\s+[1-9]', text, re.M) is not None
    moves_a, pairs_a, best_a = walk_moves(fa, has_page)
    print(f"  A moved            {len(moves_a)} times, frames {moves_a or 'none'} "
          f"({os.path.basename(a_dir)}, {pairs_a} frame pairs after the first still)")
    moves_b = None
    if fb:
        moves_b, pairs_b, _ = walk_moves(fb, has_page)
        print(f"  B moved            {len(moves_b)} times, frames {moves_b or 'none'} "
              f"({os.path.basename(b_dir)}, {pairs_b} frame pairs)")
    if has_keys and (not moves_a or (moves_b is not None and not moves_b)):
        which = 'A' if not moves_a else 'B'
        print(f"WARNING: the walk did not move the menu ({which}: no change above {MOVE_PX} px "
              "after the menu settled). The menukeys presses were ignored: check that the "
              "emulated Wiimotes are off (Source=0).")
        return 2
    if not fb:
        if best_a is not None:
            before_after(best_a[2], best_a[3], os.path.join(out, 'before_after.png'))
            print(f"  before|after       {os.path.join(out, 'before_after.png')} "
                  f"(A's biggest movement, frame {best_a[1]})")
        return 0
    compared, diffs, best = walk_pair(fa, fb)
    if compared == 0:
        return fail("no frame index is in both runs")
    print(f"  frames compared    {compared} (aligned by frame index; "
          f"A only {len(set(fa) - set(fb))}, B only {len(set(fb) - set(fa))})")
    print(f"  frames differing   {len(diffs)}")
    if diffs:
        worst = max(diffs, key=lambda d: d[1])
        print(f"  max differing px   {worst[1]} (largest step {max(d[2] for d in diffs)}, "
              f"frame {worst[0]})")
        print(f"  first differing    frame {diffs[0][0]}")
    else:
        print("  max differing px   0")
        print("  first differing    none")
    before_after(best[2], best[3], os.path.join(out, 'before_after.png'))
    print(f"  before|after       {os.path.join(out, 'before_after.png')} (frame {best[1]})")
    return 1 if diffs else 0


def ini_off(path):
    """Source = 0 on Wiimotes 1-4. Line endings and every other line are kept as they are."""
    if not os.path.isfile(path):
        return fail(f"{path}: no such file")
    with open(path, 'rb') as f:
        raw = f.read()
    nl = b'\r\n' if b'\r\n' in raw else b'\n'
    lines = raw.split(nl)
    head = re.compile(rb'\s*\[([^\]]+)\]\s*$')
    src = re.compile(rb'\s*Source\s*=')
    have, sec = set(), None
    for ln in lines:
        m = head.match(ln)
        if m:
            sec = m.group(1).decode()
        elif sec in WIIMOTES and src.match(ln):
            have.add(sec)
    out, seen, sec = [], set(), None
    for ln in lines:
        m = head.match(ln)
        if m:
            sec = m.group(1).decode()
            seen.add(sec)
            out.append(ln)
            if sec in WIIMOTES and sec not in have:
                out.append(b'Source = 0')
        elif sec in WIIMOTES and src.match(ln):
            out.append(b'Source = 0')
        else:
            out.append(ln)
    for w in WIIMOTES:
        if w not in seen:
            out += [('[%s]' % w).encode(), b'Source = 0']
    new = nl.join(out)
    if new != raw:
        with open(path, 'wb') as f:
            f.write(new)
    print(f"{path}: Source = 0 on {', '.join(WIIMOTES)}")
    return 0


def main():
    if len(sys.argv) < 2:
        return fail("usage: " + USAGE)
    pos, opts = parse_opts(sys.argv, valued=('--out', '--input'), bools=('--page',))
    if not pos:
        return fail("usage: " + USAGE)
    if pos[0] == 'ini-off':
        if len(pos) != 2:
            return fail("usage: " + USAGE)
        return ini_off(pos[1])
    if pos[0] != 'compare':
        return fail("usage: " + USAGE)
    runs = pos[1:]
    if not runs or len(runs) > 2 or '--out' not in opts or \
            bool(opts.get('--page')) == ('--input' in opts):
        return fail("usage: " + USAGE)
    a_dir, b_dir = runs[0], (runs[1] if len(runs) == 2 else None)
    fa, fb = frames(a_dir), (frames(b_dir) if b_dir else {})
    if not fa:
        return fail(f"no frame dumps in {a_dir}")
    if b_dir and not fb:
        return fail(f"no frame dumps in {b_dir}")
    out = opts['--out']
    if os.path.exists(out) and not os.path.isdir(out):
        return fail(f"{out}: is a file, not a folder")
    os.makedirs(out, exist_ok=True)
    if opts.get('--page'):
        return page(fa, fb, out)
    if not os.path.isfile(opts['--input']):
        return fail(f"{opts['--input']}: no such walk file")
    return walk(fa, fb, a_dir, b_dir, out, opts['--input'])


if __name__ == '__main__':
    sys.exit(main())
