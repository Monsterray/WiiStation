"""aq_cmp.py A/aq_NN.bin [B/aq_NN.bin]

The output driver's requests of one chained game (Gamecube/perf_prof.c perf_aq_log, debug
builds; dfsound/sdl.c calls it once per SDL audio callback). Each record: the emulated vblank
and PSX cycle when the Wii asked for audio, the mixer frames produced so far (44.1 kHz), the
frames queued in the driver's ring (48 kHz), the rate control's nudge (ppm) and the Wii time
since the first request.

One file: the queue (= output latency) and the nudge over the run, and how far the emulated
clock drifted from the Wii's audio clock (mixer frames against requests x frames per request).
Two files: the first request where the runs part, field by field. Under Dolphin the guest is
deterministic; if the vblank/cycle/mixer columns part, the interleaving of the emulator and the
audio callback moved (Wii-side timing), which the rate control turns into different output.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_file  # noqa: E402

FIELDS = ["vbl", "cycle", "spu", "queued", "ppm", "wii_us", "out", "out_hash"]


def load(path):
    d = open(path, "rb").read()
    magic, n = struct.unpack(">2I", d[:8])
    fmt = {0x41513031: ">IIIIiI", 0x41513032: ">IIIIiIII"}.get(magic)   # AQ02 adds out, out_hash
    if not fmt:
        sys.exit(f"{path}: not an AQ01/AQ02 file")
    k = struct.calcsize(fmt)
    return [struct.unpack(fmt, d[8 + k * i:8 + k * (i + 1)]) for i in range(n)]


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))] if v else 0


def summary(path, a):
    live = [r for r in a if r[4] != 0 or r[3] >= 6000]   # after the prefill (SDL_TARGET_FRAMES)
    q = [r[3] for r in live]
    ppm = [r[4] for r in live]
    print(f"{path}: {len(a)} requests, {len(live)} after prefill")
    if not live:
        return
    print(f"  queued frames (48 kHz) p5/p50/p95 {pct(q, .05)}/{pct(q, .5)}/{pct(q, .95)}"
          f"  min {min(q)} max {max(q)}  = latency {pct(q, .5) / 48:.0f} ms median")
    print(f"  ppm p5/p50/p95 {pct(ppm, .05)}/{pct(ppm, .5)}/{pct(ppm, .95)}  min {min(ppm)} max {max(ppm)}")
    # drift: Wii time against emulated audio produced, over the live part (both in seconds)
    r0, r1 = live[0], live[-1]
    wii = (r1[5] - r0[5]) / 1e6
    emu = (r1[2] - r0[2]) / 44100.0
    if wii > 0:
        print(f"  over {wii:.1f} s of Wii time the mixer produced {emu:.3f} s of audio:"
              f" emulated/Wii {emu / wii:.5f} ({(emu / wii - 1) * 1e6:+.0f} ppm)")
    # the queue's trend: first and last 10% (a queue that keeps growing or shrinking = drift
    # the controller is not absorbing; a steady one = audio and video stay in step)
    k = max(1, len(q) // 10)
    print(f"  queue mean first 10% {sum(q[:k]) / k:.0f}, last 10% {sum(q[-k:]) / k:.0f}")


def main():
    need(sys.argv, 2, "aq_cmp.py A/aq_NN.bin [B/aq_NN.bin]")
    a = load(req_file(sys.argv[1], "aq file"))
    summary(sys.argv[1], a)
    if len(sys.argv) < 3:
        return
    b = load(req_file(sys.argv[2], "aq file"))
    summary(sys.argv[2], b)
    n = min(len(a), len(b))
    for j, name in enumerate(FIELDS[:min(len(a[0]), len(b[0]))]):
        if name == "wii_us":
            continue
        f = next((i for i in range(n) if a[i][j] != b[i][j]), None)
        where = f"request {f + 1} (A vblank {a[f][0]}, Wii {a[f][5] / 1e6:.2f} s)" if f is not None else "never"
        print(f"  {name:>6} first differs: {where}")
    # cycle and wii_us are read from the other thread mid-block: a few cycles or one us of
    # jitter there is the interleaving itself; the parting is the first change in the rest
    def key(r):
        return tuple(v for j, v in enumerate(r) if j not in (1, 5))
    f = next((i for i in range(n) if key(a[i]) != key(b[i])), None)
    print(f"  parted at: " + (f"request {f + 1}" if f is not None else "never"))
    if f is not None:
        for i in range(max(0, f - 2), min(n, f + 3)):
            print(f"  {'*' if key(a[i]) != key(b[i]) else ' '} {i + 1:6d} A {a[i]}\n           B {b[i]}")


if __name__ == "__main__":
    main()
