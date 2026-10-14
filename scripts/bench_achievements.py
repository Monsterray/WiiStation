#!/usr/bin/env python3
"""Run a frozen achievement test DOL through the shared Wii bench queue."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import time
import uuid

import bench_smoke
from wii_targets import bench_wii


def collect_diagnostics(client, wii, remote, output):
    """Retain bounded public diagnostics even when a suite exits without JSON."""
    try:
        entries, truncated = client.list_dir(wii, remote)
        if truncated:
            return
        for kind, size, name in entries:
            if (kind == "f" and size < 1024 * 1024 and
                    name in ("probe.log", "storage.log", "result.json") and
                    not (output / name).exists()):
                (output / name).write_bytes(client.get_file(wii, remote + "/" + name))
    except (OSError, client.HBCError):
        pass  # Preserve the original failure and still attempt UUID cleanup.


def unit(client, wii, bundle):
    output = bundle / "results"
    output.mkdir(exist_ok=False)
    remote = "sd:/wiistation/bench-" + uuid.uuid4().hex
    (output / "remote.txt").write_text(remote + "\n")
    manifest = json.loads((bundle / "manifest.json").read_text())
    before = client.status(wii)
    (output / "before.json").write_text(json.dumps(before, indent=2))
    if before.get("agent"):
        raise RuntimeError("Wii is busy; no upload sent")
    client.file_request(wii, "M", remote)
    launched = False
    try:
        for name in manifest.get("payloads", []):
            client.put_file(wii, remote + "/" + name, (bundle / name).read_bytes())
        client.send(wii, str(bundle / "boot.dol"), [remote], name="WiiStation achievement hardware tests")
        launched = True
        deadline = time.monotonic() + manifest.get("timeout", 120)
        while time.monotonic() < deadline:
            try:
                state = client.status(wii)
                if not state.get("agent"):
                    entries, truncated = client.list_dir(wii, remote)
                    if not truncated and any(name == "result.json" for _, _, name in entries):
                        break
                    if not truncated and any(name in ("probe.log", "storage.log") for _, _, name in entries):
                        raise RuntimeError("Hardware suite returned to HBC without a result")
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
        if result.get("pass") is not True or result.get("suite") != manifest.get("suite", "achievements"):
            raise RuntimeError("Invalid hardware result")
        print("PASS: Wii suite " + result["suite"])
        print(json.dumps(result), flush=True)
    finally:
        if launched:
            try:
                current = client.status(wii)
                if current.get("agent"):
                    if current.get("app") not in ("WiiStation tests", "WiiStation HTTPS probe", "WiiStation client tests"):
                        raise RuntimeError("Another app is running; refusing to exit it")
                    client.exit_app(wii, 90)  # Includes the queue's uptime ownership check.
                client.hbc_wait(wii, 90)
            finally:
                (output / "cleanup-status.json").write_text(json.dumps(client.status(wii), indent=2))
        # Delete only this UUID directory, after result collection and HBC return.
        current = client.status(wii)
        if not current.get("agent"):
            collect_diagnostics(client, wii, remote, output)
            client.remove_tree(wii, remote, out=lambda _: None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--dol", type=Path, required=True)
    prep.add_argument("--client", type=Path, required=True)
    prep.add_argument("--bundle", type=Path, required=True)
    prep.add_argument("--suite", default="achievements")
    prep.add_argument("--payload", type=Path, action="append", default=[])
    prep.add_argument("--timeout", type=int, default=120)
    run = sub.add_parser("run")
    run.add_argument("--bundle", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        bench_smoke.prepare(args.dol, args.client, args.bundle)
        shutil.copy2(__file__, args.bundle / "bench_achievements.py")
        manifest_path = args.bundle / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["inputs"]["bench_achievements.py"] = {"sha256": bench_smoke.digest(args.bundle / "bench_achievements.py")}
        manifest.update(suite=args.suite, timeout=args.timeout, payloads=[])
        for payload in args.payload:
            if payload.name in manifest["inputs"] or payload.name in (
                    "manifest.json", "results", "result.json", "probe.log", "storage.log"):
                parser.error("Reserved payload name")
            shutil.copy2(payload, args.bundle / payload.name)
            manifest["inputs"][payload.name] = {"sha256": bench_smoke.digest(args.bundle / payload.name)}
            manifest["payloads"].append(payload.name)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        return
    wii = bench_wii("bench_achievements.py")
    bench_smoke.verify(args.bundle)
    manifest = json.loads((args.bundle / "manifest.json").read_text())
    for name, entry in manifest["inputs"].items():
        if bench_smoke.digest(args.bundle / name) != entry["sha256"]:
            parser.error("Frozen input changed: " + name)
    spec = importlib.util.spec_from_file_location("bench_hbc", args.bundle / "hbc.py")
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    unit(client, wii, args.bundle)


if __name__ == "__main__":
    main()
