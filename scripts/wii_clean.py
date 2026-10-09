"""wii_clean.py [--dry-run] [WII] -- take the test runs' files off the bench Wii's card.

Deletes from sd:/wiistation only what wii_lab.py runs leave there: their logs (perf, hb, dvd,
tty, lab, atrace, ptrace, vramio, ...), dumps (vram*, vsig_*, vtl_*, hprof_*, xfb.bin), run
inputs (autoboot/autoinput, and input scripts that are copies of scripts/autoinput/*.txt),
and stale files (settingsRX2022.cfg, smbtrace.log). settings.ini goes only when it is the
runner's own minimal file (the size of wii_lab.BASE_SETTINGS): the card's settings are the
user's, and are never read here (an SMB password may be in them). Saves, button configs
(control*.cfg), BIOS, games, fonts and languages stay. Queue it like any bench job:
  python C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation -- \\
      C:/Python312/python.exe scripts/wii_clean.py
"""
import fnmatch, os, pathlib, subprocess, sys

REPO = pathlib.Path(__file__).resolve().parents[1]
HBC_TOOL = pathlib.Path("C:/projects/hbc-reborn/tools/hbc.py")
LOGS = ["perf.log", "hb.log", "dvd.log", "tty.log", "lab.log", "atrace.log", "ptrace.log",
        "vramio.log", "pcring.log", "emu.log", "smbtrace.log", "padtrace.csv"]
DUMPS = ["vram.bin", "vram_*.bin", "vsig_*.bin", "vtl_*.bin", "hprof_*.bin", "xfb.bin"]
INPUTS = ["autoboot.txt", "autoboot.txt.disabled", "autoinput.txt", "crashtest.txt", "hangtest.txt"]
STALE = ["settingsRX2022.cfg"]


def lab_settings_size():
    sys.path.insert(0, str(REPO / "scripts"))
    import wii_lab
    return len(wii_lab.BASE_SETTINGS.encode())


def card_files(hbc):
    ls = subprocess.run(hbc + ["ls", "sd:/wiistation"], capture_output=True, text=True, timeout=60)
    if ls.returncode:
        sys.exit("hbc.py ls failed: " + ls.stdout + ls.stderr)
    return {l.split()[-1]: int(l.split()[1]) for l in ls.stdout.splitlines() if l.startswith("f ")}


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
    sys.path.insert(0, str(REPO / "scripts"))
    from wii_targets import refuse_production
    refuse_production(wii, "wii_clean.py")   # it deletes files: never on the production Wii
    hbc = [sys.executable, str(HBC_TOOL), "--wii", wii]
    files = card_files(hbc)
    gone = doomed(files)
    total = sum(files[n] for n in gone)
    for name in gone:
        if dry_run:
            print(f"would delete {name} ({files[name]} bytes)")
            continue
        r = subprocess.run(hbc + ["rm", f"sd:/wiistation/{name}"], capture_output=True, text=True, timeout=60)
        if r.returncode:
            print(f"could not delete {name}: {(r.stdout + r.stderr).strip()}")
    kept = sorted(set(files) - set(gone))
    print(f"{'would delete' if dry_run else 'deleted'} {len(gone)} files ({total / 1e6:.1f} MB); "
          f"kept {len(kept)}: {', '.join(kept)}")
    return gone


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    clean(args[0] if args else os.environ.get("WII_BENCH_IP", "192.168.8.213"), "--dry-run" in sys.argv)
