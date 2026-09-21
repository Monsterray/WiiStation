#!/usr/bin/env python3
"""padtest.py - drive WiiStation's controller path from Dolphin and check what came out.

Two halves, because the input and the answer arrive at different times:

  padtest.py make  pad.dtm [--start N] [--hold K]
      Writes a Dolphin input movie that plugs in GameCube controller 1 and, from frame
      N onward, sweeps the main stick across X then Y, sweeps the C-stick the same way,
      and then presses each button on its own. Nothing else touches the pad, so whatever
      the emulator reports came from this file.

  padtest.py check padtrace.csv
      Reads the trace a debug build wrote while that movie played and says whether the
      path is intact: both sticks reach 0 and 255 and pass through 128, and each button
      press moves exactly one bit of the pad word, a different one each time.

Together with tests/psx_analog_test.c this covers the subsystem from both ends: that test
proves the conversion maths over every possible input, this proves that a real press
travels from Dolphin through the driver, the pad plugin and out to the game.

Running it:

    python scripts/padtest.py make /c/tools/Dolphin-x64/pad.dtm
    DOLPHIN_ARGS="-m /c/tools/Dolphin-x64/pad.dtm" scripts/dolphin_run.sh out 120 \\
        "" "" scripts/autoinput/<a script that boots a game>
    python scripts/sdimage_read.py wiisxrx/padtrace.csv > out/padtrace.csv
    python scripts/padtest.py check out/padtrace.csv

The trace only fills while a game is polling the pad, so the run needs an autoboot file:
in the menu nothing calls the pad plugin and the file stays empty.
"""
import argparse, csv, struct, sys

# ControllerState, 8 bytes (Dolphin Source/Core/Core/Movie.h). The bit numbers are the
# bitfield order on a little-endian host, which is what Dolphin writes.
B0 = {"Start": 0, "A": 1, "B": 2, "X": 3, "Y": 4, "Z": 5, "Up": 6, "Down": 7}
B1 = {"Left": 0, "Right": 1, "L": 2, "R": 3,
      "disc": 4, "reset": 5, "is_connected": 6, "get_origin": 7}
BUTTONS = ["A", "B", "X", "Y", "Z", "Start", "L", "R", "Up", "Down", "Left", "Right"]
CENTRE = 128


def state(buttons=(), sx=CENTRE, sy=CENTRE, cx=CENTRE, cy=CENTRE):
    b0 = 0
    b1 = 1 << B1["is_connected"]
    for name in buttons:
        if name in B0:
            b0 |= 1 << B0[name]
        else:
            b1 |= 1 << B1[name]
    return struct.pack("<BBBBBBBB", b0, b1, 0, 0, sx, sy, cx, cy)


def sweep(hold):
    """Centre, then each axis from one end to the other and back to centre."""
    out = [state()] * (hold * 4)
    for axis in range(4):
        for v in list(range(0, 256)) + [CENTRE]:
            kw = {("sx", "sy", "cx", "cy")[axis]: v}
            out += [state(**kw)] * hold
        out += [state()] * hold
    for name in BUTTONS:
        out += [state((name,))] * hold
        out += [state()] * hold
    out += [state()] * (hold * 4)
    return out


def make(path, start, hold):
    frames = [state()] * start + sweep(hold)
    hdr = bytearray(256)
    hdr[0x00:0x04] = b"DTM\x1a"
    hdr[0x04:0x0A] = b"\0" * 6          # homebrew has no game id
    hdr[0x0A] = 1                        # bWii
    hdr[0x0B] = 0x01                     # GameCube controller 1 only
    hdr[0x0C] = 0                        # from boot, not a save state
    struct.pack_into("<Q", hdr, 0x0D, len(frames))   # frameCount
    struct.pack_into("<Q", hdr, 0x15, len(frames))   # inputCount
    hdr[0x31:0x31 + 7] = b"padtest"
    hdr[0x89] = 0                        # bSaveConfig: leave the user's settings alone
    with open(path, "wb") as f:
        f.write(hdr)
        for fr in frames:
            f.write(fr)
    print("%s: %d input frames (%d before the sweep, %d per step)"
          % (path, len(frames), start, hold))


def check(path):
    rows = list(csv.DictReader(open(path)))
    if not rows:
        print("empty trace: did a game actually poll the pad?")
        return 1
    bad = []

    for pad in sorted({r["pad"] for r in rows}):
        mine = [r for r in rows if r["pad"] == pad]
        print("port %s: %d records, driver '%s'" % (pad, len(mine), mine[0]["type"]))
        for axis in ("lx", "ly", "rx", "ry"):
            col = "out_" + axis
            vals = sorted({int(r[col]) for r in mine})
            span = "%d..%d" % (vals[0], vals[-1])
            ok = vals[0] == 0 and vals[-1] == 255 and CENTRE in vals
            print("  %-6s %-9s %3d distinct%s" % (axis, span, len(vals), "" if ok else "   <== "))
            if len(vals) == 1:
                continue            # that stick was never swept in this movie
            if vals[0] != 0:
                bad.append("port %s %s never reached 0 (lowest %d)" % (pad, axis, vals[0]))
            if vals[-1] != 255:
                bad.append("port %s %s never reached 255 (highest %d)" % (pad, axis, vals[-1]))
            if CENTRE not in vals:
                bad.append("port %s %s never read %d at rest" % (pad, axis, CENTRE))
            if len(vals) < 200:
                bad.append("port %s %s produced only %d of 256 values"
                           % (pad, axis, len(vals)))

    # Buttons: find the resting word, then every word that differs from it by exactly one
    # bit. A complete, one-to-one mapping gives as many distinct bits as buttons pressed.
    words = [int(r["out_btns"], 16) for r in rows if r["pad"] == rows[0]["pad"]]
    rest = max(set(words), key=words.count)
    single = {}
    for w in words:
        d = w ^ rest
        if d and (d & (d - 1)) == 0:
            single[d] = single.get(d, 0) + 1
    print("buttons: rest word %04x, %d distinct single-bit presses seen" % (rest, len(single)))
    for d in sorted(single):
        print("  bit %2d  %d records" % (d.bit_length() - 1, single[d]))
    if len(single) < len(BUTTONS):
        bad.append("only %d of %d buttons moved a bit of their own"
                   % (len(single), len(BUTTONS)))

    for b in bad:
        print("FAIL " + b)
    print("\n%d problem(s)" % len(bad) if bad else "\npad path intact")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make"); m.add_argument("out")
    m.add_argument("--start", type=int, default=3600,
                   help="input frames of nothing before the sweep, to let a game load")
    m.add_argument("--hold", type=int, default=2,
                   help="input frames each value is held for")
    c = sub.add_parser("check"); c.add_argument("trace")
    a = ap.parse_args()
    if a.cmd == "make":
        make(a.out, a.start, a.hold)
        return 0
    return check(a.trace)


if __name__ == "__main__":
    sys.exit(main())
