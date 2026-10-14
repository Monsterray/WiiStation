"""wii_check.py -- quick checks on the bench Wii, as wii-bench queue jobs.

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation -- \\
        C:/Python312/python.exe scripts/wii_check.py COMMAND ...

  agent [DOL] [--settle S]   start the DOL (default the release build), ask its HBC agent for
                             version and status (stack use, frames, clock), exit back to HBC.
                             Exit 0 when the agent answered and HBC came back.
  triage [--elf ELF]         read-only: HBC status, crash report (with source lines from ELF),
                             the last app's kept output. Clears nothing.
  card [FOLDER ...] [--fix]  compare sd:/wiistation/isos/FOLDER with the library
                             (C:/wiistation-lib/games, names and sizes; default every folder).
                             --fix puts each missing or different file, one plain put at a time.
                             Exit 1 when something differs (after --fix: when a put failed).

Each refuses to run outside a queue job and refuses the production Wii (scripts/wii_targets.py).
"""
import json
import os
import pathlib
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import REPO, bench_wii, card_ls, existing_file, hbc, parser, wait_in_hbc  # noqa: E402

LIBRARY = pathlib.Path(os.environ.get("WSX_LIBRARY", "C:/wiistation-lib")) / "games"
SKIP = (".orig", ".ibg", ".bmp")   # library-side extras (cue backups, Dolphin shader caches, covers)
STATUS_KEYS = ("version", "app", "agent", "time", "safety", "memory", "frames")


def agent(wii, a):
    hbc(wii, "send", a.dol, timeout=120, show=True)
    time.sleep(a.settle)                  # menu up; the agent starts after VIDEO_Init
    v = hbc(wii, "version", timeout=30, show=True)
    ok = v.rc == 0 and v.out.strip().endswith("agent")
    s = hbc(wii, "status", "--json", timeout=30)
    try:
        st = json.loads(s.out)
        for k in STATUS_KEYS:
            if k in st:
                print(f"  {k}: {json.dumps(st[k])[:400]}")
    except ValueError:
        print(f"  status: {s.text[-400:]}")
    hbc(wii, "exit", timeout=60, show=True)
    back = wait_in_hbc(wii, 120)
    print(f"agent answered: {ok}; back in HBC: {back}")
    return 0 if ok and back else 1


def triage(wii, a):
    for args in (["status"], ["crash"] + (["--elf", str(a.elf)] if a.elf else []), ["lastlog"]):
        r = hbc(wii, *args, timeout=120)
        print(f"===== hbc.py {' '.join(args)} (exit {r.rc})\n{r.text}", flush=True)
    return 0


def card(wii, a):
    folders = a.folders or sorted(p.name for p in LIBRARY.iterdir() if p.is_dir())
    bad = []
    for name in folders:
        d = LIBRARY / name
        if not d.is_dir():
            sys.exit(f"{d}: not a library folder")
        on_card = card_ls(wii, f"sd:/wiistation/isos/{name}") or {}
        diff = [f for f in sorted(d.iterdir()) if f.is_file() and f.suffix not in SKIP
                and on_card.get(f.name) != f.stat().st_size]
        if not diff:
            continue
        print(f"{name}: {', '.join(f.name for f in diff)}", flush=True)
        if not a.fix:
            bad.append(name)
            continue
        for f in diff:
            r = hbc(wii, "put", f, f"sd:/wiistation/isos/{name}/{f.name}",
                    timeout=max(300, f.stat().st_size // 100_000))   # ~100 KB/s worst case seen over Wi-Fi
            print(f"  put {f.name} ({f.stat().st_size} bytes) -> {r.rc}", flush=True)
            if r.rc:
                bad.append(name)
                print(f"  {r.text[-300:]}")
                break
    print(("differs: " + " | ".join(bad)) if bad else f"{len(folders)} folders match the library")
    return 1 if bad else 0


def main():
    ap = parser(__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("agent", help="start a DOL, ask its agent, exit back to HBC")
    p.add_argument("dol", nargs="?", type=existing_file, default=REPO / "Gamecube/WiiSXRX_Release.dol")
    p.add_argument("--settle", type=float, default=25, help="seconds from send to the first question")
    p = sub.add_parser("triage", help="read-only: status, crash report, last output")
    p.add_argument("--elf", type=existing_file, help="the ELF of the DOL that ran, for source lines")
    p = sub.add_parser("card", help="the card's game folders against the library")
    p.add_argument("folders", nargs="*", metavar="FOLDER", help="library folder names (default all)")
    p.add_argument("--fix", action="store_true", help="put the missing or different files")
    a = ap.parse_args()
    wii = bench_wii("wii_check.py")
    sys.exit({"agent": agent, "triage": triage, "card": card}[a.cmd](wii, a))


if __name__ == "__main__":
    main()
