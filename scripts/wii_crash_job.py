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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import refuse_production
refuse_production(os.environ.get("WII_BENCH_IP", "192.168.8.213"), "wii_crash_job.py")
hbc = [py, "C:/projects/hbc-reborn/tools/hbc.py", "--wii", os.environ.get("WII_BENCH_IP", "192.168.8.213")]

r = subprocess.run(hbc + ["crash", "--clear"])
print("crash --clear exit", r.returncode, flush=True)
r = subprocess.run([py, "scripts/wii_lab.py", name, chain, "--dol", dol, "--timeout", secs])
lab = r.returncode
print("wii_lab exit", lab, flush=True)
elf = os.path.splitext(dol)[0] + ".elf"   # beside the DOL copy: source lines for the report
for _ in range(120):
    r = subprocess.run(hbc + ["crash"] + (["--elf", elf] if os.path.isfile(elf) else []),
                       capture_output=True, text=True)
    if r.returncode == 0:
        print(r.stdout, flush=True)
        if lab:   # the app's last output, kept by the agent (HBC 1.9+): why it stopped
            k = subprocess.run(hbc + ["lastlog"], capture_output=True, text=True)
            os.makedirs(os.path.join(".runs", name), exist_ok=True)
            open(os.path.join(".runs", name, "lastlog.txt"), "w").write(k.stdout + k.stderr)
            print("lastlog (end):", *k.stdout.splitlines()[-15:], sep="\n", flush=True)
        sys.exit(0)
    time.sleep(5)
print("HBC did not answer in 10 min:", r.stdout, r.stderr)
