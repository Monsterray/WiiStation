#!/usr/bin/env python3
"""Freeze a release smoke test, then run it through the shared wiibench queue.

Inspired by Wii64's wii_release_check.py. Uses the installed HBC client and
outbound connections only; never writes settings, saves, or autoboot files.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wii_targets import refuse_production  # noqa: E402


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(dol, client, bundle):
    sources = {"boot.dol": dol, "boot.elf": dol.with_suffix(".elf"),
               "hbc.py": client, "bench_smoke.py": Path(__file__).resolve()}
    for source in sources.values():
        if not source.is_file():
            raise RuntimeError(f"Missing input: {source}")
    bundle.mkdir(parents=True, exist_ok=False)
    for name, source in sources.items():
        shutil.copy2(source, bundle / name)
    manifest = {"created": time.time(), "inputs": {
        name: {"source": str(source.resolve()), "sha256": digest(bundle / name)}
        for name, source in sources.items()}}
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def verify(bundle):
    manifest = json.loads((bundle / "manifest.json").read_text())
    for name in ("boot.dol", "boot.elf", "hbc.py", "bench_smoke.py"):
        if digest(bundle / name) != manifest["inputs"][name]["sha256"]:
            raise RuntimeError(f"Frozen input changed: {name}")


def check(client, wii, bundle, settle=5):
    output = bundle / "results"
    output.mkdir(exist_ok=False)

    def save(name, state):
        (output / (name + ".json")).write_text(json.dumps(state, indent=2) + "\n")

    def ours(state):
        age = time.time() - float(os.environ["WII_BENCH_JOB_START"])
        return (state.get("agent") and state.get("app") == "WiiStation"
                and "uptime_ms" in state and state["uptime_ms"] / 1000 <= age + 2)

    def wait_for(predicate, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                state = client.status(wii)
                if predicate(state):
                    return state
            except (OSError, client.HBCError):
                pass  # Networking briefly disappears during launch/reload.
            time.sleep(1)
        raise RuntimeError("Wii did not reach the expected state before the deadline")

    def retraces(state):
        return state.get("safety", {}).get("frames", {}).get("retraces", 0)

    before = client.status(wii)
    save("hbc-before", before)
    if before.get("agent") or before.get("app"):
        raise RuntimeError("Expected idle HBC; no upload sent")
    entries, truncated = client.list_dir(wii, "sd:/wiistation")
    if truncated or any(name.lower() == "autoboot.txt" for _, _, name in entries):
        raise RuntimeError("Cannot safely boot the menu: SD autoboot file exists or listing is incomplete")
    launched = False
    try:
        # Two arguments trigger WiiStation's path/ROM autoboot parsing. Keep one.
        client.send(wii, str(bundle / "boot.dol"), ["AutoSave=0"], name="WiiStation bench smoke")
        launched = True
        initial = wait_for(ours, 90)
        save("agent-start", initial)
        time.sleep(settle)
        state = client.status(wii)
        save("agent", state)
        if not ours(state) or retraces(state) <= retraces(initial):
            raise RuntimeError("App ownership or SDK retrace callback check failed")
        if state.get("safety", {}).get("stack_guard") not in ("breakpoint", "marker"):
            raise RuntimeError("SDK stack guard is not active")
        width, height, pixels = client.screen(wii)
        (output / "frame.png").write_bytes(client.yuyv_png(width, height, pixels))
        client.send_keys(wii, "h")
        save("home", wait_for(lambda s: ours(s) and "overlay_ui" in s))
        client.send_keys(wii, "h")
        closed = wait_for(lambda s: ours(s) and "overlay_ui" not in s)
        time.sleep(1)
        resumed = client.status(wii)
        save("resumed", resumed)
        if not ours(resumed) or retraces(resumed) <= retraces(closed):
            raise RuntimeError("SDK retrace callback stopped after HOME")
    finally:
        if launched:
            current = wait_for(lambda s: True, 90)
            if ours(current):
                client.exit_app(wii, 90)
            elif current.get("agent"):
                raise RuntimeError("Another app is running; refusing to exit it")
            client.hbc_wait(wii, 90)
            save("hbc-after", client.status(wii))
    after = client.status(wii)
    save("hbc-after", after)
    if after.get("agent") or after.get("version") != before.get("version"):
        raise RuntimeError("Did not return to the same HBC version")
    if after.get("crash") and after["crash"] != before.get("crash"):
        raise RuntimeError("A new crash was recorded; see hbc-after.json and boot.elf")
    save("pass", {"checks": ["menu boot", "retraces", "stack guard", "HOME", "HBC return", "no new crash"]})
    print("PASS: menu boot, SDK safety, HOME and HBC return", flush=True)


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
        prepare(args.dol, args.client, args.bundle)
        print(args.bundle.resolve())
        return
    if not os.environ.get("WII_BENCH_JOB_START") or not os.environ.get("WII_BENCH_IP"):
        parser.error("Run through wiibench.py; no Wii request was sent")
    refuse_production(os.environ["WII_BENCH_IP"], "bench_smoke.py")
    verify(args.bundle)
    spec = importlib.util.spec_from_file_location("bench_hbc", args.bundle / "hbc.py")
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    check(client, os.environ["WII_BENCH_IP"], args.bundle)


if __name__ == "__main__":
    main()
