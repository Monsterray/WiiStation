"""A bench-Wii queue job that also collects a crash: clears HBC's crash report, runs one chain
with wii_lab.py, then asks HBC for the report (a small status request -- never `hbc.py get`).
A crash leaves libogc2's crash screen up: someone presses RESET on the Wii, and the loop
below reads the report once HBC answers again (up to 10 minutes).

    python C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation --timeout 1200 -- \
        C:/Python312/python.exe scripts/wii_crash_job.py NAME CHAINFILE DOL SECS

Queue it with the full python path: the dispatcher's `bash` is WSL's, which has no python.
One game per job tells which game crashed; results go to .runs/NAME as with wii_lab.py."""
import os, subprocess, sys, time

name, chain, dol, secs = sys.argv[1:5]
py = sys.executable
hbc = [py, "C:/projects/hbc-reborn/tools/hbc.py", "--wii", os.environ.get("WII_BENCH_IP", "192.168.8.213")]

r = subprocess.run(hbc + ["crash", "--clear"])
print("crash --clear exit", r.returncode, flush=True)
r = subprocess.run([py, "scripts/wii_lab.py", name, chain, "--dol", dol, "--timeout", secs])
print("wii_lab exit", r.returncode, flush=True)
for _ in range(120):
    r = subprocess.run(hbc + ["crash"], capture_output=True, text=True)
    if r.returncode == 0:
        print(r.stdout, flush=True)
        sys.exit(0)
    time.sleep(5)
print("HBC did not answer in 10 min:", r.stdout, r.stderr)
