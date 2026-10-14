#!/usr/bin/env python3
"""Queued synthetic-disc integration checks and CPU-specific A/B/A comparisons."""
import argparse
import gzip
import importlib.util
import json
from pathlib import Path
import re
import shutil
import statistics
import time
import uuid

import bench_smoke
from wii_targets import bench_wii


def judge(output, cases):
    log = (output / "perf.log").read_text(errors="replace")
    rows = re.findall(r"achievement-bench: game=(\w+) frames=(\d+) count=(\d+) earned=(\d+) enabled=(\d+)", log)
    ends = re.findall(r"=== chain (\d+)/(\d+) end vblanks=(\d+)", log)
    if len(rows) != len(cases) or len(ends) != len(cases):
        raise RuntimeError("Missing or incomplete chain results")
    for i, (row, end, case) in enumerate(zip(rows, ends, cases), 1):
        game, frames, count, earned, enabled = row
        if (game != case["serial"] or int(frames) != case.get("frames", 240) or int(count) != case["enabled"] * 3
                or int(earned) != case["enabled"] * 3 or int(enabled) != case["enabled"]
                or end != (str(i), str(len(cases)), str(case.get("frames", 240)))):
            raise RuntimeError(f"Case {i} failed: {row}, {end}")
    if "FAILED" in log:
        raise RuntimeError("A scripted state operation failed")
    resets = re.findall(r"achievement-bench-reset: core=(\d+) earned=(\d+) expected=(\d+) count=(\d+)", log)
    swaps = re.findall(r"achievement-bench-swap: core=(\d+) result=(-?\d+) count=(\d+) earned=(\d+)", log)
    if resets != [(str(c), "3", "3", "3") for c in range(3)] or swaps != [(str(c), "0", "0", "0") for c in range(3)]:
        raise RuntimeError("Reset preservation or disc-swap shutdown failed")
    for core in range(3):
        for name, bits in ((f"before{core}.st", 3), (f"after{core}.st", 7)):
            data = gzip.decompress((output / name).read_bytes())
            at = data.rfind(b"ACH1")
            if at < 0 or data[at + 8:at + 12] != b"WAC1" or data[at + 28] != bits:
                raise RuntimeError(f"Wrong achievement state section or earned bits in {name}")
        for suffix in (core + 1, core + 4):
            records = (output / f"unlocks{suffix}.txt").read_text().splitlines()
            if len(records) != 3 or {line.split()[1] for line in records} != {"1", "2", "3"}:
                raise RuntimeError(f"Duplicate or missing journal records for fixture {suffix}")
    # Last complete wall sample in each game section, from the existing profiler.
    sections = re.split(r"=== chain \d+/\d+ end[^\n]*\n", log)
    for section in sections[:len(cases)]:
        samples = re.findall(r"wall: wall_us=(\d+) vblanks=(\d+)", section)
        if not samples or int(samples[-1][1]) != 240:
            raise RuntimeError("Missing 240 executed frames in profiler evidence")
    comparisons = []
    for core in range(3):
        times = []
        for section in sections[core * 3:core * 3 + 3]:
            samples = re.findall(r"wall: wall_us=(\d+) vblanks=(\d+)", section)
            if not samples or int(samples[-1][1]) != 240:
                raise RuntimeError("Missing complete profiler wall sample")
            times.append(int(samples[-1][0]))
        control = statistics.mean((times[0], times[2]))
        comparisons.append({"core": core, "disabled_us_1": times[0], "enabled_us": times[1],
                            "disabled_us_2": times[2], "regression_percent": (times[1] / control - 1) * 100})
    return {"pass": True, "cases": len(cases), "comparisons": comparisons}


def run(client, wii, bundle):
    output = bundle / "results"
    output.mkdir(exist_ok=False)
    remote = "sd:/wiistation/bench-" + uuid.uuid4().hex[:12]
    (output / "remote.txt").write_text(remote)
    before = client.status(wii)
    (output / "before.json").write_text(json.dumps(before, indent=2))
    if before.get("agent"):
        raise RuntimeError("Expected idle HBC")
    entries, truncated = client.list_dir(wii, "sd:/wiistation")
    if truncated:
        raise RuntimeError("Incomplete preflight directory listing")
    existing = {name for _, _, name in entries}
    names = ["autoboot.txt", "perf.log", "tty.log", "emu.log", "ptrace.log", "atrace.log",
             "vramio.log", "padtrace.csv", "crumb.log"]
    names.append("hb.log")
    names += [f"{kind}_{i:02d}.bin" for kind in ("vram", "vtl", "vsig") for i in range(1, 14)]
    backups = {}
    for name in names:
        if name in existing:
            data = client.get_file(wii, "sd:/wiistation/" + name)
            backups[name] = data
            path = output / ("backup-" + name)
            path.write_bytes(data)
            path.chmod(0o600)
    if "settings" not in existing:
        client.file_request(wii, "M", "sd:/wiistation/settings")
    settings, truncated = client.list_dir(wii, "sd:/wiistation/settings")
    if truncated:
        raise RuntimeError("Incomplete settings listing")
    # These serials are reserved by our original fixture. Refuse an existing
    # per-game config rather than disturb a user configuration on either device.
    serials = [f"ACHV0000{i}" for i in range(1, 8)]
    if any(name in {s + ".ini" for s in serials} for _, _, name in settings):
        raise RuntimeError("Synthetic fixture serial already has SD settings")
    if "usb" in before.get("inserted", []):
        usb, cut = client.list_dir(wii, "usb:/wiistation/settings")
        if cut or any(name in {s + ".ini" for s in serials} for _, _, name in usb):
            raise RuntimeError("Synthetic fixture serial already has USB settings")
    client.file_request(wii, "M", remote)
    staged_settings = []
    launched = False
    cases = []

    def launch(short_restart=False):
        nonlocal launched
        old_log = client.get_file(wii, "sd:/wiistation/perf.log") if short_restart else None
        client.send(wii, str(bundle / "boot.dol"), ["AutoSave=0"], name="WiiStation achievement integration")
        launched = True
        deadline = time.monotonic() + (90 if short_restart else 420)
        observed = False
        while time.monotonic() < deadline:
            try:
                state = client.status(wii)
                if state.get("agent"):
                    if state.get("app") != "WiiStation":
                        raise RuntimeError("Unexpected app during test")
                    observed = True
                elif observed:
                    if state.get("crash") and state["crash"] != before.get("crash"):
                        raise RuntimeError("New integration crash")
                    return state
                elif short_restart:
                    # A five-frame chain can return before the first poll sees
                    # its agent. Accept only changed, complete restart evidence.
                    log = client.get_file(wii, "sd:/wiistation/perf.log")
                    ends = re.findall(rb"=== chain (\d+)/(\d+) end vblanks=(\d+)", log)
                    if log != old_log and ends == [(str(c).encode(), b"3", b"5") for c in range(1, 4)]:
                        if state.get("crash") and state["crash"] != before.get("crash"):
                            raise RuntimeError("New restart crash")
                        return state
            except (OSError, client.HBCError):
                pass
            time.sleep(1)
        raise RuntimeError("Integration chain timed out")

    try:
        for i in range(1, 8):
            directory = remote + f"/iso{i}"
            client.file_request(wii, "M", directory)
            for name in ("fixture.bin", "fixture.cue"):
                data = (bundle / name).read_bytes().replace(b"ACHV_000.01", f"ACHV_000.0{i}".encode())
                client.put_file(wii, directory + "/" + name, data)
            definitions = (f"Game=ACHV0000{i}\n# run={remote}\n"
                           "1|Guest little endian word|0xX180010=305419896\n"
                           "2|Guest scratchpad byte|0xH200000=9\n"
                           "3|Three consecutive frame hits|0xX180000>=120.3.\n")
            client.put_file(wii, remote + f"/set{i}.txt", definitions.encode())
            core = (i - 1) % 3 if i < 7 else 1
            config = (f'Core={core}\nBiosDevice=0\nBootThruBios=0\nAutoSave=0\n'
                      f'LimitFrames={int(i == 7)}\nSkipFrames={int(i == 7)}\nPadAutoAssign=0\n'
                      'smbipaddr=""\nsmbsharename=""\nsmbusername=""\nsmbpassword=""\nRomDirSMB=""\n'
                      f'Achievements=1\nAchievementFile="{remote}/set{i}.txt"\n'
                      f'AchievementLog="{remote}/unlocks{i}.txt"\n')
            path = f"sd:/wiistation/settings/ACHV0000{i}.ini"
            staged_settings.append(path)
            client.put_file(wii, path, config.encode())
        chain = ["CHAIN", "# frozen achievement hardware matrix " + remote]
        for core in range(3):
            for enabled in (0, 1, 0):
                i = core + 1
                cases.append({"serial": f"ACHV0000{i}", "enabled": enabled})
                chain.extend([f"240 Core={core} AutoSave=0 Achievements={enabled}", remote + f"/iso{i}", "fixture.cue"])
        for core in range(3):
            i = core + 4
            script = (f"state save {remote}/before{core}.st 119\n"
                      f"state load {remote}/before{core}.st 150\n"
                      f"state save {remote}/after{core}.st 180\n")
            client.put_file(wii, remote + f"/s{core}.txt", script.encode())
            # statetool adjusts the stop clock by 119 - 150 on load; the
            # profiler must still prove 240 actual VBlanks were executed.
            cases.append({"serial": f"ACHV0000{i}", "enabled": 1, "frames": 209})
            chain.extend([f"240 {remote}/s{core}.txt AutoSave=0 Achievements=1", remote + f"/iso{i}", "fixture.cue"])
        cases.append({"serial": "ACHV00007", "enabled": 1})
        chain.extend(["240 AutoSave=0 Achievements=1 SkipFrames=1", remote + "/iso7", "fixture.cue"])
        chain_bytes = ("\n".join(chain) + "\n").encode()
        (output / "chain.txt").write_bytes(chain_bytes)
        (output / "cases.json").write_text(json.dumps(cases, indent=2))
        client.put_file(wii, "sd:/wiistation/autoboot.txt", chain_bytes)
        after = launch()
        (output / "after.json").write_text(json.dumps(after, indent=2))
        if after.get("crash") and after["crash"] != before.get("crash"):
            raise RuntimeError("New integration crash")
        for name in names:
            if name not in ("autoboot.txt", "emu.log"):
                listing, cut = client.list_dir(wii, "sd:/wiistation")
                if cut:
                    raise RuntimeError("Incomplete result listing")
                if any(n == name for _, _, n in listing):
                    (output / name).write_bytes(client.get_file(wii, "sd:/wiistation/" + name))
        listing, cut = client.list_dir(wii, remote)
        if cut:
            raise RuntimeError("Incomplete private result listing")
        for kind, size, name in listing:
            if kind == "f" and size <= 4 * 1024 * 1024:
                (output / name).write_bytes(client.get_file(wii, remote + "/" + name))
        result = judge(output, cases)
        # A second DOL boot runs only five frames, before any fixture condition
        # becomes true. All three unlocks must come from the persisted journals.
        restart = ["CHAIN", "# restart persistence " + remote]
        for core in range(3):
            restart.extend(["5 AutoSave=0 Achievements=1", remote + f"/iso{core + 1}", "fixture.cue"])
        client.put_file(wii, "sd:/wiistation/autoboot.txt", ("\n".join(restart) + "\n").encode())
        launch(short_restart=True)
        restart_log = client.get_file(wii, "sd:/wiistation/perf.log")
        (output / "restart-perf.log").write_bytes(restart_log)
        rows = re.findall(rb"achievement-bench: game=(\w+) frames=(\d+) count=(\d+) earned=(\d+) enabled=(\d+)", restart_log)
        if rows != [(f"ACHV0000{c + 1}".encode(), b"5", b"3", b"3", b"1") for c in range(3)]:
            raise RuntimeError("Journal restoration across DOL restart failed")
        for core in range(3):
            name = f"unlocks{core + 1}.txt"
            if client.get_file(wii, remote + "/" + name) != (output / name).read_bytes():
                raise RuntimeError("Restart duplicated journal entries")
        result["restart_persistence"] = True
        (output / "pass.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
    finally:
        if launched:
            current = client.status(wii)
            if current.get("agent"):
                if current.get("app") != "WiiStation":
                    raise RuntimeError("Another app is running; refusing cleanup")
                client.exit_app(wii, 90)
            client.hbc_wait(wii, 90)
            (output / "cleanup-status.json").write_text(json.dumps(client.status(wii), indent=2))
        # Restore exact bytes under the lease. No personal card files are touched.
        listing, cut = client.list_dir(wii, "sd:/wiistation")
        if cut:
            raise RuntimeError("Cannot safely restore an incomplete directory listing")
        present = {name for _, _, name in listing}
        if launched:
            for name in ("perf.log", "tty.log"):
                if name in present and not (output / name).exists():
                    (output / name).write_bytes(client.get_file(wii, "sd:/wiistation/" + name))
        for name in names:
            path = "sd:/wiistation/" + name
            if name in backups:
                client.put_file(wii, path, backups[name])
                if client.get_file(wii, path) != backups[name]:
                    raise RuntimeError("Backup restoration failed: " + name)
                (output / ("backup-" + name)).unlink()
            elif name in present:
                client.file_request(wii, "D", path)
        for path in staged_settings:
            client.file_request(wii, "D", path)
        if "settings" not in existing:
            remaining, cut = client.list_dir(wii, "sd:/wiistation/settings")
            if not cut and not remaining:
                client.file_request(wii, "D", "sd:/wiistation/settings")
        client.remove_tree(wii, remote, out=lambda _: None)
        (output / "cleanup.json").write_text(json.dumps({"restored": True, "status": client.status(wii)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--dol", type=Path, required=True)
    prep.add_argument("--client", type=Path, required=True)
    prep.add_argument("--fixture", type=Path, required=True)
    prep.add_argument("--bundle", type=Path, required=True)
    execute = sub.add_parser("run")
    execute.add_argument("--bundle", type=Path, required=True)
    recover = sub.add_parser("recover")
    recover.add_argument("--bundle", type=Path, required=True)
    recover.add_argument("--previous", type=Path, required=True)
    recover.add_argument("--verify-restart", action="store_true")
    args = parser.parse_args()
    if args.command == "prepare":
        bench_smoke.prepare(args.dol, args.client, args.bundle)
        for source in (Path(__file__), args.fixture / "fixture.bin", args.fixture / "fixture.cue"):
            shutil.copy2(source, args.bundle / source.name)
        manifest = json.loads((args.bundle / "manifest.json").read_text())
        for name in ("bench_achievement_games.py", "fixture.bin", "fixture.cue"):
            manifest["inputs"][name] = {"sha256": bench_smoke.digest(args.bundle / name)}
        (args.bundle / "manifest.json").write_text(json.dumps(manifest, indent=2))
        return
    wii = bench_wii("bench_achievement_games.py")
    manifest = json.loads((args.bundle / "manifest.json").read_text())
    for name, item in manifest["inputs"].items():
        if bench_smoke.digest(args.bundle / name) != item["sha256"]:
            parser.error("Frozen input changed: " + name)
    spec = importlib.util.spec_from_file_location("bench_hbc", args.bundle / "hbc.py")
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    if args.command == "recover":
        recover_run(client, wii, args.previous, args.verify_restart)
        return
    run(client, wii, args.bundle)


def recover_run(client, wii, previous, verify_restart=False):
    """Collect and restore a run whose network disappeared before cleanup."""
    output = previous / "results"
    remote = (output / "remote.txt").read_text().strip()
    if not re.fullmatch(r"sd:/wiistation/bench-[0-9a-f]{12}", remote):
        raise RuntimeError("Invalid recovery directory")
    status = client.status(wii)
    if status.get("agent"):
        raise RuntimeError("Recovery requires idle HBC")
    (output / "recovery-status.json").write_text(json.dumps(status, indent=2))
    kept = client.lastlog(wii)
    if kept:
        (output / "lastlog.txt").write_text(str(kept))
    names = ["autoboot.txt", "perf.log", "tty.log", "emu.log", "ptrace.log", "atrace.log",
             "vramio.log", "padtrace.csv", "crumb.log", "hb.log"]
    names += [f"{kind}_{i:02d}.bin" for kind in ("vram", "vtl", "vsig") for i in range(1, 14)]
    entries, cut = client.list_dir(wii, "sd:/wiistation")
    if cut:
        raise RuntimeError("Incomplete recovery listing")
    present = {name for _, _, name in entries}
    for name in names:
        path = "sd:/wiistation/" + name
        if name in present:
            (output / ("recovery-" + name if (output / name).exists() else name)).write_bytes(client.get_file(wii, path))
        backup = output / ("backup-" + name)
        if backup.exists():
            data = backup.read_bytes()
            client.put_file(wii, path, data)
            if client.get_file(wii, path) != data:
                raise RuntimeError("Recovery restore failed: " + name)
            backup.unlink()
        elif name in present:
            client.file_request(wii, "D", path)
    entries, cut = client.list_dir(wii, remote)
    if cut:
        raise RuntimeError("Incomplete private recovery listing")
    for kind, size, name in entries:
        if kind == "f" and size <= 4 * 1024 * 1024:
            (output / ("recovery-" + name if (output / name).exists() else name)).write_bytes(client.get_file(wii, remote + "/" + name))
    settings, cut = client.list_dir(wii, "sd:/wiistation/settings")
    if cut:
        raise RuntimeError("Incomplete settings recovery listing")
    for _, _, name in settings:
        if name in {f"ACHV0000{i}.ini" for i in range(1, 8)}:
            client.file_request(wii, "D", "sd:/wiistation/settings/" + name)
    client.remove_tree(wii, remote, out=lambda _: None)
    (output / "recovery-cleanup.json").write_text(json.dumps({"restored": True, "status": client.status(wii)}, indent=2))
    if verify_restart:
        result = judge(output, json.loads((output / "cases.json").read_text()))
        restart_log = (output / "recovery-perf.log").read_bytes()
        rows = re.findall(rb"achievement-bench: game=(\w+) frames=(\d+) count=(\d+) earned=(\d+) enabled=(\d+)", restart_log)
        ends = re.findall(rb"=== chain (\d+)/(\d+) end vblanks=(\d+)", restart_log)
        if rows != [(f"ACHV0000{c + 1}".encode(), b"5", b"3", b"3", b"1") for c in range(3)] or ends != [(str(c).encode(), b"3", b"5") for c in range(1, 4)]:
            raise RuntimeError("Recovered restart evidence incomplete")
        for c in range(1, 4):
            name = f"unlocks{c}.txt"
            if (output / ("recovery-" + name)).read_bytes() != (output / name).read_bytes():
                raise RuntimeError("Recovered restart duplicated journal entries")
        before = json.loads((output / "before.json").read_text())
        if status.get("crash") and status["crash"] != before.get("crash"):
            raise RuntimeError("New crash in recovered restart")
        result.update(restart_persistence=True, recovered_restart=True)
        (output / "pass.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
    print("Recovered evidence and restored staged files", flush=True)


if __name__ == "__main__":
    main()
