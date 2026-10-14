"""wii_targets.py -- the two Wiis this project talks to, and the guard for the one with real data.

  BENCH_WII       192.168.8.213  the dev/test Wii. Shared by every project through the
                                 wii-bench queue + lease (C:/tools/wii-bench/wiibench.py);
                                 unattended runs, logs and cleanups happen here.
  PRODUCTION_WII  192.168.8.200  Monty's everyday Wii, with REAL DATA on it: his saves, memory
                                 cards, settings and other apps. Treat it with care:
                                   - read-only by default: hbc.py status / ls / get (small) /
                                     screen are fine;
                                   - before anything that changes what runs (wiiload, exit, key)
                                     read status and take a screen: if an app other than HBC is
                                     running, he is using it -- stop and ask;
                                   - never an unattended run, a chain, a cleanup or a delete there,
                                     never a write to sd:/wiistation (settings, saves, cards);
                                   - an install (only on request) writes sd:/apps/WiiStation and
                                     nothing else: wii_install.py --production.
                                 Wii64 learned this the hard way (2026-10-09): a test loop's
                                 `exit` closed his game and the next upload replaced it.
Every script here that takes a Wii's address calls refuse_production() or, for the one allowed
production action, production_ready().

It is also the shared library of the bench-Wii scripts (wii_lab, wii_install, wii_clean,
wii_crash_job, wii_quit_test, wii_getfiles, wii_check, wii_probe/probe, bench_*):
  bench_wii(what)      the Wii's address inside a wii-bench queue job; refuses to run outside one
  queue_cmd(args)      the command line that queues a script (for --help and error messages)
  parser(doc)          an ArgumentParser whose --help is the script's docstring
  existing_file        argparse type: a path that must exist
  hbc(wii, *args)      one hbc.py call with a timeout; never hangs the queue lease
  hbc_ready(wii, s)    HBC's own menu answers on TCP 4299 (a probe HBC rejects at once)
  in_hbc(wii)          HBC itself answers (not an app's agent)
  card_ls(wii, dir)    {name: size} of the files in a card folder
  wiiload(wii, dol, args), local_ip_towards(wii)
A script that talks to the bench Wii is a queue job: run it with queue_cmd()'s line, never
directly (another workstation may hold the Wii, and only the queue knows).
"""
import argparse
import json
import os
import pathlib
import socket
import struct
import subprocess
import sys
import time
import zlib

BENCH_WII = "192.168.8.213"
PRODUCTION_WII = "192.168.8.200"
HBC_TOOL = "C:/projects/hbc-reborn/tools/hbc.py"
QUEUE = "C:/tools/wii-bench/wiibench.py"
PY = "C:/Python312/python.exe"   # the queue's shell has no python of its own: always the full path
REPO = pathlib.Path(__file__).resolve().parents[1]


def _canon(ip):
    """Normalize a Wii address for comparison: exact-string matching is bypassed by
    a scheme, port, trailing dot or case (http://192.168.8.200:4299). Hostnames are
    deliberately NOT resolved here -- a safety gate must stay fast and infallible."""
    s = (ip or "").strip().lower()
    for scheme in ("http://", "https://"):
        if s.startswith(scheme):
            s = s[len(scheme):]
    if s.startswith("::ffff:"):          # an IPv4-mapped IPv6 form
        s = s[len("::ffff:"):]
    s = s.split("@")[-1]                 # user@host
    return s.split("/")[0].split(":")[0].rstrip(".")


def is_production(ip):
    c = _canon(ip)
    if c == PRODUCTION_WII:
        return True
    if c.isdigit():                      # one decimal number: 3232236744
        return int(c) == int.from_bytes(socket.inet_aton(PRODUCTION_WII), "big")
    try:                                 # other numeric forms: 0xC0A808C8, 192.168.2248
        return socket.inet_aton(c) == socket.inet_aton(PRODUCTION_WII)
    except OSError:
        return False


def refuse_production(ip, what):
    """Stop a test tool aimed at the production Wii."""
    if is_production(ip):
        sys.exit(f"{what}: refused -- {ip} is the PRODUCTION Wii (real saves and data, in daily "
                 f"use). Test tools run on the bench Wii ({BENCH_WII}) through the queue. "
                 "See scripts/wii_targets.py.")


def production_ready(ip):
    """For the one allowed write (an install on request): HBC must be idle on the TV -- no app
    running, so nothing of his is closed or replaced. Saves a TV screenshot as a record and
    prints its path. Returns True, or exits saying why not."""
    try:
        r = subprocess.run([sys.executable, HBC_TOOL, "--wii", ip, "status", "--json"],
                           capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        sys.exit(f"production Wii {ip}: status timed out; not touching it")
    try:
        st = json.loads(r.stdout)
    except ValueError:
        sys.exit(f"production Wii {ip}: no status ({(r.stdout + r.stderr).strip()[:200]}); not touching it")
    if st.get("agent") or st.get("app"):
        sys.exit(f"production Wii {ip}: an app is running ({st.get('app') or 'agent app'}) -- "
                 "Monty may be playing; ask him first")
    shot = "C:/projects/WiiStation/.runs/prod_last_screen.png"
    s = subprocess.run([sys.executable, HBC_TOOL, "--wii", ip, "screen", shot],
                       capture_output=True, text=True, timeout=60)
    if s.returncode:
        sys.exit(f"production Wii {ip}: could not take TV screen; not touching it")
    print(f"production Wii {ip}: HBC idle by status; screen saved to {shot}")
    return True


# ---- bench-Wii queue jobs -----------------------------------------------------------------

def queue_cmd(args, timeout=None):
    """The line that queues `scripts/...` ARGS as a bench job (args: 'wii_clean.py --dry-run')."""
    t = f" --timeout {timeout}" if timeout else ""
    return f"{PY} {QUEUE} add --cwd {REPO.as_posix()}{t} -- {PY} scripts/{args}"


def bench_wii(what, ip=None):
    """The bench Wii's address for a job the wii-bench queue started ($WII_BENCH_IP; an explicit
    ip only for a second bench Wii). Outside a queue job it exits without touching any Wii: the
    queue is what keeps two workstations, or two agents, off the Wii at the same time."""
    if not os.environ.get("WII_BENCH_JOB_START"):
        sys.exit(f"{what}: run it as a wii-bench queue job, not directly (nothing was sent):\n"
                 f"  {queue_cmd(what + ' ...')}\n"
                 f"then: {PY} {QUEUE} wait ID")
    ip = ip or os.environ.get("WII_BENCH_IP") or BENCH_WII
    refuse_production(ip, what)
    return ip


def parser(doc):
    """ArgumentParser whose --help prints the script's docstring as written. No abbreviated
    flags: a typo must be an error, never a different option (a --dry-ruun once deleted)."""
    return argparse.ArgumentParser(description=doc, allow_abbrev=False,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)


def existing_file(s):
    """argparse type: a file that exists, as a Path (a clean usage error otherwise)."""
    p = pathlib.Path(s)
    if not p.is_file():
        raise argparse.ArgumentTypeError(f"{s}: no such file")
    return p


class Result:
    """What hbc() returns: rc (124 after a timeout), out (stdout), text (stdout + stderr)."""
    def __init__(self, rc, out, err=""):
        self.rc, self.out, self.text = rc, out, (out + err).strip()


def hbc(wii, *args, timeout=120, show=False, env=None):
    """One hbc.py call. A wedged Wii or agent costs at most `timeout` seconds and comes back as
    rc 124, never a hang of the queue lease nor a traceback. show=True prints a one-line record."""
    try:
        r = subprocess.run([sys.executable, HBC_TOOL, "--wii", wii, *map(str, args)],
                           capture_output=True, text=True, timeout=timeout, env=env)
        res = Result(r.returncode, r.stdout, r.stderr)
    except subprocess.TimeoutExpired:
        res = Result(124, "", f"timed out after {timeout} s")
    if show:
        print(f"$ hbc.py {' '.join(map(str, args))} -> {res.rc}: {res.text[-300:]}", flush=True)
    return res


def hbc_ready(wii, secs):
    """True once TCP 4299 accepts a connection (HBC's menu, or an app's agent). The probe sends a
    header HBC rejects at once: a bare connect and close holds HBC's loader for 10 s, and with a
    backlog of 3, repeated probes made it stop answering (see wiibench.py hbc_idle)."""
    end = time.monotonic() + secs
    while time.monotonic() < end:
        try:
            with socket.create_connection((wii, 4299), timeout=2) as s:
                s.sendall(b"PING" + bytes(12))
            return True
        except OSError:
            time.sleep(1)
    return False


def in_hbc(wii):
    """HBC itself answers: its version does not end in ' agent' (an app's agent does)."""
    r = hbc(wii, "version", timeout=20)
    return r.rc == 0 and not r.out.strip().endswith("agent")


def wait_in_hbc(wii, secs):
    end = time.monotonic() + secs
    while time.monotonic() < end:
        if in_hbc(wii):
            return True
        time.sleep(5)
    return False


def card_ls(wii, folder, timeout=60):
    """{name: size} of the files in a card folder; None when ls fails (the folder is missing).
    --json, never the text: HBC 1.10.6 put the file time before the name ("f SIZE DATE TIME NAME"),
    and a text parser then read "2026-09-28 00:12 settings.ini" as the name."""
    r = hbc(wii, "ls", "--json", folder, timeout=timeout)
    if r.rc:
        return None
    try:
        return {e["name"]: e["size"] for e in json.loads(r.out) if e["type"] == "f"}
    except (ValueError, KeyError, TypeError):
        return None


def wiiload(wii, dol, args):
    """The wiiload protocol, version 0.5: HAXX, version, args length, compressed and
    uncompressed sizes (big-endian), the zlib data, then the NUL-separated arguments."""
    data = pathlib.Path(dol).read_bytes()
    z = zlib.compress(data, 6)
    argb = b"".join(a.encode() + b"\0" for a in [pathlib.Path(dol).name] + list(args)) + b"\0"
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


if __name__ == "__main__":   # self-check: python scripts/wii_targets.py (touches no Wii)
    for ip in ("192.168.8.200", "http://192.168.8.200:4299", "3232237768", "0xC0A808C8", "192.168.8.200."):
        assert is_production(ip), ip
    assert not is_production(BENCH_WII)
    os.environ.pop("WII_BENCH_JOB_START", None)
    try:
        bench_wii("self-check")
        raise AssertionError("bench_wii ran outside a queue job")
    except SystemExit as e:
        assert "queue job" in str(e.code)
    os.environ["WII_BENCH_JOB_START"] = "1"
    try:
        bench_wii("self-check", PRODUCTION_WII)
        raise AssertionError("bench_wii took the production Wii")
    except SystemExit as e:
        assert "PRODUCTION" in str(e.code)
    assert bench_wii("self-check") == os.environ.get("WII_BENCH_IP", BENCH_WII)
    print("wii_targets self-check ok")
