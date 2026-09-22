#!/usr/bin/env python3
"""chain_table.py RUN_DIR [--csv OUT.csv]

Split the perf.log of a chained autoboot (several games in one boot; see GamecubeMain.cpp)
into its games, and print where each game's time went, as shares of its wall time.

perf.log holds, per game, one or more "--- perf" blocks -- each a running total since the
game started -- and then the line "=== chain <n>/<total> end vblanks=<v> rom=<file> ===".
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
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
END = re.compile(r'^=== chain (\d+)/(\d+) end vblanks=(\d+) rom=(.*) ===')
PAIR = re.compile(r'(\w+)=(-?\d+)(?:/(\d+))?')


def parse_block(lines):
    """{'wall': {'wall_us': ...}, 'gpuprim': {'poly': (us, calls)}, ...}"""
    out = {}
    for ln in lines:
        m = re.match(r'^(\w+):\s', ln)
        if not m:
            continue
        d = out.setdefault(m.group(1), {})
        for k, a, b in PAIR.findall(ln[m.end():]):
            d[k] = (int(a), int(b)) if b else int(a)
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
            res.append((int(m.group(1)), m.group(4), int(m.group(3)),
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
    print('%-34s %6s %6s ' % ('game', 'vbl', 'wall') + ' '.join('%6s' % c for c in COLS))
    rows = []
    for n, rom, vbl, b in gs:
        name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(rom)[0])[:34]
        r = row(b, vbl)
        if r is None:
            print('%-34s %6d   (no perf block: did not start, or a release build)' % (name, vbl))
            continue
        rows.append((name, vbl, r))
        print('%-34s %6d %6.1f ' % (name, vbl, r['wall_s'])
              + ' '.join('%6.1f' % r[c] if c != 'speed' else '%6.2f' % r[c] for c in COLS))
    if len(rows) > 1:
        m = {c: sum(r[c] for _, _, r in rows) / len(rows) for c in COLS}
        print('%-34s %6s %6s ' % ('mean of %d' % len(rows), '', '')
              + ' '.join('%6.1f' % m[c] if c != 'speed' else '%6.2f' % m[c] for c in COLS))
    if '--csv' in sys.argv:
        with open(sys.argv[sys.argv.index('--csv') + 1], 'w') as f:
            f.write('game,vblanks,wall_s,' + ','.join(COLS) + '\n')
            for name, vbl, r in rows:
                f.write('"%s",%d,%.2f,' % (name, vbl, r['wall_s'])
                        + ','.join('%.2f' % r[c] for c in COLS) + '\n')


if __name__ == '__main__':
    main()
