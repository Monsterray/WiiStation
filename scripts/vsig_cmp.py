"""vsig_cmp.py A/vsig_NN.bin B/vsig_NN.bin

Compares two runs' per-vblank guest signatures (Gamecube/perf_prof.c vsig: a hash of every
8th word of guest RAM, the cycle count and the PC at each vblank) and prints the first
vblank where they differ, with a few vblanks of context. Two runs of the same game, build
and input must agree everywhere; the first difference is where the guests parted.

VSG2 files also carry what the host fed the guest: a hash of every CD sector read so far
and their count (cd, cdn), Lightrec's blocks compiled (jitb) and code-buffer cleans
(cleans). Those are compared too, and the first vblank where each part differs is named,
so a run can say whether the CD data, the JIT or the guest itself parted first. A VSG1
file compares on ram/cycle/pc only.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_file  # noqa: E402

FIELDS = ["ram", "cycle", "pc", "cd", "cdn", "jitb", "cleans"]


def load(path):
    d = open(path, "rb").read()
    magic, n = struct.unpack(">2I", d[:8])
    k = {0x56534731: 3, 0x56534732: 7}.get(magic)
    if not k:
        sys.exit(f"{path}: not a VSG1/VSG2 file")
    return [struct.unpack(f">{k}I", d[8 + 4 * k * i:8 + 4 * k * (i + 1)]) for i in range(n)]


def main():
    need(sys.argv, 3, "vsig_cmp.py A/vsig_NN.bin B/vsig_NN.bin")
    a, b = load(req_file(sys.argv[1], 'vsig file')), load(req_file(sys.argv[2], 'vsig file'))
    k = min(len(a[0]), len(b[0])) if a and b else 3
    a = [r[:k] for r in a]
    b = [r[:k] for r in b]
    n = min(len(a), len(b))
    print(f"{sys.argv[1]}: {len(a)} vblanks, {sys.argv[2]}: {len(b)} vblanks")
    first = next((i for i in range(n) if a[i] != b[i]), None)
    if first is None:
        print(f"the same for all {n} vblanks")
        return
    print(f"first difference at vblank {first + 1}:")
    for j in range(k):
        f = next((i for i in range(n) if a[i][j] != b[i][j]), None)
        print(f"  {FIELDS[j]:>6}: " + (f"first differs at vblank {f + 1}" if f is not None else "the same"))
    for i in range(max(0, first - 2), min(n, first + 4)):
        mark = " " if a[i] == b[i] else "*"
        print(f" {mark} {i + 1:6d}  A " + " ".join(f"{FIELDS[j]}={a[i][j]:08x}" for j in range(k)))
        print(f"          B " + " ".join(f"{FIELDS[j]}={b[i][j]:08x}" for j in range(k)))


if __name__ == "__main__":
    main()
