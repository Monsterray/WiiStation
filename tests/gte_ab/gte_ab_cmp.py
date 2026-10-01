"""Compare two gte_ab output files (tests/gte_ab/run.sh): per op and input kind, how many cases differ, and in
which registers (data by name, FLAG split out)."""
import sys, struct, collections
OPS = "RTPS NCLIP OP DPCS INTPL MVMVA NCDS CDP NCDT NCCS CC NCS NCT SQR DCPL DPCT AVSZ3 AVSZ4 RTPT GPF GPL NCCT".split()
DN = "VXY0 VZ0 VXY1 VZ1 VXY2 VZ2 RGB OTZ IR0 IR1 IR2 IR3 SXY0 SXY1 SXY2 SXYP SZ0 SZ1 SZ2 SZ3 RGB0 RGB1 RGB2 RES1 MAC0 MAC1 MAC2 MAC3 IRGB ORGB LZCS LZCR".split()
KIND = ["sdk any32", "sdk game", "sdk mid", "rnd any32", "rnd game", "rnd mid"]
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
assert len(a) == len(b)
R = 66 * 4
tot = collections.Counter(); diff = collections.Counter(); fdiff = collections.Counter()
regs = collections.defaultdict(collections.Counter)
flagbits = collections.defaultdict(collections.Counter)
ex = {}
for off in range(0, len(a), R):
    ra = struct.unpack_from('<66I', a, off); rb = struct.unpack_from('<66I', b, off)
    o, k = ra[0] & 0xff, (ra[0] >> 8 & 0xff) + 3 * (ra[0] >> 16)
    tot[o, k] += 1
    if ra == rb:
        continue
    names = [DN[i] for i in range(32) if ra[2 + i] != rb[2 + i]]
    cnames = ["C%d" % i for i in range(31) if ra[34 + i] != rb[34 + i]]
    fl = ra[65] ^ rb[65]
    if names or cnames:
        diff[o, k] += 1
        if k < 3:
            for n in names + cnames:
                regs[o][n] += 1
        ex.setdefault((o, k < 3), (ra, rb, names))
    if fl:
        fdiff[o, k] += 1
        for bit in range(12, 32):
            if k < 3 and fl >> bit & 1:
                flagbits[o][bit] += 1
print("%-6s %s" % ("op", "  ".join("%-11s" % k for k in KIND)))
for o, name in enumerate(OPS):
    cells = []
    for k in range(6):
        t = tot[o, k]
        cells.append("%5.1f/%5.1f" % (100.0 * diff[o, k] / t, 100.0 * fdiff[o, k] / t) if t else "-")
    print("%-6s %s" % (name, "  ".join("%-11s" % c for c in cells)))
print()
for o, name in enumerate(OPS):
    if regs[o] or flagbits[o]:
        print(name, "regs:", dict(regs[o].most_common(8)), "flag bits:", dict(flagbits[o].most_common(6)))
if len(sys.argv) > 3:
    for (o, isdk), (ra, rb, names) in sorted(ex.items()):
        if not isdk:
            continue
        print()
        print("%s op=%08x FLAG old %08x new %08x" % (OPS[o], ra[1], ra[65], rb[65]))
        for n in names:
            i = DN.index(n)
            print("  %-5s old %08x new %08x" % (n, ra[2 + i], rb[2 + i]))
