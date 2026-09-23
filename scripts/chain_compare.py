#!/usr/bin/env python3
"""chain_compare.py RUN_A [RUN_B] [--detail GROUP]

Compare chained runs game by game: the columns chain_table.py prints (or one --detail group),
A's value, B's value and B - A, so a change is read off one table instead of two.

  chain_compare.py A B     two chains of the same games (a build before and after a change).
                           Games are matched by name, in order, so a game listed twice
                           matches its own second entry.
  chain_compare.py A       one A/B chain -- the same game twice in a row with different
                           settings, like scripts/chains/lc_ab.txt: each pair is compared.

Under Dolphin two runs of the same build and chain agree to 0.1% of wall in every column, so a
larger move is the change. Dolphin charges instructions, not cache misses: a change whose gain
is fewer misses (the locked cache) shows nothing here and has to be run on a Wii.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chain_table import games, row, COLS, DETAIL, detail  # noqa: E402


def entries(run, group):
    out = []
    for n, rom, vbl, b in games(os.path.join(run, 'perf.log')):
        base, _, sets = rom.partition(' {')
        name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(base)[0])
        vals = detail(b, group) if group else row(b, vbl)
        if vals:
            out.append((name, sets.rstrip('}'), vals))
    return out


def show(name, a, b, cols):
    w = lambda c, v, sign='': ('%' + sign + '11d') % v if c.endswith('#') else ('%' + sign + '11.2f') % v
    print('%-28s' % name[:28] + ''.join('%11s' % c[:10] for c in cols))
    for tag, v in (('  A ' + a[1], a[2]), ('  B ' + b[1], b[2])):
        print('%-28s' % tag[:28] + ''.join(w(c, v[c]) for c in cols))
    print('%-28s' % '  B - A' + ''.join(w(c, b[2][c] - a[2][c], '+') for c in cols))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    group = sys.argv[sys.argv.index('--detail') + 1] if '--detail' in sys.argv else None
    if group:
        args = [a for a in args if a != group]
    cols = [c for c, _, _, _ in DETAIL[group]] if group else [c for c in COLS if c != 'speed']
    if len(args) == 2:
        a, b = entries(args[0], group), entries(args[1], group)
        used = set()
        for ea in a:
            for j, eb in enumerate(b):
                if j not in used and eb[0] == ea[0]:
                    used.add(j)
                    show(ea[0], ea, eb, cols)
                    break
    else:
        e = entries(args[0], group)
        for i in range(len(e) - 1):
            if e[i][0] == e[i + 1][0]:
                show(e[i][0], e[i], e[i + 1], cols)


if __name__ == '__main__':
    main()
