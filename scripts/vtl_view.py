"""vtl_view.py VTL_NN.BIN [--top N] [--slow-ms MS] [--around VBLANK]

Reads a per-vblank timeline (Gamecube/perf_prof.c perf_vtl_flush; a chained run brings one
back per game as .runs/NAME/vtl_NN.bin) and shows where a slowdown a person saw is:

  - runs of slow vblanks: wall longer than --slow-ms (default 17.5, a 60 Hz vblank is 16.7)
    with no frame-limiter wait, merged when less than a second apart, the costliest first;
    for each, where the time went and frames presented
  - --around V: every vblank from V-30 to V+90, one line each

Times are in ms. The Wii's time base runs at 60.75 MHz; Dolphin emulates the same clock, so in
Dolphin these are emulated times (host stalls such as shader compiles never show).
Columns: core = the core's slices minus what runs inside them (GPU registers, SPU, and in
VTL2 files Lightrec compiling); other = everything outside a slice (presenting, input, menu).
"""
import os
import struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, parse_opts, req_file, req_float, req_int  # noqa: E402

TB_HZ = 60_750_000

def load(path):
    d = open(path, 'rb').read()
    for end in '<>':
        magic, shift, n = struct.unpack_from(end + 'III', d, 0)
        if magic in (0x56544c31, 0x56544c32):
            break
    else:
        sys.exit(f"{path}: not a timeline")
    nf = 6 if magic == 0x56544c31 else 7          # VTL1: no jit field
    if n == 0 or 12 + 2 * nf * n > len(d):         # the play build streams it: count 0, to the end
        n = (len(d) - 12) // (2 * nf)
    unit_ms = (1 << shift) / TB_HZ * 1000
    rows = []
    for i in range(n):
        r = struct.unpack_from(end + f'{nf}H', d, 12 + 2 * nf * i)
        w, c, g, l, s, p = r[:6]
        j = r[6] if nf == 7 else 0
        rows.append((w * unit_ms, c * unit_ms, g * unit_ms, l * unit_ms, s * unit_ms, p, j * unit_ms))
    return rows

def main():
    need(sys.argv, 2, "vtl_view.py VTL.BIN [--top N] [--slow-ms MS] [--around V]")
    args, opts = parse_opts(sys.argv, valued=('--top', '--slow-ms', '--around'))
    if len(args) != 1:
        sys.exit("need exactly one timeline file")
    path = req_file(args[0], 'timeline file')
    top = req_int(opts.get('--top', 12), '--top')
    slow = req_float(opts.get('--slow-ms', 17.5), '--slow-ms')
    rows = load(path)
    n = len(rows)
    wall = sum(r[0] for r in rows)
    print(f"{path}: {n} vblanks, {wall / 1000:.1f} s wall, {n / 60 / (wall / 1000):.3f}x"
          f" ({sum(r[5] for r in rows)} frames presented, {sum(r[6] for r in rows) / 1000:.2f} s compiling)")
    if '--around' in opts:
        v0 = req_int(opts['--around'], '--around')
        print(" vblank   wall   core    gpu    spu    jit  limit  pres")
        for i in range(max(0, v0 - 30), min(n, v0 + 90)):
            w, c, g, l, s, p, j = rows[i]
            print(f"{i + 1:7d} {w:6.1f} {c - g - s - j:6.1f} {g:6.1f} {s:6.1f} {j:6.1f} {l:6.1f} {p:4d}")
        return
    bad, runs = slow_runs(rows, slow)
    print(f"{bad} slow vblanks (> {slow} ms, no limiter wait) in {len(runs)} runs; the costliest:")
    print(RUNS_HEAD)
    for r in runs[:top]:
        print(run_line(r))


def slow_runs(rows, slow=17.5):
    """(count of slow vblanks, [run]) -- runs of vblanks longer than `slow` ms with no limiter
    wait, merged when under a second apart, the costliest first. A run is a dict: first and
    last vblank (1-based), lost_ms (wall beyond 60 Hz), wall_ms, the shares, fps."""
    bad = [i for i, r in enumerate(rows) if r[0] > slow and r[3] < 0.1]
    spans = []
    for i in bad:
        if spans and i - spans[-1][1] <= 60:
            spans[-1][1] = i
        else:
            spans.append([i, i])
    runs = []
    for a, b in spans:
        seg = rows[a:b + 1]
        w = sum(r[0] for r in seg)
        c, g, s, j = (sum(r[k] for r in seg) for k in (1, 2, 4, 6))
        runs.append(dict(first=a + 1, last=b + 1, lost_ms=w - (b - a + 1) * 1000 / 60, wall_ms=w,
                         core=100 * (c - g - s - j) / w, gpu=100 * g / w, spu=100 * s / w,
                         jit=100 * j / w, other=100 * (w - c) / w,
                         fps=sum(r[5] for r in seg) / (w / 1000)))
    runs.sort(key=lambda r: -r['lost_ms'])
    return len(bad), runs


RUNS_HEAD = "  vblanks          lost_ms  wall_ms  core%   gpu%   spu%   jit%  other%  fps"


def run_line(r):
    return (f"  {r['first']:6d}-{r['last']:<6d} {r['lost_ms']:9.0f} {r['wall_ms']:8.0f} {r['core']:6.1f}"
            f" {r['gpu']:6.1f} {r['spu']:6.1f} {r['jit']:6.1f} {r['other']:7.1f} {r['fps']:5.1f}")

if __name__ == '__main__':
    main()
