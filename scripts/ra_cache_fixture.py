#!/usr/bin/env python3
"""ra_cache_fixture.py -- a made-up RetroAchievements set for Local mode's card cache (test runs only).

    C:/Python312/python.exe scripts/ra_cache_fixture.py SETFILE --hash HEX32 --game-id N --out DIR

SETFILE is a Custom-mode definition file (Docs/ACHIEVEMENTS.md, "Definition file"). The script
writes DIR/achievements/ra_cache/gameid-<hash>.json and sets-<hash>.json, the two answers Local
mode keeps on the card (Gamecube/ach_ra_cache.h), in the shape the server gives them. Put DIR on a
test card with `wsx.sh chain ... --card DIR`: Local mode then loads that "official" set with no
network and no account, which is how a Dolphin run shows that Local mode works from the card and
sends nothing. The hash is the one the log prints ("Achievements: disc hash ...").

Nothing here talks to a server or reads an account.
"""
import argparse
import json
import pathlib
import re
import sys


def parse_set(path):
    title, games, achievements = "", [], []
    for raw in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line[0] in "#;":
            continue
        if line.startswith("Game="):
            games.append(line[5:])
        elif line.startswith("Title="):
            title = line[6:]
        else:
            f = line.split("|", 5)
            if len(f) < 3 or not f[0].isdigit():
                sys.exit("bad line: " + line)
            f += [""] * (6 - len(f))
            kind = {"progression": "progression", "missable": "missable", "win": "win_condition"}.get(f[4], "")
            achievements.append({
                "ID": int(f[0]), "Title": f[1], "Description": f[5],
                "Flags": 3, "Points": int(f[3] or 0), "MemAddr": f[2], "Author": "fixture",
                "BadgeName": "%05d" % int(f[0]), "Created": 1700000000, "Modified": 1700000000,
                "Type": kind, "Rarity": 12.5, "RarityHardcore": 6.25,
            })
    return title or "Fixture", achievements


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("setfile")
    ap.add_argument("--hash", required=True, help="the disc's RetroAchievements hash, 32 hex digits")
    ap.add_argument("--game-id", type=int, required=True, help="the game id to give the set (made up: 9000000 and up)")
    ap.add_argument("--out", required=True, help="the folder to write achievements/ra_cache/ in")
    a = ap.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{32}", a.hash):
        sys.exit("--hash must be 32 hex digits")
    title, achievements = parse_set(a.setfile)
    h = a.hash.lower()
    icon = "https://media.retroachievements.org/Images/%06d.png" % (a.game_id % 1000000)
    sets = {
        "Success": True, "GameId": a.game_id, "Title": title, "ConsoleId": 12, "ImageIconUrl": icon,
        "RichPresenceGameId": a.game_id, "RichPresencePatch": "",
        "Sets": [{"AchievementSetId": a.game_id, "GameId": a.game_id, "Title": title, "Type": "core",
                  "ImageIconUrl": icon, "Achievements": achievements, "Leaderboards": []}],
    }
    out = pathlib.Path(a.out) / "achievements" / "ra_cache"
    out.mkdir(parents=True, exist_ok=True)
    (out / ("gameid-%s.json" % h)).write_text(json.dumps({"Success": True, "GameID": a.game_id}, separators=(",", ":")))
    (out / ("sets-%s.json" % h)).write_text(json.dumps(sets, separators=(",", ":")))
    print("wrote %s: %d achievements for game %d" % (out, len(achievements), a.game_id))


if __name__ == "__main__":
    main()
