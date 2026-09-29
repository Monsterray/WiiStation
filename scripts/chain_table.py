#!/usr/bin/env python3
"""chain_table.py RUN_DIR [--csv OUT.csv] [--detail GROUP]

Split the perf.log of a chained autoboot (several games in one boot; see GamecubeMain.cpp)
into its games, and print where each game's time went, as shares of its wall time.

perf.log holds, per game, one or more "--- perf" blocks -- each a running total since the
game started -- and then the line "=== chain <n>/<total> end vblanks=<v> [set=K=V,...] rom=<file> ===".
A game run with settings of its own is named with them, e.g. "Crash Bash LockedCache=2".
The last block before each end line is that game's whole run.

Also takes each game's VRAM snapshot (sd:/wiisxrx/vram_NN.bin) off the card image and draws
it as RUN_DIR/vram_NN.png, so each row can be checked against what the game was doing. Do
that straight after the run: the next run's card replaces them.

The columns, all percent of wall except where named:
  load    wall minus the frame limiter's idle: 100 means the emulator could not keep up
  speed   emulated seconds (vblanks / 59.94) per wall second
  psx     recompiled PlayStation code, GTE included (jit_us - hw_us)
  gte     the geometry coprocessor, inside psx
  gpu     everything behind the GP0 and DMA2 registers
  poly    polygon primitives, inside gpu
  hle     the HLE BIOS, exception handlers included
  spu     the sound mixer, its output resampler included
  mdec    video decode (psxDma1)
  cd      CD sector reads

--detail GROUP prints one of these instead: the sub-lines each probe writes, per game, so a
question about one subsystem needs no hand-written parsing. Percent of wall unless a name
ends in '#' (a count). The groups and their fields are the DETAIL table below; add a field
there when a probe gains one.
  gpu   the GPU split: command loop parts, primitive classes, the OpenGX draw, GP1, present
  tex   texture uploads: CLUT expansion, tiling, new/unaligned/invalidated counts
  cpu   slices, the HLE soft calls, nested entries, BIOS, GTE
  lc    the locked cache: which regions, DMA traffic, waiting
  pmc   Broadway's counters (needs a PERF_PROF_PMC=1 build)
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
END = re.compile(r'^=== chain (\d+)/(\d+) end vblanks=(\d+)(?: set=(\S+))?(?: at=\S+)? rom=(.*) ===')
PAIR = re.compile(r'(\w+)=(-?\d+(?:/\d+)*)')   # a number, or a/b/c... (a tuple)


def parse_block(lines):
    """{'wall': {'wall_us': ...}, 'gpuprim': {'poly': (us, calls)}, ...}"""
    out = {}
    for ln in lines:
        m = re.match(r'^(\w+):\s', ln)
        if not m:
            continue
        d = out.setdefault(m.group(1), {})
        for k, v in PAIR.findall(ln[m.end():]):
            v = tuple(int(x) for x in v.split('/'))
            d[k] = v if len(v) > 1 else v[0]
    return out


def games(path):
    """[(n, rom, vblanks, block)] -- block is the game's last perf block, or {} if none."""
    res, blocks, cur = [], [], None
    for ln in open(path, encoding='utf-8', errors='replace'):
        ln = ln.rstrip('\r\n')
        if ln.startswith('--- perf'):
            cur = []
            blocks.append(cur)
            continue
        m = END.match(ln)
        if m:
            rom = m.group(5) + (' {%s}' % m.group(4) if m.group(4) else '')
            res.append((int(m.group(1)), rom, int(m.group(3)),
                        parse_block(blocks[-1]) if blocks else {}))
            blocks, cur = [], None
            continue
        if cur is not None:
            cur.append(ln)
    return res


def row(b, vbl):
    g = lambda line, key: b.get(line, {}).get(key, 0)
    wall = g('wall', 'wall_us')
    if not wall:
        return None
    pct = lambda us: 100.0 * us / wall
    hw = g('inside', 'hw_us')
    poly = b.get('gpuprim', {}).get('poly', (0, 0))
    return {
        'wall_s': wall / 1e6,
        'load': pct(wall - g('inside', 'limit_us')),
        'speed': (vbl / 59.94) / (wall / 1e6),
        'psx': pct(g('slicecost', 'jit_us') - hw),
        'gte': pct(g('gte', 'us')),
        'gpu': pct(g('inside', 'hw_gpu_us')),
        'poly': pct(poly[0] if isinstance(poly, tuple) else 0),
        'hle': pct(g('bios', 'us')),
        'spu': pct(g('inside', 'spu_us')),
        'mdec': pct(g('texk', 'mdec_us')),
        'cd': pct(g('cd', 'total_us')),
    }


COLS = ['load', 'speed', 'psx', 'gte', 'gpu', 'poly', 'hle', 'spu', 'mdec', 'cd']

# (column, perf.log line, field, kind): kind '%' = microseconds shown as percent of wall,
# '#' = a count, '/' = the first number of a "time/count" pair as percent of wall.
DETAIL = {
    'gpu': [('gpu', 'inside', 'hw_gpu_us', '%'), ('parse', 'gpusplit', 'parse_us', '%'),
            ('vram', 'gpusplit', 'vram_us', '%'), ('offscr', 'gpusplit', 'off_us', '%'),
            ('prim', 'gpusplit', 'prim_us', '%'), ('poly', 'gpuprim', 'poly', '/'),
            ('rect', 'gpuprim', 'rect', '/'), ('fill', 'gpuprim', 'misc', '/'),
            ('ogx', 'gpudraw', 'ogx_us', '%'), ('state', 'gpudraw', 'state_us', '%'),
            ('vfmt', 'gpudraw', 'common_us', '%'), ('texsel', 'gpudraw', 'tex_us', '%'),
            ('gp1', 'gpuregs', 'gp1_us', '%'), ('upload', 'gpupres', 'upload_us', '%')],
    'tex': [('conv', 'texk', 'conv_us', '%'), ('tile', 'texk', 'tile_us', '%'),
            ('new#', 'ogx', 'sub_new', '#'), ('unalgn#', 'ogx', 'unaligned', '#'),
            ('hits#', 'ogx', 'sub_hit', '#'), ('inval#', 'ogxdraw', 'inval', '#'),
            ('ddone#', 'gpu', 'drawdone', '#')],
    'cpu': [('sched', 'slicecost', 'sched_us', '%'), ('jit', 'slicecost', 'jit_us', '%'),
            ('post', 'slicecost', 'post_us', '%'), ('nest#', 'nested', 'n', '#'),
            ('nsched', 'nested', 'sched_us', '%'), ('njit', 'nested', 'jit_us', '%'),
            ('runs#', 'softcall', 'runs', '#'), ('steps#', 'softcall', 'steps', '#'),
            ('escp#', 'softcall', 'escapes', '#'), ('runesc#', 'softcall', 'run_escapes', '#'),
            ('bios', 'bios', 'us', '%'), ('exc#', 'bios', 'exc', '#'), ('gte', 'gte', 'us', '%')],
    'lc':  [('mask#', 'lc', 'mask', '#'), ('stores#', 'lc', 'stores', '#'),
            ('storekb#', 'lc', 'store_kb', '#'), ('loads#', 'lc', 'loads', '#'),
            ('waitus#', 'lc', 'wait_us', '#')],
    'pmc': [('pmc1#', 'pmc', 'pmc1', '#'), ('pmc2#', 'pmc', 'pmc2', '#'),
            ('pmc3#', 'pmc', 'pmc3', '#'), ('pmc4#', 'pmc', 'pmc4', '#')],
    # screen re-uploads (UploadScreen) asked for, done, and each early return that skipped one
    # the CD image (cd:, cdriso.c), the SD card's own commands beneath it (sd:, the driver
    # wrapper in fileBrowser-libfat.c; times real only on a Wii), read-ahead and CHD.
    # h1..hbig: card read commands of 1, 2-8, 9-32, 33-128, >128 sectors; kind 'k' = KB
    'cd':  [('cdus', 'cd', 'total_us', '%'), ('reads#', 'cd', 'reads', '#'),
            ('rand#', 'cd', 'rand', '#'), ('sdrd#', 'sd', 'rd', '#'), ('sdkb#', 'sd', 'sec', 'k'),
            ('h1#', 'sd', 'h[0]', '#'), ('h8#', 'sd', 'h[1]', '#'), ('h32#', 'sd', 'h[2]', '#'),
            ('h128#', 'sd', 'h[3]', '#'), ('hbig#', 'sd', 'h[4]', '#'), ('bg#', 'sd', 'bg', '#'),
            ('sdus#', 'sd', 'us', '#'), ('worst#', 'sd', 'worst_us', '#'),
            ('pfhit#', 'cdpf', 'hit', '#'), ('pfmiss#', 'cdpf', 'miss', '#'),
            ('chdmiss#', 'chd', 'miss', '#')],
    'upl': [('calls#', 'efbloss', 'upl_calls', '#'), ('done#', 'efbloss', 'upl_done', '#'),
            ('dis#', 'uplret', 'dis', '#'), ('skip#', 'uplret', 'skip', '#'),
            ('rgb24#', 'uplret', 'rgb24', '#'), ('px1#', 'uplret', 'px1', '#'),
            ('pres#', 'gpuflip', 'presents', '#')],
}


def detail(b, group):
    """{column: value} for one game's block."""
    wall = b.get('wall', {}).get('wall_us', 0) or 1
    out = {}
    for col, line, key, kind in DETAIL[group]:
        key, _, i = key.partition('[')   # 'h[2]': element 2 of h=a/b/c/...
        v = b.get(line, {}).get(key, 0)
        if isinstance(v, tuple):
            v = v[int(i[:-1]) if i else 0]
        elif i and i != '0]':
            v = 0
        out[col] = 100.0 * v / wall if kind in '%/' else v // 2 if kind == 'k' else v
    return out


def fmt(col, v):
    return '%8d' % v if col.endswith('#') else '%8.2f' % v


def vram_pictures(run, n):
    card = os.path.join(os.environ.get('WSX_PROFILE', os.path.join(REPO, '.dolphin')),
                        'Load', 'WiiSD.raw')
    for i in range(1, n + 1):
        b = os.path.join(run, 'vram_%02d.bin' % i)
        p = os.path.join(run, 'vram_%02d.png' % i)
        if os.path.exists(p) or not os.path.exists(card):
            continue
        subprocess.run([sys.executable, os.path.join(HERE, 'sdimage_read.py'), card,
                        'wiisxrx/vram_%02d.bin' % i, b], capture_output=True)
        if os.path.exists(b) and os.path.getsize(b) == 1024 * 512 * 2:
            subprocess.run([sys.executable, os.path.join(HERE, 'vram2png.py'), b, p],
                           capture_output=True)


def main():
    run = sys.argv[1]
    log = os.path.join(run, 'perf.log')
    gs = games(log)
    if not gs:
        sys.exit('%s: no "=== chain" lines -- not a chained run, or it ended before the '
                 'first game did' % log)
    vram_pictures(run, len(gs))
    if '--detail' in sys.argv:
        group = sys.argv[sys.argv.index('--detail') + 1]
        cols = [c for c, _, _, _ in DETAIL[group]]
        print('%-46s ' % 'game' + ' '.join('%8s' % c for c in cols))
        for n, rom, vbl, b in gs:
            base, _, sets = rom.partition(' {')
            name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(base)[0])
            name = (name + (' ' + sets.rstrip('}') if sets else ''))[:46]
            d = detail(b, group)
            print('%-46s ' % name + ' '.join(fmt(c, d[c]) for c in cols))
        return
    print('%-46s %6s %6s ' % ('game', 'vbl', 'wall') + ' '.join('%6s' % c for c in COLS))
    rows = []
    for n, rom, vbl, b in gs:
        base, _, sets = rom.partition(' {')
        name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(base)[0])
        name = (name + (' ' + sets.rstrip('}') if sets else ''))[:46]
        r = row(b, vbl)
        if r is None:
            print('%-46s %6d   (no perf block: did not start, or a release build)' % (name, vbl))
            continue
        rows.append((name, vbl, r))
        print('%-46s %6d %6.1f ' % (name, vbl, r['wall_s'])
              + ' '.join('%6.1f' % r[c] if c != 'speed' else '%6.2f' % r[c] for c in COLS))
    if len(rows) > 1:
        m = {c: sum(r[c] for _, _, r in rows) / len(rows) for c in COLS}
        print('%-46s %6s %6s ' % ('mean of %d' % len(rows), '', '')
              + ' '.join('%6.1f' % m[c] if c != 'speed' else '%6.2f' % m[c] for c in COLS))
    if '--csv' in sys.argv:
        with open(sys.argv[sys.argv.index('--csv') + 1], 'w') as f:
            f.write('game,vblanks,wall_s,' + ','.join(COLS) + '\n')
            for name, vbl, r in rows:
                f.write('"%s",%d,%.2f,' % (name, vbl, r['wall_s'])
                        + ','.join('%.2f' % r[c] for c in COLS) + '\n')


if __name__ == '__main__':
    main()
