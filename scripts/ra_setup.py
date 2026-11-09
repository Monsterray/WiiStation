"""ra_setup.py [--seed] [--account FILE [--ra-name NAME]] [--production | --lrwii] [--dry-run] -- the per-Wii files
Official mode (RetroAchievements) needs, put on the Wii's card through HBC.

  --seed          sd:/wiistation/achievements/tls_seed.bin: 64 random bytes (os.urandom), this
                  Wii's own TLS entropy seed. Made in a private temporary folder, sent, overwritten
                  with zeros and deleted; never printed, never kept on the PC.
  --account FILE  sd:/wiistation/achievements/ra_account.ini from FILE (User= and Password=, or
                  User= and Token=). Sent as it is: this script never opens it (only its size is
                  looked at), and nothing of it is printed. After the first login WiiStation keeps
                  User= and Token= there and drops the password.
  --ra-name NAME  with --account: the file goes to sd:/wiistation/achievements/ra/NAME.ini instead,
                  an account a Mii can be linked to (Players, Edit, Account). NAME: letters, digits,
                  _ and -, under 32 characters. Same rules as above: sent unopened, never replaced.

Neither file is ever replaced: if the Wii has one already, it is left as it is and the script
says so (a seed moves on by itself as it is used; a token file is the login). To start again,
delete the file on the Wii first, on purpose.

The CA bundle (cacert.pem) is not here: it ships with every install (wii_install.py), pinned
(scripts/ra_ca_bundle.py). The clock needs nothing: the Wii learns its time zone from the
server's Date header (sd:/wiistation/achievements/utc_offset.txt).

Which Wii:
  (default)       the bench Wii, as a bench-queue job:
                    python "$WII_BENCH" add --cwd <repo> \\
                        -- python scripts/ra_setup.py --seed --account examples/ra_account.ini
  --production    Monty's everyday Wii (PRODUCTION_WII in wii_targets.py) and
  --lrwii         the living-room Wii (LRWII in wii_targets.py): only when he asks, run directly (not queued).
                  HBC must be idle on the TV (no app running): checked first, with a screenshot.
                  The script writes these two files under sd:/wiistation/achievements/ and nothing
                  else; it reads only that folder's file names.
--dry-run says what it would do and talks to no Wii (with --production, it still checks HBC).
"""
import argparse
import os
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import LRWII, PRODUCTION_WII, bench_wii, card_ls, hbc, production_ready  # noqa: E402

FOLDER = "sd:/wiistation/achievements"
SEED_BYTES = 64
ACCOUNT_MAX = 4096
NAME_OK = re.compile(r"[A-Za-z0-9_-]{1,31}")   # an account file's name (Gamecube/ach_account.h accepts more; this makes only these)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", action="store_true", help="a new TLS seed, when the Wii has none")
    ap.add_argument("--account", type=pathlib.Path, help="the account file to send, when the Wii has none")
    ap.add_argument("--ra-name", metavar="NAME", help="with --account: put it in achievements/ra/NAME.ini, for a Mii's link")
    ap.add_argument("--production", action="store_true", help="the everyday Wii (on request only)")
    ap.add_argument("--lrwii", action="store_true", help="the living-room Wii (on request only)")
    ap.add_argument("--dry-run", action="store_true")
    o = ap.parse_args()
    if not o.seed and not o.account:
        ap.error("nothing to do: --seed and/or --account FILE")
    if o.production and o.lrwii:
        ap.error("one Wii at a time")
    if o.ra_name is not None:
        if not o.account:
            ap.error("--ra-name goes with --account FILE")
        if not NAME_OK.fullmatch(o.ra_name):
            ap.error("--ra-name: letters, digits, _ and -, under 32 characters")
    if o.account:
        try:
            size = o.account.stat().st_size   # the only thing looked at
        except OSError:
            sys.exit(f"{o.account}: no such file")
        if not 0 < size <= ACCOUNT_MAX:
            sys.exit(f"{o.account}: {size} bytes, not an account file")
    prod = PRODUCTION_WII if o.production else LRWII if o.lrwii else None
    if prod:
        if os.environ.get("WII_BENCH_JOB_START"):
            sys.exit("--production/--lrwii is run directly, never as a bench queue job")
        production_ready(prod)   # exits unless HBC is idle; saves a screenshot
        wii = prod
    elif o.dry_run:
        wii = None
    else:
        wii = bench_wii("ra_setup.py")

    have = (card_ls(wii, FOLDER) or {}) if wii else {}
    have_ra = (card_ls(wii, FOLDER + "/ra") or {}) if wii and o.ra_name else {}
    print(f"{FOLDER} on {wii or '(no Wii: dry run)'}: "
          f"{', '.join(sorted(n for n in have if n in ('tls_seed.bin', 'ra_account.ini', 'cacert.pem', 'utc_offset.txt'))) or 'none of the Official files'}")

    def put(local, name):
        if o.dry_run:
            print(f"would put {name}")
            return True
        r = hbc(wii, "put", local, f"{FOLDER}/{name}", timeout=120)   # hbc.py put makes the folder
        print(f"{name}: {'sent' if r.rc == 0 else 'FAILED (exit %d)' % r.rc}")
        return r.rc == 0

    ok = True
    if o.seed:
        if "tls_seed.bin" in have:
            print("tls_seed.bin: there already; left as it is")
        else:
            with tempfile.TemporaryDirectory() as d:   # private to this user
                p = pathlib.Path(d) / "tls_seed.bin"
                try:
                    p.write_bytes(os.urandom(SEED_BYTES))
                    ok &= put(p, "tls_seed.bin")
                finally:
                    p.write_bytes(bytes(SEED_BYTES))   # the bytes do not stay on the PC's disk
                    p.unlink()
    if o.account and o.ra_name:
        name = f"ra/{o.ra_name}.ini"
        if f"{o.ra_name}.ini" in have_ra or f"{o.ra_name}.ini.new" in have_ra:
            print(f"{name}: there already (a login or a token); left as it is")
        else:
            ok &= put(o.account, name)
    elif o.account:
        if "ra_account.ini" in have or "ra_account.ini.new" in have:
            print("ra_account.ini: there already (a login or a token); left as it is")
        else:
            ok &= put(o.account, "ra_account.ini")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
