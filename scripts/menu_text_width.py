#!/usr/bin/env python3
"""Check the menu's text and the Settings tabs' grid against the 640-pixel screen.

The menu font is a bitmap font: fonts/En.dat holds one 1152-byte tile per glyph,
preceded by the glyph's own advance width.  IplFont::drawString advances by
(width + 1) * scale per character and IplFont::getStringWidth adds 5, so the same
arithmetic here gives the exact pixel width the Wii will draw.  Nothing clips a
TextBox or a Button label, so text that is too long simply runs off.

Two checks:
  - the Options pages' help lines fit their columns (OptionsFrame.cpp);
  - every Settings tab keeps the same grid, and no row of buttons overlaps itself or
    runs off the right (SettingsFrame.cpp).  The grid is one x that all the left-hand
    labels are centred on and one x that the first button of every labelled row starts
    at, which is what makes the five tabs look alike.

    python scripts/menu_text_width.py            # check, exit 1 on any problem
    python scripts/menu_text_width.py -v         # also print every line and row
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Gamecube', 'menu', 'OptionsFrame.cpp')
TABS = os.path.join(ROOT, 'Gamecube', 'menu', 'SettingsFrame.cpp')
FONT = os.path.join(ROOT, 'fonts', 'En.dat')
RECORD = 1152 + 4          # [u16 code][u8 ?][u8 width][1152-byte tile]
SCREEN_RIGHT = 630.0       # leave a few pixels of margin

TAB_LABEL_CX = 150.0       # the Saves tab's grid, which the others were put on
TAB_BUTTON_X = 295.0
# Rows of buttons with no left-hand label: they are centred across the tab instead, so
# they are exempt from the column. Keyed by FRAME_BUTTONS index of a button in the row.
UNLABELLED_ROWS = {30, 31, 73, 74, 75}
# Entries that stay in the tables so that no later index moves, but are never shown:
# the CPU core and GPU plugin (now on the Plugins page) and the three spare audio slots.
HIDDEN_BUTTONS = {5, 6, 58, 64, 65, 66, 42, 43, 44}
HIDDEN_LABELS = {0, 15, 16, 24}


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


def tab_rows():
    """Every FRAME_BUTTONS entry, in table order, as (index, x, y, width), plus every
    FRAME_TEXTBOXES entry as (index, x, y). Fields are found by their decimal point:
    x/y/width/height are the only ones written that way."""
    src = open(TABS, encoding='utf-8').read()
    def table(opener, closer, n):
        body = src[src.index(opener) + len(opener):src.index(closer)]
        out = []
        for line in body.splitlines():
            if not line.strip().startswith('{\tNULL'):
                continue
            f = [t for t in line.split('\t') if re.match(r'^\s*[0-9]+\.[0-9]+,$', t)]
            out.append([float(t.strip().rstrip(',')) for t in f[:n]])
        return out
    return (table('} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =', 'struct TextBoxInfo', 4),
            table('} FRAME_TEXTBOXES[NUM_FRAME_TEXTBOXES] =', 'SettingsFrame::SettingsFrame()', 2))


def check_tabs(verbose):
    """The five tabs share one y grid, so buttons from different tabs sit on the same
    line and cannot be told apart from the table alone. That is enough for the two
    things worth checking: nothing runs off the right, and every line that carries a
    labelled row has a button starting the column."""
    buttons, labels = tab_rows()
    bad = []
    rows = {}
    for i, (x, y, wd, ht) in enumerate(buttons):
        if y < 60 or i in HIDDEN_BUTTONS:
            continue                      # the tab strip, and entries never shown
        rows.setdefault(y, []).append((i, x, wd))
    for y in sorted(rows):
        r = sorted(rows[y], key=lambda t: t[1])
        right = max(x + wd for _, x, wd in r)
        note = ''
        for i, x, wd in r:
            if x + wd > 640.0:
                bad.append('button %d at y=%g ends at %g, past the right edge' % (i, y, x + wd))
                note = '   <== off screen'
        if not any(x == TAB_BUTTON_X for _, x, _ in r) \
           and not any(i in UNLABELLED_ROWS for i, _, _ in r):
            bad.append('no button at y=%g starts the column at %g' % (y, TAB_BUTTON_X))
            note = '   <== off the grid'
        if verbose or note:
            print('  y=%-4g right %3g  %s%s' % (y, right, ' '.join('%d@%g+%g' % t for t in r), note))
    for i, (x, y) in enumerate(labels):
        if i in HIDDEN_LABELS:
            continue
        if x != TAB_LABEL_CX:
            bad.append('label %d is centred on %g, not %g' % (i, x, TAB_LABEL_CX))
            print('  label %d centred on %g, not %g   <== off the grid' % (i, x, TAB_LABEL_CX))
    print('%d Settings tab problem(s)' % len(bad))
    return len(bad)


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
    bad += check_tabs(verbose)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
