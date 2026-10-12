"""settings_texts.py [REV] -- the words of the Settings screens, before and after a change.

    python scripts/settings_texts.py            # REV = HEAD~ against the working tree
    python scripts/settings_texts.py HEAD~2     # any revision against the working tree

For every row of the Settings tabs (Gamecube/menu/SettingsFrame.cpp), it lists the row's label
and every text each of its buttons can show. The "before" side reads the file at REV.
The "after" side reads the working tree. A keyed button takes its text from the settings
table. A row without a label override takes the table's label. A button with no key keeps
its own text.

Run-time texts follow the code's own rules. Each rule is checked against the source first.
A cycle shows the text of its current value. A memory card shows Off or its file type.
The Auto Load Slot shows Slot N or Default.

The two sides must match row by row and button by button. Exit 0 when they match.
Exit 1 when they differ. Exit 2 when REV cannot be read.
"""
import json
import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import settings_check as sc  # noqa: E402

REPO = sc.REPO
FILE = "Gamecube/menu/SettingsFrame.cpp"
SHARED = ["Gamecube/GamecubeMain.cpp", "Gamecube/wiiSXconfig.h"]

# the variable a cycle's text is chosen by -> its key in the table
VAR_KEY = {
    "screenMode": "ScreenMode",
    "bilinearFilter": "BilinearFilter",
    "lightGun": "LightGun",
    "spuInterpolation": "Interpolation",
    "lang": "lang",
}
# the buttons whose text is set at run time in the before file, by an assignment to buttonString
BEFORE_CYCLE_BUTTONS = (22, 28, 45, 54, 59)
CARD_BUTTONS = (76, 77)
SLOT_BUTTON = 38


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def unquote(text):
    return sc.unquote(text)


def string_array(code, name):
    m = re.search(r"static char " + name + r"\[\d+\]\[\d+\]\s*=\s*\{(.*?)\};", code, flags=re.S)
    if not m:
        return None
    return [unquote(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', m.group(1))]


def frame_buttons(code):
    """FRAME_BUTTONS in order: dicts with text (the expression, or None for a keyed button),
    key and value (strings, or None)."""
    m = re.search(r"FRAME_BUTTONS\[NUM_FRAME_BUTTONS\] =\s*\{(.*?)\n\};", code, flags=re.S)
    if not m:
        raise SystemExit("cannot find FRAME_BUTTONS")
    out = []
    for line in m.group(1).splitlines():
        r = re.match(r"^\s*\{(.*)\}\s*,?\s*$", line)
        if not r:
            continue
        f = sc.split_fields(r.group(1))
        if f[0] != "NULL":
            raise SystemExit("unexpected FRAME_BUTTONS line: " + line)
        out.append({
            "text": None if f[2] == "NULL" else f[2],
            "key": unquote(f[5]) if len(f) > 5 else None,
            "value": f[6] if len(f) > 6 else None,
        })
    return out


def set_tables(code):
    """each SetRow table: its name, and its rows (key, label expression, button indexes)"""
    out = []
    for m in re.finditer(r"static const SetRow (\w+)\[\] =\s*\{(.*?)\n\};", code, flags=re.S):
        rows = []
        for line in m.group(2).splitlines():
            r = re.match(r"^\s*\{\s*eng::(ROW_\w+),(.*)\}\s*,?\s*$", line)
            if not r:
                continue
            f = sc.split_fields(r.group(2))
            if len(f) == 7:
                key = None if f[0] == "NULL" else unquote(f[0])
                label, buttons = f[1], f[2]
            else:
                key, label, buttons = None, f[0], f[1]
            rows.append({
                "key": key,
                "label": label,
                "buttons": [int(x) for x in re.findall(r"-?\d+", buttons) if int(x) >= 0],
            })
        out.append((m.group(1), rows))
    return out


def free_texts(code, fs):
    m = re.search(r"static const eng::FreeText SAV_FREE\[NUM_FREE_TEXTS\] =\s*\{(.*?)\n\};", code, flags=re.S)
    if not m:
        return []
    return [fs[int(n)] for n in re.findall(r"FRAME_STRINGS\[(\d+)\]", m.group(1))]


def table_row(by_key, key):
    if key not in by_key:
        raise SystemExit("row or button key '%s' is not in OPTIONS" % key)
    return by_key[key]


def range_of(row):
    return range(row["min"], row["max"] + 1)


def keyed_texts(by_key, consts, key, value):
    """the texts a keyed button can show: its label, its choice, or each choice of a cycle"""
    row = table_row(by_key, key)
    if value == "SETTING_LABEL":
        return [row["label"]]
    if value == "SETTING_CURRENT":
        return [row["texts"][v - row["min"]] for v in range_of(row)]
    v = sc.evaluate(value, consts)
    return [row["texts"][v - row["min"]]]


def before_runtime(code, b, by_key, fs, arrays):
    """the texts a button shows in the before file, for the buttons set at run time"""
    if b in BEFORE_CYCLE_BUTTONS:
        m = re.search(r"FRAME_BUTTONS\[%d\]\.buttonString = ([^;]+);" % b, code)
        if not m:
            raise SystemExit("no run-time text for button %d" % b)
        rhs = m.group(1).strip()
        r = re.fullmatch(r"(\w+)\[(?:(\d+) \+ )?(?:\(int\))?(\w+)\]", rhs)
        if not r:
            raise SystemExit("unknown run-time text for button %d: %s" % (b, rhs))
        name, base, var = r.group(1), r.group(2), r.group(3)
        row = table_row(by_key, VAR_KEY[var])
        arr = fs if name == "FRAME_STRINGS" else arrays[name]
        if base is None:
            return [arr[v - row["min"]] for v in range_of(row)]
        return [arr[int(base) + v] for v in range_of(row)]
    if b in CARD_BUTTONS:
        if "FRAME_STRINGS[92 + " not in code:
            raise SystemExit("card text rule not found")
        return [fs[92], fs[93], fs[94]]          # Off, Shared, Game
    if b == SLOT_BUTTON:
        if 'sprintf(FRAME_STRINGS[42], "Slot %d", loadButtonSlot+1)' not in code:
            raise SystemExit("Auto Load Slot rule not found")
        row = table_row(by_key, "LoadButtonSlot")
        default = sc.c_constants()["LOADBUTTON_DEFAULT"]
        return [("Default" if v == default else "Slot %d" % (v + 1)) for v in range_of(row)]
    return None


def text_of(expr, fs, gpu):
    r = re.fullmatch(r"FRAME_STRINGS\[(\d+)\]", expr)
    if r:
        return [fs[int(r.group(1))]]
    r = re.fullmatch(r"GPU_PLUGIN_STRINGS\[(\d+)\]", expr)
    if r:
        return [gpu[int(r.group(1))]]
    raise SystemExit("unknown button text: " + expr)


def snapshot(code, side, by_key, consts):
    fs = string_array(code, "FRAME_STRINGS")
    gpu = string_array(code, "GPU_PLUGIN_STRINGS") or []
    arrays = {"LANG_STRINGS": string_array(code, "LANG_STRINGS") or [],
              "TEXTURE_FILTER_STRINGS": string_array(code, "TEXTURE_FILTER_STRINGS") or []}
    buttons = frame_buttons(code)

    def button_texts(b):
        ent = buttons[b]
        if side == "before":
            rt = before_runtime(code, b, by_key, fs, arrays)
            if rt is not None:
                return rt
            return text_of(ent["text"], fs, gpu)
        if ent["key"]:
            return keyed_texts(by_key, consts, ent["key"], ent["value"])
        if b in CARD_BUTTONS:
            if "cardText(" not in code:
                raise SystemExit("card text rule not found")
            c = b - CARD_BUTTONS[0]
            row = table_row(by_key, "Memcard%d" % c)
            file_row = table_row(by_key, "Memcard%dFile" % c)
            return [row["texts"][0], file_row["texts"][0], file_row["texts"][1]]
        return text_of(ent["text"], fs, gpu)

    out = {"tables": []}
    for name, rows in set_tables(code):
        rs = []
        for r in rows:
            if r["key"]:
                row = table_row(by_key, r["key"])
                if r["label"] == "NULL":
                    label = row["label"]
                else:
                    label = unquote(r["label"])
            else:
                label = None if r["label"] == "NULL" else fs[int(re.fullmatch(r"FRAME_STRINGS\[(\d+)\]", r["label"]).group(1))]
            rs.append({"label": label, "buttons": [button_texts(b) for b in r["buttons"]]})
        out["tables"].append({"name": name, "rows": rs})
    out["free"] = free_texts(code, fs)
    return out


def git_show(rev, path):
    res = subprocess.run(["git", "-C", str(REPO), "show", "%s:%s" % (rev, path)],
                         capture_output=True, text=True, encoding="utf-8")
    if res.returncode != 0:
        print("cannot read %s:%s: %s" % (rev, path, res.stderr.strip()))
        sys.exit(2)
    return res.stdout


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD~"
    for path in SHARED:
        if subprocess.run(["git", "-C", str(REPO), "diff", "--quiet", rev, "--", path]).returncode != 0:
            print("%s differs from %s: the table is read from the working tree, so it must match" % (path, rev))
            return 2
    consts = sc.c_constants()
    by_key = {r["key"]: r for r in sc.table()}
    before_code = strip_comments(git_show(rev, FILE))
    after_code = strip_comments((REPO / FILE).read_text(encoding="utf-8"))
    before = snapshot(before_code, "before", by_key, consts)
    after = snapshot(after_code, "after", by_key, consts)
    if before == after:
        n = sum(len(r["buttons"]) for t in after["tables"] for r in t["rows"])
        print("settings texts: %d rows, %d buttons: the same words before (%s) and after"
              % (sum(len(t["rows"]) for t in after["tables"]), n, rev))
        return 0
    for bt, at in zip(before["tables"], after["tables"]):
        for i, (br, ar) in enumerate(zip(bt["rows"], at["rows"])):
            if br != ar:
                print("%s row %d:\n  before %s\n  after  %s" % (at["name"], i, json.dumps(br), json.dumps(ar)))
    if before["free"] != after["free"]:
        print("free texts:\n  before %s\n  after  %s" % (before["free"], after["free"]))
    print("settings texts: DIFFERENT")
    return 1


if __name__ == "__main__":
    sys.exit(main())
