#!/usr/bin/env python3
"""library_check.py [LIB] -- check the game library (default C:/wiistation-lib, or WSX_LIBRARY).

A game is a folder under LIB/games/ that holds a boot file: a .cue (every FILE line must name
an existing file), a .ccd (needs .img and .sub of the same name), a .toc (its FILE/DATAFILE
names must exist), an .mdf (needs its .mds), a .pbp, .chd, .iso or a .cbn (a .cbn wins over
the .cue beside it, which only names its tracks: boot the .cbn), or an .exe (a PS1 test program).
Gex 2 and Gex 3 are .ccd sets; FF7 sits one folder down, so a game's key is its path under games/.

Checks, exit 1 on any problem:
  - every game's boot file and the files it names exist
  - every sd:/wiistation/isos/<key> line in scripts/chains/*.txt names a game in the library

Writes LIB/games.tsv (key, boot file, format, status) for reading by hand.
--keys prints the game keys only (profile.sh stage all).
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
LIB = os.environ.get("WSX_LIBRARY", "C:/wiistation-lib")
KEYS_ONLY = "--keys" in sys.argv   # print the game keys and stop (profile.sh uses it for "all")
for a in sys.argv[1:]:
    if not a.startswith("--"):
        LIB = a
GAMES = os.path.join(LIB, "games")
# in order of preference when a folder holds several (scripts/disc_convert.py makes one of each)
BOOT_EXT = (".cbn", ".cue", ".ccd", ".toc", ".mdf", ".pbp", ".chd", ".iso", ".exe")


def games():
    """Yield (key, folder, boot file) for every folder with a boot file."""
    for root, _dirs, files in os.walk(GAMES):
        boots = sorted(f for f in files if f.lower().endswith(BOOT_EXT))
        if not boots:
            continue
        key = os.path.relpath(root, GAMES).replace(os.sep, "/")
        # a folder with several cues (Dino Crisis 2 has one) is one game: take the first cue;
        # across formats, the first in BOOT_EXT order
        boot = next(b for ext in BOOT_EXT for b in boots if b.lower().endswith(ext))
        yield key, root, boot


def problems_of(folder, boot):
    """Return (format, [problem strings]) for one game."""
    path = os.path.join(folder, boot)
    low = boot.lower()
    if low.endswith(".cue"):
        text = open(path, "rb").read().decode("latin-1")
        named = re.findall(r'^\s*FILE\s+"(.*?)"', text, flags=re.M | re.I)
        bad = [n for n in named if not os.path.isfile(os.path.join(folder, n))]
        return "cue", ["cue %s names missing %s" % (boot, n) for n in bad] or []
    if low.endswith(".ccd"):
        base = os.path.splitext(path)[0]
        missing = [e for e in (".img", ".sub") if not os.path.isfile(base + e)]
        return "ccd", ["ccd %s is missing %s" % (boot, e) for e in missing]
    if low.endswith(".toc"):
        names = re.findall(r'(?:FILE|DATAFILE|AUDIOFILE)\s+"([^"]+)"', open(path, errors="replace").read())
        return "toc", ["toc %s names missing %s" % (boot, n) for n in names
                       if not os.path.isfile(os.path.join(folder, n))]
    if low.endswith(".mdf"):
        mds = os.path.splitext(boot)[0] + ".mds"
        return "mds", [] if os.path.isfile(os.path.join(folder, mds)) else ["mdf %s is missing %s" % (boot, mds)]
    for ext in (".cbn", ".pbp", ".chd", ".iso"):
        if low.endswith(ext):
            return ext[1:], [] if os.path.getsize(path) > 0 else ["empty %s" % boot]
    return "exe", [] if os.path.isfile(path) else ["missing %s" % boot]


def chain_keys():
    """The games the chain files name (sd:/wiistation/isos/<key>)."""
    keys = {}
    for cf in glob.glob(os.path.join(REPO, "scripts", "chains", "*.txt")):
        for line in open(cf, "rb").read().decode("latin-1").splitlines():
            m = re.match(r"^\d+\s+sd:/wiistation/isos/(.+?)\s*$", line.strip())
            m2 = re.match(r"^sd:/wiistation/isos/(.+?)\s*$", line.strip())
            k = (m or m2)
            if k:
                keys.setdefault(k.group(1), set()).add(os.path.basename(cf))
    return keys


def main():
    if not os.path.isdir(GAMES):
        print("no library at %s" % LIB, file=sys.stderr)
        return 1
    if KEYS_ONLY:
        for key, _folder, _boot in sorted(games()):
            print(key)
        return 0
    rows, bad = [], 0
    found = set()
    for key, folder, boot in sorted(games()):
        fmt, probs = problems_of(folder, boot)
        found.add(key)
        status = "ok" if not probs else "; ".join(probs)
        bad += bool(probs)
        rows.append((key, boot, fmt, status))
    for key, files in sorted(chain_keys().items()):
        if key not in found:
            bad += 1
            rows.append((key, "-", "-", "named by %s but not in the library" % ", ".join(sorted(files))))
    with open(os.path.join(LIB, "games.tsv"), "w") as f:
        f.write("key\tboot\tformat\tstatus\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    for r in rows:
        print("%-7s %-40s %s" % (r[2], r[0], r[3] if r[3] != "ok" else "ok"))
    print("%d game(s), %d problem(s); table at %s" % (len(rows), bad, os.path.join(LIB, "games.tsv")))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
