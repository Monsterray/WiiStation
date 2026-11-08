"""wii_install.py [--dol PATH] [--play-build] [--remove-autoboot] [--dry-run] -- install WiiStation on the dev Wii's SD card.

Run it as a bench-queue job (it talks to HBC on the Wii):

    python "$WII_BENCH" add --cwd <repo> \\
        --name "WiiStation install" -- python scripts/wii_install.py

It stages, in .runs/wii_install/, what a release zip holds, and uploads it with hbc-reborn's
`hbc.py sync` (only new or changed files go over: size, then CRC-32; nothing on the card is
deleted, so the user's settings, saves and games stay):
  sd:/apps/WiiStation/      boot.dol (Gamecube/WiiSXRX_Release.dol), meta.xml, icon.png
                            (meta.xml stamped: version "5.16.0-dev <commit>[+]", date = the DOL's;
                            HBC then shows which build is installed. --production keeps it as is)
  sd:/wiistation/controllers/  the HID controller files (Gamecube/release/controllers)
  sd:/wiistation/fonts/        En.dat, the menu fonts (fonts/menu) and the language fonts (fonts/*.zip)
  sd:/wiistation/lang/         the menu languages (lang/)
  sd:/wiistation/example_settings.ini  every setting, with its default and what it does
  sd:/wiistation/achievements/cacert.pem  the CA bundle Official mode trusts, pinned
                            (scripts/ra_ca_bundle.py: a bundle that is not the pinned one is refused)
--remove-autoboot deletes sd:/wiistation/autoboot.txt, which a lab run (wii_lab.py) left before
2026-10-01 and which makes an HBC start run that chain instead of the menu.
The fonts go last (Chs.dat is 8 MB). The Wii's address is $WII_BENCH_IP (the queue sets it); run
directly it refuses (nothing sent), except --dry-run, which stages and talks to no Wii.

--production installs on Monty's everyday Wii (192.168.8.200, real data: scripts/wii_targets.py),
only when he asks for it, run directly (not through the bench queue): it first checks that HBC is
idle on his TV (no app running), then writes sd:/apps/WiiStation (boot.dol, meta.xml, icon.png)
and nothing else -- not sd:/wiistation, where his settings, saves and cards live; no deletes.

--play-build installs the play build (Gamecube/WiiSXRX_play.dol, `wsx.sh build debug play`;
Docs/DEBUG_PLAY.md) as a second HBC app beside the normal one: sd:/apps/WiiStationDebug/ with
the same icon and a meta.xml named "WiiStation (debug)", always version-stamped. The normal app
is not touched. With --production too, that folder is all it writes. The build writes its logs
to sd:/wiistation/debuglogs/ (60 MB at most) and nothing else on the card.
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
STAGE = REPO / ".runs" / "wii_install"


def dev_version(dol):
    """'5.16.0-dev a696e46' (+ when the tree had uncommitted source changes) for the build that
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


def stage(dol, dev=True, folder="WiiStation"):
    if STAGE.exists():
        shutil.rmtree(STAGE)
    app = STAGE / "apps" / folder
    app.mkdir(parents=True)
    shutil.copy2(dol, app / "boot.dol")
    for f in ("meta.xml", "icon.png"):
        shutil.copy2(REPO / "Gamecube/release/apps/WiiStation" / f, app / f)
    if folder != "WiiStation":   # the play build: its own name in HBC's list
        meta = app / "meta.xml"
        meta.write_text(meta.read_text(encoding="utf-8").replace(
            "<name>WiiStation</name>", "<name>WiiStation (debug)</name>").replace(
            "<short_description>PSX Emulator for Wii</short_description>",
            "<short_description>Debug build: tests to try inside</short_description>"), encoding="utf-8")
        import wii_asks   # the description: what to try on the Wii (scripts/wii_asks.txt)
        meta.write_text(wii_asks.meta_xml(meta.read_text(encoding="utf-8")), encoding="utf-8")
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
    return [(app, "sd:/apps/" + folder)] + [
        (STAGE / "wiistation" / d, "sd:/wiistation/" + d) for d in ("controllers", "lang", "fonts")]


def main():
    sys.path.insert(0, str(REPO / "scripts"))
    from wii_targets import (LRWII, PRODUCTION_WII, bench_wii, card_ls, existing_file, hbc, hbc_tool, parser,
                             production_ready)
    ap = parser(__doc__)   # unknown or abbreviated flags are errors: "--dry-ru" must not install
    ap.add_argument("--dol", type=existing_file, default=None,
                    help="the DOL to install (default the release build; with --play-build WiiSXRX_play.dol)")
    ap.add_argument("--play-build", action="store_true",
                    help="the play build as its own HBC app, sd:/apps/WiiStationDebug (Docs/DEBUG_PLAY.md)")
    ap.add_argument("--remove-autoboot", action="store_true", help="delete sd:/wiistation/autoboot.txt (bench only)")
    ap.add_argument("--dry-run", action="store_true", help="stage and list; talk to no Wii")
    ap.add_argument("--production", action="store_true",
                    help="Monty's Wii (192.168.8.200): only when he asks; run directly, not queued")
    ap.add_argument("--lrwii", action="store_true",
                    help="the living-room Wii (192.168.8.243), production-style: as --production")
    ap.add_argument("--setup-folders", action="store_true",
                    help="with --production/--lrwii, on request: also WiiStation's shipped folders "
                         "(controllers, fonts, lang, example_settings.ini) and isos/ and bios/ if missing; "
                         "never settings.ini, saves or cards, never a delete")
    o = ap.parse_args()
    if o.production and o.lrwii:
        ap.error("--production and --lrwii: one Wii at a time")
    if o.setup_folders and not (o.production or o.lrwii):
        ap.error("--setup-folders is for --production/--lrwii (the bench install always syncs them)")
    prod_wii = PRODUCTION_WII if o.production else LRWII if o.lrwii else None
    dol = o.dol or REPO / ("Gamecube/WiiSXRX_play.dol" if o.play_build else "Gamecube/WiiSXRX_Release.dol")
    if not dol.is_file():
        sys.exit(f"{dol}: no such file (scripts/wsx.sh build {'debug play' if o.play_build else 'release'})")
    if prod_wii and os.environ.get("WII_BENCH_JOB_START"):
        sys.exit("--production/--lrwii is run directly, never as a bench queue job")
    folder = "WiiStationDebug" if o.play_build else "WiiStation"
    trees = stage(dol, dev=o.play_build or not prod_wii, folder=folder)
    if prod_wii:
        wii = prod_wii
        if o.remove_autoboot:
            sys.exit("--remove-autoboot is not allowed on the production Wii")
        production_ready(wii)
        if not o.setup_folders:
            trees = trees[:1]   # sd:/apps/WiiStation (or WiiStationDebug) only
        print(f"PRODUCTION-style Wii {wii}: HBC is idle; installing the app folder"
              f"{' and WiiStation\'s shipped folders' if o.setup_folders else ' only'}", flush=True)
    elif o.dry_run:
        wii = None   # stage and list only: no Wii is contacted
    else:
        wii = bench_wii("wii_install.py")
    # Every Wii round trip gets a timeout: without one a wedged agent hangs the queue
    # lease forever (seen 2026-10-10: bare sync attempt against a busy Wii).
    def hbc_run(*args, timeout):
        # A wedged agent must fail the job with a readable line, never hang the lease
        # and never a bare traceback.
        try:
            return subprocess.run([sys.executable, hbc_tool(), "--wii", wii, *args], timeout=timeout)
        except subprocess.TimeoutExpired:
            # a timed-out sync may have left some files on the card: rerunning the install
            # finishes it, since it compares size and CRC-32 and sends only what is missing
            what = ("the sync may be partial: rerun the install to repair it "
                    "(size and CRC-32 compare; only what is missing or changed is sent)"
                    if args[0] == "sync" else "nothing after this point ran")
            sys.exit(f"hbc.py {' '.join(args[:2])} timed out after {timeout} s: "
                     f"Wii or agent wedged, aborting ({what})")
    if o.remove_autoboot and not o.dry_run:
        hbc_run("rm", "sd:/wiistation/autoboot.txt", timeout=120)
    for local, remote in trees:
        n = sum(f.stat().st_size for f in local.rglob("*") if f.is_file())
        print(f"{local.relative_to(STAGE)} -> {remote} ({n} bytes staged)", flush=True)
        if o.dry_run:
            continue
        r = hbc_run("sync", str(local), remote, timeout=900)
        if r.returncode:
            sys.exit(f"sync of {remote} failed (exit {r.returncode})")
    example = REPO / "Gamecube/release/example_settings.ini"   # beside settings.ini; WiiStation does not read it
    shipped = not prod_wii or o.setup_folders
    if shipped:
        print(f"{example.name} -> sd:/wiistation/{example.name}", flush=True)
    if not o.dry_run and shipped:
        r = hbc_run("put", str(example), "sd:/wiistation/" + example.name, timeout=300)
        if r.returncode:
            sys.exit(f"upload of {example.name} failed (exit {r.returncode})")
    if shipped:   # the CA bundle, only the pinned one; the folder's secrets are never touched
        from ra_ca_bundle import BUNDLE, verify_pin
        print(f"{BUNDLE.name} -> sd:/wiistation/achievements/{BUNDLE.name} ({verify_pin()})", flush=True)
        if not o.dry_run:
            r = hbc_run("put", str(BUNDLE), "sd:/wiistation/achievements/" + BUNDLE.name, timeout=300)
            if r.returncode:
                sys.exit(f"upload of {BUNDLE.name} failed (exit {r.returncode})")
    if prod_wii and o.setup_folders and not o.dry_run:
        # the folders a player fills, made only when missing (hbc.py put makes the folder);
        # nothing that is there is read, replaced or listed beyond its name
        top = card_ls(wii, "sd:/wiistation") is not None
        for d, note in (("isos", "Put your PlayStation games here, one folder per game."),
                        ("bios", "Put a PlayStation BIOS here (scph*.bin) to boot through it.")):
            if top and card_ls(wii, f"sd:/wiistation/{d}") is not None:
                print(f"sd:/wiistation/{d}/ is there", flush=True)
                continue
            readme = STAGE / f"README_{d}.txt"
            readme.write_text(note + "\r\n")
            r = hbc(wii, "put", readme, f"sd:/wiistation/{d}/README.txt", timeout=120)
            print(f"sd:/wiistation/{d}/ made ({'ok' if r.rc == 0 else 'failed: ' + r.text[-100:]})", flush=True)
    verb = "would install (dry-run: nothing was written)" if o.dry_run else "installed"
    print(f"{verb} {dol.name} ({dol.stat().st_size} bytes) as sd:/apps/{folder}/boot.dol")


if __name__ == "__main__":
    main()
