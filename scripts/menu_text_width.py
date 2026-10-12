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
    - the rows of each tab, as the menu engine places them (SET_PAGES, SetRow tables);
    - a row with a left-hand label starts at TAB_BUTTON_X, or, when it cannot fit there,
      as far right as it can;
    - a row with no label is centred on the screen;
    - every left-hand label is centred on TAB_LABEL_CX;
    - every label a button can show fits inside that button;
    - nothing comes within MENU_EDGE of an edge, or enters the logo's corner;
    - every button in the table is on a row, or in HIDDEN_BUTTONS.

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
ENGINE = os.path.join(ROOT, 'Gamecube', 'menu', 'MenuEngine.h')   # OptionsFrame's grid: menu::engine::grid
FONT = os.path.join(ROOT, 'fonts', 'En.dat')
RECORD = 1152 + 4          # [u16 code][u8 ?][u8 width][1152-byte tile]

# The five tab buttons are FRAME_BUTTONS 0-4. Every other Settings button is in a row of a
# SET_PAGES tab (SettingsFrame.cpp), which the engine lays out. settings_layout() works out the
# same places from the rows, and the checks run on those.
TAB_STRIP = [0, 1, 2, 3, 4]
# In the tables so that no later index moves, but never shown on any tab.
# 25/26/27 are the Dithering trio: it moved to the Advanced Graphics page
# (OptionsFrame), which button 78 opens. 74 is the CD page, now an Advanced tab.
HIDDEN_BUTTONS = {5, 6, 58, 64, 65, 66, 42, 43, 44, 60, 61, 74, 25, 26, 27}

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
        # The menu engine's grid constants (MenuEngine.h): "const float NAME = 20.0f;"
        for name, value in re.findall(r'^\s*const float\s+(\w+)\s*=\s*(-?[0-9.]+)f?\s*;', text, re.M):
            d[name] = float(value)
    # OptionsFrame's names for the engine's grid: the same numbers, under the old names.
    for old, new in (('RADIO_X0', 'CHOICE_X0'), ('RADIO_DX', 'CHOICE_DX'), ('RADIO_W', 'CHOICE_W')):
        if new in d:
            d[old] = d[new]
    return d


def _button_body(src):
    start = src.index('} FRAME_BUTTONS[NUM_FRAME_BUTTONS] =')
    return src[start:src.index('\n};', start)]


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


SET_PAGE_RE = re.compile(r'\{\s*SettingsFrame::SUBMENU_(\w+),\s*(\w+),\s*COUNT\(\w+\),\s*([0-9.]+)f?,')
SET_ROW_RE = re.compile(
    r'\{\s*eng::(ROW_\w+),\s*(FRAME_STRINGS\[\d+\]|NULL),\s*\{([^}]*)\},\s*\{([^}]*)\},\s*'
    r'(-?[0-9.]+)f?,\s*([0-9.]+)f?,\s*(\d+)\s*\}')


def settings_pages():
    """The Settings tabs of SettingsFrame.cpp, in SET_PAGES order: (tab, row pitch, rows).
    A row is a dict: buttons (FRAME_BUTTONS indexes), widths, label (True when it has one),
    x0 (0 centres the row), gap (0 means TAB_GAP) and plain."""
    src = open(TABS, encoding='utf-8').read()
    pages = []
    for tab, rows_name, dy in SET_PAGE_RE.findall(src):
        m = re.search(r'static const SetRow ' + rows_name + r'\[\] =\s*\{(.*?)\n\};', src, re.S)
        rows = []
        for kind, label, btns, ws, x0, gap, plain in SET_ROW_RE.findall(m.group(1)):
            buttons = [int(v) for v in re.findall(r'-?\d+', btns) if int(v) >= 0]
            widths = [float(v) for v in re.findall(r'\d+(?:\.\d+)?', ws)][:len(buttons)]
            rows.append({'kind': kind, 'label': label != 'NULL', 'buttons': buttons,
                         'widths': widths, 'x0': float(x0), 'gap': float(gap), 'plain': int(plain)})
        pages.append((tab, float(dy), rows))
    return pages


def place_row(widths, gap, x0, screen_w):
    """MenuEngine's rule for a row of buttons: x0 is the first left edge, or the row is centred
    when x0 is 0. Returns (first left edge, total width, [(left, width)])."""
    total = sum(widths) + gap * (len(widths) - 1)
    if not x0:
        x0 = (screen_w - total) / 2
    boxes, x = [], x0
    for w_ in widths:
        boxes.append((x, w_))
        x += w_ + gap
    return x0, total, boxes


def settings_layout(d):
    """The Settings tabs as the engine places them, from the rows. One dict per row, and one for
    the tab strip (tab 'Tabs'): y, h, buttons [(index, left, width)], total width, the table's
    x0, whether it has a label, the label centre, and the widths. The tab strip is centred with
    TAB_ROW_GAP. A row starts at its x0, or is centred; its buttons are GAP apart."""
    src = open(TABS, encoding='utf-8').read()
    tab_w = [float(v) for v in re.findall(r'[0-9.]+', re.search(
        r'const float settingsTabWidths\[NUM_TAB_BUTTONS\] =\s*\{([^}]*)\}', src).group(1))]
    label_cx = float(re.search(r'SET_PAGE\.labelCx\s*=\s*([0-9.]+)f', src).group(1))

    x0, total, boxes = place_row(tab_w, d['TAB_ROW_GAP'], 0, d['MENU_W'])
    out = [dict(tab='Tabs', y=d['TAB_Y'], h=d['BUTTON_H'], total=total, x0_given=0, label=False,
                labelcx=None, widths=tab_w, gap=d['TAB_ROW_GAP'],
                buttons=[(i, x, w_) for i, (x, w_) in enumerate(boxes)])]
    for tab, dy, rows in settings_pages():
        pitch = dy or d['ROW_DY']
        for n, row in enumerate(rows):
            gap = row['gap'] or d['TAB_GAP']
            x0, total, boxes = place_row(row['widths'], gap, row['x0'], d['MENU_W'])
            out.append(dict(tab=tab, y=d['ROW_Y0'] + n * pitch, h=d['BUTTON_H'], total=total,
                            x0_given=row['x0'], label=row['label'], labelcx=label_cx,
                            widths=row['widths'], gap=gap,
                            buttons=[(b, x, w_) for b, (x, w_) in zip(row['buttons'], boxes)]))
    return out


def count_buttons():
    """How many FRAME_BUTTONS entries the table has."""
    src = open(TABS, encoding='utf-8').read()
    return len([l for l in _button_body(src).splitlines() if l.strip().startswith(BUTTON_LINE)])


def check_row(d, texts, px, r, verbose, bad):
    tab, y, h, total = r['tab'], r['y'], r['h'], r['total']
    items = r['buttons']
    limit = right_edge(d, y + h)
    note = ''

    # A row with no label is centred. The table's x0 must agree, if it gives one.
    if not r['label']:
        want0 = round((d['MENU_W'] - total) / 2)
        if r['x0_given'] and abs(r['x0_given'] - want0) > 1:
            bad.append('%s row at y=%g has no label, so it should be centred at x=%g, not %g'
                       % (tab, y, want0, r['x0_given']))
            note = '  <== not centred'
    else:
        # A row with a label starts at TAB_BUTTON_X, or as far right as it can.
        want0 = d['TAB_BUTTON_X'] if d['TAB_BUTTON_X'] + total <= limit else limit - total
        if abs(r['x0_given'] - want0) > 0.01:
            bad.append('%s row at y=%g starts at %g; it should start at %g'
                       % (tab, y, r['x0_given'], want0))
            note = '  <== off the grid'
        if r['labelcx'] != d['TAB_LABEL_CX']:
            bad.append('%s row at y=%g: its labels are centred on %g, not %g'
                       % (tab, y, r['labelcx'], d['TAB_LABEL_CX']))
            note = '  <== label off the grid'

    x0 = items[0][1]
    if x0 < d['MENU_EDGE']:
        bad.append('%s row at y=%g starts at %g, inside the %g margin'
                   % (tab, y, x0, d['MENU_EDGE']))
        note = '  <== past the left edge'
    if x0 + total > limit + 0.01:
        corner = " (the logo's corner)" if limit < d['MENU_W'] - d['MENU_EDGE'] else ''
        bad.append('%s row at y=%g ends at %g, past %g%s' % (tab, y, x0 + total, limit, corner))
        note = '  <== too far right'
    for b, bx, bw in items:
        for t in texts.get(b, []):
            if px(t) > bw:
                bad.append('%s row at y=%g: button %d is %g wide but %r needs %d'
                           % (tab, y, b, bw, t, px(t)))
                note = '  <== label too wide'
    if verbose or note:
        print('  %-8s y=%-4g %5.0f..%-5.0f of %-4.0f  %s%s'
              % (tab, y, x0, x0 + total, limit,
                 ' '.join('%d@%g+%g' % (b, bx, bw) for b, bx, bw in items), note))


def check_tabs(d, w, verbose, bad):
    texts = button_labels()
    space = w[' ']
    px = lambda s: sum(w.get(c, space) + 1 for c in s) + 5
    rows = settings_layout(d)
    for r in rows:
        check_row(d, texts, px, r, verbose, bad)

    listed = {b for r in rows for b, _, _ in r['buttons']}
    missing = sorted(set(range(count_buttons())) - listed - HIDDEN_BUTTONS)
    if missing:
        bad.append('buttons %s are in the table but on no row; add them to a row '
                   'of SET_PAGES or to HIDDEN_BUTTONS' % missing)


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


CUR_BUTTON_RE = re.compile(r'\{\s*NULL,[^\n]*?(\d+),\s*(\d+),\s*([0-9.]+)f,\s*([0-9.]+)f\s*\}')


def current_rom_boxes(d):
    """The Current ROM page as the engine places it (CurrentRomFrame.cpp). Each row of action
    buttons starts at its x0, with its gap between buttons; each button is BUTTON_H high, at
    its row's top plus its own dy. Rows are rowY0 and the default pitch."""
    src = open(os.path.join(os.path.dirname(OPTS), 'CurrentRomFrame.cpp'), encoding='utf-8').read()
    table = re.search(r'currentRomButtons\[NUM_FRAME_BUTTONS\] =\s*\{(.*?)\n\};', src, re.S).group(1)
    x0 = [float(v) for v in re.search(r'CUR_ROW_X0\[CUR_ROWS_N\]\s*=\s*\{([^}]*)\}', src).group(1).replace('f', '').split(',')]
    gap = [float(v) for v in re.search(r'CUR_ROW_GAP\[CUR_ROWS_N\]\s*=\s*\{([^}]*)\}', src).group(1).replace('f', '').split(',')]
    top0 = float(re.search(r'CUR_PAGE\.rowY0\s*=\s*([0-9.]+)f', src).group(1))
    buttons = sorted((int(r), int(s), float(w), float(dy)) for r, s, w, dy in CUR_BUTTON_RE.findall(table))
    boxes = []
    for row in range(len(x0)):
        x = x0[row]
        top = top0 + row * d['ROW_DY']
        for r, s, w, dy in buttons:
            if r != row:
                continue
            boxes.append((x, top + dy, x + w, top + dy + d['BUTTON_H']))
            x += w + (gap[row] or d['TAB_GAP'])
    return boxes


def check_logo(d, verbose, bad):
    """MenuContext.cpp lists the frames that put something where the logo sits in the top
    right. Measure each frame's own button table and say whether that list is still true.

    MainFrame builds its buttons one by one and OptionsFrame generates its rows, so
    neither has a table to read; OptionsFrame's widest row is worked out from its own
    defines and MainFrame draws nothing on the right at all. SettingsFrame's boxes come
    from its rows, as the engine places them. Current ROM's come from its action rows,
    the same way."""
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
        elif frame == 'FRAME_SETTINGS':
            # The engine places the Settings tabs, so their boxes come from the rows.
            boxes = [(x, r['y'], x + bw, r['y'] + r['h'])
                     for r in settings_layout(d) for _, x, bw in r['buttons']]
        elif frame == 'FRAME_CURRENTROM':
            # The engine places this page too, from its action rows (current_rom_boxes).
            boxes = current_rom_boxes(d)
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
    d = defines(OPTS, LAYOUT, ENGINE)
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
