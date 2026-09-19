#!/usr/bin/env python3
"""perf_compare.py A.log B.log [--block N]
Side-by-side comparison of two perf.log files from the debug build (sd:/wiisxrx/perf.log,
read out of the SD image with sdimage_read.py). Picks the N-th report block of each file
(default: the last block both files have, so runs of different length compare the same
present count), and prints the counters that decide CPU-core and renderer questions:
emulated CPU time per present, JIT resets and fallbacks, exceptions, limiter idle, SPU and
hardware time, present time, plus any 'jitmem'/'gte' lines. Values are shown as A, B and
B/A so a 5 % change is visible at a glance."""
import re, sys

KEYS = [("cpu", "cpu_us"), ("cpu", "jit_full"), ("cpu", "jit_part"), ("cpu", "interp_fb"), ("cpu", "exc"),
        ("wall", "wall_us"), ("wall", "vblanks"), ("inside", "limit_us"), ("inside", "spu_us"),
        ("inside", "hw_us"), ("inside", "hw_gpu_us"), ("slice", "cycles"), ("slice", "avg"),
        ("gpu", "present_us"), ("gpu", "drawdone"), ("gpu", "drawdone_us"),
        ("gpu", "tex_hit"), ("gpu", "miss"), ("gpu", "resets"), ("gpu", "evicts"),
        ("ogx", "sub_new"), ("ogx", "sub_hit"), ("ram", "mem2"), ("ram", "peak"),
        ("offsoft", "prims"), ("sio", "start")]

def blocks(path):
    out, cur = [], None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("--- perf frames="):
            cur = {"frames": int(re.search(r"frames=(\d+)", line).group(1))}; out.append(cur)
        elif cur is not None and ":" in line:
            tag, rest = line.split(":", 1)
            for k, v in re.findall(r"(\w+)=([-\d./]+)", rest):
                cur.setdefault(tag.strip(), {})[k] = v
    return out

def main():
    a, b = sys.argv[1], sys.argv[2]
    n = int(sys.argv[sys.argv.index("--block") + 1]) if "--block" in sys.argv else None
    ba, bb = blocks(a), blocks(b)
    if not ba or not bb:
        sys.exit("no report blocks found")
    idx = n if n is not None else min(len(ba), len(bb)) - 1
    A, B = ba[idx], bb[idx]
    print(f"block {idx}: A frames={A['frames']}  B frames={B['frames']}")
    print(f"{'counter':<20}{'A':>14}{'B':>14}{'B/A':>8}")
    for tag, key in KEYS:
        va, vb = A.get(tag, {}).get(key), B.get(tag, {}).get(key)
        if va is None and vb is None: continue
        try:
            fa, fb = float(str(va).split("/")[0]), float(str(vb).split("/")[0])
            ratio = f"{fb / fa:.3f}" if fa else "-"
        except (TypeError, ValueError):
            ratio = "-"
        print(f"{tag + '.' + key:<20}{str(va):>14}{str(vb):>14}{ratio:>8}")
    fa, fb = A["frames"], B["frames"]
    ca, cb = float(A.get("cpu", {}).get("cpu_us", 0)), float(B.get("cpu", {}).get("cpu_us", 0))
    if fa and fb and ca and cb:
        print(f"\ncpu_us per present: A {ca / fa:.0f}  B {cb / fb:.0f}  B/A {(cb / fb) / (ca / fa):.3f}")

if __name__ == "__main__":
    main()
