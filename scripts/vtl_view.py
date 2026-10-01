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
import struct, sys

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
    unit_ms = (1 << shift) / TB_HZ * 1000
    rows = []
    for i in range(n):
        r = struct.unpack_from(end + f'{nf}H', d, 12 + 2 * nf * i)
        w, c, g, l, s, p = r[:6]
        j = r[6] if nf == 7 else 0
        rows.append((w * unit_ms, c * unit_ms, g * unit_ms, l * unit_ms, s * unit_ms, p, j * unit_ms))
    return rows

def main():
    args = sys.argv[1:]
    path = args[0]
    top = int(args[args.index('--top') + 1]) if '--top' in args else 12
    slow = float(args[args.index('--slow-ms') + 1]) if '--slow-ms' in args else 17.5
    rows = load(path)
    n = len(rows)
    wall = sum(r[0] for r in rows)
    print(f"{path}: {n} vblanks, {wall / 1000:.1f} s wall, {n / 60 / (wall / 1000):.3f}x"
          f" ({sum(r[5] for r in rows)} frames presented, {sum(r[6] for r in rows) / 1000:.2f} s compiling)")
    if '--around' in args:
        v0 = int(args[args.index('--around') + 1])
        print(" vblank   wall   core    gpu    spu    jit  limit  pres")
        for i in range(max(0, v0 - 30), min(n, v0 + 90)):
            w, c, g, l, s, p, j = rows[i]
            print(f"{i + 1:7d} {w:6.1f} {c - g - s - j:6.1f} {g:6.1f} {s:6.1f} {j:6.1f} {l:6.1f} {p:4d}")
        return
    bad = [i for i, r in enumerate(rows) if r[0] > slow and r[3] < 0.1]
    runs = []
    for i in bad:
        if runs and i - runs[-1][1] <= 60:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    def cost(a, b):
        seg = rows[a:b + 1]
        w = sum(r[0] for r in seg)
        return w - (b - a + 1) * 1000 / 60, seg
    runs.sort(key=lambda r: -cost(*r)[0])
    print(f"{len(bad)} slow vblanks (> {slow} ms, no limiter wait) in {len(runs)} runs; the costliest:")
    print("  vblanks          lost_ms  wall_ms  core%   gpu%   spu%   jit%  other%  fps")
    for a, b in runs[:top]:
        lost, seg = cost(a, b)
        w = sum(r[0] for r in seg)
        c, g, s, j = (sum(r[k] for r in seg) for k in (1, 2, 4, 6))
        fps = sum(r[5] for r in seg) / (w / 1000) if w else 0
        print(f"  {a + 1:6d}-{b + 1:<6d} {lost:9.0f} {w:8.0f} {100 * (c - g - s - j) / w:6.1f} {100 * g / w:6.1f}"
              f" {100 * s / w:6.1f} {100 * j / w:6.1f} {100 * (w - c) / w:7.1f} {fps:5.1f}")

if __name__ == '__main__':
    main()
