#!/usr/bin/env python3
"""ptrace_summary.py PTRACE.LOG [--big AREA] [--show N]
Compact per-frame summary of a primitive-trace episode written by the debug
build (sd:/wiisxrx/ptrace.log). For every present in the episode: entry
count, semi-transparent count, the first command bytes in order, every
primitive larger than --big (default 20000 PSX pixels^2) with its position
in the frame, and the first --show semi-transparent primitives.
Legend: cmd 02 fill, E3/E4 draw area, E5 draw offset, F5 display address,
F6/F7 display range, F8 display mode; flags S semi-transparent, T textured,
Q quad, G gouraud."""
import sys, re

def main():
    path = sys.argv[1]; big = 20000; show = 12
    a = sys.argv[2:]
    while a:
        k = a.pop(0)
        if k == "--big": big = int(a.pop(0))
        elif k == "--show": show = int(a.pop(0))
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    hdr = [l for l in lines if l.startswith("ptrace:")]
    print(hdr[-1][:160] if hdr else "no ptrace header")
    frames = {}
    for l in lines:
        m = re.match(r"pt: \+(\d+) cmd=([0-9a-f]{2}) (\S{4}) abr=(\d) col=([0-9a-f]{6}) \((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\)", l)
        if m: frames.setdefault(int(m.group(1)), []).append(m.groups())
    for fr in sorted(frames):
        ps = frames[fr]
        semi = [(i, p) for i, p in enumerate(ps) if p[2][0] == "S"]
        bigs = [(i, p) for i, p in enumerate(ps) if (int(p[7]) - int(p[5])) * (int(p[8]) - int(p[6])) > big]
        ctl = [(i, p) for i, p in enumerate(ps) if p[1] in ("02", "e3", "e4", "e5", "f5", "f6", "f7", "f8")]
        print(f"\nframe +{fr}: {len(ps)} entries, {len(semi)} semi, first cmds: {' '.join(p[1] for p in ps[:8])}")
        for i, p in ctl: print(f"  [{i:4d}] ctl cmd={p[1]} col={p[4]} ({p[5]},{p[6]})-({p[7]},{p[8]})")
        for i, p in bigs: print(f"  [{i:4d}] BIG cmd={p[1]} {p[2]} abr={p[3]} col={p[4]} ({p[5]},{p[6]})-({p[7]},{p[8]})")
        for i, p in semi[:show]: print(f"  [{i:4d}] S   cmd={p[1]} {p[2]} abr={p[3]} col={p[4]} ({p[5]},{p[6]})-({p[7]},{p[8]})")
        if len(semi) > show: print(f"  ... {len(semi) - show} more semi-transparent")

if __name__ == "__main__":
    main()
