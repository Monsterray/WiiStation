"""wii_install.py [--dol PATH] [--remove-autoboot] [--dry-run] -- install WiiStation on the dev Wii's SD card.

Run it as a bench-queue job (it talks to HBC on the Wii):

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation \
        --name "WiiStation install" -- C:/Python312/python.exe scripts/wii_install.py

It stages, in .runs/wii_install/, what a release zip holds, and uploads it with hbc-reborn's
`hbc.py sync` (only new or changed files go over: size, then CRC-32; nothing on the card is
deleted, so the user's settings, saves and games stay):
  sd:/apps/WiiStation/      boot.dol (Gamecube/WiiSXRX_Release.dol), meta.xml, icon.png
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
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
HBC = pathlib.Path("C:/projects/hbc-reborn/tools/hbc.py")
STAGE = REPO / ".runs" / "wii_install"


def stage(dol):
    if STAGE.exists():
        shutil.rmtree(STAGE)
    app = STAGE / "apps" / "WiiStation"
    app.mkdir(parents=True)
    shutil.copy2(dol, app / "boot.dol")
    for f in ("meta.xml", "icon.png"):
        shutil.copy2(REPO / "Gamecube/release/apps/WiiStation" / f, app / f)
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


def main():
    a = sys.argv[1:]
    dol = pathlib.Path(a[a.index("--dol") + 1]) if "--dol" in a else REPO / "Gamecube/WiiSXRX_Release.dol"
    trees = stage(dol)
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
    if "--remove-autoboot" in a and "--dry-run" not in a:
        subprocess.run([sys.executable, str(HBC), "--wii", wii, "rm", "sd:/wiistation/autoboot.txt"])
    for local, remote in trees:
        n = sum(f.stat().st_size for f in local.rglob("*") if f.is_file())
        print(f"{local.relative_to(STAGE)} -> {remote} ({n} bytes staged)", flush=True)
        if "--dry-run" in a:
            continue
        r = subprocess.run([sys.executable, str(HBC), "--wii", wii, "sync", str(local), remote])
        if r.returncode:
            sys.exit(f"sync of {remote} failed (exit {r.returncode})")
    example = REPO / "Gamecube/release/example_settings.ini"   # beside settings.ini; WiiStation does not read it
    if "--production" not in a:
        print(f"{example.name} -> sd:/wiistation/{example.name}", flush=True)
    if "--dry-run" not in a and "--production" not in a:
        r = subprocess.run([sys.executable, str(HBC), "--wii", wii, "put", str(example), "sd:/wiistation/" + example.name])
        if r.returncode:
            sys.exit(f"upload of {example.name} failed (exit {r.returncode})")
    print(f"installed {dol.name} ({dol.stat().st_size} bytes) as sd:/apps/WiiStation/boot.dol")


if __name__ == "__main__":
    main()
