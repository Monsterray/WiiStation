#!/usr/bin/env python3
"""menu_tour.py -- the picture half of the one-boot menu walk (scripts/menu_pages_walk.sh).

  menu_tour.py extract RUN CACHE_DIR "PAGES"   split a "menutour PAGES" run's frames into one
                                               still per page: CACHE_DIR/pN.png
  menu_tour.py diff CACHE_A CACHE_B "PAGES"    per page: frames differing 0 or 1 (menu_compare's masks)

The tour opens page k at menu frame 30 + STEP*k (MenuContext.cpp), so page k is the last still
stretch (menu_compare.still_stretches) that starts in its window (the dump count drifts, see MARGIN).
Exit: 0 identical, 1 a difference or a missing page, 3 usage.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_compare as mc  # noqa: E402

STEP = 60      # menu frames per page: MenuContext.cpp
FIRST = 31     # frame index of the first page's opening
MARGIN = 20    # menu frames run ~0.7 per page faster than the dumps count: page k opens up to 15 dump frames early


def extract(run, cache, pages):
    paths = mc.frames(run)
    stills = mc.still_stretches(paths)
    os.makedirs(cache, exist_ok=True)
    missing = 0
    for k, p in enumerate(pages):
        lo, end = FIRST + STEP * k - MARGIN, FIRST + STEP * (k + 1) - MARGIN if k + 1 < len(pages) else float('inf')
        mine = [s for s in stills if lo <= s[0] < end]
        if not mine:
            print(f"page {p}: no still (the DOL has no menutour, or the run was too short)")
            missing += 1
            continue
        shutil.copyfile(paths[mine[-1][0]], os.path.join(cache, f"p{p}.png"))
        print(f"page {p}: frame {mine[-1][0]}")
    return 1 if missing else 0


def diff(ca, cb, pages):
    bad = 0
    for p in pages:
        a, b = (mc.load(os.path.join(c, f"p{p}.png")) for c in (ca, cb))
        if a is None or b is None:
            print(f"page {p}: no picture in {'A' if a is None else 'B'}")
            bad += 1
            continue
        n, _ = mc.diff(a, b)
        print(f"page {p}: frames differing {1 if n else 0} ({max(n, 0)} px)")
        if n:
            mc.before_after(a, b, os.path.join(cb, f"p{p}_vs_A.png"))
            bad += 1
    return 1 if bad else 0


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in ('extract', 'diff'):
        print(__doc__)
        return 3
    pages = sys.argv[4].split()
    if sys.argv[1] == 'extract':
        return extract(sys.argv[2], sys.argv[3], pages)
    return diff(sys.argv[2], sys.argv[3], pages)


if __name__ == '__main__':
    sys.exit(main())
