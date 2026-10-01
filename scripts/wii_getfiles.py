"""Fetch named small files from sd:/wiisxrx on the bench Wii, smallest first, each only if
under the limit (large hbc.py get crashed HBC 1.4.1/1.5.0). Waits for HBC up to 15 min.
Queue it (full python path), only with the user's go-ahead:
  python C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation --timeout 1200 -- \\
      C:/Python312/python.exe scripts/wii_getfiles.py OUTDIR LIMIT_BYTES NAME [NAME ...]"""
import subprocess, sys, time, os
out, limit, names = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
py = sys.executable
hbc = [py, "C:/projects/hbc-reborn/tools/hbc.py", "--wii", "192.168.8.213"]
for _ in range(45):
    ls = subprocess.run(hbc + ["ls", "sd:/wiisxrx"], capture_output=True, text=True).stdout
    if "lab.log" in ls:
        break
    time.sleep(20)
sizes = {l.split()[-1]: int(l.split()[1]) for l in ls.splitlines() if l.startswith("f ")}
os.makedirs(out, exist_ok=True)
for n in sorted(names, key=lambda n: sizes.get(n, 1 << 30)):
    if n not in sizes or sizes[n] > limit:
        print("skip", n, sizes.get(n))
        continue
    r = subprocess.run(hbc + ["get", "sd:/wiisxrx/" + n, os.path.join(out, n)], timeout=120)
    print("get", n, sizes[n], "exit", r.returncode, flush=True)
    if r.returncode:
        sys.exit(1)
