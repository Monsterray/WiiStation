#!/usr/bin/env python3
"""warn_summary.py build.log [--by file|kind|dir] [--kind -Wsomething] [--top N]

Summarise a WARN=1 build log (scripts/build.sh debug-warn / release-warn). GCC prints one
warning per line as "path:line:col: warning: text [-Wflag]", with notes and source excerpts in
between; this counts the warning lines only, deduplicates the same flag+file+line (LTO reports
some of them from several translation units), and groups them.

Default view is by warning kind, which is the one that says what kind of work is left: a
thousand -Wunused-parameter are one afternoon of nothing, while ten -Wreturn-type are ten
latent bugs. --by dir shows which subsystem owns them, --kind lists every site of one flag."""
import re, sys, collections

# GCC on this toolchain prints lowercased absolute Windows paths, with or without a column:
#   c:\projects\wiistation\gamecube\libgui\Frame.h:49:42: warning: ... [-Wunused-parameter]
#   c:\projects\wiistation\gpulib\gpu.h:32: warning: "SWAP16" redefined
LINE = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:]+):(?P<line>\d+):(?:(?P<col>\d+):)?\s+"
                  r"(?P<sev>warning|error|note):\s+(?P<text>.*?)\s*(?:\[(?P<flag>-W[^\]]+)\])?$")
ROOT = re.compile(r"^.*?[/\\]wiistation[/\\]", re.I)

def parse(path):
    out = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        m = LINE.match(raw.rstrip("\n"))
        if not m or m.group("sev") == "note":
            continue
        f = ROOT.sub("", m.group("file").replace("\\", "/"))
        out.append({"file": f, "line": int(m.group("line")), "sev": m.group("sev"),
                    "flag": m.group("flag") or "(no flag)", "text": m.group("text")})
    return out

def dedup(ws):
    seen, out = set(), []
    for w in ws:
        k = (w["file"], w["line"], w["flag"], w["text"])
        if k in seen:
            continue
        seen.add(k); out.append(w)
    return out

def main():
    path = sys.argv[1]
    by = sys.argv[sys.argv.index("--by") + 1] if "--by" in sys.argv else "kind"
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 25
    only = sys.argv[sys.argv.index("--kind") + 1] if "--kind" in sys.argv else None

    ws = parse(path)
    errors = [w for w in ws if w["sev"] == "error"]
    ws = dedup([w for w in ws if w["sev"] == "warning"])
    if only:
        ws = [w for w in ws if w["flag"] == only]
        for w in sorted(ws, key=lambda w: (w["file"], w["line"])):
            print(f'{w["file"]}:{w["line"]}: {w["text"]}')
        print(f"\n{len(ws)} sites of {only}")
        return

    print(f"errors: {len(errors)}    distinct warnings: {len(ws)}")
    if errors:
        for e in errors[:10]:
            print(f'  ERROR {e["file"]}:{e["line"]}: {e["text"]}')

    key = {"kind": lambda w: w["flag"],
           "file": lambda w: w["file"],
           "dir": lambda w: w["file"].split("/")[0] if "/" in w["file"] else "(root)"}[by]
    counts = collections.Counter(key(w) for w in ws)
    width = max((len(k) for k, _ in counts.most_common(top)), default=10)
    print(f"\nby {by}:")
    for k, n in counts.most_common(top):
        print(f"  {k:<{width}}  {n}")
    rest = len(ws) - sum(n for _, n in counts.most_common(top))
    if rest > 0:
        print(f"  {'(others)':<{width}}  {rest}")

if __name__ == "__main__":
    main()
