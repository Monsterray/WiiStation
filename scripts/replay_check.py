"""replay_check.py CAPTURE_PERF REPLAY_PERF [--game N] [--all]

Does a replay of a recording (scripts/movie_capture.sh) follow the session it was recorded
from? Lines up the two perf.logs' reports by emulated vblank and compares only what the GAME
did -- interrupts, CD reads, pad and SIO traffic, BIOS calls, GTE calls -- never host time
(microseconds) or what OpenGX happened to draw, which differ between any two runs.

  CAPTURE_PERF  .runs/capture_<name>_<date>/perf.log (the recording session)
  REPLAY_PERF   a chained run's perf.log; --game N picks the N-th chained game (default 1)

Reports come every 1800 presented frames, and presenting depends on the host, so two runs
report at different vblanks: only reports at the SAME vblank are compared. The first one that
differs bounds where the replay left the session. --all goes on past it, so a small
difference that stays small (a timing shift) shows apart from one that grows (a different game).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import flag, need, req_file, req_int  # noqa: E402

# guest-side fields: name -> regex on the report's lines
FIELDS = {
    'irq': r'^irq:(.*)',
    # seq= and rand= say how the HOST read the image (the read-ahead thread changes them)
    'cd': r'^cd: reads=(\d+) bytes=(\d+)',
    'sio': r'^sio: (.*)',
    'pad': r'^padproto: (\S+ \S+)',
    'bios': r'^bios: calls=(\d+) custom=\d+ us=\d+ exc=(\d+)',
    'gte': r'^gte: calls=(\d+)',
    'xa': r'^xa:(.*)',
}

def reports(path, game):
    out, cur, g = [], {}, 1
    for line in open(path, errors='replace'):
        if line.startswith('=== chain'):
            g += 1
            continue
        if g != game:
            continue
        m = re.match(r'^wall: wall_us=\d+ vblanks=(\d+)', line)
        if m:
            cur['vbl'] = int(m.group(1))
        for k, rx in FIELDS.items():
            m = re.match(rx, line)
            if m:
                cur[k] = ' '.join(m.groups()).strip()
        if line.startswith('gpupres:') and 'vbl' in cur:   # the last line of a report block
            out.append(cur)
            cur = {}
    return out

def main():
    need(sys.argv, 3, "replay_check.py CAPTURE_PERF REPLAY_PERF [--game N] [--all]")
    a = [x for x in sys.argv[1:] if not x.startswith('--') and x != flag(sys.argv, '--game')]
    if len(a) != 2:
        sys.exit("need exactly a capture and a replay perf.log")
    game = flag(sys.argv, '--game', '1')
    game = req_int(game, '--game')
    show_all = '--all' in sys.argv
    cap = {r['vbl']: r for r in reports(req_file(a[0], 'perf.log'), 1)}
    rep = {r['vbl']: r for r in reports(req_file(a[1], 'perf.log'), game)}
    common = sorted(set(cap) & set(rep))
    print(f"capture reports at vblanks {sorted(cap)[:12]}{'...' if len(cap) > 12 else ''}")
    print(f"replay  reports at vblanks {sorted(rep)[:12]}{'...' if len(rep) > 12 else ''}")
    if not common:
        print("no report at a common vblank: cannot compare (the replay drew differently from the start?)")
        return 2
    for v in common:
        diff = [k for k in FIELDS if cap[v].get(k) != rep[v].get(k)]
        if diff:
            print(f"vblank {v}: DIFFERENT in {', '.join(diff)}")
            for k in diff:
                print(f"   capture {k}: {cap[v].get(k)}\n   replay  {k}: {rep[v].get(k)}")
            if not show_all:
                return 1
            continue
        print(f"vblank {v}: identical")
    print(f"the replay follows the session through vblank {common[-1]} ({len(common)} common reports)")
    return 0

if __name__ == '__main__':
    sys.exit(main())
