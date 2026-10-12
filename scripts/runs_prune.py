#!/usr/bin/env python3
"""runs_prune.py [--apply] [--keep-days N] -- thin out .runs/ and keep what tracks progress.

Policy (user, 2026-10-12: "keep important runs from major and some minor generations"):
  * every run keeps its text: run.info, run.log, perf.log, tables, notes, sheet.png and any
    other file under 1 MB -- what wsx.sh table/compare and the regression checks read;
  * landmark runs keep everything: per minor version (Major.Minor; runs from before the version
    string are grouped by day), the chain run with the most games, the latest on a tie;
    and every run from the last --keep-days days (default 2), the work in progress;
  * every other run loses its bulk: frames/, boot.dol, *.wav/*.flac, vram*.bin and any other
    file of 1 MB or more.
Runs filed with scripts/baseline_add.py are already copied to baselines/ and are not touched.
Without --apply it only prints what it would delete. Landmarks are listed in .runs/LANDMARKS.txt.
"""
import collections
import os
import pathlib
import re
import shutil
import sys
import time

RUNS = pathlib.Path(__file__).resolve().parents[1] / ".runs"
BIG = 1 << 20
VERSION = re.compile(rb"\x00([45]\.\d{1,2}\.\d{1,2})\x00")


def version(run, cache={}):
    dol = run / "boot.dol"
    if dol.exists():
        st = dol.stat()
        k = (st.st_size, int(st.st_mtime))
        if k not in cache:
            m = VERSION.search(dol.read_bytes())
            cache[k] = m.group(1).decode() if m else None
        if cache[k]:
            return ".".join(cache[k].split(".")[:2])
    return "day " + time.strftime("%Y-%m-%d", time.localtime(when(run)))


def when(run):
    """The run's time: its run.info/run.log (pruning changes the folder's own time, never these)."""
    for f in ("run.info", "run.log"):
        if (run / f).exists():
            return (run / f).stat().st_mtime
    return run.stat().st_mtime


def games(run):
    """How many games a chain run played (its per-game vsig dumps), 0 for a single run."""
    info = run / "run.info"
    if not info.exists() or "chains/" not in info.read_text(errors="replace"):
        return 0
    return max(1, len(list(run.glob("vsig_*.bin"))))


def bulk(run):
    for p in run.rglob("*"):
        if p.is_dir() and p.name == "frames":
            yield p
        elif p.is_file() and "frames" not in p.parts[len(run.parts):] and (
                p.name == "boot.dol" or p.suffix in (".wav", ".flac") or p.name.startswith("vram")
                or p.stat().st_size >= BIG):
            yield p


def size(p):
    return sum(f.stat().st_size for f in p.rglob("*") if f.is_file()) if p.is_dir() else p.stat().st_size


def main():
    apply = "--apply" in sys.argv
    days = int(sys.argv[sys.argv.index("--keep-days") + 1]) if "--keep-days" in sys.argv else 2
    runs = [r for r in RUNS.iterdir() if r.is_dir() and ((r / "run.info").exists() or (r / "run.log").exists())]
    recent = time.time() - days * 86400
    best = {}
    for r in runs:
        g = games(r)
        if g:
            v = version(r)
            key = (g, when(r))
            if v not in best or key > best[v][0]:
                best[v] = (key, r)
    landmarks = {r for _, r in best.values()} | {r for r in runs if when(r) >= recent}
    freed, per = 0, collections.Counter()
    for r in runs:
        if r in landmarks:
            continue
        v = version(r)   # before boot.dol goes
        for p in list(bulk(r)):
            n = size(p)
            freed += n
            per[v] += n
            if apply:
                shutil.rmtree(p) if p.is_dir() else p.unlink()
    lines = [f"{v:16} {r.name}  ({k[0]} games)" for v, (k, r) in sorted(best.items())]
    if apply:
        (RUNS / "LANDMARKS.txt").write_text("Kept whole by runs_prune.py (per minor version: the "
                                            "largest chain run)\n" + "\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"{len(landmarks)} landmark/recent runs kept whole; {len(runs) - len(landmarks)} thinned")
    for v, n in sorted(per.items()):
        print(f"  {v:16} {n / 1e9:6.2f} GB")
    print(f"{'freed' if apply else 'would free'} {freed / 1e9:.1f} GB")


if __name__ == "__main__":
    main()
