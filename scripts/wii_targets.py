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
"""
import json
import subprocess
import sys

BENCH_WII = "192.168.8.213"
PRODUCTION_WII = "192.168.8.200"
HBC_TOOL = "C:/projects/hbc-reborn/tools/hbc.py"


def is_production(ip):
    return (ip or "").strip() == PRODUCTION_WII


def refuse_production(ip, what):
    """Stop a test tool aimed at the production Wii."""
    if is_production(ip):
        sys.exit(f"{what}: refused -- {ip} is the PRODUCTION Wii (real saves and data, in daily "
                 f"use). Test tools run on the bench Wii ({BENCH_WII}) through the queue. "
                 "See scripts/wii_targets.py.")


def production_ready(ip):
    """For the one allowed write (an install on request): HBC must be idle on the TV -- no app
    running, so nothing of his is closed or replaced. Returns True, or exits saying why not."""
    r = subprocess.run([sys.executable, HBC_TOOL, "--wii", ip, "status", "--json"],
                       capture_output=True, text=True, timeout=30)
    try:
        st = json.loads(r.stdout)
    except ValueError:
        sys.exit(f"production Wii {ip}: no status ({(r.stdout + r.stderr).strip()[:200]}); not touching it")
    if st.get("agent") or st.get("app"):
        sys.exit(f"production Wii {ip}: an app is running ({st.get('app') or 'agent app'}) -- "
                 "Monty may be playing; ask him first")
    return True
