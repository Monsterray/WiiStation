"""make_release.py [--allow-dirty] [--dol PATH] -- build WiiStation-<version>.zip, what a player
unzips onto the SD card root:

  apps/WiiStation/   boot.dol (Gamecube/WiiSXRX_Release.dol), meta.xml, icon.png
  wiistation/        controllers/, fonts/, lang/, example_settings.ini, achievements/cacert.pem,
                     isos/ and bios/ (each with a one-line README.txt, so the folders exist)
  README.txt         Gamecube/release/apps/README

No games, BIOS, saves, covers or settings go in. The staging is wii_install.py's (same code),
so the zip and a dev install hold the same files. Output: .runs/release/WiiStation-<version>.zip.

Refuses a DOL without the real HTTPS transport (marker string), a DOL older than the newest commit, a DOL that does not carry the version in
Gamecube/version.h, and a dirty tracked tree (--allow-dirty: for review before the commit).
Prints the zip's file list and its sha256.
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
import time
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
import wii_install  # noqa: E402

NOTES = {"isos": "Put your PlayStation games here, one folder per game.",
         "bios": "Put a PlayStation BIOS here (scph*.bin) to boot through it."}


def git(*a):
    return subprocess.check_output(["git", "-C", str(REPO), *a], text=True).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dol", type=pathlib.Path, default=REPO / "Gamecube/WiiSXRX_Release.dol")
    ap.add_argument("--allow-dirty", action="store_true", help="package although tracked files have changes")
    o = ap.parse_args()
    ver = re.search(r'WIISTATION_VERSION "([^"]+)"', (REPO / "Gamecube/version.h").read_text()).group(1)
    if not o.dol.is_file():
        sys.exit(f"{o.dol}: no such file (bash scripts/wsx.sh build release)")
    if o.dol.read_bytes().find(ver.encode()) < 0:
        sys.exit(f"{o.dol.name} does not contain the version {ver}: rebuild it")
    if o.dol.read_bytes().find(b"WS-RA-TRANSPORT:curl+mbedtls") < 0:
        sys.exit(f"{o.dol.name} has the stub achievement transport (no HTTPS): Official mode cannot work.\n"
                 "Run scripts/build-ra-https.sh, then rebuild (SETUP.md, 'Official mode HTTPS stack').")
    newest = int(git("log", "-1", "--format=%ct"))
    if o.dol.stat().st_mtime < newest:
        sys.exit(f"{o.dol.name} is older than the newest commit: rebuild it")
    dirty = git("status", "--porcelain", "--untracked-files=no", "--", ".", ":!deps")
    if dirty and not o.allow_dirty:
        sys.exit("tracked files have changes (commit them, or --allow-dirty):\n" + dirty)
    out = REPO / ".runs" / "release"
    root = out / "stage"
    wii_install.stage(o.dol, dev=False, root=root)   # release meta.xml, not the dev stamp
    wii_install.stage_extras(root)
    for d, note in NOTES.items():
        (root / "wiistation" / d).mkdir()
        (root / "wiistation" / d / "README.txt").write_text(note + "\r\n")
    (root / "README.txt").write_bytes((REPO / "Gamecube/release/apps/README").read_bytes())
    zpath = out / f"WiiStation-{ver}.zip"
    zpath.unlink(missing_ok=True)
    stamp = time.localtime(newest)[:6]   # the newest commit's time, so the same tree gives the same zip
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(p for p in root.rglob("*") if p.is_file()):
            zi = zipfile.ZipInfo(f.relative_to(root).as_posix(), stamp)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, f.read_bytes())
    with zipfile.ZipFile(zpath) as z:
        for i in z.infolist():
            print(f"{i.file_size:>10}  {i.filename}")
    print(f"{zpath}  ({zpath.stat().st_size} bytes)")
    print(f"sha256 {hashlib.sha256(zpath.read_bytes()).hexdigest()}")
    if dirty:
        print("WARNING: built from a tree with uncommitted changes (--allow-dirty)")


if __name__ == "__main__":
    main()
