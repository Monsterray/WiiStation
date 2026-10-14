"""wii_getfiles.py -- fetch named small files from a folder of the bench Wii's card, smallest first.

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation \\
        --timeout 1200 -- C:/Python312/python.exe scripts/wii_getfiles.py OUTDIR NAME [NAME ...]
        [--limit BYTES] [--dir sd:/wiistation]

A wii-bench queue job only, and only with the user's go-ahead: large `hbc.py get`s crashed
HBC 1.4.1/1.5.0, so each file comes only when it is under --limit (default 64 KB); the others
are listed as skipped. Waits up to 15 minutes for HBC's own menu first (an app may still be
closing). Exit 1 when a get fails.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import bench_wii, card_ls, hbc, parser, wait_in_hbc  # noqa: E402

ap = parser(__doc__)
ap.add_argument("out", help="local folder for the files")
ap.add_argument("names", nargs="+", metavar="NAME", help="file names in --dir")
ap.add_argument("--limit", type=int, default=64 * 1024, help="largest file fetched, in bytes")
ap.add_argument("--dir", default="sd:/wiistation", help="the card folder (default sd:/wiistation)")
a = ap.parse_args()
wii = bench_wii("wii_getfiles.py")
if not wait_in_hbc(wii, 900):
    sys.exit("HBC did not answer in 15 minutes: nothing fetched")
sizes = card_ls(wii, a.dir)
if sizes is None:
    sys.exit(f"hbc.py ls {a.dir} failed: nothing fetched")
os.makedirs(a.out, exist_ok=True)
for n in sorted(a.names, key=lambda n: sizes.get(n, 1 << 30)):
    if n not in sizes or sizes[n] > a.limit:
        print("skip", n, sizes.get(n, "absent"))
        continue
    r = hbc(wii, "get", f"{a.dir}/{n}", os.path.join(a.out, n), timeout=120)
    print("get", n, sizes[n], "exit", r.rc, flush=True)
    if r.rc:
        sys.exit(f"get {n} failed: {r.text[-200:]}")
