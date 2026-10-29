#!/usr/bin/env python3
"""bench_ra_mii.py -- Official mode with Local players on the bench Wii (Docs/MII_PLAN.md step 4c).

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation --timeout 1500 \\
        -- C:/Python312/python.exe scripts/bench_ra_mii.py NAME [--account FILE --ra-name dev] [--dol debug|release|PATH]

A wii-bench queue job (it refuses to run directly, and refuses the production Wiis). It runs
scripts/chains/ach_official_ra.txt through wii_lab.py: Spyro in spectator mode (no unlock reaches
an account), seat 1 a made-up Mii linked to achievements/ra/dev.ini (so it plays Official with
that account), seat 2 "#1", the console's first Mii, earning Local. The log has the account's
login, the set, the Local players, and achdump's page players. A real unlock needs real play.

  --account FILE --ra-name NAME   first puts FILE on the card as achievements/ra/NAME.ini (ra_setup.py:
                                  never opened here, never replaces a file that is there)
  The test players.ini (the link) goes on the card for the run; the card's own is saved first and
  put back afterwards (removed when there was none).
  The DOL needs the HTTPS stack (build_ogc3/deps/ra_https) or the login says "No secure connection".
"""
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wii_targets import PY, REPO, bench_wii, card_ls, existing_file, hbc, hbc_ready, parser  # noqa: E402

LINK_KEY = "ffffffff00000001"   # the made-up Official Mii of ach_official_ra.txt


def main():
    ap = parser(__doc__)
    ap.add_argument("name", help="the run's folder: .runs/NAME")
    ap.add_argument("--account", type=existing_file, help="an account file to put as achievements/ra/NAME.ini first")
    ap.add_argument("--ra-name", default="dev", help="that account's name (default dev, the one the chain links)")
    ap.add_argument("--dol", default="debug", help="debug, release or a DOL's path (default debug)")
    a = ap.parse_args()
    wii = bench_wii("bench_ra_mii.py")
    out = REPO / ".runs" / f"{a.name}_card"
    out.mkdir(parents=True, exist_ok=True)

    if a.account:
        r = subprocess.run([PY, str(REPO / "scripts/ra_setup.py"), "--account", str(a.account), "--ra-name", a.ra_name], cwd=REPO)
        if r.returncode:
            sys.exit(f"ra_setup.py failed ({r.returncode})")

    link = out / "players_test.ini"
    link.write_text(f'[Accounts]\n{LINK_KEY} = "{a.ra_name}"\n', newline="\n")
    backup = out / "players_card.ini"
    had = "players.ini" in (card_ls(wii, "sd:/wiistation") or {})
    if had and hbc(wii, "get", "sd:/wiistation/players.ini", backup, timeout=60).rc:
        sys.exit("could not save the card's players.ini; not starting")
    r = subprocess.run([PY, str(REPO / "scripts/wii_lab.py"), a.name, str(REPO / "scripts/chains/ach_official_ra.txt"),
                        "--dol", a.dol, "--put", f"{link}=players.ini", "--timeout", "600"], cwd=REPO)
    print("wii_lab exit", r.returncode, flush=True)
    if not hbc_ready(wii, 120):
        print(f"HBC did not answer: the card's players.ini is NOT back (the saved copy is {backup})")
    elif had:
        print("the card's players.ini is back" if hbc(wii, "put", backup, "sd:/wiistation/players.ini", timeout=60).rc == 0
              else f"could not put players.ini back: it is in {backup}")
    else:
        hbc(wii, "rm", "sd:/wiistation/players.ini", timeout=60)
        print("the test players.ini is removed (the card had none)")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
