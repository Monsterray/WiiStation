#!/usr/bin/env python3
"""wii_lab.py -- run a chain on the Wii on the bench, unattended, and collect its logs.

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation \\
        --timeout 2400 -- C:/Python312/python.exe scripts/wii_lab.py NAME CHAINFILE
        [--dol debug|release|PATH] [--set K=V,K=V] [--send-isos] [--timeout SECS]

A wii-bench queue job only (it refuses to run directly, and refuses the production Wii);
the queue gives it the bench Wii ($WII_BENCH_IP). The queue's --timeout must exceed this
script's (default 1800 s for the chain, plus up to 5 min to collect after a failure).

The Wii must be sitting in the Homebrew Channel. This script:
  1. waits for HBC's wiiload port (TCP 4299) and listens on --port (4300) itself;
  2. sends the DOL with wiiload, with the argument lab=<this PC>:<port>;
  3. WiiStation connects back (Gamecube/lab_net.c) and gets the chain as autoboot.txt, the
     input scripts it names, the base settings (as scripts/wsx.sh uses) and, with
     --send-isos, the game folders; stale result files are emptied first;
  4. when the chain is done WiiStation sends perf.log, vramio.log, ptrace.log, atrace.log
     and each game's vram_NN.bin back into .runs/NAME/, and exits to HBC;
  5. prints the chain table and waits until HBC answers again, ready for the next test.

A chain file is the same as for `wsx.sh chain` (scripts/chains/). The games must be on the
Wii's card under sd:/wiistation/isos/ (or sent once with --send-isos: slow over Wi-Fi).
A crash returns to HBC after 10 s with no results: the script says so and exits 3.
Windows asks once to let python accept connections on the port.
"""
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import (bench_wii, existing_file, hbc, hbc_ready, card_ls, local_ip_towards,  # noqa: E402
                        parser, wiiload)

REPO = pathlib.Path(__file__).resolve().parents[1]
# --send-isos sends game folders from the library (scripts/profile.sh, WSX_LIBRARY), as the Dolphin
# runs stage them; it used to read the user's own Dolphin folder, which lacks the converted formats
LIBRARY = pathlib.Path(os.environ.get("WSX_LIBRARY", "C:/wiistation-lib")) / "games"
BASE_SETTINGS = "gpuPlugin = 2\nFPS = 1\nPadType1 = 1\nPadAutoAssign = 0\n"
RESULTS = ["perf.log", "vramio.log", "ptrace.log", "atrace.log", "lab.log", "tty.log", "dvd.log", "hb.log",
           "xfb.bin", "vram.bin"]   # a "dump <vblank>": the TV picture and VRAM then


AFTER_FAILURE = ["hb.log", "dvd.log", "lab.log", "tty.log", "perf.log"]   # small files worth having
AFTER_FAILURE_LIMIT = 4 * 1024 * 1024   # HBC 1.10 sent a 1.5 MB hb.log fine; old HBCs crashed on large gets


def collect_after_failure(wii, run, wait_s=300):
    """A run that hung or crashed sent no results. Wait for the Wii to come back to HBC (the
    HBC agent's hang watchdog returns it within ~60 s of the last progress), then fetch its
    crash report, the agent's kept log and WiiStation's own small logs from the card, so a
    failed run explains itself without anyone at the Wii. If HBC never answers, the Wii is
    wedged (IOS itself stuck): say so -- only a power cycle helps then."""
    print(f"waiting up to {wait_s} s for the Wii to come back to HBC ...", flush=True)
    if not hbc_ready(wii, wait_s):
        print("the Wii did not come back to HBC: it needs a power cycle (IOS is stuck)")
        return False
    run.mkdir(parents=True, exist_ok=True)
    for cmd in ("crash", "lastlog"):
        r = hbc(wii, cmd, timeout=60)
        (run / f"hbc_{cmd}.txt").write_text(r.text, encoding="utf-8")
        print(f"--- hbc.py {cmd}\n{r.text[-3000:]}")
    sizes = card_ls(wii, "sd:/wiistation") or {}
    for name in AFTER_FAILURE:
        if name not in sizes or sizes[name] > AFTER_FAILURE_LIMIT:
            print(f"not fetched: {name} ({sizes.get(name, 'absent')})")
            continue
        r = hbc(wii, "get", f"sd:/wiistation/{name}", run / name, timeout=120)
        print(f"fetched {name} ({sizes[name]} bytes)" if r.rc == 0 else f"failed: {name} ({r.text[-200:]})")
    for name in ("hb.log", "dvd.log", "lab.log"):
        f = run / name
        if f.is_file() and f.stat().st_size:
            print(f"--- {name} (end)\n" + "\n".join(f.read_text(errors="replace").splitlines()[-25:]))
    return True


# The card is also the one the user plays from: a run replaces its settings.ini with the
# runner's and leaves logs and dumps behind (2026-10-09: the user played with the FPS overlay
# and manual pads, and 14 MB of test files, on their Wii). So the user's settings.ini is
# fetched before a run -- never printed: it can hold the share's password -- and put back
# afterwards, and scripts/wii_clean.py takes the run's files off the card.
CARD = {}


def backup_settings(wii, run):
    sizes = card_ls(wii, "sd:/wiistation")
    if sizes is None:
        sys.exit("hbc.py ls sd:/wiistation failed: the card's settings.ini cannot be kept; not starting")
    run.mkdir(parents=True, exist_ok=True)
    CARD["wii"] = wii
    CARD["backup"] = None
    if "settings.ini" in sizes and sizes["settings.ini"] != len(BASE_SETTINGS.encode()):
        dst = run / "settings.user.ini"
        if hbc(wii, "get", "sd:/wiistation/settings.ini", dst, timeout=120).rc == 0:
            CARD["backup"] = dst
            print(f"the card's settings.ini ({sizes['settings.ini']} bytes) is kept for after the run")
        else:
            sys.exit("could not save the card's settings.ini before the run; not starting")


def finish_card():
    """After any run: the user's settings.ini back (or the runner's removed), the run's files off."""
    if not CARD:
        return
    wii, backup = CARD["wii"], CARD["backup"]
    if not hbc_ready(wii, 120):
        print("the Wii is not in HBC: the card was not tidied" +
              (f"; the user's settings are in {backup}" if backup else ""))
        return
    if backup:
        if hbc(wii, "put", backup, "sd:/wiistation/settings.ini", timeout=120).rc == 0:
            backup.unlink()   # it may hold a password: no copy left behind
            print("the card's own settings.ini is back")
        else:
            print(f"could not put settings.ini back: it is in {backup}")
    else:   # the runner's own, perhaps with the share's login (--smb-from): none of it stays
        hbc(wii, "rm", "sd:/wiistation/settings.ini", timeout=60)
    import wii_clean
    wii_clean.clean(wii)


def recv_line(c):
    b = bytearray()
    while True:
        ch = c.recv(1)
        if not ch:
            raise ConnectionError("closed")
        if ch == b"\n":
            return b.decode("utf-8", "replace").rstrip("\r")
        b += ch


def recv_exact(c, n, out):
    while n > 0:
        d = c.recv(min(n, 65536))
        if not d:
            raise ConnectionError("closed mid-file")
        out.write(d)
        n -= len(d)


def chain_files(chain):
    """(input scripts, game folders, number of games) named by a chain file."""
    scripts, folders, games = [], [], 0
    lines = [l.strip() for l in chain.read_text().splitlines()]
    for l in lines:
        if re.match(r"(sd|usb|smb|dvd):/", l):   # a game's folder line, on any device
            games += 1
        if l.startswith("sd:/wiistation/isos/"):
            folders.append(l[len("sd:/wiistation/isos/"):])
        for w in l.split():
            if w.startswith("sd:/wiistation/") and w.endswith(".txt") and "/isos/" not in w:
                scripts.append(w[len("sd:/wiistation/"):])
    return scripts, folders, games


def main():
    ap = parser(__doc__)
    ap.add_argument("name", help="the run's folder: .runs/NAME")
    ap.add_argument("chain", type=existing_file, help="a chain file (scripts/chains/*.txt)")
    ap.add_argument("--wii", help="another bench Wii than the queue's $WII_BENCH_IP")
    ap.add_argument("--port", type=int, default=4300, help="the port WiiStation connects back to")
    ap.add_argument("--dol", default="debug", help="debug, release or a DOL's path (default debug)")
    ap.add_argument("--set", default="", help="extra settings, K=V,K=V")
    ap.add_argument("--smb-from", type=existing_file, default=None,
                    help="a settings.ini with the SMB share login: its smb* and RomDir* lines go to the "
                         "Wii's settings, unread and unprinted (not into run.info)")
    ap.add_argument("--send-isos", action="store_true", help="send the chain's game folders from the library")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds for the whole chain")
    a = ap.parse_args()
    dol = {"debug": REPO / "Gamecube/WiiSXRX_debug.dol",
           "release": REPO / "Gamecube/WiiSXRX_Release.dol"}.get(a.dol, pathlib.Path(a.dol))
    if not dol.is_file():
        ap.error(f"--dol {a.dol}: no such file ({dol})")
    a.wii = bench_wii("wii_lab.py", a.wii)   # unattended runs and cleanups: the bench Wii, in a queue job

    global RUN_DIR
    run = REPO / ".runs" / a.name
    RUN_DIR = run
    run.mkdir(parents=True, exist_ok=True)
    shutil.copy(dol, run / "boot.dol")          # a rebuild meanwhile cannot change what boots
    shutil.copy(a.chain, run / "chain.txt")

    scripts, folders, games = chain_files(a.chain)
    settings = BASE_SETTINGS + "".join(
        f"{k.strip()} = {v.strip()}\n" for k, v in (kv.split("=", 1) for kv in a.set.split(",") if "=" in kv))
    # the share login: only into what the Wii gets, never into `settings` (run.info prints it)
    share = ""
    if a.smb_from:
        share = "".join(l if l.endswith("\n") else l + "\n"
                        for l in a.smb_from.read_text(encoding="utf-8").splitlines(True)
                        if re.match(r"\s*(smb[A-Za-z]+|RomDir[A-Za-z]+)\s*=", l))
    puts = [("autoboot.txt", a.chain.read_bytes()), ("settings.ini", (settings + share).encode()),
            ("autoinput.txt", b"")]
    for f in scripts:
        p = REPO / "scripts/autoinput" / f
        if not p.is_file():
            sys.exit(f"chain names sd:/wiistation/{f}: no scripts/autoinput/{f}")
        puts.append((f, p.read_bytes()))
    wants = RESULTS + [f"vram_{i:02d}.bin" for i in range(1, games + 1)] \
        + [f"vtl_{i:02d}.bin" for i in range(1, games + 1)]   # per-vblank timelines (scripts/vtl_view.py)
    wants += [f"vsig_{i:02d}.bin" for i in range(1, games + 1)]   # per-vblank guest signatures (scripts/vsig_cmp.py)
    wants += [f"hprof_{i:02d}.bin" for i in range(1, games + 1)]   # PROBES=hprof (scripts/hprof_view.py); empty otherwise
    puts += [(w, b"") for w in wants if w != "lab.log"]   # no stale result from an earlier run (lab.log: WiiStation restarts it, and is writing it now)
    isos = []
    if a.send_isos:
        for d in folders:
            for p in sorted((LIBRARY / d).glob("*")):
                isos.append((f"isos/{d}/{p.name}", p))

    if not hbc_ready(a.wii, 60):
        sys.exit(f"no Homebrew Channel at {a.wii}:4299 (is the Wii on, in HBC, on the network?)")
    backup_settings(a.wii, run)
    host = local_ip_towards(a.wii)
    srv = socket.create_server(("0.0.0.0", a.port))
    srv.settimeout(120)
    print(f"sending {dol.name} ({dol.stat().st_size} bytes) to {a.wii}, lab={host}:{a.port}")
    wiiload(a.wii, run / "boot.dol", [f"lab={host}:{a.port}"])

    try:
        c, _ = srv.accept()
    except socket.timeout:
        back = hbc_ready(a.wii, 30)
        sys.exit("the DOL never connected back; " + (
            "the Wii is back in HBC, so WiiStation started and gave up: its steps are in sd:/wiistation/lab.log"
            if back else "the Wii is not in HBC: it did not start, or it hangs"))
    with c:
        c.settimeout(60)
        hello = recv_line(c)
        for path, data in puts:
            c.sendall(f"PUT {len(data)} {path}\n".encode() + data)
        for path, p in isos:
            print(f"  sending {path} ({p.stat().st_size >> 20} MB)")
            c.sendall(f"PUT {p.stat().st_size} {path}\n".encode())
            with open(p, "rb") as f, c.makefile("wb") as w:
                shutil.copyfileobj(f, w, 1 << 16)
        for w in wants:
            c.sendall(f"WANT {w}\n".encode())
        c.sendall(b"GO\n")
        reply = recv_line(c)
        if reply != "OK":   # WiiStation could not write a file (sd:/wiistation/lab.log says which)
            sys.exit(f"{hello}: the Wii answered {reply!r} to the files; it goes back to HBC in 5 s")
        print(f"{hello}: {len(puts)} files staged, {games} games; running (up to {a.timeout} s)")

    srv.settimeout(a.timeout)
    t0 = time.monotonic()
    started = time.strftime("%Y-%m-%d %H:%M:%S")
    try:
        c, _ = srv.accept()
    except socket.timeout:
        print(f"no results after {a.timeout} s: the chain hung or crashed", flush=True)
        collect_after_failure(a.wii, run)
        sys.exit(2)
    got, incomplete = [], ""
    with c:
        c.settimeout(120)
        if recv_line(c) != "RESULTS":
            sys.exit("unexpected reply")
        try:
            while True:
                l = recv_line(c)
                if l == "END":
                    break
                _, n, path = l.split(" ", 2)
                with open(run / pathlib.Path(path).name, "wb") as f:
                    recv_exact(c, int(n), f)
                if int(n) == 0 and path.startswith("hprof_"):   # not an hprof build
                    (run / pathlib.Path(path).name).unlink()
                    continue
                got.append(path)
        except (socket.timeout, ConnectionError) as e:
            # The Wii stopped sending (it hung or crashed in lab_report): keep what came.
            incomplete = f"incomplete: {type(e).__name__} after {len(got)} files\n"
            print(f"results {incomplete.strip()}")
    srv.close()
    (run / "run.info").write_text(
        f"dol={a.dol} ({dol.name}) wii={a.wii} chain={a.chain} secs={int(time.monotonic() - t0)}\n"
        f"started={started} ended={time.strftime('%Y-%m-%d %H:%M:%S')} (PC clock)\n"
        f"settings: {settings.strip().replace(chr(10), ' ')}\nplatform: hardware\n{incomplete}")
    print(f"results: {', '.join(got)} -> {run}")
    if incomplete:
        sys.exit(1)
    subprocess.run([sys.executable, str(REPO / "scripts/chain_table.py"), str(run)])
    print("HBC ready for the next test" if hbc_ready(a.wii, 90) else "HBC did not come back within 90 s")


RUN_DIR = None   # set by main(): the run's directory, for the automatic triage


def auto_triage(run, rc):
    """The automatic triage of a run that looks wrong (scripts/run_triage.py --auto). Never changes
    the exit code; skipped by WSX_NO_LLM=1, and by wii_crash_job.py, which triages after the crash."""
    if os.environ.get("WSX_NO_LLM") or os.environ.get("WSX_TRIAGE_DEFER") or run is None:
        return
    try:
        subprocess.run([sys.executable, str(REPO / "scripts/run_triage.py"), "--auto", str(run),
                        "--rc", str(rc)], timeout=150)
    except Exception as e:
        print(f"triage unavailable ({e})", flush=True)


if __name__ == "__main__":
    code = 1
    try:
        main()
        code = 0
    except SystemExit as e:
        if e.code is None:
            code = 0
        elif isinstance(e.code, int):
            code = e.code
        else:
            print(e.code, file=sys.stderr)   # sys.exit("message"): what Python does with a message
            code = 1
    finally:
        finish_card()
        auto_triage(RUN_DIR, code)
    sys.exit(code)
