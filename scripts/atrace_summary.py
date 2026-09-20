#!/usr/bin/env python3
"""Summarise the debug build's audio timeline (sd:/wiisxrx/atrace.log, extracted by
dolphin_run.sh as <outdir>/atrace.log).

Records (written by perf_audio_event, Gamecube/perf_prof.c):
  F/R  an XA sector was queued (R = stream start): a = ring fill before the feed,
       b = cycle - cycles_played, f = bit0 driver busy, bits1-3 XARepeat, bit4 ends a gap
  H    the mixer started repeating the last XA sample (ring empty)
  G    the mixer started contributing silence (repeats used up)
  S    heartbeat every 16th SPU_async: a = ring fill, b = cycle - cycles_played, f = busy

Prints, for the sectors: the emulated-vs-wall speed between consecutive sectors, the
sector interval in both clocks, and the ring fill; for the gaps: their wall-clock length
(gap start to the sector that ended it) as a histogram; and the busy fraction seen by the
heartbeat. Emulated ms = PSX cycles / 33868.8.

usage: atrace_summary.py <atrace.log> [--events N]  (N = print the first N raw events)
"""
import sys, re, statistics

PSX_MS = 33868.8

def load(path):
    ev = []
    rx = re.compile(r"ae: (\w) w=(\d+) c=(\d+) a=(-?\d+) b=(-?\d+) f=(\d+)")
    for line in open(path, errors="replace"):
        m = rx.match(line)
        if m:
            k, w, c, a, b, f = m.groups()
            ev.append((k, int(w), int(c), int(a), int(b), int(f)))
    return ev

def hist(vals, edges, unit="ms"):
    out = []
    for lo, hi in zip(edges, edges[1:]):
        n = sum(1 for v in vals if lo <= v < hi)
        if n:
            out.append(f"{lo:>4}-{hi:<4}{unit}: {n}")
    return "  ".join(out) if out else "(none)"

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    ev = load(sys.argv[1])
    nraw = 0
    if "--events" in sys.argv:
        nraw = int(sys.argv[sys.argv.index("--events") + 1])
    if not ev:
        print("no events"); return
    w0 = ev[0][1]
    kinds = {}
    for e in ev:
        kinds[e[0]] = kinds.get(e[0], 0) + 1
    print(f"events: {len(ev)}  kinds: {kinds}  span: {(ev[-1][1]-w0)/1000:.1f} ms wall, "
          f"{((ev[-1][2]-ev[0][2]) & 0xffffffff)/PSX_MS:.1f} ms emulated")

    # --- sectors: speed, interval, fill -------------------------------------------------
    sectors = [e for e in ev if e[0] in "FR"]
    speeds, dt_wall, dt_emu, fills, lags = [], [], [], [], []
    for p, q in zip(sectors, sectors[1:]):
        if q[0] == "R":
            continue
        dw = (q[1] - p[1]) / 1000.0
        de = ((q[2] - p[2]) & 0xffffffff) / PSX_MS
        if dw > 0:
            speeds.append(de / dw); dt_wall.append(dw); dt_emu.append(de)
    for s in sectors:
        fills.append(s[3]); lags.append(s[4] / PSX_MS)
    if speeds:
        print(f"sectors: {len(sectors)} (starts {sum(1 for s in sectors if s[0]=='R')})")
        print(f"  interval emulated ms: median {statistics.median(dt_emu):.1f} "
              f"min {min(dt_emu):.1f} max {max(dt_emu):.1f}   (37.8k stereo sector = 53.3)")
        print(f"  interval wall ms:     median {statistics.median(dt_wall):.1f} "
              f"min {min(dt_wall):.1f} max {max(dt_wall):.1f}")
        print(f"  speed emulated/wall:  median {statistics.median(speeds):.3f} "
              f"min {min(speeds):.3f} max {max(speeds):.3f}")
        print(f"  ring fill at feed (samples): median {statistics.median(fills):.0f} "
              f"min {min(fills)} max {max(fills)}  zero-fill feeds {sum(1 for f in fills if f == 0)}")
        print(f"  mixer lag at feed (ms emulated, cycle - cycles_played): median "
              f"{statistics.median(lags):.2f} max {max(lags):.2f}")
        busy = [s[5] & 1 for s in sectors]
        print(f"  driver busy at feed: {sum(busy)}/{len(busy)}")

    # --- gaps: G to the next sector -------------------------------------------------------
    gaps_wall, gaps_emu = [], []
    holds = 0
    open_g = None
    for e in ev:
        if e[0] == "H":
            holds += 1
        elif e[0] == "G":
            open_g = e
        elif e[0] in "FR" and open_g is not None:
            gaps_wall.append((e[1] - open_g[1]) / 1000.0)
            gaps_emu.append(((e[2] - open_g[2]) & 0xffffffff) / PSX_MS)
            open_g = None
    print(f"holds (ring ran empty, last sample repeated): {holds}")
    print(f"gaps (silence until the next sector): {len(gaps_wall)}"
          + (f", open at end: 1" if open_g else ""))
    if gaps_wall:
        print("  wall ms:     " + hist(gaps_wall, [0, 5, 12, 30, 45, 50, 57, 80, 120, 1000]))
        print("  emulated ms: " + hist(gaps_emu, [0, 5, 12, 30, 45, 50, 57, 80, 120, 1000]))
        print(f"  wall median {statistics.median(gaps_wall):.1f} ms, "
              f"emulated median {statistics.median(gaps_emu):.1f} ms")

    # --- heartbeat --------------------------------------------------------------------------
    hb = [e for e in ev if e[0] == "S"]
    if hb:
        busy = sum(1 for e in hb if e[5] & 1)
        pulled = sum(1 for e in hb if e[4] > 200000)
        print(f"heartbeat: {len(hb)} samples, driver busy {busy} ({100*busy/len(hb):.0f}%), "
              f"mixer clock pulled back at {pulled} ({100*pulled/len(hb):.0f}%)")

    if nraw:
        print("--- first events ---")
        for e in ev[:nraw]:
            print(f"{e[0]} +{(e[1]-w0)/1000:9.2f}ms c={e[2]} a={e[3]} b={e[4]} f={e[5]}")

if __name__ == "__main__":
    main()
