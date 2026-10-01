"""vsig_cmp.py A/vsig_NN.bin B/vsig_NN.bin

Compares two runs' per-vblank guest signatures (Gamecube/perf_prof.c vsig: a hash of every
8th word of guest RAM, the cycle count and the PC at each vblank) and prints the first
vblank where they differ, with a few vblanks of context. Two runs of the same game, build
and input must agree everywhere; the first difference is where the guests parted.
"""
import struct
import sys


def load(path):
    d = open(path, "rb").read()
    magic, n = struct.unpack(">2I", d[:8])
    if magic != 0x56534731:
        sys.exit(f"{path}: not a VSG1 file")
    return [struct.unpack(">3I", d[8 + 12 * i:20 + 12 * i]) for i in range(n)]


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    n = min(len(a), len(b))
    first = next((i for i in range(n) if a[i] != b[i]), None)
    print(f"{sys.argv[1]}: {len(a)} vblanks, {sys.argv[2]}: {len(b)} vblanks")
    if first is None:
        print(f"the same for all {n} vblanks")
        return
    print(f"first difference at vblank {first + 1}:")
    for i in range(max(0, first - 2), min(n, first + 4)):
        mark = " " if a[i] == b[i] else "*"
        print(f" {mark} {i + 1:6d}  A ram={a[i][0]:08x} cycle={a[i][1]:10d} pc={a[i][2]:08x}"
              f"   B ram={b[i][0]:08x} cycle={b[i][1]:10d} pc={b[i][2]:08x}")


if __name__ == "__main__":
    main()
