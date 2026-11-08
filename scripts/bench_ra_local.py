#!/usr/bin/env python3
"""bench_ra_local.py -- Local mode (Achievements = 4) on the bench Wii (Docs/ACHIEVEMENTS.md, "Local mode").

    python "$WII_BENCH" add --cwd <repo> --timeout 900 \
        -- python scripts/bench_ra_local.py NAME [--fixture] [--dol debug|release|PATH]

A wii-bench queue job (it refuses to run directly, and refuses the production Wiis). It runs
scripts/chains/ach_local_bench.txt through wii_lab.py: Spyro twice in Local mode, Guest alone,
AchievementSpectator=1. The log shows where the set came from and every request decision.

  (default)   a real download: the DOL needs the HTTPS stack (build_ogc3/deps/ra_https), tls_seed.bin,
              cacert.pem and an account on the card (ra_setup.py --account FILE: sent unopened, never
              read here). The first game downloads and saves the set, the second loads it from the
              card. Refuses when the card already has a saved set for Spyro's hash. Afterwards the job
              removes what it made (the saved set, the game's journal, its unlock pictures: only names
              that were not on the card before). The account file itself changes as always (the first
              password login leaves User= and Token= in it).
  --fixture   a made-up set for Spyro's disc hash (tests/fixtures/ach_local, from
              scripts/ra_cache_fixture.py; Alpha at 200 frames, Beta at 400) is put in the cache for
              the run and taken out again: it proves Local loads from the card with no network, no
              account and no HTTPS stack. Refuses when the card already has a saved set for the hash.
"""
import json
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wii_targets import PY, REPO, bench_wii, card_ls, hbc, hbc_ready, parser  # noqa: E402

HASH = "3e9eb765a7b2912e60c30f363f0a3574"   # Spyro the Dragon (NTSC-U), as the log's "disc hash" line
CACHE = "sd:/wiistation/achievements/ra_cache"
NAMES = [f"gameid-{HASH}.json", f"sets-{HASH}.json"]
FIXTURE = REPO / "tests/fixtures/ach_local/achievements/ra_cache"


ACH = "sd:/wiistation/achievements"


def ls_all(wii, folder):
    """[(name, type)] of a card folder (files and folders: card_ls lists files only); [] when missing."""
    for _ in range(6):   # right after the lab the card can answer errno 19 (no device) for a few seconds
        r = hbc(wii, "ls", "--json", folder, timeout=60)
        if r.rc == 0 or "errno 19" not in r.text:
            break
        time.sleep(5)
    try:
        return [(e["name"], e["type"]) for e in json.loads(r.out)] if not r.rc else []
    except (ValueError, KeyError, TypeError):
        return []


def snapshot(wii):
    """What the run may add: the cache, the Local journals and the picture folders (names only)."""
    return {f: set(ls_all(wii, f"{ACH}/{f}")) for f in ("ra_cache", "ra_local", "shots")}


def remove_new(wii, before, after):
    """Take away what the run made (a new journal, new pictures, the saved set), nothing that was there before."""
    for folder, entries in after.items():
        for name, kind in sorted(entries - before[folder]):
            path = f"{ACH}/{folder}/{name}"
            if kind != "f":
                for sub, _ in ls_all(wii, path):
                    hbc(wii, "rm", f"{path}/{sub}", timeout=60)
            print(("removed " if hbc(wii, "rm", path, timeout=60).rc == 0 else "COULD NOT REMOVE ") + path, flush=True)


def main():
    ap = parser(__doc__)
    ap.add_argument("name", help="the run's folder: .runs/NAME")
    ap.add_argument("--fixture", action="store_true", help="a made-up set in the cache for the run (see above)")
    ap.add_argument("--dol", default="debug", help="debug, release or a DOL's path (default debug)")
    a = ap.parse_args()
    wii = bench_wii("bench_ra_local.py")
    puts = []
    before = snapshot(wii)
    if not a.fixture:   # a real download needs an account and the TLS files on the card: names only, nothing is read
        have = {n for n, _ in ls_all(wii, ACH)}
        accounts = {n for n, _ in ls_all(wii, f"{ACH}/ra")}
        print("card has: " + ", ".join(sorted(have & {"tls_seed.bin", "cacert.pem", "ra_account.ini", "utc_offset.txt"})) +
              " | ra/: " + ", ".join(sorted(accounts)), flush=True)
        if not ({"tls_seed.bin", "cacert.pem"} <= have and ("ra_account.ini" in have or accounts)):
            sys.exit("the card lacks tls_seed.bin, cacert.pem or an account (ra_setup.py): nothing was sent")
        if any(n in {x for x, _ in before["ra_cache"]} for n in NAMES):
            sys.exit(f"the card already has a saved set for Spyro in {CACHE}: a download would not show (remove it first, on purpose)")
    if a.fixture:
        hbc(wii, "rm", "sd:/wiistation/achievements/ra_local/9000001.unlocks", timeout=60)   # a run of the fixture starts with an empty journal
        have = card_ls(wii, CACHE) or {}
        if any(n in have for n in NAMES):
            sys.exit(f"the card already has a saved set for Spyro in {CACHE}: not replacing it (remove it first, on purpose)")
        for n in NAMES:
            if not (FIXTURE / n).is_file():
                sys.exit(f"{FIXTURE / n} is missing: scripts/ra_cache_fixture.py ... --out tests/fixtures/ach_local")
            puts += ["--put", f"{FIXTURE / n}=achievements/ra_cache/{n}"]
    r = subprocess.run([PY, str(REPO / "scripts/wii_lab.py"), a.name, str(REPO / "scripts/chains/ach_local_bench.txt"),
                        "--dol", a.dol, "--timeout", "700", *puts], cwd=REPO)
    print("wii_lab exit", r.returncode, flush=True)
    if a.fixture:
        if not hbc_ready(wii, 120):
            print(f"HBC did not answer: the made-up set is STILL in {CACHE} ({', '.join(NAMES)}): remove it")
        else:
            for n in NAMES:
                hbc(wii, "rm", f"{CACHE}/{n}", timeout=60)
            hbc(wii, "rm", "sd:/wiistation/achievements/ra_local/9000001.unlocks", timeout=60)   # the made-up game's journal
            for i in (1, 2, 3):   # and the pictures its unlocks saved
                hbc(wii, "rm", f"sd:/wiistation/achievements/shots/9000001/{i}.png", timeout=60)
            hbc(wii, "rm", "sd:/wiistation/achievements/shots/9000001", timeout=60)
            print("the made-up set, its journal and its pictures are taken out again")
    if not a.fixture:
        if not hbc_ready(wii, 120):
            print("HBC did not answer: the set, journal and pictures this run made are STILL on the card (ra_cache, ra_local, shots)")
        else:
            remove_new(wii, before, snapshot(wii))
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
