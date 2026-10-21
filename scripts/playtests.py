"""playtests.py [--all] -- the games to play for the open play-test requests, with commands.

Reads scripts/playtests.txt (library folder | what to do | why | how) and prints, game by game,
what to do in it, whether the game is in the library (C:/wiistation-lib/games, or
$WSX_LIBRARY/games), the recordings that already exist (scripts/autoinput/*_play.txt), and
for each request the command to paste:
  bash scripts/movie_capture.sh "<library folder>" NAME [options]   (a recording in Dolphin)
or, for "wii" requests, which app to play on the Wii (the "WiiStation (debug)" app logs the
session to sd:/wiistation/debuglogs/; tell the agent, which fetches and reads it).
--all also prints the reason for each request.
"""
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
LIBRARY = pathlib.Path(os.environ.get("WSX_LIBRARY", "C:/wiistation-lib")) / "games"


def slug(name):
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")


def main():
    args = sys.argv[1:]
    if any(a not in ("--all",) for a in args):
        sys.exit(__doc__.strip())
    games = {}
    for line in (HERE / "playtests.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 2:
            print(f"playtests.txt: skipped a line without 'game | what to do': {line}")
            continue
        parts += [""] * (4 - len(parts))
        games.setdefault(parts[0], []).append(parts[1:4])
    plays = sorted(p.name for p in (HERE / "autoinput").glob("*_play.txt"))
    for game, reqs in games.items():
        have = "in the library" if (LIBRARY / game).is_dir() else "NOT in the library"
        key = slug(game.split("(")[0].split("[")[0])
        recs = [p for p in plays if p.startswith(key[:12])]
        print(f"{game}  ({have}; recordings: {', '.join(recs) if recs else 'none'})")
        for what, why, how in reqs:
            print(f"  - {what}")
            if "--all" in args and why:
                print(f"      why: {why}")
            if how.strip().lower() == "wii":
                print('      on the Wii: play it in the "WiiStation (debug)" app, then tell the agent')
            else:
                name, _, opts = how.partition(" ")
                print(f'      bash scripts/movie_capture.sh "{game}" {name or slug(game)}{" " + opts if opts else ""}')
    print(f"\n{sum(len(r) for r in games.values())} requests in {len(games)} games.")


if __name__ == "__main__":
    main()
