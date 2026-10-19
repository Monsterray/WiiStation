"""softmode_cmp.py RUN -- the soft renderers' SoftGpuMode values against each other, per game.

RUN is a chain run where each game runs once per mode (scripts/chains/softmodes_play.txt:
SoftGpuMode=0 Accurate, 1 Hi-Fi, 2 Fast). For each game, each mode's VRAM at the end
(vram_NN.bin) is compared with Accurate's: pixels that differ, how many of them differ by at
most one 5-bit step in every channel (dither and rounding), the largest channel difference,
and how many of them have bit 15 changed. The cost of each mode is in the chain's own table
(`wsx.sh table NAME`: the load and gpu columns; run nothing else beside it).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need  # noqa: E402

END = re.compile(r"=== chain (\d+)/\d+ end vblanks=\d+(?: set=(\S+))? at=\S+ rom=(.+?) ===")


def main():
    need(sys.argv, 2, "softmode_cmp.py RUN")
    run = sys.argv[1]
    games = {}
    for line in open(os.path.join(run, "perf.log"), errors="replace"):
        m = END.search(line)
        if m:
            sets = dict(kv.split("=", 1) for kv in (m.group(2) or "").split(",") if "=" in kv)
            mode = int(sets.get("SoftGpuMode", 0))
            games.setdefault(m.group(3), {})[mode] = int(m.group(1))
    names = {0: "Accurate", 1: "Hi-Fi", 2: "Fast"}
    print(f"{'game':<44} {'mode':<8} {'differ':>8} {'<=1 step':>9} {'max':>4} {'bit15':>6}")
    for rom, modes in games.items():
        if 0 not in modes:
            continue
        a = struct.unpack("<524288H", open(os.path.join(run, "vram_%02d.bin" % modes[0]), "rb").read())
        for mode in (1, 2):
            if mode not in modes:
                continue
            b = struct.unpack("<524288H", open(os.path.join(run, "vram_%02d.bin" % modes[mode]), "rb").read())
            n = small = stp = mx = 0
            for p, q in zip(a, b):
                if p != q:
                    n += 1
                    d = max(abs((p & 31) - (q & 31)), abs(((p >> 5) & 31) - ((q >> 5) & 31)),
                            abs(((p >> 10) & 31) - ((q >> 10) & 31)))
                    small += d <= 1
                    mx = max(mx, d)
                    stp += (p ^ q) >> 15
            print(f"{rom[:44]:<44} {names[mode]:<8} {n:>8} {small:>9} {mx:>4} {stp:>6}")


if __name__ == "__main__":
    main()
