#!/usr/bin/env python3
"""Check the menu's layout against the rules in Gamecube/menu/MenuLayout.h.

Nothing in this menu clips: a button or a string placed past an edge is simply drawn off
the screen, a label wider than its button is drawn straight over its neighbours, and the
spinning logo is drawn over whatever a frame left in its corner. So the geometry is worth
checking without booting anything, which is what this does.

The font is a bitmap font -- fonts/En.dat holds one 1152-byte tile per glyph, preceded by
that glyph's advance width -- and IplFont::drawString advances by (width + 1) * scale per
character while getStringWidth adds 5. Doing the same arithmetic here gives the exact
pixel width the Wii will draw.

What it checks:

  Settings tabs (SettingsFrame.cpp)
    - a row with a left-hand label starts at TAB_BUTTON_X, or, when it cannot fit there,
      as far right as it can;
    - a row of only buttons is centred on the screen;
    - the gaps between the buttons of one row are all equal;
    - every left-hand label is centred on TAB_LABEL_CX;
    - every label a button can show fits inside that button;
    - nothing comes within MENU_EDGE of an edge, or enters the logo's corner.

  Options pages (OptionsFrame.cpp)
    - each help line fits its column, allowing for the logo's corner on the lower lines;
    - each help entry starts with a capital.

    python scripts/menu_text_width.py            # check, exit 1 on any problem
    python scripts/menu_text_width.py -v         # print every row and line as well
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPTS = os.path.join(ROOT, 'Gamecube', 'menu', 'OptionsFrame.cpp')
TABS = os.path.join(ROOT, 'Gamecube', 'menu', 'SettingsFrame.cpp')
LAYOUT = os.path.join(ROOT, 'Gamecube', 'menu', 'MenuLayout.h')
FONT = os.path.join(ROOT, 'fonts', 'En.dat')
RECORD = 1152 + 4          # [u16 code][u8 ?][u8 width][1152-byte tile]

# The rows of each Settings tab, by FRAME_BUTTONS index. The tables cannot say this: all
# five tabs share one y grid, so buttons from different tabs sit on the same line and
# nothing in the table distinguishes them. `label` is the FRAME_TEXTBOXES index of the
# row's left-hand label, or None for a row of only buttons, which is centred instead.
TAB_ROWS = [
    ('General', [73, 74, 75],  None),
    ('General', [7, 8, 9, 10], 1),
    ('General', [11, 12, 13],  2),
    ('General', [54],          21),
    ('General', [55, 56],      22),
    ('General', [14, 15, 62],  3),
    ('Video',   [16, 17],      4),
    ('Video',   [18, 19, 63],  5),
    ('Video',   [20, 21],      6),
    ('Video',   [22, 23, 57],  7),
    ('Video',   [28, 29, 24],  9),
    ('Video',   [78],          None),
    ('Input',   [30, 31],      None),
    ('Input',   [32, 33],      10),
    ('Input',   [59],          29),
    ('Input',   [34, 35],      11),
    ('Input',   [36, 37],      12),
    ('Input',   [38],          13),
    ('Audio',   [39, 40, 41],  14),
    ('Audio',   [45],          17),
    ('Audio',   [67, 68],      25),
    ('Audio',   [69, 70, 71],  26),
    ('Audio',   [72],          None),
    ('Saves',   [46, 47, 48, 49], 18),
    ('Saves',   [76, 77],      23),
    ('Saves',   [50, 51],      19),
    ('Saves',   [52, 53],      20),
]
TAB_STRIP = [0, 1, 2, 3, 4]          # the five tab buttons, their own centred row
# In the tables so that no later index moves, but never shown on any tab.
# 25/26/27 are the Dithering trio: it moved to the Advanced Graphics page
# (OptionsFrame), which button 78 opens.
HIDDEN_BUTTONS = {5, 6, 58, 64, 65, 66, 42, 43, 44, 60, 61, 25, 26, 27}
HIDDEN_LABELS = {0, 15, 16, 24, 8}   # 8: the Dithering label, now on Advanced Graphics
# Not a row's left-hand label: small markers placed inside a row, which say which memory
# card each Memcard Type button belongs to. They sit where the row puts them, not on the
# label column.
MARKER_LABELS = {27, 28}
# Buttons whose label is not the string their table entry names: activateSubmenu or a
# click handler swaps in one of a set, and the button has to fit all of them.
CYCLING = {
    22: ['4:3', '16:9', 'Force 16:9'],                  # FRAME_STRINGS[27 + screenMode]
    28: ['Default', 'Near', 'Bilinear'],                # TEXTURE_FILTER_STRINGS
    38: ['Default', '1', '2', '3', '4'],                # Auto Load Slot
    45: ['Simple', 'Gaussian'],                         # FRAME_STRINGS[46 + spuInterpolation]
    54: ['En', 'Chs', 'Kr', 'Es', 'Pte', 'It', 'De',    # LANG_STRINGS
         'Cht', 'Jp', 'Fr', 'Br', 'Ca', 'Tu'],
    59: ['Off', 'GunCon', 'Justifier', 'Mouse'],        # FRAME_STRINGS[70 + lightGun]
    76: ['Off', 'Shared', 'Game'],                             # FRAME_STRINGS[92 + memCardFile[0]]
    77: ['Off', 'Shared', 'Game'],                             # FRAME_STRINGS[92 + memCardFile[1]]
}

STRING_TABLES = ('FRAME_STRINGS', 'LANG_STRINGS', 'GPU_PLUGIN_STRINGS', 'TEXTURE_FILTER_STRINGS')
BUTTON_LINE = '{\tNULL'


def glyph_widths(path):
    data = open(path, 'rb').read()
    w = {}
    for i in range(len(data) // RECORD):
        rec = data[i * RECORD:(i + 1) * RECORD]
        w[chr(struct.unpack('>H', rec[0:2])[0])] = rec[3]
    return w


def defines(*paths):
    d = {}
    for p in paths:
        text = open(p, encoding='utf-8').read()
        for name, value in re.findall(r'^#define\s+(\w+)\s+([0-9.]+)\s*(?:/\*|//|$)', text, re.M):
            d[name] = float(value)
    return d


def _button_body(src):
    return src[src.index('} FRAME_BUTTONS[NUM_FRAME_BUTTONS] ='):src.index('struct TextBoxInfo')]


def button_labels():
    """Every label every button can show, by index."""
    src = open(TABS, encoding='utf-8').read()
    strings = {}
    for name in STRING_TABLES:
        pattern = 'static char ' + name + r'\[[0-9]+\]\[[0-9]+\] =\s*\{(.*?)\n\s*\};'
        m = re.search(pattern, src, re.S)
        if m:
            strings[name] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    out, idx = {}, -1
    for line in _button_body(src).splitlines():
        if not line.strip().startswith(BUTTON_LINE):
            continue
        idx += 1
        if idx in CYCLING:
            out[idx] = CYCLING[idx]
            continue
        m = re.search(r'(\w+)\[([0-9]+)\]', line.split('//')[0])
        out[idx] = [strings[m.group(1)][int(m.group(2))]] if m and m.group(1) in strings else []
    return out


def tables():
    """FRAME_BUTTONS as {index: (x, y, w, h)} and FRAME_TEXTBOXES as {index: (x, y)}.
    Fields are found by their decimal point: x/y/width/height are the only ones written
    that way, and the focus indices beside them are plain integers."""
    src = open(TABS, encoding='utf-8').read()

    def table(body, n):
        out = {}
        for line in body.splitlines():
            if not line.strip().startswith(BUTTON_LINE):
                continue
            f = [t for t in line.split('\t') if re.match(r'^\s*[0-9]+\.[0-9]+,$', t)]
            out[len(out)] = tuple(float(t.strip().rstrip(',')) for t in f[:n])
        return out

    a = src.index('} FRAME_TEXTBOXES[NUM_FRAME_TEXTBOXES] =')
    return (table(_button_body(src), 4),
            table(src[a:src.index('SettingsFrame::SettingsFrame()', a)], 2))


def logo_box(d, cx, cy):
    """The box the logo sweeps about (cx, cy): left, top, right, bottom."""
    return (cx - d['MENU_LOGO_W'] / 2, cy - d['MENU_LOGO_H'] / 2,
            cx + d['MENU_LOGO_W'] / 2, cy + d['MENU_LOGO_H'] / 2)


def right_edge(d, bottom):
    """How far right something whose lowest pixel is at `bottom` may reach. Same rule as
    MENU_ROW_RIGHT() in MenuLayout.h."""
    l, t, _, _ = logo_box(d, d['LOGO_PAGE_X'], d['LOGO_PAGE_Y'])
    if bottom > t:
        return l - d['MENU_EDGE']
    return d['MENU_W'] - d['MENU_EDGE']


def check_tabs(d, w, verbose, bad):
    buttons, labels = tables()
    texts = button_labels()
    space = w[' ']
    px = lambda s: sum(w.get(c, space) + 1 for c in s) + 5

    def check_row(tab, idxs, label):
        centred = label is None
        geom = [buttons[i] for i in idxs]
        y, h = geom[0][1], geom[0][3]
        limit = right_edge(d, y + h)
        x0, note = geom[0][0], ''

        # The gaps within a row must all be the same; which value is the row's own
        # business, since the tab strip is deliberately airier than a row of settings.
        gaps = [round(b[0] - (a[0] + a[2]), 2) for a, b in zip(geom, geom[1:])]
        if gaps and len(set(gaps)) > 1:
            bad.append('%s row at y=%g has gaps %s between its buttons; they must be equal'
                       % (tab, y, gaps))
            note = '  <== uneven gaps'
        gap = gaps[0] if gaps else d['TAB_GAP']
        total = sum(g[2] for g in geom) + gap * (len(geom) - 1)

        for i, g in zip(idxs, geom):
            for t in texts.get(i, []):
                if px(t) > g[2]:
                    bad.append('%s row at y=%g: button %d is %g wide but %r needs %d'
                               % (tab, y, i, g[2], t, px(t)))
                    note = '  <== label too wide'

        if centred:
            want0 = round((d['MENU_W'] - total) / 2)
            if abs(x0 - want0) > 1:
                bad.append('%s row at y=%g has no label, so it should be centred at x=%g, not %g'
                           % (tab, y, want0, x0))
                note = '  <== not centred'
        else:
            want0 = d['TAB_BUTTON_X'] if d['TAB_BUTTON_X'] + total <= limit else limit - total
            if abs(x0 - want0) > 0.01:
                bad.append('%s row at y=%g starts at %g; it should start at %g'
                           % (tab, y, x0, want0))
                note = '  <== off the grid'
            lx = labels[label][0]
            if lx != d['TAB_LABEL_CX']:
                bad.append('%s row at y=%g: label %d is centred on %g, not %g'
                           % (tab, y, label, lx, d['TAB_LABEL_CX']))
                note = '  <== label off the grid'

        if x0 < d['MENU_EDGE']:
            bad.append('%s row at y=%g starts at %g, inside the %g margin'
                       % (tab, y, x0, d['MENU_EDGE']))
            note = '  <== past the left edge'
        if x0 + total > limit + 0.01:
            corner = " (the logo's corner)" if limit < d['MENU_W'] - d['MENU_EDGE'] else ''
            bad.append('%s row at y=%g ends at %g, past %g%s' % (tab, y, x0 + total, limit, corner))
            note = '  <== too far right'
        if verbose or note:
            print('  %-8s y=%-4g %5.0f..%-5.0f of %-4.0f  %s%s'
                  % (tab, y, x0, x0 + total, limit,
                     ' '.join('%d@%g+%g' % (i, g[0], g[2]) for i, g in zip(idxs, geom)), note))

    check_row('Tabs', TAB_STRIP, None)
    for tab, idxs, label in TAB_ROWS:
        check_row(tab, idxs, label)

    listed = set(TAB_STRIP) | {i for _, idxs, _ in TAB_ROWS for i in idxs}
    missing = sorted(set(buttons) - listed - HIDDEN_BUTTONS)
    if missing:
        bad.append('buttons %s are in the table but on no row above; add them to TAB_ROWS '
                   'or to HIDDEN_BUTTONS' % missing)
    on_a_row = {l for _, _, l in TAB_ROWS if l is not None}
    for i in sorted(set(labels) - HIDDEN_LABELS - MARKER_LABELS - on_a_row):
        bad.append('label %d is in the table but on no row above' % i)


def check_help(d, w, verbose, bad):
    src = open(OPTS, encoding='utf-8').read()
    scale = d['HELP_SCALE']
    line_h = 24.0 * scale
    space = w[' ']
    px = lambda s: (sum(w.get(c, space) + 1 for c in s) + 5) * scale

    # A page's help starts where its next row would have been, so the space above it is the
    # same as the space between two rows. XXX_HELP belongs to XXX_ROWS.
    rowcount = {}
    for name, body in re.findall(r'static const OptRow (\w+)_ROWS\[\] =\s*\{(.*?)\n\};', src, re.S):
        rowcount[name] = len(re.findall(r'\bROW_(?:CYCLE|RADIO|INFO)\s*\(', body))

    for page, body in re.findall(r'static const OptHelp (\w+)_HELP\[\] =\s*\{(.*?)\n\};', src, re.S):
        name = page + '_HELP'
        if page not in rowcount:
            bad.append('%s has no %s_ROWS to sit under' % (name, page))
            continue
        entries = re.findall(r'\{\s*(NULL|"(?:[^"\\]|\\.)*")\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', body)
        clean = [('' if t == 'NULL' else t[1:-1], x.replace('\\"', '"')) for t, x in entries]
        y0 = d['ROW_Y0'] + rowcount[page] * d['ROW_DY']

        # Both columns are centred on the screen together, then moved left far enough to
        # clear the logo. OptionsFrame::activateSubmenu does exactly this.
        w_term = max([px(t) for t, _ in clean if t] or [0])
        w_text = max([px(x) for _, x in clean] or [0])
        term_r = (d['MENU_W'] - (w_term + d['HELP_COL_GAP'] + w_text)) / 2 + w_term
        text_x = term_r + d['HELP_COL_GAP']
        shift, y = 0, y0
        for n, (term, text) in enumerate(clean):
            if n > 0 and term:
                y += d['HELP_CELL_GAP']
            shift = max(shift, int(text_x + px(text) - right_edge(d, y + line_h)))
            y += d['HELP_LINE_DY']
        if term_r - w_term - shift < d['MENU_EDGE']:
            shift = int(term_r - w_term - d['MENU_EDGE'])
        term_r -= shift
        text_x -= shift

        # The lines of one explanation sit HELP_LINE_DY apart; a line that starts a new
        # term gets HELP_CELL_GAP before it. OptionsFrame::activateSubmenu does the same.
        y = y0
        for n, (term, text) in enumerate(entries):
            term = '' if term == 'NULL' else term[1:-1]
            text = text.replace('\\"', '"')
            if n > 0 and term:
                y += d['HELP_CELL_GAP']
            limit = right_edge(d, y + line_h)
            note = ''
            if term:
                left = term_r - px(term)      # right-aligned: it ends at TERM_R
                if left < d['MENU_EDGE']:
                    bad.append('%s line %d: term starts at %.0f, inside the margin'
                               % (name, n, left))
                    note = '  <== term too wide'
            if (n == 0 or term) and text and text[0].isalpha() and not text[0].isupper():
                bad.append('%s line %d starts lowercase: %r' % (name, n, text[:32]))
                note = '  <== not capitalised'
            end = text_x + px(text) if text else text_x
            if end > limit:
                corner = " (the logo's corner)" if limit < d['MENU_W'] - d['MENU_EDGE'] else ''
                bad.append('%s line %d ends at %.0f, past %.0f%s' % (name, n, end, limit, corner))
                note = '  <== too wide'
            if verbose or note:
                print('  %-13s %2d y=%-4.0f %5.0f..%-5.0f of %-4.0f  %-17s %s%s'
                      % (name, n, y, text_x, end, limit, term, text, note))
            y += d['HELP_LINE_DY']
        bottom = y - d['HELP_LINE_DY'] + line_h
        if bottom > d['MENU_H'] - d['MENU_EDGE']:
            bad.append('%s has %d lines, whose last ends at %.0f, off the bottom'
                       % (name, len(entries), bottom))


FRAME_FILE = {
    'FRAME_MAIN': 'MainFrame.cpp',
    'FRAME_LOADROM': 'LoadRomFrame.cpp',
    'FRAME_FILEBROWSER': 'FileBrowserFrame.cpp',
    'FRAME_CURRENTROM': 'CurrentRomFrame.cpp',
    'FRAME_SETTINGS': 'SettingsFrame.cpp',
    'FRAME_CONFIGUREINPUT': 'ConfigureInputFrame.cpp',
    'FRAME_CONFIGUREBUTTONS': 'ConfigureButtonsFrame.cpp',
    'FRAME_OPTIONS': 'OptionsFrame.cpp',
}


def check_logo(d, verbose, bad):
    """MenuContext.cpp lists the frames that put something where the logo sits in the top
    right. Measure each frame's own button table and say whether that list is still true.

    MainFrame builds its buttons one by one and OptionsFrame generates its rows, so
    neither has a table to read; OptionsFrame's widest row is worked out from its own
    defines and MainFrame draws nothing on the right at all."""
    menu = os.path.dirname(OPTS)
    src = open(os.path.join(menu, 'MenuContext.cpp'), encoding='utf-8').read()
    m = re.search(r'LOGO_BOTTOM_FRAMES\[\] =\s*\{(.*?)\};', src, re.S)
    if not m:
        bad.append('MenuContext.cpp has no LOGO_BOTTOM_FRAMES list')
        return
    listed = set(re.findall(r'FRAME_\w+', m.group(1)))

    l, t, r, b = logo_box(d, d['LOGO_MAIN_X'], d['LOGO_MAIN_Y'])
    hits = set()
    for frame, fname in FRAME_FILE.items():
        path = os.path.join(menu, fname)
        if frame == 'FRAME_OPTIONS':
            # A radio row is the widest thing a page draws, on the first row.
            x1 = d['RADIO_X0'] + 2 * d['RADIO_DX'] + d['RADIO_W']
            boxes = [(d['RADIO_X0'], d['ROW_Y0'], x1, d['ROW_Y0'] + d['BUTTON_H'])]
        else:
            text = open(path, encoding='utf-8', errors='replace').read()
            mm = re.search(r'\} FRAME_BUTTONS\[NUM_FRAME_BUTTONS\] =(.*?)\n\};', text, re.S)
            boxes = []
            for line in (mm.group(1).splitlines() if mm else []):
                if not line.strip().startswith(BUTTON_LINE):
                    continue
                f = [t2.strip().rstrip(',') for t2 in line.split('\t')
                     if re.match(r'^\s*[0-9]+\.[0-9]+,$', t2)]
                if len(f) < 4:
                    continue
                x, y, bw, bh = (float(v) for v in f[:4])
                boxes.append((x, y, x + bw, y + bh))
        over = [bx for bx in boxes if bx[2] > l and bx[0] < r and bx[3] > t and bx[1] < b]
        if over:
            hits.add(frame)
        if verbose:
            print('  %-24s %2d buttons, %s' % (fname, len(boxes),
                  'clear of the logo' if not over else
                  'under the logo: ' + ', '.join('%g,%g..%g,%g' % bx for bx in over[:3])))
    for f in sorted(hits - listed):
        bad.append('%s draws under the logo but is not in LOGO_BOTTOM_FRAMES' % f)
    for f in sorted(listed - hits):
        bad.append('%s is in LOGO_BOTTOM_FRAMES but draws nothing under the logo' % f)


def main():
    verbose = '-v' in sys.argv
    d = defines(OPTS, LAYOUT)
    w = glyph_widths(FONT)
    bad = []
    print('Options pages')
    check_help(d, w, verbose, bad)
    print('Settings tabs')
    check_tabs(d, w, verbose, bad)
    print('The logo')
    check_logo(d, verbose, bad)
    for b in bad:
        print('FAIL ' + b)
    print('\n%d problem(s)' % len(bad) if bad else '\nlayout ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
