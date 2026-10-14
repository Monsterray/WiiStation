"""A bench-Wii queue job that also collects a crash: clears HBC's crash report, runs one chain
with wii_lab.py, then asks HBC for the report (a small status request -- never `hbc.py get`).
A crash leaves libogc2's crash screen up: someone presses RESET on the Wii, and the loop
below reads the report once HBC answers again (up to 10 minutes).

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation --timeout 1200 -- \\
        C:/Python312/python.exe scripts/wii_crash_job.py NAME CHAINFILE DOL SECS

Queue it with the full python path: the dispatcher's `bash` is WSL's, which has no python.
One game per job tells which game crashed; results go to .runs/NAME as with wii_lab.py."""
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import bench_wii, existing_file, hbc, parser  # noqa: E402

ap = parser(__doc__)
ap.add_argument("name", help="the run's folder: .runs/NAME")
ap.add_argument("chain", type=existing_file, help="a chain file (scripts/chains/*.txt), best one game")
ap.add_argument("dol", type=existing_file, help="the DOL; its .elf beside it gives source lines")
ap.add_argument("secs", type=int, help="seconds for the chain (wii_lab.py --timeout)")
a = ap.parse_args()
name, chain, dol, secs = a.name, str(a.chain), str(a.dol), str(a.secs)
py = sys.executable
wii = bench_wii("wii_crash_job.py")

r = hbc(wii, "crash", "--clear", timeout=60)
print("crash --clear exit", r.rc, r.text, flush=True)
if r.rc:
    sys.exit("crash --clear failed: a stale report would be misattributed to this run")
def triage(rc):
    # the automatic triage of this run, once, after the crash report: wii_lab.py leaves it to us
    # (WSX_TRIAGE_DEFER). Never changes this script's exit code.
    if os.environ.get("WSX_NO_LLM"):
        return
    try:
        subprocess.run([py, "scripts/run_triage.py", "--auto", os.path.join(".runs", name), "--rc", str(rc)],
                       timeout=150)
    except Exception as e:
        print("triage unavailable", e, flush=True)


r = subprocess.run([py, "scripts/wii_lab.py", name, chain, "--dol", dol, "--timeout", secs],
                   env=dict(os.environ, WSX_TRIAGE_DEFER="1"))
lab = r.returncode
print("wii_lab exit", lab, flush=True)
elf = os.path.splitext(dol)[0] + ".elf"   # beside the DOL copy: source lines for the report
end = time.monotonic() + 600   # 10 minutes, however long each try takes
while time.monotonic() < end:
    r = hbc(wii, "crash", *(["--elf", elf] if os.path.isfile(elf) else []), timeout=60)
    if r.rc == 0:
        print(r.out, flush=True)
        os.makedirs(os.path.join(".runs", name), exist_ok=True)
        if r.out.strip():   # the crash report, kept beside the run (scripts/run_triage.py reads it)
            open(os.path.join(".runs", name, "crash.txt"), "w").write(r.out)
        if lab:   # the app's last output, kept by the agent (HBC 1.9+): why it stopped
            k = hbc(wii, "lastlog", timeout=60)
            open(os.path.join(".runs", name, "lastlog.txt"), "w").write(k.text)
            print("lastlog (end):", *k.out.splitlines()[-15:], sep="\n", flush=True)
        triage(lab)
        sys.exit(lab)   # the wii_lab run's own result: a failed run must not look like success
    time.sleep(5)
print("HBC did not answer in 10 min:", r.text)
triage(1)
sys.exit("no crash report and HBC unreachable: total loss, not success")
