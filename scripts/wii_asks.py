"""wii_asks.py -- the list of things to try on the Wii, shown in HBC as the debug app's description.

    python scripts/wii_asks.py list
    python scripts/wii_asks.py add GAME "WHAT TO DO" ["WHAT TO LOOK FOR"]
    python scripts/wii_asks.py rm ID [ID ...]
    python scripts/wii_asks.py text          # what HBC will show

The list is scripts/wii_asks.txt, one ask per line: "ID | GAME | WHAT TO DO | WHAT TO LOOK FOR".
IDs are never reused. `wii_install.py --play-build` writes the list into the "WiiStation (debug)"
meta.xml (long_description), so install the play build again after a change (only meta.xml is sent).
"""
import pathlib
import sys
from xml.sax.saxutils import escape

FILE = pathlib.Path(__file__).with_name("wii_asks.txt")
HEADER = "# WiiStation (debug): what to try on the Wii. One ask per line: ID | GAME | WHAT TO DO | WHAT TO LOOK FOR (scripts/wii_asks.py)\n"


def load():
    asks, last = [], 0
    if FILE.exists():
        for line in FILE.read_text(encoding="utf-8").splitlines():
            if line.startswith("#!last "):
                last = int(line.split()[1])
            elif line.strip() and not line.startswith("#"):
                f = [p.strip() for p in line.split("|")] + ["", "", ""]
                asks.append({"id": int(f[0]), "game": f[1], "do": f[2], "look": f[3]})
    return asks, max([last] + [a["id"] for a in asks])


def save(asks, last):
    for a in asks:
        for k in ("game", "do", "look"):
            if "|" in a[k] or "\n" in a[k]:
                sys.exit(f"no '|' or line break in an ask: {a[k]!r}")
    lines = [HEADER, f"#!last {last}\n"] + [
        f"{a['id']} | {a['game']} | {a['do']} | {a['look']}\n".replace(" | \n", "\n") for a in asks]
    FILE.write_text("".join(lines), encoding="utf-8", newline="\n")


def text():
    """The long_description: the asks, numbered, then the logs line"""
    asks, _ = load()
    if not asks:
        return "Nothing to try right now. Play as usual: this build logs to sd:/wiistation/debuglogs."
    out = ["Please try these, then say which number you did:", ""]
    for a in asks:
        out.append(f"{a['id']}. {a['game']}: {a['do']}" + (f" Look for: {a['look']}" if a["look"] else ""))
    out += ["", "This build logs to sd:/wiistation/debuglogs."]
    return "\n".join(out)


def meta_xml(s):
    """meta.xml text with its long_description replaced by text()"""
    a, b = s.index("<long_description>") + len("<long_description>"), s.index("</long_description>")
    return s[:a] + "\n" + escape(text()) + "\n   " + s[b:]


def main(argv):
    asks, last = load()
    cmd = argv[1] if len(argv) > 1 else "list"
    if cmd == "add" and len(argv) in (4, 5):
        last += 1
        asks.append({"id": last, "game": argv[2], "do": argv[3], "look": argv[4] if len(argv) == 5 else ""})
        save(asks, last)
        print(f"added {last}")
    elif cmd == "rm" and len(argv) > 2:
        ids = {int(x) for x in argv[2:]}
        missing = ids - {a["id"] for a in asks}
        if missing:
            sys.exit(f"no ask {sorted(missing)}")
        save([a for a in asks if a["id"] not in ids], last)
        print(f"removed {sorted(ids)}")
    elif cmd == "list":
        for a in asks:
            print(f"{a['id']:3}  {a['game']}: {a['do']}" + (f"  [look for: {a['look']}]" if a["look"] else ""))
    elif cmd == "text":
        print(text())
    else:
        sys.exit(__doc__)
    if cmd in ("add", "rm"):
        print("now: queue scripts/wii_install.py --play-build (and --lrwii --play-build) to show it in HBC")


if __name__ == "__main__":
    main(sys.argv)
