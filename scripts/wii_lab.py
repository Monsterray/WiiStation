#!/usr/bin/env python3
"""wii_lab.py -- run a chain on the Wii on the bench, unattended, and collect its logs.

    python scripts/wii_lab.py NAME CHAINFILE [--wii 192.168.8.213] [--dol debug|release|PATH]
                              [--set K=V,K=V] [--send-isos] [--timeout SECS]

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
Wii's card under sd:/wiisxrx/isos/ (or sent once with --send-isos: slow over Wi-Fi).
A crash returns to HBC after 10 s with no results: the script says so and exits 3.
Windows asks once to let python accept connections on the port.
"""
import argparse
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

REPO = pathlib.Path(__file__).resolve().parents[1]
SHARED = pathlib.Path("C:/tools/Dolphin-x64/User/Load/WiiSDSync/wiisxrx")   # the user's games, for --send-isos
BASE_SETTINGS = "gpuPlugin = 2\nFPS = 1\nPadType1 = 1\nPadAutoAssign = 0\n"
RESULTS = ["perf.log", "vramio.log", "ptrace.log", "atrace.log", "lab.log",
           "xfb.bin", "vram.bin"]   # a "dump <vblank>": the TV picture and VRAM then


def hbc_ready(wii, secs):
    """True once TCP 4299 accepts a connection (HBC is in its menu). The probe sends a
    header HBC rejects at once: a bare connect and close holds HBC's loader for 10 s, and
    with a backlog of 3, repeated probes made it stop answering (see wiibench.py hbc_idle)."""
    end = time.monotonic() + secs
    while time.monotonic() < end:
        try:
            with socket.create_connection((wii, 4299), timeout=2) as s:
                s.sendall(b"PING" + bytes(12))
            return True
        except OSError:
            time.sleep(1)
    return False


def wiiload(wii, dol, args):
    """The wiiload protocol, version 0.5: HAXX, version, args length, compressed and
    uncompressed sizes (big-endian), the zlib data, then the NUL-separated arguments."""
    data = dol.read_bytes()
    z = zlib.compress(data, 6)
    argb = b"".join(a.encode() + b"\0" for a in [dol.name] + args) + b"\0"
    for attempt in range(6):          # HBC takes one connection at a time: a probe or
        try:                          # another sender can hold it for a moment
            with socket.create_connection((wii, 4299), timeout=10) as s:
                s.sendall(b"HAXX" + bytes([0, 5]) + struct.pack(">HII", len(argb), len(z), len(data)))
                s.sendall(z)
                s.sendall(argb)
            return
        except OSError as e:
            print(f"  wiiload attempt {attempt + 1}: {e}")
            time.sleep(5)
    sys.exit("HBC did not take the DOL")


def local_ip_towards(wii):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.connect((wii, 4299))
        return u.getsockname()[0]


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
        if l.startswith("sd:/wiisxrx/isos/"):
            folders.append(l[len("sd:/wiisxrx/isos/"):])
            games += 1
        for w in l.split():
            if w.startswith("sd:/wiistation/") and w.endswith(".txt") and "/isos/" not in w:
                scripts.append(w[len("sd:/wiistation/"):])
    return scripts, folders, games


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    ap.add_argument("chain", type=pathlib.Path)
    ap.add_argument("--wii", default=os.environ.get("WII_BENCH_IP", "192.168.8.213"))
    ap.add_argument("--port", type=int, default=4300)
    ap.add_argument("--dol", default="debug")
    ap.add_argument("--set", default="")
    ap.add_argument("--send-isos", action="store_true")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds for the whole chain")
    a = ap.parse_args()

    dol = {"debug": REPO / "Gamecube/WiiSXRX_debug.dol",
           "release": REPO / "Gamecube/WiiSXRX_Release.dol"}.get(a.dol, pathlib.Path(a.dol))
    run = REPO / ".runs" / a.name
    run.mkdir(parents=True, exist_ok=True)
    shutil.copy(dol, run / "boot.dol")          # a rebuild meanwhile cannot change what boots
    shutil.copy(a.chain, run / "chain.txt")

    scripts, folders, games = chain_files(a.chain)
    settings = BASE_SETTINGS + "".join(
        f"{k.strip()} = {v.strip()}\n" for k, v in (kv.split("=", 1) for kv in a.set.split(",") if "=" in kv))
    puts = [("autoboot.txt", a.chain.read_bytes()), ("settingsRX2022.cfg", settings.encode()),
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
            for p in sorted((SHARED / "isos" / d).glob("*")):
                isos.append((f"isos/{d}/{p.name}", p))

    if not hbc_ready(a.wii, 60):
        sys.exit(f"no Homebrew Channel at {a.wii}:4299 (is the Wii on, in HBC, on the network?)")
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
        sys.exit(f"no results after {a.timeout} s: the chain hung or crashed")
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


if __name__ == "__main__":
    main()
