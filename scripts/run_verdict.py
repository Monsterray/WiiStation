#!/usr/bin/env python3
"""run_verdict.py RUN_DIR [--base RUN_DIR] [--menu-rc N]

The last thing wsx.sh run / chain / menu print: one VERDICT line and at most 6 lines under it,
so a reader (a person, or a model with a small context) knows from the end of the output whether
the run worked, how fast each game went, and where to look next -- without opening perf.log or
the frame dumps. Also written to RUN_DIR/verdict.txt and RUN_DIR/verdict.json; verdict.json is
written last and is what `wsx.sh wait NAME` waits for.

  status OK        nothing looks wrong (run_triage.triggers() found nothing)
  status CHECK     the run finished but something looks wrong: the reasons follow, file:line
  status NO-RESULTS  no perf.log, or no game reached its perf block
  status DIFFERS   (with --base) frames or audio are not the same as the base run's

--base RUN_DIR: also compare with an earlier run of the same test -- the speed change per game
(matched by name), frames (frame_compare.py, when both runs dumped frames) and audio
(wav_compare.py --exact, when both have a dump).
--menu-rc N: a menu comparison's result (menu_compare.py: 0 same, 1 differs, 2 the walk did not
move, other: failed).

Exit code: 0 OK, 1 CHECK or DIFFERS, 2 NO-RESULTS. Standard library only.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from chain_table import games, row  # noqa: E402
from run_triage import read_rc, triggers  # noqa: E402


def short_name(rom):
    base, _, sets = rom.partition(' {')
    name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(os.path.basename(base))[0])
    return (name + (' ' + sets.rstrip('}') if sets else ''))[:40]


def game_rows(d):
    """[(name, vblanks, speed or None)] for a chain; [] for a single run without chain lines."""
    log = os.path.join(d, 'perf.log')
    play = os.path.join(d, 'debuglogs', 'play.log')
    if os.path.isfile(play):
        log = play
    if not os.path.isfile(log):
        return None
    out = []
    for n, rom, vbl, b in games(log):
        r = row(b, vbl)
        out.append((short_name(rom), vbl, r['speed'] if r else None))
    if not out:   # a single run: its last "wall: wall_us=N vblanks=V" line (a running total)
        text = open(log, encoding='utf-8', errors='replace').read()
        m = re.findall(r'^wall:.*?\bwall_us=(\d+)\b.*?\bvblanks=(\d+)', text, re.M)
        if m and int(m[-1][0]):
            us, vbl = int(m[-1][0]), int(m[-1][1])
            out.append(('run', vbl, (vbl / 59.94) / (us / 1e6)))
    return out


def is_fast(d):
    """A --fast run (Dolphin unthrottled): its wall-clock speed is not the game's speed."""
    try:
        return 'FAST=1' in open(os.path.join(d, 'run.info'), encoding='utf-8', errors='replace').read()
    except OSError:
        return False


def frames_count(d):
    return len(glob.glob(os.path.join(d, 'frames', 'framedump_*.png')))


def wav_of(d):
    w = glob.glob(os.path.join(d, '*dspdump1.wav'))
    return w[0] if w else None


def tool(args):
    """Run one of the compare scripts; (exit code, its most telling line)."""
    p = subprocess.run([sys.executable] + args, capture_output=True, text=True, errors='replace')
    lines = [l.strip() for l in (p.stdout + p.stderr).splitlines() if l.strip()]
    pick = next((l for l in lines if re.search(r'differ|identical|same|first', l, re.I)), lines[-1] if lines else '')
    return p.returncode, pick[:110]


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('run')
    ap.add_argument('--base')
    ap.add_argument('--menu-rc', type=int)
    a = ap.parse_args()
    d = a.run.rstrip('/\\')
    name = os.path.basename(d)
    rc = read_rc(d)
    reasons = triggers(d, rc) if a.menu_rc is None else []
    rows = game_rows(d) if a.menu_rc is None else []
    status = 'OK'
    if a.menu_rc is not None:
        status = {0: 'OK', 1: 'DIFFERS', 2: 'CHECK'}.get(a.menu_rc, 'NO-RESULTS')
        if a.menu_rc == 2:
            reasons.append('the walk did not move the menu (menukeys ignored?)')
        elif a.menu_rc not in (0, 1):
            reasons.append(f'menu_compare.py exit {a.menu_rc}: see the menu log above')
    elif rows is None or (rows and all(s is None for _, _, s in rows)) or any(r.startswith('no perf') for r in reasons):
        status = 'NO-RESULTS'
    elif reasons:
        status = 'CHECK'

    fast = is_fast(d)
    games_txt = ''
    if rows:   # "1 Crash Team Racing 0.99x": the game's settings are in `wsx.sh table`
        cut = lambda n: (n if len(n) <= 22 else n[:22].rsplit(' ', 1)[0]).rstrip(' -')
        spd = lambda s: ('' if fast else '%.2fx' % s) if s is not None else 'no block'
        if len(rows) == 1:
            games_txt = ('%s %s' % (cut(rows[0][0]), spd(rows[0][2]))).strip()
            if games_txt == 'run':   # a fast single run: nothing to say
                games_txt = ''
        else:
            games_txt = '; '.join(('%d %s %s' % (i, cut(n), spd(s))).strip() for i, (n, _, s) in enumerate(rows, 1))
    extra = []
    if fast and rows:
        extra.append('fast run: no speed (use --realtime to time)')
    nf = frames_count(d)
    if nf:
        extra.append(f'{nf} frames')
    if wav_of(d):
        extra.append('audio dump')

    cmp_lines = []
    if a.base:
        b = a.base.rstrip('/\\')
        if not os.path.isdir(b):
            cmp_lines.append(f'base {b}: no such run')
        else:
            brows = game_rows(b) or []
            deltas = []
            for i, (n, _, s) in enumerate(rows or []):   # by position: an A/B chain repeats a name
                if not fast and not is_fast(b) and i < len(brows) and brows[i][0] == n and s is not None and brows[i][2]:
                    deltas.append('%d %+.1f%%' % (i + 1, 100.0 * (s / brows[i][2] - 1)))
            if deltas:
                cmp_lines.append('speed vs %s: %s' % (os.path.basename(b), '; '.join(deltas)))
            if nf and frames_count(b):
                code, line = tool([os.path.join(HERE, 'frame_compare.py'), d, b])
                cmp_lines.append('frames vs base: %s (%s)' % ('same' if code == 0 else 'DIFFER', line))
                if code != 0 and status == 'OK':
                    status = 'DIFFERS'
            wa, wb = wav_of(d), wav_of(b)
            if wa and wb:
                code, line = tool([os.path.join(HERE, 'wav_compare.py'), wa, wb, '--exact'])
                cmp_lines.append('audio vs base: %s (%s)' % ('same samples' if code == 0 else 'DIFFER', line))
                if code != 0 and status == 'OK':
                    status = 'DIFFERS'

    head = f'VERDICT {name}: {status}'
    if games_txt:
        head += ' | ' + games_txt
    if extra:
        head += ' | ' + ', '.join(extra)
    body = ['  ' + r[:150] for r in reasons[:4]] + ['  ' + c for c in cmp_lines]
    if os.path.isfile(os.path.join(d, 'triage.txt')):
        body.append('  triage: ' + os.path.join(d, 'triage.txt'))
    if any(r.startswith('no perf block') for r in reasons):
        body.append('  the first perf block is written after 1800 presented frames (~60-90 s of a game): run longer')
    if status in ('CHECK', 'NO-RESULTS'):
        body.append('  next: wsx.sh triage %s   (or read run.log / perf.log at the lines above)' % name)
    elif rows and len(rows) > 1:
        body.append('  next: wsx.sh table %s [--detail gpu|tex|cpu|lc|pmc] for where the time went' % name)
    lines = [head] + body[:6]
    print('\n'.join(lines))
    with open(os.path.join(d, 'verdict.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    js = dict(name=name, status=status, rc=rc, reasons=reasons, base=a.base,
              games=[dict(name=n, vblanks=v, speed=s) for n, v, s in (rows or [])],
              frames=nf, compare=cmp_lines, finished=time.strftime('%Y-%m-%d %H:%M:%S'))
    tmp = os.path.join(d, 'verdict.json.tmp')
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(js, f, indent=1)
    os.replace(tmp, os.path.join(d, 'verdict.json'))   # last, and atomic: `wsx.sh wait` keys on it
    try:
        os.remove(os.path.join(d, 'running'))   # wsx.sh writes it when the run starts
    except OSError:
        pass
    return {'OK': 0, 'CHECK': 1, 'DIFFERS': 1}.get(status, 2)


if __name__ == '__main__':
    sys.exit(main())
