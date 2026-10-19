"""settings_roundtrip.py -- every key WiiStation writes, read back to the same value.

    python scripts/settings_roundtrip.py make OUT.ini
    python scripts/settings_roundtrip.py check IN.ini WRITTEN.ini

make: a settings file with every key of the OPTIONS table (Gamecube/GamecubeMain.cpp) at a
value other than its default (Gamecube/release/example_settings.ini), inside the table's range.
A few keys keep their default because changing them changes the run itself (KEEP below).

check: WRITTEN is the settings.ini that WiiStation wrote after it read IN (a Save from the
menu: autoinput "menuclick 14 1"). Every table key that a settings file holds must be in
WRITTEN, with the value IN gave it (the last line of a key wins, as in readConfig); a key no file
holds (FilterTour) must not be. Exit 1 on any difference. A game's own file (settings/<ID>.ini)
holds only what differs from settings.ini: SETTINGS.md 1.4.

The Dolphin round trip (Docs/SETTINGS_AUDIT.md):
    python scripts/settings_roundtrip.py make .runs/rt_in.ini
    KEEP_PROFILE=1 bash scripts/wsx.sh run rt --autoboot none --secs 25 --nodump \\
        --input scripts/autoinput/menu_save_sd.txt --settings .runs/rt_in.ini
    python scripts/sdimage_read.py .profiles/<profile>/Load/WiiSD.raw wiistation/settings.ini .runs/rt/written.ini
    python scripts/settings_roundtrip.py check .runs/rt/settings.cfg .runs/rt/written.ini
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import settings_check as sc  # noqa: E402
from cliutil import req_file  # noqa: E402

# Kept at the default: lang and MenuFont load a font (the menu text changes), smbipaddr starts
# a network mount, BiosDevice falls back to HLE when that device has no BIOS file.
KEEP = {"lang", "MenuFont", "smbipaddr", "BiosDevice"}


def values(path):
    """{key: value} as readConfig takes them: the last line of a key wins"""
    return {k: v for _, k, v in sc.ini(path)}


def make(out):
    default = values(sc.REPO / "Gamecube/release/example_settings.ini")
    lines = []
    for r in sc.table():
        if not r["stored"]:
            continue
        k, d = r["key"], default[r["key"]]
        if k in KEEP:
            v = d
        elif r["min"] is None:
            v = '"rt %s"' % k if k not in ("RomDirSD", "RomDirUSB") else '"/wiistation/isos, /rt"'
        else:
            n = int(d)
            v = str(n + 1 if n < r["max"] else r["min"])
        lines.append(f"{k} = {v}")
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print(f"{out}: {len(lines)} keys, {sum(1 for r in sc.table() if r['key'] in KEEP)} at the default")


def check(given, written):
    want, got = values(req_file(given, "settings in")), values(req_file(written, "written ini"))
    bad = 0
    for r in sc.table():
        k = r["key"]
        if not r["stored"]:
            if k in got:
                print(f"{k}: written, but no settings file should hold it")
                bad = 1
            continue
        if k not in got:
            print(f"{k}: not written")
            bad = 1
        elif k in want and want[k] != got[k]:
            print(f"{k}: read {want[k]}, written {got[k]}")
            bad = 1
    print(f"checked {len(sc.table())} keys: {'differences' if bad else 'all read back the same'}")
    return bad


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["make"] and len(a) == 2:
        make(a[1])
    elif a[:1] == ["check"] and len(a) == 3:
        sys.exit(check(a[1], a[2]))
    else:
        print(__doc__.split("\n\n")[0])
        sys.exit(2)
