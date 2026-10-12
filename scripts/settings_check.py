"""settings_check.py [WRITTEN_SETTINGS_INI] -- the settings table against the docs and the examples.

    python scripts/settings_check.py
    python scripts/settings_check.py .runs/ini_defaults/settings.ini

The OPTIONS table in Gamecube/GamecubeMain.cpp is the one source. The checks:

  1. example_settings.ini has every table key, in the same [sections] and order.
     It has no other key. With a settings.ini that WiiStation wrote from its defaults,
     each example value is that default. Keys that WiiStation changes at run time are skipped.
  2. SETTINGS.md documents every table key. It documents no key that the table lacks.
     A key in a range counts, such as "`PadType1` to `PadType10`".
  3. A single-key row gives the table's min and max in its Values column.
  4. A key with a menu location in its SETTINGS.md Menu column has a label.
     A label means a menu location. Grid settings need no label: the ports and Co-Op
     are drawn as one grid, not as rows.
  5. A labelled row has one choice text for each value. It has one help line.

Exit 1 on any difference.
"""
import os
import pathlib, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import req_file  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = re.compile(r"^(FPS|PadType\d+|PadAssign\d+)$")
GRID = re.compile(r"^(PadType|PadAssign|PadLightgun|Coop)")
SETTING_TOKEN = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")


def evaluate(expr, consts):
    """an int from a C constant expression: numbers, known names, + - and parentheses"""
    text = re.sub(r"[A-Za-z_]\w*", lambda m: str(consts[m.group(0)]), expr.strip())
    if not re.fullmatch(r"[\d\s()+\-]+", text):
        raise ValueError("cannot evaluate: " + expr)
    return int(eval(text, {"__builtins__": {}}))


def c_constants():
    """{name: int} from the enums and the numeric #defines of Gamecube/wiiSXconfig.h"""
    s = (REPO / "Gamecube/wiiSXconfig.h").read_text(encoding="utf-8")
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    consts = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(-?\d+)\s*$", s, flags=re.M):
        consts[m.group(1)] = int(m.group(2))
    for m in re.finditer(r"enum\s*\w*\s*\{([^}]*)\}", s):
        nxt = 0
        for item in m.group(1).split(","):
            if not item.strip():
                continue
            name, _, expr = item.partition("=")
            if expr.strip():
                nxt = evaluate(expr, consts)
            consts[name.strip()] = nxt
            nxt += 1
    return consts


def split_fields(body):
    """the comma-separated fields of a C initializer; commas inside quotes or brackets stay"""
    out, cur, quoted, depth, i = [], "", False, 0, 0
    while i < len(body):
        ch = body[i]
        if quoted:
            cur += ch
            if ch == "\\":
                cur += body[i + 1]
                i += 1
            elif ch == '"':
                quoted = False
        elif ch == '"':
            quoted = True
            cur += ch
        elif ch in "([{":
            depth += 1
            cur += ch
        elif ch in ")]}":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def unquote(text):
    return text[1:-1] if text.startswith('"') else text


def table():
    """the value rows of OPTIONS: dicts with section, key, min, max (None for a text setting),
    label, choices (the count of choice texts, or None) and help. A NULL value starts a section."""
    consts = c_constants()
    s = (REPO / "Gamecube/GamecubeMain.cpp").read_text(encoding="utf-8")
    counts = {m.group(1): len(re.findall(r'"', m.group(2))) // 2
              for m in re.finditer(r"static const char\* const (OPT_\w+)\[\]\s*=\s*\{([^}]*)\}", s)}
    t = s[s.index("} OPTIONS[] ="):]
    t = t[:t.index("};")]

    def number(text):
        return None if text == "CONFIG_STRING_TYPE" else evaluate(text, consts)

    out, sec = [], None
    for line in t.splitlines():
        m = re.match(r'^\s*\{\s*"([^"]+)",(.*)\}\s*,?\s*$', line)
        if not m:
            continue
        f = split_fields(m.group(2))
        if f[0] == "NULL":
            sec = m.group(1)
            continue
        out.append({
            "section": sec, "key": m.group(1),
            "min": number(f[1]), "max": number(f[2]),
            "label": unquote(f[4]) if len(f) > 4 and f[4] != "NULL" else None,
            "choices": counts[f[5]] if len(f) > 5 and f[5] != "NULL" else None,
            "help": unquote(f[6]) if len(f) > 6 and f[6] != "NULL" else None,
        })
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


def doc_rows():
    """the rows of the SETTINGS.md tables whose first heading is Key: dicts with tokens (the
    setting keys of the Key cell), group (a range such as "A" to "B"), values and menu (or None)"""
    out, header = [], None
    for line in (REPO / "SETTINGS.md").read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            header = None
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-+:?", c) for c in cells):
            continue
        if header is None:
            if cells[0] == "Key":
                header = cells
            continue
        if len(cells) < 2:
            continue
        tokens = [t for t in re.findall(r"`([^`]+)`", cells[0]) if SETTING_TOKEN.match(t)]
        group = len(tokens) > 1 and (" to " in cells[0] or "..." in cells[0])
        menu = cells[header.index("Menu")] if "Menu" in header else None
        out.append({"tokens": tokens, "group": group, "values": cells[1], "menu": menu})
    return out


def doc_numbers(text):
    """the integers that a Values cell gives: "0 = Off, 1 = On", "`0`, `1`", "0 to 12, see ..." """
    out = []
    for item in text.replace("`", "").split(","):
        item = item.strip()
        left = item.split("=")[0].strip() if "=" in item else item
        m = re.fullmatch(r"(\d+) to (\d+)", left)
        if m:
            out += range(int(m.group(1)), int(m.group(2)) + 1)
        elif re.fullmatch(r"\d+", left):
            out.append(int(left))
    return out


def stem(key):
    return re.sub(r"\d+$", "", key)


def main():
    bad = 0
    rows = table()
    by_key = {r["key"]: r for r in rows}

    # 1. the example file
    ex = ini(REPO / "Gamecube/release/example_settings.ini")
    want = [(r["section"], r["key"]) for r in rows]
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
        written = {k: v for _, k, v in ini(req_file(sys.argv[1], 'settings ini'))}
        for _, k, v in ex:
            if k in written and not RUNTIME.match(k) and written[k] != v:
                print(f"{k}: example {v}, WiiStation's default {written[k]}")
                bad = 1

    # 2. SETTINGS.md: the keys
    doc = doc_rows()
    documented, groups = set(), set()
    for d in doc:
        documented.update(d["tokens"])
        if d["group"]:
            groups.update(stem(t) for t in d["tokens"])
    for t in sorted(documented):
        if t not in by_key:
            bad = 1
            print(f"SETTINGS.md documents {t}, which the table does not have")
    for r in rows:
        k = r["key"]
        if k in documented or (re.search(r"\d$", k) and stem(k) in groups):
            continue
        bad = 1
        print(f"{k}: the table has it, SETTINGS.md does not")

    # 3. SETTINGS.md: the range of each single-key row
    checked = 0
    for d in doc:
        if d["group"]:
            continue
        nums = doc_numbers(d["values"])
        for t in d["tokens"]:
            r = by_key.get(t)
            if r is None or r["min"] is None:
                continue
            checked += 1
            if not nums:
                bad = 1
                print(f"{t}: SETTINGS.md Values gives no numbers")
            elif (min(nums), max(nums)) != (r["min"], r["max"]):
                bad = 1
                print(f"{t}: SETTINGS.md gives {min(nums)} to {max(nums)}, "
                      f"the table {r['min']} to {r['max']}")

    # 4. menu: a labelled row is in the Menu column, a Menu entry has a label
    for d in doc:
        if d["menu"] is None:
            continue
        shown = not re.match(r"(none|not in the menu)", d["menu"], re.I)
        for t in d["tokens"]:
            r = by_key.get(t)
            if r is None or GRID.match(t):
                continue
            if shown and not r["label"]:
                bad = 1
                print(f"{t}: SETTINGS.md Menu says '{d['menu']}', the table has no label")
            if not shown and r["label"]:
                bad = 1
                print(f"{t}: the table label '{r['label']}', SETTINGS.md Menu says '{d['menu']}'")

    # 5. labels: one choice text per value, and a help line
    labelled = 0
    for r in rows:
        k = r["key"]
        if r["label"] is None:
            if r["choices"] or r["help"]:
                bad = 1
                print(f"{k}: choices or help without a label")
            continue
        labelled += 1
        if r["min"] is None:
            bad = 1
            print(f"{k}: a label on a text setting")
            continue
        if r["choices"] is None:
            bad = 1
            print(f"{k}: a label without choices")
        elif r["choices"] != r["max"] - r["min"] + 1:
            bad = 1
            print(f"{k}: {r['choices']} choice texts for {r['max'] - r['min'] + 1} values")
        if not r["help"]:
            bad = 1
            print(f"{k}: no help line")

    print(f"checked {len(rows)} keys, {checked} ranges, {labelled} menu labels")
    print("settings: ok" if not bad else "settings: DIFFERENT")
    return bad


if __name__ == "__main__":
    sys.exit(main())
