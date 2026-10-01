"""vtl_view.py VTL_NN.BIN [--top N] [--slow-ms MS] [--around VBLANK]

Reads a per-vblank timeline (Gamecube/perf_prof.c perf_vtl_flush; a chained run brings one
back per game as .runs/NAME/vtl_NN.bin) and shows where a slowdown a person saw is:

  - runs of slow vblanks: wall longer than --slow-ms (default 17.5, a 60 Hz vblank is 16.7)
    with no frame-limiter wait, merged when less than a second apart, the longest first;
    for each, where the time went (core, GPU registers, SPU, the rest) and frames presented
  - --around V: every vblank from V-30 to V+90, one line each

Times are in ms. The Wii's time base runs at 60.75 MHz; Dolphin emulates the same clock.
"""
import struct, sys

TB_HZ = 60_750_000

def load(path):
    d = open(path, 'rb').read()
    magic, shift, n = struct.unpack_from('<III', d, 0)
    if magic != 0x56544c31:
        magic, shift, n = struct.unpack_from('>III', d, 0)   # written by the Wii: big-endian
        end = '>'
    else:
        end = '<'
    unit_ms = (1 << shift) / TB_HZ * 1000
    rows = [struct.unpack_from(end + '6H', d, 12 + 12 * i) for i in range(n)]
    return [(w * unit_ms, c * unit_ms, g * unit_ms, l * unit_ms, s * unit_ms, p)
            for w, c, g, l, s, p in rows]

def main():
    args = sys.argv[1:]
    path = args[0]
    top = int(args[args.index('--top') + 1]) if '--top' in args else 12
    slow = float(args[args.index('--slow-ms') + 1]) if '--slow-ms' in args else 17.5
    rows = load(path)
    n = len(rows)
    wall = sum(r[0] for r in rows)
    print(f"{path}: {n} vblanks, {wall / 1000:.1f} s wall, {n / 60 / (wall / 1000):.3f}x"
          f" ({sum(r[5] for r in rows)} frames presented)")
    if '--around' in args:
        v0 = int(args[args.index('--around') + 1])
        print(" vblank   wall   core    gpu    spu  limit  pres")
        for i in range(max(0, v0 - 30), min(n, v0 + 90)):
            w, c, g, l, s, p = rows[i]
            print(f"{i + 1:7d} {w:6.1f} {c:6.1f} {g:6.1f} {s:6.1f} {l:6.1f} {p:4d}")
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
    print("  vblanks          lost_ms  wall_ms  core%   gpu%   spu%  other%  fps")
    for a, b in runs[:top]:
        lost, seg = cost(a, b)
        w = sum(r[0] for r in seg)
        c, g, s = (sum(r[k] for r in seg) for k in (1, 2, 4))
        fps = sum(r[5] for r in seg) / (w / 1000) if w else 0
        print(f"  {a + 1:6d}-{b + 1:<6d} {lost:9.0f} {w:8.0f} {100 * (c - g) / w:6.1f} {100 * g / w:6.1f}"
              f" {100 * s / w:6.1f} {100 * (w - c - s) / w:7.1f} {fps:5.1f}")
    print("(core% excludes the GPU registers, which run inside the core's slices; other% is"
          " everything outside a slice: presenting, input, the menu, the SD card)")

if __name__ == '__main__':
    main()
