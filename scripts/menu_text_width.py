#!/usr/bin/env python3
"""Check that the Options pages' help lines fit on the 640-pixel screen.

The menu font is a bitmap font: fonts/En.dat holds one 1152-byte tile per glyph,
preceded by the glyph's own advance width.  IplFont::drawString advances by
(width + 1) * scale per character and IplFont::getStringWidth adds 5, so the same
arithmetic here gives the exact pixel width the Wii will draw.  Nothing clips a
TextBox, so a line that is too long simply runs off the right edge.

    python scripts/menu_text_width.py            # check, exit 1 on any overflow
    python scripts/menu_text_width.py -v         # also print every line's width
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Gamecube', 'menu', 'OptionsFrame.cpp')
FONT = os.path.join(ROOT, 'fonts', 'En.dat')
RECORD = 1152 + 4          # [u16 code][u8 ?][u8 width][1152-byte tile]
SCREEN_RIGHT = 630.0       # leave a few pixels of margin


def glyph_widths(path):
    data = open(path, 'rb').read()
    w = {}
    for i in range(len(data) // RECORD):
        rec = data[i * RECORD:(i + 1) * RECORD]
        w[chr(struct.unpack('>H', rec[0:2])[0])] = rec[3]
    return w


def defines(src):
    d = {}
    for name, value in re.findall(r'^#define\s+(\w+)\s+([0-9.]+)', src, re.M):
        d[name] = float(value)
    return d


def main():
    verbose = '-v' in sys.argv
    src = open(SRC, encoding='utf-8').read()
    w = glyph_widths(FONT)
    d = defines(src)
    scale = d['HELP_SCALE']
    space = w[' ']

    def px(s):
        return (sum(w.get(c, space) + 1 for c in s) + 5) * scale

    budgets = [('term', d['HELP_X'], d['HELP_TEXT_X']),
               ('text', d['HELP_TEXT_X'], SCREEN_RIGHT)]
    bad = 0
    for table in re.findall(r'static const OptHelp (\w+)\[\] =\s*\{(.*?)\n\};', src, re.S):
        name, body = table
        for term, text in re.findall(r'\{\s*(NULL|"(?:[^"\\]|\\.)*")\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', body):
            term = '' if term == 'NULL' else term[1:-1]
            for (kind, x0, x1), s in zip(budgets, (term, text)):
                if not s:
                    continue
                end = x0 + px(s.replace('\\"', '"'))
                if verbose or end > x1:
                    print('%-13s %-4s %6.1f / %6.1f  %s%s'
                          % (name, kind, end, x1, s, '   <== TOO WIDE' if end > x1 else ''))
                if end > x1:
                    bad += 1
    print('%d line(s) too wide' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
