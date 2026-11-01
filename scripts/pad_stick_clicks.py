"""pad_stick_clicks.py GCPADNEW_INI -- bind port 1's stick clicks for a recording.

A GameCube pad has no stick buttons; WiiStation reads Z+A as L3 and Z+B as R3 (controller-GC.c).
This makes the real pad's left stick click press Z and A, and its right stick click Z and B, in
GCPad1 of a Dolphin GCPadNew.ini (movie_capture.sh runs it on the movie profile's copy). The
click inputs: XInput "Thumb L"/"Thumb R", a DInput pad's "Button 10"/"Button 11" (Logitech Dual
Action, most PC pads). Prints what it did; changes nothing for a keyboard or an unknown device.
"""
import re
import sys

CLICKS = {"XInput": ("Thumb L", "Thumb R"), "DInput": ("Button 10", "Button 11")}


def add(line, key, extra):
    """Buttons/<key> = <expr>  ->  Buttons/<key> = <expr> | `extra` (once)"""
    m = re.match(r"(Buttons/%s\s*=\s*)(.*)$" % key, line)
    if not m or "`%s`" % extra in m.group(2):
        return line
    expr = m.group(2).strip()
    return m.group(1) + (expr + " | " if expr else "") + "`%s`" % extra


def main(path):
    text = open(path, encoding="utf-8").read().splitlines()
    out, section, device = [], None, ""
    for line in text:
        if line.startswith("["):
            section = line.strip()
        elif section == "[GCPad1]" and line.startswith("Device"):
            device = line.split("=", 1)[1].strip()
        out.append(line)
    kind = device.split("/", 1)[0]
    if kind not in CLICKS or "Keyboard" in device:
        print(f"stick clicks: not bound ({device or 'no device'} on port 1)")
        return
    l3, r3 = CLICKS[kind]
    section = None
    for i, line in enumerate(out):
        if line.startswith("["):
            section = line.strip()
        elif section == "[GCPad1]":
            out[i] = add(add(add(line, "A", l3), "B", r3), "Z", l3)
            out[i] = add(out[i], "Z", r3)
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    print(f"stick clicks: {l3} = L3 (Z+A), {r3} = R3 (Z+B) on port 1 ({device})")


if __name__ == "__main__":
    main(sys.argv[1])
