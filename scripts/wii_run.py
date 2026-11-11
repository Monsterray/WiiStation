#!/usr/bin/env python3
"""wii_run.py NAME (CHAINFILE | --menu AUTOINPUT) [wii_lab.py options]  -- one hardware run, ending in a verdict.

    python scripts/wii_run.py NAME scripts/chains/foo.txt [--dol debug|release|PATH] [--set K=V,..]
                              [--timeout SECS] [--send-isos] [--put LOCAL=PATH] [--smb-from F] [--base RUN]
    python scripts/wii_run.py NAME --menu scripts/autoinput/walk.txt     (a menu walk, no game)

Start it with the Bash tool's run_in_background and read the end of its output: the last lines are
    VERDICT NAME: OK|CHECK|NO-RESULTS|DIFFERS | ...        (scripts/run_verdict.py, also .runs/NAME/verdict.txt)
It queues scripts/wii_lab.py on the bench Wii through the wii-bench queue and lease server (the Wii
is only ever the bench's $WII_BENCH_IP: no --wii here), waits for the job quietly (a progress line
every 5 min), finds the run in .runs/NAME, and runs the verdict on it. wii_lab.py itself puts the
card's own settings back and takes the run's files off the card. Nothing to poll.

--menu FILE: the input script of a menu walk (menukeys / menupage / menutour / menuclick lines, see
the wiistation-diagnostics skill). WiiStation ends the run when the script does, writes
menu.log ("menu walk: ok=1 pages=N last_page=X ...") and goes back to HBC. Needs a DOL with that
(2026-11-09 and later). Default --timeout for a menu walk is 600 s.

Exit code: 0 OK, 1 CHECK or DIFFERS, 2 NO-RESULTS (hung, crashed, or sent nothing),
           3 the Wii did not come back to HBC (needs a power cycle) or the job never finished.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import PY, queue  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[1]
LOST = ("did not come back", "needs a power cycle", "is not in HBC", "not back after the job")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    ap.add_argument("chain", nargs="?", help="a chain file (scripts/chains/*.txt)")
    ap.add_argument("--menu", metavar="AUTOINPUT", help="a menu walk: this input script, no game")
    ap.add_argument("--base", help="an earlier run to compare with (run_verdict.py --base)")
    ap.add_argument("--queue-wait", type=int, default=3600,
                    help="seconds to wait for the job to START (other jobs may hold the Wii); then it is cancelled")
    a, rest = ap.parse_known_args()
    if bool(a.chain) == bool(a.menu):
        ap.error("give a chain file, or --menu AUTOINPUT")
    if "--wii" in rest or "--port" in rest:
        ap.error("no --wii / --port: this is the bench Wii through the queue")
    timeout = 600 if a.menu else 1800
    if "--timeout" in rest:
        timeout = int(rest[rest.index("--timeout") + 1])
    run = REPO / ".runs" / a.name
    run.mkdir(parents=True, exist_ok=True)
    for f in ("verdict.json", "verdict.txt", "menu.log"):   # nothing of an earlier try under this name
        (run / f).unlink(missing_ok=True)
    cmd = [PY, "scripts/wii_lab.py", a.name]
    if a.menu:
        chain = run / "chain_in.txt"
        chain.write_text("CHAIN\n", newline="\n")   # no games: the menu, and the script walks it
        cmd += [chain.as_posix(), "--autoinput", pathlib.Path(a.menu).resolve().as_posix()]
    else:
        cmd += [pathlib.Path(a.chain).resolve().as_posix()]
    cmd += rest + ([] if "--timeout" in rest else ["--timeout", str(timeout)])

    q = [PY, queue()]
    p = subprocess.run(q + ["add", "--wii", "devwii", "--cwd", str(REPO), "--name", f"wii_run {a.name}",
                            "--timeout", str(timeout + 900), "--"] + cmd, capture_output=True, text=True)
    if p.returncode:
        print(p.stdout + p.stderr)
        print(f"VERDICT {a.name}: NO-RESULTS | the job was not queued")
        return 2
    job = p.stdout.split()[0]
    print(f"queued job {job}; waiting (the queue's turn, then up to {timeout} s of run)", flush=True)

    # The queue's own `wait` blocks until the job is done. Its once-a-minute progress goes to stderr:
    # dropped here, one line per 5 minutes is ours. A job that never starts is cancelled.
    t0 = time.monotonic()
    w = subprocess.Popen(q + ["wait", job, "--tail", "30"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                         errors="replace")
    limit, told, started = a.queue_wait + timeout + 900 + 300, t0, False
    rc = None
    while rc is None:
        try:
            out, _ = w.communicate(timeout=20)
            rc = w.returncode
        except subprocess.TimeoutExpired:
            now = time.monotonic()
            if now - told >= 300:
                print(f"  ... {int((now - t0) / 60)} min", flush=True)
                told = now
            if now - t0 > limit:
                w.kill()
                subprocess.run(q + ["cancel", job], capture_output=True)
                print(f"the job did not finish in {limit} s (cancelled if it had not started)")
                print(f"VERDICT {a.name}: WII-LOST | queue job {job} unfinished: `wiibench.py status`, no force restart")
                return 3
    print(out.rstrip(), flush=True)

    lost = any(s in out for s in LOST)
    d = str(run)
    # the job exit is the run's raw result; the verdict reads what came back
    v = subprocess.run([sys.executable, str(REPO / "scripts/run_verdict.py"), d] + (["--base", a.base] if a.base else []))
    if lost:
        print(f"VERDICT {a.name}: WII-LOST | the Wii did not come back to HBC (job {job}; needs a power cycle, not a forced one)")
        return 3
    return v.returncode


if __name__ == "__main__":
    sys.exit(main())
