#!/usr/bin/env python3
"""play_logs.py FOLDER [--slow-ms MS] [--top N] [--selftest]

Reads the play build's logs (WiiSXRX_play.dol, Docs/DEBUG_PLAY.md) and prints one block per
game session: which game, when and how long, the settings, the speed of each stretch played
between visits to the menu, the slowest stretches by vblank, and how it ended (a crash or
hang HBC kept, a fatal stop, or no end line at all).

FOLDER is a copy of sd:/wiistation/debuglogs/ (or .runs/NAME/debuglogs/ after a Dolphin run):
  play.log, play_old.log   sessions, settings, perf reports (Gamecube/play_log.c)
  vtl_<session>.bin        per-vblank timelines (scripts/vtl_view.py reads one in detail)
  hb.log, hb_old.log       the heartbeat: what the console was doing every 2 s

Columns: speed = emulated vblanks against the game's rate (1.00x = full speed); load = the
share of the time the emulator worked (the rest the frame limiter waited; 100% = it could not
keep up); psx/gpu/spu = shares of the time (scripts/chain_table.py names them all).
Standard library only.
"""
import datetime
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import parse_opts, req_float, req_int  # noqa: E402
from chain_table import parse_block  # noqa: E402
import vtl_view  # noqa: E402

KINDS = {0: 'exception', 1: 'fatal stop', 2: 'hang', 3: 'assert', 4: 'abort', 5: 'stack overrun'}
EXC = {2: 'machine check', 3: 'DSI (bad data address)', 4: 'ISI (bad code address)', 6: 'alignment',
       7: 'program', 8: 'FPU unavailable', 0x0f: 'performance monitor'}
KEYS = ('Core', 'gpuPlugin', 'SoftGpuMode', 'GxColor', 'GxTexMin', 'SkipFrames', 'FrameRate', 'CpuTiming', 'GpuTiming', 'SoundDriver', 'LockedCache',
        'VideoMode', 'TVMode')
ATTR = re.compile(r'(\w+)=(\S+)')


def when(s):
    try:
        return datetime.datetime.strptime(s.replace('T', ' '), '%Y-%m-%d %H:%M:%S')
    except ValueError:
        return None


def attrs(line):
    """key=value pairs of a marker line; file= takes the rest of the line (names have spaces)"""
    head, _, file = line.partition(' file=')
    d = dict(ATTR.findall(head.replace(' at=', ' at=', 1)))
    m = re.search(r' at=(\S+ \S+)', head)
    if m:
        d['at'] = m.group(1)
    if file:
        d['file'] = file.rsplit(' ===', 1)[0]
    return d


def read_lines(folder, name):
    out = []
    for f in (name + '_old.log', name + '.log'):
        p = os.path.join(folder, f)
        if os.path.isfile(p):
            out += open(p, encoding='utf-8', errors='replace').read().splitlines()
    return out


def sessions(lines):
    """[session dict]; crash lines go to the last session before them that has no end line"""
    res, cur, seg, block, crashes = [], None, None, None, set()
    for ln in lines:
        if ln.startswith('=== play session ') and ' start ' in ln:
            cur = dict(id=ln.split()[3], start=attrs(ln), segs=[], settings=[], fatal=[], crash=[],
                       end=None, sets=[], last_time=None)
            res.append(cur)
            seg = block = None
            continue
        if ln.startswith('crash: '):
            d = dict(ATTR.findall(ln))
            reason = ln.split(' reason=', 1)[1].split(' frames=')[0] if ' reason=' in ln else ''
            if d.get('check') not in crashes:
                crashes.add(d.get('check'))
                owner = next((s for s in reversed(res) if not s['end']), None)
                if owner:
                    owner['crash'].append((d, reason))
                else:
                    print(f"crash kept by HBC from before these logs: {d.get('kind')} pc={d.get('pc')} {reason}")
            continue
        if cur is None:
            continue
        if ln.startswith('=== play session ') and ' end ' in ln:
            cur['end'] = attrs(ln)
            cur = seg = block = None
        elif ln.startswith('settings: '):
            cur['settings'].append(ln[10:])
        elif ln.startswith('play: go '):
            seg = dict(go=attrs(ln.split(' set:')[0]), block=None)
            cur['segs'].append(seg)
            cur['sets'].append(dict(ATTR.findall(ln.split(' set:', 1)[1])) if ' set:' in ln else {})
        elif ln.startswith('play: menu '):
            seg = None
        elif ln.startswith('--- perf'):
            block = []
            if seg is not None:
                seg['block'] = block   # running totals since this stretch began: the last wins
        elif ln.startswith('fatal: '):
            cur['fatal'].append(ln[7:])
        elif ln.startswith('time: '):
            cur['last_time'] = ln[6:]
        elif block is not None:
            block.append(ln)
    return res


def heartbeats(folder):
    """[(datetime, line)] for every hb/ph line, from the boot time in each header"""
    out, boot = [], None
    for ln in read_lines(folder, 'hb'):
        m = re.search(r'boot at=(\S+ \S+)', ln)
        if m:
            boot = when(m.group(1))
            continue
        m = re.match(r'(hb|ph)\s+([\d.]+) (.*)', ln)
        if m and boot:
            out.append((boot + datetime.timedelta(seconds=float(m.group(2))), ln))
    return out


def stretch(seg):
    b = parse_block(seg['block'] or [])
    w = b.get('wall', {})
    wall, vbl = w.get('wall_us', 0) / 1e6, w.get('vblanks', 0)
    if not wall:
        return None
    pct = lambda us: 100.0 * us / (wall * 1e6)
    g = lambda line, key: b.get(line, {}).get(key, 0)
    rate = 50.0 if w.get('pal') else 59.94
    return dict(wall=wall, vbl=vbl, speed=vbl / rate / wall, load=pct(wall * 1e6 - g('inside', 'limit_us')),
                psx=pct(g('slicecost', 'jit_us') - g('inside', 'hw_us')), gpu=pct(g('inside', 'hw_gpu_us')),
                spu=pct(g('inside', 'spu_us')), mdec=pct(g('texk', 'mdec_us')), cd=pct(g('cd', 'total_us')))


def hms(s):
    s = int(s)
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}"


def report(folder, slow, top):
    ses = sessions(read_lines(folder, 'play'))
    if not ses:
        print(f"{folder}: no sessions in play.log")
        return
    hbs = heartbeats(folder)
    for i, s in enumerate(ses):
        st = s['start']
        t0 = when(st.get('at', ''))
        t1 = when(s['end']['at']) if s['end'] else when(s['last_time'] or '')
        print(f"session {s['id']}  {st.get('game', '-')}  {os.path.basename(st.get('file', '-'))}")
        print(f"  build {st.get('build', '?')}  started {st.get('at', '?')}"
              + (f", {hms((t1 - t0).total_seconds())} to its end" if t0 and t1 else ''))
        sets = s['sets'][0] if s['sets'] else {}
        if s['settings'] or sets:
            print(f"  settings: {', '.join(s['settings']) or '-'}; "
                  + ' '.join(f"{k}={sets[k]}" for k in KEYS if k in sets))
        for a, b in zip(s['sets'], s['sets'][1:]):
            ch = [f"{k} {a[k]}->{b[k]}" for k in b if k in a and a[k] != b[k]]
            if ch:
                print(f"  changed in the menu: {', '.join(ch)}")
        rows = [r for r in (stretch(g) for g in s['segs']) if r]
        if rows:
            wall = sum(r['wall'] for r in rows)
            vbl = sum(r['vbl'] for r in rows)
            avg = lambda k: sum(r[k] * r['wall'] for r in rows) / wall
            print(f"  played {hms(wall)} in {len(s['segs'])} stretch(es), {vbl} vblanks: speed {avg('speed'):.2f}x,"
                  f" load {avg('load'):.0f}% (psx {avg('psx'):.0f}, gpu {avg('gpu'):.0f}, spu {avg('spu'):.0f},"
                  f" mdec {avg('mdec'):.0f}, cd {avg('cd'):.0f})")
            if len(rows) > 1:
                for k, (g, r) in enumerate(((g, stretch(g)) for g in s['segs']), 1):
                    if r:
                        print(f"    stretch {k}: from {g['go'].get('at', '?')}, {hms(r['wall'])},"
                              f" speed {r['speed']:.2f}x, load {r['load']:.0f}%")
        else:
            print("  no perf report: the game ran less than 1800 frames before the log stopped")
        vp = os.path.join(folder, f"vtl_{s['id']}.bin")
        if os.path.isfile(vp):
            vt = vtl_view.load(vp)
            n, runs = vtl_view.slow_runs(vt, slow)
            # wall 0 with limit 0xffff (552 ms): the first vblank of a stretch (perf_prof.c vtl_tick)
            starts = [k for k, r in enumerate(vt) if r[0] == 0 and r[3] > 500]
            print(f"  timeline: {len(vt)} vblanks; {n} slow (> {slow} ms, the limiter not waiting)"
                  f" in {len(runs)} runs" + (", the worst:" if runs else ""))
            if runs:
                print("  " + vtl_view.RUNS_HEAD.strip() + "   played  stretch")
                for r in runs[:top]:
                    into = sum(v[0] for v in vt[:r['first'] - 1]) / 1000
                    k = sum(1 for z in starts if z < r['first'])
                    print(f"{vtl_view.run_line(r)}  {hms(into)}  {k}")
        else:
            print("  timeline: none (vtl file missing)")
        for f in s['fatal']:
            print(f"  FATAL: {f}")
        for d, reason in s['crash']:
            k = int(d.get('kind', 0))
            what = KINDS.get(k, f"kind {k}")
            if k == 0:
                what += ' ' + EXC.get(int(d.get('exception', 0)), f"exception {d.get('exception')}")
            print(f"  CRASH (HBC's report, seen at the next boot): {what}, pc={d.get('pc')} lr={d.get('lr')}"
                  f" dar={d.get('dar')} after {int(d.get('uptime_ms', 0)) / 1000:.0f} s; {reason}")
            print("    resolve with: hbc.py crash --elf WiiSXRX_play.elf (the ELF of this build)")
        e = s['end']
        if e:
            drops = [f"{k}={e[k]}" for k in ('text_drops', 'vtl_drops') if e.get(k, '0') != '0']
            print(f"  ended {e.get('at')} ({e.get('why')})" + (f"; lost to full queues: {' '.join(drops)}" if drops else ''))
        else:
            nxt = when(ses[i + 1]['start'].get('at', '')) if i + 1 < len(ses) else None
            last = [ln for t, ln in hbs if t0 and t >= t0 and (not nxt or t < nxt)]
            print("  NO END LINE: switched off, reset, crashed or hung." + (" The last heartbeat:" if last else ""))
            for ln in last[-2:]:
                print(f"    {ln[:160]}")
        print()


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    open(os.path.join(d, 'play.log'), 'w').write(
        "=== play boot at=2026-10-19 10:00:00 build=5.15.0 x y ===\n"
        "=== play session 20261019-100001 start at=2026-10-19 10:00:01 build=5.15.0 x y game=SCUS94228 file=sd:/a b/Spyro.cue ===\n"
        "settings: sd:/wiistation/settings.ini\n"
        "play: go at=2026-10-19 10:00:02 vblank=0 game=SCUS94228 set: Core=0 gpuPlugin=2\n"
        "--- perf frames=1800 ---\nwall: wall_us=60000000 vblanks=3596 pal=0\ninside: limit_us=30000000 hw_us=0\n"
        "play: menu at=2026-10-19 10:01:02 vblank=3596 game=SCUS94228\n"
        "play: go at=2026-10-19 10:01:30 vblank=3596 game=SCUS94228 set: Core=0 gpuPlugin=0\n"
        "=== play boot at=2026-10-19 11:00:00 build=5.15.0 x y ===\n"
        "crash: check=1234abcd kind=0 exception=3 code=0 pc=80001234 lr=80005678 dar=00000000 dsisr=0 uptime_ms=90000 app=WiiStation reason=bad frames=80001234\n"
        "crash: check=1234abcd kind=0 exception=3 code=0 pc=80001234 lr=80005678 dar=00000000 dsisr=0 uptime_ms=90000 app=WiiStation reason=bad frames=80001234\n")
    open(os.path.join(d, 'hb.log'), 'w').write(
        "# WiiStation heartbeat (Gamecube/hb_probe.c): every 2 s; boot at=2026-10-19 10:00:00\n"
        "hb    95.0 vi=1 psxvbl=4000 pc=80001234 calls=1 slowest=1ms | game 93.0s sd:/a b/Spyro.cue\n")
    import io
    import contextlib
    import struct
    ms = lambda v: int(v * 60.75e6 / 1000 / 512)   # vtl units (VTL_SHIFT 9)
    recs = [(0, 0, 0, 0xffff, 0, 0, 0)] + [(ms(16.7), ms(8), 0, ms(8), 0, 1, 0)] * 100 \
        + [(0, 0, 0, 0xffff, 0, 0, 0)] + [(ms(40), ms(40), ms(20), 0, 0, 1, 0)] * 5   # stretch 2 is slow
    with open(os.path.join(d, 'vtl_20261019-100001.bin'), 'wb') as f:
        f.write(struct.pack('>III', 0x56544c32, 9, 0) + b''.join(struct.pack('>7H', *r) for r in recs))
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        report(d, 17.5, 3)
    out = buf.getvalue()
    for want in ('SCUS94228  Spyro.cue', 'speed 1.00x, load 50%', 'changed in the menu: gpuPlugin 2->0',
                 'CRASH', 'DSI', 'NO END LINE', 'hb    95.0', 'timeline: 107 vblanks; 5 slow',
                 '103-107', '0:00:01  2'):
        assert want in out, (want, out)
    assert out.count('CRASH') == 1, out
    print("selftest ok")


def main():
    args, opts = parse_opts(sys.argv, valued=('--slow-ms', '--top'), bools=('--selftest', '--help', '-h'))
    if '--selftest' in opts:
        return selftest()
    if '--help' in opts or '-h' in opts or len(args) != 1:
        print(__doc__)
        return
    if not os.path.isdir(args[0]):
        sys.exit(f"{args[0]}: not a folder")
    report(args[0], req_float(opts.get('--slow-ms', 17.5), '--slow-ms'), req_int(opts.get('--top', 5), '--top'))


if __name__ == '__main__':
    main()
