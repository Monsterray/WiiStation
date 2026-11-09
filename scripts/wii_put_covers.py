#!/usr/bin/env python3
"""wii_put_covers.py -- put a cover pack (scripts/cover_pack.py output) on the bench Wii's card.

    python wiibench.py add --wii dev -- python scripts/wii_put_covers.py PACK_DIR [--usb]

PACK_DIR is the folder holding index.txt (".../covers"). It is uploaded to sd:/wiistation/covers
(--usb: usb:/wiistation/covers), index.txt LAST, so a half-finished upload never has an index that
names files that are not there. Uploads only: nothing on the card is deleted, so files of an older
pack stay (an index only ever names files it brought). Refuses to run outside a queue job and on a
protected Wii (scripts/wii_targets.py)."""
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
from wii_targets import bench_wii, hbc, parser  # noqa: E402


def main():
    ap = parser(__doc__)
    ap.add_argument("pack", type=pathlib.Path, help="the pack folder that holds index.txt")
    ap.add_argument("--usb", action="store_true", help="usb:/wiistation/covers instead of sd:")
    o = ap.parse_args()
    index = o.pack / "index.txt"
    if not index.is_file():
        sys.exit(f"{index}: no such file (is {o.pack} a cover pack's covers folder?)")
    wii = bench_wii("wii_put_covers.py")
    dest = ("usb:" if o.usb else "sd:") + "/wiistation/covers"
    rc = 0
    for sub in ("box", "disc", "3d"):
        d = o.pack / sub
        files = sorted(d.iterdir()) if d.is_dir() else []
        if not files:
            continue
        r = hbc(wii, "put", "-r", str(d), f"{dest}/{sub}", timeout=60 + 2 * len(files), show=True)
        print(f"{sub}: {len(files)} files -> {dest}/{sub}: rc {r.rc}", flush=True)
        rc = rc or r.rc
    if rc:
        sys.exit(f"an upload failed (rc {rc}); index.txt was NOT put, so the card's old index (if any) stands")
    r = hbc(wii, "put", str(index), f"{dest}/index.txt", timeout=60, show=True)
    print(f"index.txt ({sum(1 for _ in index.open(encoding='utf-8'))} rows) -> {dest}/index.txt: rc {r.rc}", flush=True)
    r2 = hbc(wii, "ls", f"{dest}/box", timeout=60)
    print(f"on the card now ({dest}/box):\n{r2.text[:2000]}", flush=True)
    sys.exit(r.rc)


if __name__ == "__main__":
    main()
