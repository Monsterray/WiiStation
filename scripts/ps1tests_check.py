"""ps1tests_check.py RUN [RUN2] [--ref DIR] [--test NAME] [--all]

Compares a run's console output (RUN/tty.log, from scripts/chains/ps1tests.txt) with the
console output of the same JaCzekanski ps1-tests program on a real PlayStation
(C:/tools/ps1-tests/ref/<dir>_<test>.psx.log). A line is matched to the real one with the
same label (the line with its numbers taken out), so a value that is off shows as
"PS1 ... / emu ...", and a line the program never printed (it hung or gave up) as missing.
With RUN2, both runs are shown against the PS1, line by line, for an A/B of two cores.

Per test: SAME (every line as on the PS1), or the count of lines that differ or are missing.
--all prints the same lines too; --test NAME only that test.
"""
import pathlib, re, sys

REF = pathlib.Path("C:/tools/ps1-tests/ref")


def clean(line):
    return line.rstrip().removeprefix("% ")


def label(line):
    line = re.sub(r"0x[0-9a-fA-F]+|-?\d+(\.\d+)?|--CRASH--", "#", line)
    return re.sub(r"\s+", " ", line).strip()   # columns pad to the value's width


def refs(d):
    out = {}
    for p in sorted(d.glob("*.psx.log")):
        lines = [clean(l) for l in p.read_text(errors="replace").splitlines()]
        lines = [l for l in lines if l]
        out[p.name.removesuffix(".psx.log")] = lines
    return out


def sections(tty, ref):
    """the tty.log lines from each test's banner (its reference's first line) to the next"""
    lines = [clean(l) for l in tty.read_text(errors="replace").splitlines()]
    starts = sorted((i, name) for name, r in ref.items() for i, l in enumerate(lines) if l == r[0])
    out = {}
    for k, (i, name) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(lines)
        out[name] = [l for l in lines[i:end] if l]
    return out


def by_label(lines):
    d = {}
    for l in lines:
        d.setdefault(label(l), []).append(l)
    return d


def main():
    a = sys.argv[1:]
    ref_dir = pathlib.Path(a[a.index("--ref") + 1]) if "--ref" in a else REF
    only = a[a.index("--test") + 1] if "--test" in a else None
    show_all = "--all" in a
    runs = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] not in ("--ref", "--test"))]
    ref = refs(ref_dir)
    secs = [sections(pathlib.Path(r) / "tty.log", ref) for r in runs]
    for name, rl in ref.items():
        if only and only not in name:
            continue
        got = [s.get(name) for s in secs]
        heads = []
        for r, g in zip(runs, got):
            if g is None:
                heads.append(f"{pathlib.Path(r).name}: NOT RUN")
                continue
            gl = by_label(g)
            bad = sum(1 for l in rl if l not in gl.get(label(l), []))
            heads.append(f"{pathlib.Path(r).name}: " + ("SAME" if not bad else f"{bad}/{len(rl)} lines differ or missing"))
        print(f"== {name}: " + " | ".join(heads))
        if only is None and all(h.endswith("SAME") or h.endswith("NOT RUN") for h in heads) and not show_all:
            continue
        maps = [by_label(g) if g else {} for g in got]
        seen = {}
        for l in rl:
            k = label(l)
            n = seen.get(k, 0)
            seen[k] = n + 1
            emu = [(m.get(k, [])[n] if n < len(m.get(k, [])) else "(missing)") for m in maps]
            if not show_all and all(e == l for e in emu):
                continue
            print(f"   PS1  {l}")
            for r, e in zip(runs, emu):
                print(f"   {'=' if e == l else '*'} {pathlib.Path(r).name:<9} {e}")


if __name__ == "__main__":
    main()
