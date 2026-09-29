#!/usr/bin/env python3
"""probe.py -- send argvprobe.dol to the bench Wii with two test arguments and print what
it reports back over TCP 4300 (argc/argv, net_init, if_config, the connect). Run it through
the bench queue: python C:/tools/wii-bench/wiibench.py add -- python scripts/wii_probe/probe.py"""
import os
import pathlib
import socket
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from wii_lab import hbc_ready, wiiload  # noqa: E402

wii = os.environ.get("WII_BENCH_IP", "192.168.8.213")
dol = pathlib.Path(__file__).resolve().parent / "argvprobe.dol"
if not hbc_ready(wii, 60):
    sys.exit("no Homebrew Channel")
srv = socket.create_server(("0.0.0.0", 4300))
srv.settimeout(90)
wiiload(wii, dol, ["lab=192.168.8.147:4300", "second-arg"])
try:
    c, peer = srv.accept()
except socket.timeout:
    sys.exit("the probe never connected back" + (" (the Wii is back in HBC)" if hbc_ready(wii, 20) else " (the Wii is not in HBC)"))
data = b""
with c:
    c.settimeout(10)
    while True:
        d = c.recv(4096)
        if not d:
            break
        data += d
print(f"from {peer[0]}:")
print(data.decode("ascii", "replace"))
print("HBC back" if hbc_ready(wii, 40) else "HBC not back within 40 s")
