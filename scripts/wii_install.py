"""wii_install.py [--dol PATH] [--remove-autoboot] [--dry-run] -- install WiiStation on the dev Wii's SD card.

Run it as a bench-queue job (it talks to HBC on the Wii):

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation \
        --name "WiiStation install" -- C:/Python312/python.exe scripts/wii_install.py

It stages, in .runs/wii_install/, what a release zip holds, and uploads it with hbc-reborn's
`hbc.py sync` (only new or changed files go over: size, then CRC-32; nothing on the card is
deleted, so the user's settings, saves and games stay):
  sd:/apps/WiiStation/      boot.dol (Gamecube/WiiSXRX_Release.dol), meta.xml, icon.png
                            (meta.xml stamped: version "5.12.3-dev <commit>[+]", date = the DOL's;
                            HBC then shows which build is installed. --production keeps it as is)
  sd:/wiistation/controllers/  the HID controller files (Gamecube/release/controllers)
  sd:/wiistation/fonts/        En.dat, the menu fonts (fonts/menu) and the language fonts (fonts/*.zip)
  sd:/wiistation/lang/         the menu languages (lang/)
  sd:/wiistation/example_settings.ini  every setting, with its default and what it does
--remove-autoboot deletes sd:/wiistation/autoboot.txt, which a lab run (wii_lab.py) left before
2026-10-01 and which makes an HBC start run that chain instead of the menu.
The fonts go last (Chs.dat is 8 MB). The Wii's address is $WII_BENCH_IP (the queue sets it), else 192.168.8.213.

--production installs on Monty's everyday Wii (192.168.8.200, real data: scripts/wii_targets.py),
only when he asks for it, run directly (not through the bench queue): it first checks that HBC is
idle on his TV (no app running), then writes sd:/apps/WiiStation (boot.dol, meta.xml, icon.png)
and nothing else -- not sd:/wiistation, where his settings, saves and cards live; no deletes.
"""
import datetime
import os
import re
import pathlib
import shutil
import subprocess
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
HBC = pathlib.Path("C:/projects/hbc-reborn/tools/hbc.py")
STAGE = REPO / ".runs" / "wii_install"


def dev_version(dol):
    """'5.12.3-dev a696e46' (+ when the tree had uncommitted source changes) for the build that
    made dol: the version.h and commit of the checkout the DOL lies in, else this repo's."""
    root = REPO
    try:
        root = pathlib.Path(subprocess.check_output(["git", "-C", str(dol.parent), "rev-parse", "--show-toplevel"],
                                                    text=True, stderr=subprocess.DEVNULL).strip())
    except (subprocess.CalledProcessError, OSError):
        pass
    ver = re.search(r'WIISTATION_VERSION "([^"]+)"', (root / "Gamecube/version.h").read_text()).group(1)
    git = ["git", "-C", str(root)]
    rev = subprocess.check_output(git + ["rev-parse", "--short", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(git + ["status", "--porcelain", "--untracked-files=no", "--", ".", ":!deps"], text=True).strip()
    return f"{ver}-dev {rev}{'+' if dirty else ''}"


def stamp_meta(meta, dol):
    """The bench Wii's HBC shows which build is installed: version = dev_version, date = the DOL's."""
    s = meta.read_text(encoding="utf-8")
    s = re.sub(r"<version>[^<]*</version>", f"<version>{dev_version(dol)}</version>", s)
    day = datetime.datetime.fromtimestamp(dol.stat().st_mtime).strftime("%Y%m%d%H%M%S")
    s = re.sub(r"<release_date>[^<]*</release_date>", f"<release_date>{day}</release_date>", s)
    meta.write_text(s, encoding="utf-8")
    return s


def stage(dol, dev=True):
    if STAGE.exists():
        shutil.rmtree(STAGE)
    app = STAGE / "apps" / "WiiStation"
    app.mkdir(parents=True)
    shutil.copy2(dol, app / "boot.dol")
    for f in ("meta.xml", "icon.png"):
        shutil.copy2(REPO / "Gamecube/release/apps/WiiStation" / f, app / f)
    if dev:   # the production Wii keeps the release meta.xml
        v = re.search(r"<version>([^<]*)", stamp_meta(app / "meta.xml", dol)).group(1)
        print(f"meta.xml version: {v}", flush=True)
    shutil.copytree(REPO / "Gamecube/release/controllers", STAGE / "wiistation" / "controllers")
    fonts = STAGE / "wiistation" / "fonts"
    fonts.mkdir(parents=True)
    shutil.copy2(REPO / "fonts/En.dat", fonts)
    for f in (REPO / "fonts/menu").glob("*.dat"):
        shutil.copy2(f, fonts)
    for z in (REPO / "fonts").glob("*.zip"):
        zipfile.ZipFile(z).extractall(fonts)
    shutil.copytree(REPO / "lang", STAGE / "wiistation" / "lang")
    return [(app, "sd:/apps/WiiStation")] + [
        (STAGE / "wiistation" / d, "sd:/wiistation/" + d) for d in ("controllers", "lang", "fonts")]


KNOWN_FLAGS = ("--dol", "--remove-autoboot", "--dry-run", "--production")


def main():
    a = sys.argv[1:]
    if any(x in ("--help", "-h") for x in a):
        print(__doc__.strip())
        return
    for arg in a:
        if arg.startswith("--") and arg not in KNOWN_FLAGS:   # exact: "--dry-run=1" must not pass and then be ignored
            sys.exit(f"wii_install.py: unknown flag {arg} (nothing was done) -- "
                     f"see the module docstring; --dry-run previews")
    dol = pathlib.Path(a[a.index("--dol") + 1]) if "--dol" in a else REPO / "Gamecube/WiiSXRX_Release.dol"
    trees = stage(dol, dev="--production" not in a)
    sys.path.insert(0, str(REPO / "scripts"))
    from wii_targets import PRODUCTION_WII, refuse_production, production_ready
    if "--production" in a:
        wii = PRODUCTION_WII
        if "--remove-autoboot" in a:
            sys.exit("--remove-autoboot is not allowed on the production Wii")
        production_ready(wii)
        trees = trees[:1]   # sd:/apps/WiiStation only
        print(f"PRODUCTION Wii {wii}: HBC is idle; installing the app folder only", flush=True)
    else:
        wii = os.environ.get("WII_BENCH_IP", "192.168.8.213")
        refuse_production(wii, "wii_install.py (without --production)")
    # Every Wii round trip gets a timeout: without one a wedged agent hangs the queue
    # lease forever (seen 2026-10-10: bare sync attempt against a busy Wii).
    def hbc_run(*args, timeout):
        # A wedged agent must fail the job with a readable line, never hang the lease
        # and never a bare traceback.
        try:
            return subprocess.run([sys.executable, str(HBC), "--wii", wii, *args], timeout=timeout)
        except subprocess.TimeoutExpired:
            # a timed-out sync may have left some files on the card: rerunning the install
            # finishes it, since it compares size and CRC-32 and sends only what is missing
            what = ("the sync may be partial: rerun the install to repair it "
                    "(size and CRC-32 compare; only what is missing or changed is sent)"
                    if args[0] == "sync" else "nothing after this point ran")
            sys.exit(f"hbc.py {' '.join(args[:2])} timed out after {timeout} s: "
                     f"Wii or agent wedged, aborting ({what})")
    if "--remove-autoboot" in a and "--dry-run" not in a:
        hbc_run("rm", "sd:/wiistation/autoboot.txt", timeout=120)
    for local, remote in trees:
        n = sum(f.stat().st_size for f in local.rglob("*") if f.is_file())
        print(f"{local.relative_to(STAGE)} -> {remote} ({n} bytes staged)", flush=True)
        if "--dry-run" in a:
            continue
        r = hbc_run("sync", str(local), remote, timeout=900)
        if r.returncode:
            sys.exit(f"sync of {remote} failed (exit {r.returncode})")
    example = REPO / "Gamecube/release/example_settings.ini"   # beside settings.ini; WiiStation does not read it
    if "--production" not in a:
        print(f"{example.name} -> sd:/wiistation/{example.name}", flush=True)
    if "--dry-run" not in a and "--production" not in a:
        r = hbc_run("put", str(example), "sd:/wiistation/" + example.name, timeout=300)
        if r.returncode:
            sys.exit(f"upload of {example.name} failed (exit {r.returncode})")
    verb = "would install (dry-run: nothing was written)" if "--dry-run" in a else "installed"
    print(f"{verb} {dol.name} ({dol.stat().st_size} bytes) as sd:/apps/WiiStation/boot.dol")


if __name__ == "__main__":
    main()
