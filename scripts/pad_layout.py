#!/usr/bin/env python3
"""pad_layout.py GCPADNEW.INI

Draw ports 1 and 2 of a Dolphin GCPadNew.ini as PlayStation pads. Each button shows the
Dolphin key or pad input that operates it, through WiiStation's default GameCube mapping
(Gamecube/gc_input/controller-GC.c):
  A Cross, B Square, X Circle, Y Triangle, Start Start, Z+Start Select,
  L L1, R R1, Z+L L2, Z+R R2, D-pad D-pad.
The PlayStation pad is digital, so the GameCube sticks have no function.

scripts/movie_capture.sh uses it after "dolphin_movie.py pads" (the dolphin-movie repo)
has prepared the profile's copy. The keyboard layout for an empty port 2 is in
dolphin-movie now ("pads --fill-pad2").
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_file  # noqa: E402

EXACT = {'NUMPADENTER': 'NumEnter', 'RETURN': 'Enter', 'PRIOR': 'PgUp', 'NEXT': 'PgDn',
         'DELETE': 'Del', 'INSERT': 'Ins', 'HOME': 'Home', 'END': 'End'}


def sections(text):
    out, cur = {}, None
    for ln in text.splitlines():
        ln = ln.strip()
        if ln.startswith('['):
            cur = out.setdefault(ln.strip('[]'), {})
        elif cur is not None and ' = ' in ln:
            k, v = ln.split(' = ', 1)
            cur[k] = v.strip('`')
    return out


def name(v):
    if not v:
        return '-'
    return EXACT.get(v, v.replace('NUMPAD', 'Num'))


W, M = 24, 32   # a pad half, and the middle between them


def half(cells, labels):
    """One side of the pad, W wide: a diamond of four cells (top, left, right, bottom)."""
    c = lambda v: '[' + v[:7].center(7) + ']'
    i = W - 2
    return [' ' + '_' * i + ' ',
            '/' + labels[0].center(i) + '\\',
            '|' + c(cells[0]).center(i) + '|',
            '|' + (' ' + labels[1]).ljust(i // 2) + (labels[2] + ' ').rjust(i - i // 2) + '|',
            '|' + (c(cells[1]) + '  ' + c(cells[2])).center(i) + '|',
            '|' + c(cells[3]).center(i) + '|',
            '\\' + labels[3].center(i) + '/',
            " '" + '-' * (i - 2) + "' "]


def draw(title, s):
    k = lambda e: name(s.get(e, ''))
    z = k('Buttons/Z')
    combo = lambda v: '-' if '-' in (z, v) else z + '+' + v
    left = half([k('D-Pad/Up'), k('D-Pad/Left'), k('D-Pad/Right'), k('D-Pad/Down')],
                ['Up', 'Left', 'Right', 'Down'])
    right = half([k('Buttons/Y'), k('Buttons/B'), k('Buttons/X'), k('Buttons/A')],
                 ['Triangle', 'Square', 'Circle', 'Cross'])
    c = lambda v: '[' + v[:13].center(13) + ']'
    mid = [''] * len(left)
    mid[2] = ('Select'.center(15) + ' ' + 'Start'.center(15)).center(M)
    mid[3] = (c(combo(k('Buttons/Start'))) + ' ' + c(k('Buttons/Start'))).center(M)
    shoulders = ('  L2 %s   L1 %s' % (combo(k('Triggers/L')), k('Triggers/L'))).ljust(W + M) + \
                '  R1 %s   R2 %s' % (k('Triggers/R'), combo(k('Triggers/R')))
    out = ['%s  (Dolphin: %s)' % (title, s.get('Device', 'no device')), shoulders]
    out += [l + m.ljust(M) + r for l, m, r in zip(left, mid, right)]
    return '\n'.join(x.rstrip() for x in out)


def main():
    need(sys.argv, 2, "pad_layout.py GCPADNEW.INI")
    text = open(req_file(sys.argv[1], 'ini'), encoding='utf-8', errors='replace').read()
    sec = sections(text)
    print(draw('PORT 1', sec.get('GCPad1', {})))
    print()
    print(draw('PORT 2', sec.get('GCPad2', {})))


if __name__ == '__main__':
    main()
