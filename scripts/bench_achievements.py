#!/usr/bin/env python3
"""Run a frozen achievement test DOL through the shared Wii bench queue."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import time
import uuid

import bench_smoke


def unit(client, wii, bundle):
    output = bundle / "results"
    output.mkdir(exist_ok=False)
    remote = "sd:/wiistation/bench-" + uuid.uuid4().hex
    (output / "remote.txt").write_text(remote + "\n")
    before = client.status(wii)
    (output / "before.json").write_text(json.dumps(before, indent=2))
    if before.get("agent"):
        raise RuntimeError("Wii is busy; no upload sent")
    client.file_request(wii, "M", remote)
    launched = False
    try:
        client.send(wii, str(bundle / "boot.dol"), [remote], name="WiiStation achievement hardware tests")
        launched = True
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            try:
                state = client.status(wii)
                if not state.get("agent"):
                    entries, truncated = client.list_dir(wii, remote)
                    if not truncated and any(name == "result.json" for _, _, name in entries):
                        break
                    if state.get("crash") and state["crash"] != before.get("crash"):
                        raise RuntimeError("Hardware suite crashed")
            except (OSError, client.HBCError):
                pass
            time.sleep(1)
        else:
            raise RuntimeError("Hardware suite did not produce a fresh result")
        after = client.status(wii)
        (output / "after.json").write_text(json.dumps(after, indent=2))
        if after.get("agent") or (after.get("crash") and after["crash"] != before.get("crash")):
            raise RuntimeError("Hardware suite did not return cleanly")
        entries, truncated = client.list_dir(wii, remote)
        if truncated:
            raise RuntimeError("Incomplete result directory")
        for kind, size, name in entries:
            if kind == "f" and size < 1024 * 1024 and "/" not in name and name not in (".", ".."):
                (output / name).write_bytes(client.get_file(wii, remote + "/" + name))
        result = json.loads((output / "result.json").read_text())
        if result.get("pass") is not True or result.get("suite") != "achievements":
            raise RuntimeError("Invalid hardware result")
        print("PASS: full achievement engine suite on big-endian Wii + SD journal/state tests")
        print(json.dumps(result), flush=True)
    finally:
        if launched:
            try:
                current = client.status(wii)
                if current.get("agent"):
                    if current.get("app") != "WiiStation tests":
                        raise RuntimeError("Another app is running; refusing to exit it")
                    client.exit_app(wii, 90)  # Includes the queue's uptime ownership check.
                client.hbc_wait(wii, 90)
            finally:
                (output / "cleanup-status.json").write_text(json.dumps(client.status(wii), indent=2))
        # Delete only this UUID directory, after result collection and HBC return.
        current = client.status(wii)
        if not current.get("agent"):
            client.remove_tree(wii, remote, out=lambda _: None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--dol", type=Path, required=True)
    prep.add_argument("--client", type=Path, required=True)
    prep.add_argument("--bundle", type=Path, required=True)
    run = sub.add_parser("run")
    run.add_argument("--bundle", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        bench_smoke.prepare(args.dol, args.client, args.bundle)
        shutil.copy2(__file__, args.bundle / "bench_achievements.py")
        manifest_path = args.bundle / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["inputs"]["bench_achievements.py"] = {"sha256": bench_smoke.digest(args.bundle / "bench_achievements.py")}
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        return
    if not os.environ.get("WII_BENCH_JOB_START") or not os.environ.get("WII_BENCH_IP"):
        parser.error("Run through wiibench.py; no Wii request sent")
    bench_smoke.verify(args.bundle)
    manifest = json.loads((args.bundle / "manifest.json").read_text())
    if bench_smoke.digest(args.bundle / "bench_achievements.py") != manifest["inputs"]["bench_achievements.py"]["sha256"]:
        parser.error("Frozen collector changed")
    spec = importlib.util.spec_from_file_location("bench_hbc", args.bundle / "hbc.py")
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    unit(client, os.environ["WII_BENCH_IP"], args.bundle)


if __name__ == "__main__":
    main()
