"""settings_check.py [WRITTEN_SETTINGS_INI] -- example_settings.ini against the code.

    python scripts/settings_check.py
    python scripts/settings_check.py .runs/ini_defaults/settings.ini

Checks that Gamecube/release/example_settings.ini has every key of the OPTIONS table in
Gamecube/GamecubeMain.cpp, in the same [sections] and the same order, and no other key.
With a settings.ini that WiiStation wrote from its defaults (Settings > Save, no settings
file before), it also checks that each example value is the default. Keys that WiiStation
changes at run time are not compared (FPS: 1 in the debug build; the ports: auto-assign).
Exit 1 on any difference.
"""
import pathlib, re, sys

REPO = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = re.compile(r"^(FPS|PadType\d+|PadAssign\d+)$")


def table():
    """[(section, key)] from the OPTIONS table; a row with NULL value starts a section"""
    s = (REPO / "Gamecube/GamecubeMain.cpp").read_text(encoding="utf-8")
    t = s[s.index("} OPTIONS[] ="):]
    t = t[:t.index("};")]
    out, sec = [], None
    for line in t.splitlines():
        if line.strip().startswith("//"):
            continue
        m = re.match(r'\s*\{\s*"([^"]+)",\s*([^,]+),', line)
        if not m:
            continue
        if m.group(2).strip() == "NULL":
            sec = m.group(1)
        else:
            out.append((sec, m.group(1)))
    return out


def ini(path):
    """[(section, key, value)] from an INI file as WiiStation reads it"""
    out, sec = [], None
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        l = line.strip()
        if not l or l[0] in "#;":
            continue
        if l.startswith("["):
            sec = l.strip("[]")
            continue
        k, _, v = l.partition("=")
        out.append((sec, k.strip(), v.strip()))
    return out


def main():
    bad = 0
    want = table()
    ex = ini(REPO / "Gamecube/release/example_settings.ini")
    got = [(s, k) for s, k, _ in ex]
    if got != want:
        bad = 1
        ws, gs = set(want), set(got)
        for s, k in want:
            if (s, k) not in gs:
                print(f"example lacks [{s}] {k}")
        for s, k in got:
            if (s, k) not in ws:
                print(f"example has [{s}] {k}, which the code does not")
        if ws == gs:
            print("example has the keys, in another order")
    if len(sys.argv) > 1:
        written = {k: v for _, k, v in ini(sys.argv[1])}
        for _, k, v in ex:
            if k in written and not RUNTIME.match(k) and written[k] != v:
                print(f"{k}: example {v}, WiiStation's default {written[k]}")
                bad = 1
    print("settings: ok" if not bad else "settings: DIFFERENT")
    return bad


if __name__ == "__main__":
    sys.exit(main())
