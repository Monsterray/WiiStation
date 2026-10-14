"""wii_clean.py [--dry-run] [--wii IP] -- take the test runs' files off the bench Wii's card.

Deletes from sd:/wiistation only what wii_lab.py runs leave there: their logs (perf, hb, dvd,
tty, lab, atrace, ptrace, vramio, ...), dumps (vram*, vsig_*, vtl_*, hprof_*, aq_*, xfb.bin), run
inputs (autoboot/autoinput, and input scripts that are copies of scripts/autoinput/*.txt),
and stale files (settingsRX2022.cfg, smbtrace.log). settings.ini goes only when it is the
runner's own minimal file (the size of wii_lab.BASE_SETTINGS): the card's settings are the
user's, and are never read here (an SMB password may be in them). Saves, button configs
(control*.cfg), BIOS, games, fonts and languages stay. Queue it like any bench job:
  python C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation -- \\
      C:/Python312/python.exe scripts/wii_clean.py
"""
import fnmatch, os, pathlib, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import bench_wii, card_ls, hbc, parser, refuse_production  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[1]
LOGS = ["perf.log", "hb.log", "dvd.log", "tty.log", "lab.log", "atrace.log", "ptrace.log",
        "vramio.log", "pcring.log", "emu.log", "smbtrace.log", "padtrace.csv", "quit.log"]
DUMPS = ["vram.bin", "vram_*.bin", "vsig_*.bin", "vtl_*.bin", "hprof_*.bin", "aq_*.bin", "xfb.bin"]
INPUTS = ["autoboot.txt", "autoboot.txt.disabled", "autoinput.txt", "crashtest.txt", "hangtest.txt"]
STALE = ["settingsRX2022.cfg"]


def lab_settings_size():
    import wii_lab
    return len(wii_lab.BASE_SETTINGS.encode())


def doomed(files):
    scripts = {p.name for p in (REPO / "scripts/autoinput").glob("*.txt")}
    out = []
    for name, size in sorted(files.items()):
        if (name in LOGS or name in INPUTS or name in STALE or name in scripts
                or any(fnmatch.fnmatch(name, p) for p in DUMPS)):
            out.append(name)
        elif name == "settings.ini" and size == lab_settings_size():
            out.append(name)
    return out


def clean(wii, dry_run=False):
    refuse_production(wii, "wii_clean.py")   # it deletes files: never on the production Wii
    files = card_ls(wii, "sd:/wiistation")
    if files is None:
        sys.exit("hbc.py ls sd:/wiistation failed: nothing was deleted")
    gone = doomed(files)
    total = sum(files[n] for n in gone)
    for name in gone:
        if dry_run:
            print(f"would delete {name} ({files[name]} bytes)")
            continue
        r = hbc(wii, "rm", f"sd:/wiistation/{name}", timeout=60)
        if r.rc:
            print(f"could not delete {name}: {r.text}")
    kept = sorted(set(files) - set(gone))
    print(f"{'would delete' if dry_run else 'deleted'} {len(gone)} files ({total / 1e6:.1f} MB); "
          f"kept {len(kept)}: {', '.join(kept)}")
    return gone


if __name__ == "__main__":
    # argparse refuses unknown flags and prefixes: a --dry-ruun typo once performed a real delete
    ap = parser(__doc__)
    ap.add_argument("--dry-run", action="store_true", help="list what would go; delete nothing")
    ap.add_argument("--wii", help="another bench Wii than the queue's $WII_BENCH_IP")
    a = ap.parse_args()
    clean(bench_wii("wii_clean.py", a.wii), a.dry_run)
