#!/usr/bin/env python3
"""padtest.py - check what WiiStation's controller path did with a known sweep.

  padtest.py check padtrace.csv
      Reads the pad timeline a debug build wrote (sd:/wiisxrx/padtrace.csv) and says
      whether the path is intact: both sticks reach 0 and 255, pass through 128 and are
      finely graded the whole way, and each button press moves exactly one bit of the pad
      word, a different one each time.

  padtest.py make pad.dtm
      Writes a Dolphin input movie that sweeps the sticks and presses each button.
      KEPT BUT NOT WORKING: the installed Dolphin accepts -m and never plays the movie
      (see Docs/CONTROLLER_TESTING.md section 3). The sweep that actually runs is inside
      the emulator -- put "padsweep <vblank>" in sd:/wiisxrx/autoinput.txt -- which also
      works on real hardware. This generator is kept for the day a Dolphin build plays
      movies, because it is the only way to test the path above the driver.

The whole recipe, and what the numbers mean, is in Docs/CONTROLLER_TESTING.md.
"""
import argparse, csv, struct, sys

# ControllerState, 8 bytes (Dolphin Source/Core/Core/Movie.h). The bit numbers are the
# bitfield order on a little-endian host, which is what Dolphin writes.
B0 = {"Start": 0, "A": 1, "B": 2, "X": 3, "Y": 4, "Z": 5, "Up": 6, "Down": 7}
B1 = {"Left": 0, "Right": 1, "L": 2, "R": 3,
      "disc": 4, "reset": 5, "is_connected": 6, "get_origin": 7}
BUTTONS = ["A", "B", "X", "Y", "Z", "Start", "L", "R", "Up", "Down", "Left", "Right"]
CENTRE = 128
# A GameCube stick has 193 usable positions (-96..96), not 256, so a perfect sweep of one
# produces 193 distinct values, not 256. What matters is that the values are finely graded
# and reach both ends -- a stick with dead travel repeats values instead.
LEAST_VALUES = 150
# The default GameCube mapping reaches 14 of the PlayStation's 16 buttons; L3 and R3 are
# "None" and no press can produce them.
LEAST_BUTTON_BITS = 14


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


def make(path, start, hold, least):
    """The movie is read one record per controller poll, not per video frame, and how
    often the emulator polls is not knowable from here -- it depends on the game and on
    how libogc drives the SI bus. So the sweep is simply repeated until the movie is long
    enough that one is always in progress, however fast it is being consumed. A movie that
    runs out just stops feeding input, which reads as a pad sitting at rest."""
    one = sweep(hold)
    frames = [state()] * start + one * max(1, -(-least // len(one)))
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
    print("%s: %d input frames (%d before the first sweep, %d per step, %d sweeps)"
          % (path, len(frames), start, hold, (len(frames) - start) // len(one)))


def check(path):
    rows = list(csv.DictReader(open(path)))
    # A run killed mid-write leaves the last line half flushed in the SD image; drop any
    # row that is short rather than failing on it.
    whole = [r for r in rows if all(v is not None and v != "" for v in r.values())]
    if len(whole) != len(rows):
        print("note: dropped %d truncated row(s) at the end of the trace"
              % (len(rows) - len(whole)))
    rows = whole
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
            if len(vals) == 1:      # that stick was never swept on this port
                print("  %-6s %-9s %3d distinct   (not swept)" % (axis, span, len(vals)))
                continue
            ok = (vals[0] == 0 and vals[-1] == 255 and CENTRE in vals
                  and len(vals) >= LEAST_VALUES)
            print("  %-6s %-9s %3d distinct%s" % (axis, span, len(vals), "" if ok else "   <=="))
            if vals[0] != 0:
                bad.append("port %s %s never reached 0 (lowest %d)" % (pad, axis, vals[0]))
            if vals[-1] != 255:
                bad.append("port %s %s never reached 255 (highest %d)" % (pad, axis, vals[-1]))
            if CENTRE not in vals:
                bad.append("port %s %s never read %d at rest" % (pad, axis, CENTRE))
            if len(vals) < LEAST_VALUES:
                bad.append("port %s %s produced only %d distinct values, so part of its "
                           "travel does nothing" % (pad, axis, len(vals)))

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
    if len(single) < LEAST_BUTTON_BITS:
        bad.append("only %d buttons moved a bit of their own, expected %d"
                   % (len(single), LEAST_BUTTON_BITS))

    for b in bad:
        print("FAIL " + b)
    print("\n%d problem(s)" % len(bad) if bad else "\npad path intact")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make"); m.add_argument("out")
    m.add_argument("--start", type=int, default=0,
                   help="input frames of nothing before the first sweep")
    m.add_argument("--hold", type=int, default=3,
                   help="input frames each value is held for")
    m.add_argument("--least", type=int, default=80000,
                   help="repeat the sweep until the movie is at least this many frames")
    c = sub.add_parser("check"); c.add_argument("trace")
    a = ap.parse_args()
    if a.cmd == "make":
        make(a.out, a.start, a.hold, a.least)
        return 0
    return check(a.trace)


if __name__ == "__main__":
    sys.exit(main())
