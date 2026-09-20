#!/usr/bin/env python3
"""run_summary.py RUN_DIR [RUN_DIR ...] [--zero-min MS] [--zero-max MS] [--detail]

One compact block per run from what dolphin_run.sh leaves behind, and a comparison table
when given several runs. Reads, when present:
  run.info    what wsx.sh staged (dol, settings, input, env)
  perf.log    the debug build's counters: the last xa:/xamix:/rate:/cpu: lines
  atrace.log  the audio timeline: core speed, XA gaps, rate-control nudge and queue
  *dspdump*.wav  Dolphin's audio dump: runs of exact digital silence (an output ring that
              ran dry, or a missing XA sector) between --zero-min and --zero-max ms
              (default 3..400; longer runs are the game's own silences and are counted apart)

Made for reading by eye or by a model with a small context: no paths, no per-event
listings unless --detail, numbers rounded. Standard library only.
"""
import sys, os, re, glob, wave, array, statistics

PSX_MS = 33868.8


def kv(line):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'(\w+)=(-?\d+)', line)}


def read_perf(run):
    p = os.path.join(run, 'perf.log')
    out = {}
    if not os.path.isfile(p):
        return out
    for line in open(p, errors='replace'):
        head = line.split(':', 1)[0]
        if head in ('xa', 'xamix', 'rate', 'cpu', 'limit'):
            out.setdefault(head, {}).update(kv(line))   # later blocks overwrite: counters are cumulative
    return out


def read_trace(run):
    p = os.path.join(run, 'atrace.log')
    if not os.path.isfile(p):
        return None
    rx = re.compile(r'ae: (\w) w=(\d+) c=(\d+) a=(-?\d+) b=(-?\d+) f=(\d+)')
    ev = [tuple(int(x) if i else x for i, x in enumerate(m.groups()))
          for m in (rx.match(l) for l in open(p, errors='replace')) if m]
    if not ev:
        return None
    r = {'events': len(ev), 'span_s': (ev[-1][1] - ev[0][1]) / 1e6}
    sect = [e for e in ev if e[0] in 'FR']
    speeds = []
    for a, b in zip(sect, sect[1:]):
        if b[0] == 'R':
            continue
        dw = (b[1] - a[1]) / 1000.0
        if dw > 0:
            speeds.append(((b[2] - a[2]) & 0xffffffff) / PSX_MS / dw)
    if speeds:
        r['speed'] = (statistics.median(speeds), min(speeds), max(speeds))
    r['sectors'] = len(sect)
    r['holds'] = sum(1 for e in ev if e[0] == 'H')
    r['xa_gaps'] = sum(1 for e in ev if e[0] == 'G')
    for drv, name in ((0, 'sdl'), (1, 'cube')):
        rc = [e for e in ev if e[0] == 'D' and (e[5] & 1) == drv]
        if not rc:
            continue
        ppm = [e[4] for e in rc]
        q = [e[3] for e in rc]
        t0 = rc[0][1]
        buckets = {}
        for e in rc:
            buckets.setdefault(int((e[1] - t0) / 10e6), []).append(e)
        r['rc_' + name] = {
            'n': len(rc), 'ppm_med': statistics.median(ppm), 'ppm_min': min(ppm), 'ppm_max': max(ppm),
            'q_med': statistics.median(q), 'q_min': min(q), 'q_max': max(q),
            'sat': sum(1 for e in rc if e[5] & 2),
            'i_med': statistics.median((e[5] >> 2) - 4096 for e in rc),
            'per10': ' '.join(f"{10*k}:{statistics.median(x[4] for x in v):.0f}/{statistics.median(x[3] for x in v):.0f}"
                              for k, v in sorted(buckets.items())),
        }
    return r


def read_wav(run, zmin, zmax):
    files = [f for f in glob.glob(os.path.join(run, '*dspdump*.wav'))]
    if not files:
        return None
    path = max(files, key=os.path.getsize)   # Dolphin opens a tiny first file before the real one
    with wave.open(path, 'rb') as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())    # the header count is a placeholder after a kill: trust the bytes
    if width != 2:
        return {'error': f'{width*8}-bit samples'}
    s = array.array('h')
    s.frombytes(raw[:len(raw) - len(raw) % (2 * ch)])
    n = len(s) // ch
    # a frame is silent when every channel is exactly 0
    runs = []
    start = None
    if ch == 2:
        it = range(0, 2 * n, 2)
        for i in it:
            z = s[i] == 0 and s[i + 1] == 0
            if z and start is None:
                start = i >> 1
            elif not z and start is not None:
                runs.append((start, (i >> 1) - start)); start = None
    else:
        for i in range(n):
            z = all(s[i * ch + c] == 0 for c in range(ch))
            if z and start is None:
                start = i
            elif not z and start is not None:
                runs.append((start, i - start)); start = None
    if start is not None:
        runs.append((start, n - start))
    ms = lambda fr: fr * 1000.0 / rate
    sel = [(st, ln) for st, ln in runs if zmin <= ms(ln) <= zmax]
    long_ = [(st, ln) for st, ln in runs if ms(ln) > zmax]
    edges = [3, 5, 10, 20, 30, 45, 60, 80, 120, 200, 400, 10**9]
    hist = {}
    for st, ln in sel:
        for lo, hi in zip(edges, edges[1:]):
            if lo <= ms(ln) < hi:
                hist[(lo, hi)] = hist.get((lo, hi), 0) + 1
    tl = {}
    for st, ln in sel:
        k = int(st / rate / 10)
        tl[k] = tl.get(k, 0) + 1
    return {
        'dur_s': n / rate, 'rate': rate, 'zero_n': len(sel), 'zero_ms': sum(ms(ln) for _, ln in sel),
        'hist': ' '.join(f"{lo}-{hi if hi < 10**9 else ''}:{c}" for (lo, hi), c in sorted(hist.items())),
        'timeline': ' '.join(f"{10*k}s:{c}" for k, c in sorted(tl.items())),
        'long_n': len(long_), 'long_s': sum(ln for _, ln in long_) / rate,
        'first_sound_s': (long_[0][1] / rate) if long_ and long_[0][0] == 0 else 0.0,
        'runs': [(st / rate, ms(ln)) for st, ln in sel],
    }


def fmt_perf(pf):
    parts = []
    xa, xm, rt = pf.get('xa'), pf.get('xamix'), pf.get('rate')
    if xa:
        parts.append(f"xa sectors={xa.get('sectors')} fill={xa.get('fill_min')}..{xa.get('fill_max')} trunc={xa.get('trunc')}")
    if xm:
        parts.append(f"xamix gaps={xm.get('gaps')} hold={xm.get('hold')} gap_samples={xm.get('gap_samples')} | spu pulls={xm.get('pulls')} busy={xm.get('busy')} | out dry={xm.get('dry')} drop={xm.get('drop')}")
    if rt:
        parts.append(f"rate ppm={rt.get('ppm')} i={rt.get('i')} [{rt.get('min')}..{rt.get('max')}] occ avg={rt.get('occ_avg')} min={rt.get('occ_min')} max={rt.get('occ_max')} updates={rt.get('updates')} sat={rt.get('sat')} changes={rt.get('changes')} prefill={rt.get('prefill')} | limit debt_max={rt.get('debt_max')} drops={rt.get('debt_drops')}")
    return parts


def fmt_trace(tr):
    if not tr:
        return []
    parts = []
    sp = tr.get('speed')
    s = f"speed emu/wall {sp[0]:.3f} (min {sp[1]:.2f} max {sp[2]:.2f})" if sp else "speed n/a (no XA sectors)"
    parts.append(f"trace {tr['events']} ev over {tr['span_s']:.0f} s: {s} | XA sectors={tr['sectors']} holds={tr['holds']} gaps={tr['xa_gaps']}")
    for name in ('sdl', 'cube'):
        rc = tr.get('rc_' + name)
        if rc:
            parts.append(f"rc[{name}] ppm med {rc['ppm_med']:.0f} [{rc['ppm_min']}..{rc['ppm_max']}] i med {rc['i_med']:.0f} sat {rc['sat']}/{rc['n']} | queued med {rc['q_med']:.0f} [{rc['q_min']}..{rc['q_max']}]")
            parts.append(f"   per 10 s ppm/queued: {rc['per10']}")
    return parts


def fmt_wav(wv, detail):
    if not wv:
        return []
    if 'error' in wv:
        return [f"audio: {wv['error']}"]
    parts = [f"audio {wv['dur_s']:.0f} s: zero runs {wv['zero_n']} ({wv['zero_ms']:.0f} ms) hist {wv['hist'] or '-'} | at {wv['timeline'] or '-'} | silences>max {wv['long_n']} ({wv['long_s']:.0f} s), first sound at {wv['first_sound_s']:.1f} s"]
    if detail:
        parts.append('   ' + '  '.join(f"{t:.2f}s/{l:.0f}ms" for t, l in wv['runs'][:80]))
    return parts


def main():
    args = [a for a in sys.argv[1:]]
    zmin, zmax, detail = 3.0, 400.0, False
    runs = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--zero-min': zmin = float(args[i + 1]); i += 2
        elif a == '--zero-max': zmax = float(args[i + 1]); i += 2
        elif a == '--detail': detail = True; i += 1
        else: runs.append(a.rstrip('/\\')); i += 1
    if not runs:
        print(__doc__); sys.exit(1)
    table = []
    for run in runs:
        name = os.path.basename(run)
        info = ''
        ip = os.path.join(run, 'run.info')
        if os.path.isfile(ip):
            info = open(ip, errors='replace').read().strip().replace('\n', ' | ')
        pf, tr, wv = read_perf(run), read_trace(run), read_wav(run, zmin, zmax)
        print(f"== {name}" + (f"  [{info}]" if info else ''))
        for line in fmt_perf(pf) + fmt_trace(tr) + fmt_wav(wv, detail):
            print('  ' + line)
        if not (pf or tr or wv):
            print('  (nothing to read: no perf.log, atrace.log or audio dump)')
        rc = (tr or {}).get('rc_sdl') or (tr or {}).get('rc_cube') or {}
        xm = pf.get('xamix', {})
        table.append((name,
                      f"{tr['speed'][0]:.2f}" if tr and tr.get('speed') else '-',
                      str(xm.get('gaps', tr['xa_gaps'] if tr else '-')),
                      str(xm.get('dry', '-')),
                      f"{rc['ppm_med']:.0f}" if rc else '-',
                      f"{wv['zero_n']}/{wv['zero_ms']:.0f}" if wv and 'zero_n' in wv else '-'))
    if len(table) > 1:
        print()
        hdr = ('run', 'speed', 'xa_gaps', 'out_dry', 'ppm_med', 'zero_n/ms')
        w = [max(len(r[c]) for r in table + [hdr]) for c in range(len(hdr))]
        for r in [hdr] + table:
            print('  '.join(x.ljust(w[c]) for c, x in enumerate(r)))


if __name__ == '__main__':
    main()
