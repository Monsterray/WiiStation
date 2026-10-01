"""hprof_view.py HPROF_NN.BIN --elf ELF [--top N] [--min PCT] [--callers]

Reads a sampling profile (Gamecube/hprof.c, a PROBES=hprof build; a chained run brings one
back per game as .runs/NAME/hprof_NN.bin) and shows where the Wii's CPU was: the share in
WiiStation's own code, in Lightrec's generated code ("jit") and elsewhere, then the
functions with the most samples. One sample is one period (100 us at 729 MHz).

--callers: who called libgcc's 64-bit divide helpers (__udivdi3 and its kin, a libgcc call on
this 32-bit CPU) in the sampled moments, by function and by source line. They are leaves, so
LR at such a sample is the caller.

The ELF must be the one built with the DOL that ran (keep a copy beside the DOL): the
buckets are addresses.
"""
import argparse
import bisect
import os
import struct
import subprocess
import sys

NM = os.environ.get("NM", "C:/devkitPro/devkitPPC-r41-2/bin/powerpc-eabi-nm")


def load(path):
    d = open(path, "rb").read()
    magic, base, shift, n, period, total, jit, other = struct.unpack(">8I", d[:32])
    if magic not in (0x48505231, 0x48505232):
        sys.exit(f"{path}: not an HPR1/HPR2 file")
    counts = struct.unpack(f">{n}I", d[32:32 + 4 * n])
    lr = struct.unpack(f">{n}I", d[32 + 4 * n:32 + 8 * n]) if magic == 0x48505232 else (0,) * n
    return base, shift, period, total, jit, other, counts, lr


def symbols(elf):
    out = subprocess.run([NM, "-n", "-S", "-C", "--defined-only", elf],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and p[2] in "tTwW":
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    syms.sort()
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile")
    ap.add_argument("--elf", required=True)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--min", type=float, default=0.0, help="hide functions under this percent")
    ap.add_argument("--callers", action="store_true", help="the 64-bit divide helpers' callers")
    a = ap.parse_args()

    base, shift, period, total, jit, other, counts, lr = load(a.profile)
    syms = symbols(a.elf)
    starts = [s[0] for s in syms]

    def by_function(hist):
        # A bucket goes to the symbol that covers most of its bytes: a small function
        # (the idle loop) can start inside a bucket that begins in the one before.
        per, unknown = {}, 0
        size = 1 << shift
        for i, c in enumerate(hist):
            if not c:
                continue
            lo = base + (i << shift)
            k = bisect.bisect_right(starts, lo) - 1
            best, best_n = None, 0
            while k < len(syms) and (k < 0 or syms[k][0] < lo + size):
                if k >= 0:
                    s0, s1 = syms[k][0], syms[k][0] + max(syms[k][1], 4)
                    n = min(s1, lo + size) - max(s0, lo)
                    if n > best_n:
                        best, best_n = syms[k][2], n
                k += 1
            if best:
                per[best] = per.get(best, 0) + c
            else:
                unknown += c
        return per, unknown

    if a.callers:
        per, _ = by_function(lr)
        n = sum(lr)
        print(f"{a.profile}: {n} samples in the 64-bit divide helpers, by caller")
        for name, c in sorted(per.items(), key=lambda kv: -kv[1])[:a.top]:
            print(f"  {c:8d} {100.0 * c / n:6.2f}%  {name}")
        top = sorted(((c, i) for i, c in enumerate(lr) if c), reverse=True)[:12]
        addrs = [f"{base + (i << shift):08x}" for _, i in top]
        lines = subprocess.run([NM.replace("-nm", "-addr2line"), "-e", a.elf] + addrs,
                               capture_output=True, text=True).stdout.splitlines()
        print("  by source line (32-byte bucket of the return address):")
        for (c, _), ad, ln in zip(top, addrs, lines):
            print(f"  {c:8d}  {ad}  {ln}")
        return

    per, unknown = by_function(counts)
    text = total - jit - other
    ms = period / 729000.0   # one sample, in ms
    pct = lambda c: 100.0 * c / total if total else 0.0
    print(f"{a.profile}: {total} samples ({total * ms / 1000:.1f} s at {period} cycles each)")
    idle = per.get("idle_func", 0)
    print(f"  text {pct(text):5.1f}%   jit {pct(jit):5.1f}%   other {pct(other):5.1f}%"
          f"   text without a symbol {pct(unknown):4.1f}%   idle_func {pct(idle):4.1f}%")
    busy = total - idle
    print(f"  {'samples':>8} {'%':>6} {'busy%':>6} {'ms':>8}  function")
    per.pop("idle_func", None)   # the limiter's idle: in the header line, not a cost
    for name, c in sorted(per.items(), key=lambda kv: -kv[1])[:a.top]:
        if pct(c) < a.min:
            break
        print(f"  {c:8d} {pct(c):6.2f} {100.0 * c / busy if busy else 0:6.2f} {c * ms:8.0f}  {name}")


if __name__ == "__main__":
    main()
