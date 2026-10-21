"""perf_windows.py PERF.LOG [--game N] [--top K] [--slow A-B,C-D,...]

Counters per vblank in the windows between perf reports. perf.log's counters are cumulative
within a game, so two reports bracket a window; `report <vblank>` lines in an input script
(Gamecube/PadWiiSX.c) put reports where you want them (e.g. at the edges of the slow stretches
`scripts/vtl_view.py` lists).

Prints one row per window (vblanks, then wall ms per vblank), and with --slow the counters
whose per-vblank rate differs most between the slow windows and the rest, which is the
question "what does the game do more of when it is slow".
"""
import argparse
import re
import sys

NUM = re.compile(r"([A-Za-z_][\w\[\]]*)=(-?\d+(?:\.\d+)?)")


def reports(path, game):
    """[(vblanks, {prefix.key: value})] for one game's reports, in order."""
    out, cur, g = [], None, 1
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("=== chain "):   # "=== chain 1/2 end ..." closes a game's reports
            g += 1
            cur = None
            continue
        if line.startswith("--- perf frames="):
            cur = {}
            out.append((g, cur))
            continue
        if cur is None or ":" not in line:
            continue
        pre, rest = line.split(":", 1)
        pre = pre.strip()
        if " " in pre or not pre:
            continue
        for k, v in NUM.findall(rest):
            cur.setdefault(f"{pre}.{k}", float(v))
    games = sorted({gg for gg, _ in out})
    pick = game if game is not None else (games[-1] if games else 0)
    rs = [r for gg, r in out if gg == pick and "wall.vblanks" in r]
    return rs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--game", type=int, default=None, help="chain game number (default the last one with reports)")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--slow", default="", help="vblank windows that are slow, A-B,C-D (a window counts as slow if it lies inside one)")
    ap.add_argument("--wall", default="wall.wall_us", help="the counter that is wall time in us")
    o = ap.parse_args()
    rs = reports(o.log, o.game)
    if len(rs) < 2:
        sys.exit(f"{o.log}: fewer than two reports for that game")
    slow = [tuple(int(x) for x in w.split("-")) for w in o.slow.split(",") if w]
    wins = []
    for a, b in zip(rs, rs[1:]):
        va, vb = int(a["wall.vblanks"]), int(b["wall.vblanks"])
        if vb <= va:
            continue
        d = {k: (b[k] - a.get(k, 0.0)) / (vb - va) for k in b if k in a}
        s = any(lo <= va and vb <= hi + 1 for lo, hi in slow)
        wins.append((va, vb, s, d))
    wall = o.wall if o.wall in wins[0][3] else next((k for k in wins[0][3] if k.startswith("wall.") and "us" in k), None)
    print(f"{'window':>13} {'slow':>4} {'wall ms/vbl':>11}")
    for va, vb, s, d in wins:
        w = d.get(wall, 0) / 1000 if wall else 0
        print(f"{va:>6}-{vb:<6} {'*' if s else '':>4} {w:11.2f}")
    if not slow:
        return
    S = [d for *_, s, d in wins if s]
    F = [d for *_, s, d in wins if not s]
    if not S or not F:
        sys.exit("--slow: need both slow and other windows")
    keys = set().union(*S) & set().union(*F)
    mean = lambda L, k: sum(d.get(k, 0) for d in L) / len(L)
    timed = lambda k: re.search(r"(_us|us_|ticks|_ms)", k) and not re.search(r"(worst|max|min|peak)", k)
    rows = [(mean(S, k), mean(F, k), k) for k in keys]
    t = sorted((r for r in rows if timed(r[2])), key=lambda r: -abs(r[0] - r[1]))
    c = sorted((r for r in rows if not timed(r[2]) and max(abs(r[0]), abs(r[1])) >= 1),
               key=lambda r: -abs(r[0] - r[1]) / (abs(r[0]) + abs(r[1])))
    print("\ntime counters, by difference (us per vblank)")
    print(f"{'counter':44} {'slow':>12} {'other':>12} {'diff':>10}")
    for ms, mf, k in t[:o.top]:
        print(f"{k:44} {ms:12.1f} {mf:12.1f} {ms - mf:10.1f}")
    print("\nevent counters, by ratio (per vblank)")
    print(f"{'counter':44} {'slow':>12} {'other':>12} {'ratio':>10}")
    for ms, mf, k in c[:o.top]:
        print(f"{k:44} {ms:12.1f} {mf:12.1f} {(ms + 1e-9) / (mf + 1e-9):10.2f}")


if __name__ == "__main__":
    main()
