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

MAPPING = {"S": "previous", "T": "unknown", "-": "current"}
CAPTURE = {3: "captured-back-buffer", 2: "captured-pending", 1: "captured-live", 4: "queued", 5: "queued",
           0: "none", -1: "readback-off", -2: "efb-not-capturable", -3: "not-dirty", -4: "capture-rejected",
           -5: "previous:no-capture", -6: "unknown-mapping", -7: "pending-rejected", -8: "no-source"}
GATE = ["pendingPresented", "contaminated", "mixed", "untracked", "prevSnap", "liveSnap",
        "mapValid", "contentValid", "contentDirty", "asyncInFlight"]
READ_RX = re.compile(r"pt: \+(\d+) cmd=c([123]) (\S)(\S)\S\S abr=(\d+) col=([0-9a-f]{6}) "
                     r"\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\)")

def decode_reads(lines):
    """One line per GP0 C0 read: pairs the C1 (outcome), C2 (state bits, tile counts) and
    C3 (post-merge hash / non-black words) entries written by the debug build's read path."""
    out, cur = [], None
    for l in lines:
        m = READ_RX.match(l)
        if not m:
            continue
        fr, kind, f0, f1, abr, col, x0, y0, x1, y1 = m.groups()
        if kind == "1":
            mapping = "previous" if f0 == "S" else ("unknown" if f1 == "T" else "current")
            cap = int(abr) - 8
            cur = [f"+{fr} ({x0},{y0})-({x1},{y1}) | {mapping} {CAPTURE.get(cap, cap)} merged={int(col, 16)}"]
            out.append(cur)
        elif kind == "2" and cur is not None:
            bits = int(col, 16)
            cur.append("| " + ",".join(n for i, n in enumerate(GATE) if bits & (1 << i)) +
                       f" map={x0}/prev={y0} prevTiles={x1} liveTiles={y1}")
        elif kind == "3" and cur is not None:
            cur.append(f"| hash={col} nonblack={x1} of {y1}")
    return [" ".join(r) for r in out]

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
    reads = decode_reads(lines)
    if reads:
        print("\nVRAM->CPU reads (debug readback probes): rect | mapping capture merged | state bits | psxVuw after merge")
        for r in reads:
            print("  " + r)
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
