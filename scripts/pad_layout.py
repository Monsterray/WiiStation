#!/usr/bin/env python3
"""pad_layout.py GCPadNew.ini [--fill-pad2 OUT.ini]

Draw ports 1 and 2 as PlayStation pads, each button showing the Dolphin key (or pad
input) bound to it, through WiiStation's default GameCube mapping
(Gamecube/gc_input/controller-GC.c): A Cross, B Square, X Circle, Y Triangle, Start Start,
Z+Start Select, L L1, R R1, Z+L L2, Z+R R2, D-pad D-pad. The pad is digital, so the
GameCube sticks do nothing.

--fill-pad2 OUT.ini writes the file to OUT.ini with a keyboard layout for port 2 when its
[GCPad2] section binds no button (Dolphin's default), so two players can share the
keyboard: D-pad on the numpad (8 4 5 6), the face buttons on the Home/End/Delete/PgDn
block, Start on numpad Enter. scripts/movie_capture.sh uses it on the test profile's copy;
the user's own file is never written.
"""
import sys

PAD2 = {  # the fill-in for an empty [GCPad2]: numpad + the six-key block above the arrows
    'Buttons/A': 'END', 'Buttons/B': 'DELETE', 'Buttons/X': 'NEXT', 'Buttons/Y': 'HOME',
    'Buttons/Z': 'NUMPAD0', 'Buttons/Start': 'NUMPADENTER',
    'Triggers/L': 'INSERT', 'Triggers/R': 'PRIOR',
    'D-Pad/Up': 'NUMPAD8', 'D-Pad/Down': 'NUMPAD5', 'D-Pad/Left': 'NUMPAD4', 'D-Pad/Right': 'NUMPAD6',
}
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
    a = sys.argv[1:]
    text = open(a[0], encoding='utf-8', errors='replace').read()
    sec = sections(text)
    if '--fill-pad2' in a:
        out = a[a.index('--fill-pad2') + 1]
        p2 = sec.get('GCPad2', {})
        if not any(k.startswith(('Buttons/', 'D-Pad/', 'Triggers/')) for k in p2):
            lines, skip = [], False
            for ln in text.splitlines():
                if ln.strip().startswith('['):
                    skip = ln.strip() == '[GCPad2]'
                    if skip:
                        lines += ['[GCPad2]', 'Device = DInput/0/Keyboard Mouse']
                        lines += ['%s = `%s`' % kv for kv in PAD2.items()]
                        continue
                if not skip:
                    lines.append(ln)
            if '[GCPad2]' not in text:
                lines += ['[GCPad2]', 'Device = DInput/0/Keyboard Mouse'] + ['%s = `%s`' % kv for kv in PAD2.items()]
            text = '\n'.join(lines) + '\n'
            sec = sections(text)
        open(out, 'w', encoding='utf-8', newline='\r\n').write(text)
    print(draw('PORT 1', sec.get('GCPad1', {})))
    print()
    print(draw('PORT 2', sec.get('GCPad2', {})))


if __name__ == '__main__':
    main()
