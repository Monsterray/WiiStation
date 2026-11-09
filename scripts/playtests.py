"""playtests.py [--all] [--game TEXT] -- the play tests to do, with the command for each.

Reads scripts/playtests.txt (library folder | what to do | why | how) and prints the open
requests, numbered, with the command to paste:
  bash scripts/movie_capture.sh "<library key>" NAME [options]   (a recording in Dolphin)
or, for "wii" requests, the app to play on a Wii (the "WiiStation (debug)" app logs the
session to sd:/wiistation/debuglogs/; tell the agent, which fetches and reads it).

A request is done once its recording exists (scripts/autoinput/NAME_play.txt); done ones are
listed at the end. The library folder is matched to a library key (the game's folder under
$GAME_LIBRARY/games, as games.tsv lists it): the whole key, or a
key that ends with it (Formats/..., a disc folder), so the pasted command finds exactly one
game. --all also prints the reason for each request; --game keeps the games whose name
contains TEXT.
"""
import argparse
import os
import pathlib
import subprocess
import sys

import tools

HERE = pathlib.Path(__file__).resolve().parent
LIB = pathlib.Path(tools.get("GAME_LIBRARY"))


def slug(name):
    return "_".join("".join(c if c.isalnum() else " " for c in name.lower()).split())


def library_keys():
    """The bootable library keys (games.tsv, made by library_check.py; made now if missing)."""
    tsv = LIB / "games.tsv"
    if not tsv.is_file():
        subprocess.run([sys.executable, str(HERE / "library_check.py")], capture_output=True)
    keys = []
    if tsv.is_file():
        for line in tsv.read_text(encoding="utf-8", errors="replace").splitlines()[1:]:
            cols = line.split("\t")
            if len(cols) >= 4 and cols[3].strip() == "ok":
                keys.append(cols[0])
    return keys


def resolve(folder, keys):
    """The one library key for a request's folder, or (None, why)."""
    f = folder.replace("\\", "/").strip("/")
    exact = [k for k in keys if k.lower() == f.lower()]
    if exact:
        return exact[0], ""
    tail = [k for k in keys if k.lower().endswith("/" + f.lower())]
    if len(tail) == 1:
        return tail[0], ""
    inside = [k for k in keys if k.lower().startswith(f.lower() + "/")]   # a multi-disc folder
    if inside:
        return None, f"a folder of {len(inside)} discs: name one ({inside[0]}, ...)"
    return None, "not in the library" if not tail else f"matches {len(tail)} library games"


def read_requests():
    path = HERE / "playtests.txt"
    if not path.is_file():
        sys.exit(f"{path}: missing")
    reqs = []
    for n, line in enumerate(path.read_text(encoding="utf-8-sig", errors="replace").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|", 3)]   # the last field may hold a '|'
        if len(parts) < 2 or not parts[0] or not parts[1]:
            print(f"playtests.txt:{n}: skipped (needs at least 'game | what to do'): {line.strip()[:60]}")
            continue
        parts += [""] * (4 - len(parts))
        reqs.append(dict(line=n, game=parts[0], what=parts[1], why=parts[2], how=parts[3]))
    return reqs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true", help="also print why each test is wanted")
    ap.add_argument("--game", default="", help="only games whose name contains this")
    a = ap.parse_args()
    keys = library_keys()
    recordings = {p.name[:-len("_play.txt")] for p in (HERE / "autoinput").glob("*_play.txt")}
    todo, done = [], []
    for r in read_requests():
        if a.game.lower() not in r["game"].lower():
            continue
        how = r["how"].strip()
        if how.lower() == "wii":
            r["cmd"] = 'on the Wii: play it in the "WiiStation (debug)" app, then tell the agent'
            todo.append(r)
            continue
        name, _, opts = how.partition(" ")
        name = name or slug(r["game"])
        key, why = resolve(r["game"], keys)
        if name in recordings:
            r["name"] = name
            done.append(r)
            continue
        if key is None:
            r["cmd"] = f"cannot record yet: {r['game']} is {why}"
        else:
            r["cmd"] = f'bash scripts/movie_capture.sh "{key}" {name}' + (f" {opts.strip()}" if opts.strip() else "")
        todo.append(r)

    if not todo and not done:
        print("no play tests requested" + (f" for '{a.game}'" if a.game else ""))
        return
    for i, r in enumerate(todo, 1):
        print(f"{i:2}. {r['game']}: {r['what']}")
        if a.all and r["why"]:
            print(f"      why: {r['why']}")
        print(f"      {r['cmd']}")
    if done:
        print(f"\nRecorded already ({len(done)}): " + ", ".join(f"{r['name']} ({r['game']})" for r in done))
    print(f"\n{len(todo)} to do, {len(done)} done. Replay one: bash scripts/movie_capture.sh --play NAME")


if __name__ == "__main__":
    main()
