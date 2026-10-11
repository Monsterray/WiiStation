import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("bench_smoke", Path(__file__).resolve().parents[1] / "scripts/bench_smoke.py")
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


class Client:
    HBCError = RuntimeError

    def __init__(self):
        self.agent = False
        self.home = False
        self.frames = 0
        self.sent = False
        self.exited = False
        self.autoboot = False
        self.new_crash = False
        self.guard = "marker"

    def status(self, wii):
        self.frames += 1
        result = {"agent": self.agent, "version": "1.10.0", "crash": "old crash"}
        if self.agent:
            result.update(app="WiiStation", uptime_ms=0,
                          safety={"frames": {"retraces": self.frames}, "stack_guard": self.guard})
            if self.home:
                result["overlay_ui"] = {}
        elif self.exited and self.new_crash:
            result["crash"] = "new crash"
        return result

    def list_dir(self, wii, path):
        return ([('f', 10, 'autoboot.txt')] if self.autoboot else []), False

    def send(self, wii, dol, args, name):
        assert args == ["AutoSave=0"]
        self.sent = self.agent = True

    def screen(self, wii):
        return 2, 1, b"pixels"

    def yuyv_png(self, width, height, pixels):
        return b"png"

    def send_keys(self, wii, keys):
        self.home = not self.home

    def exit_app(self, wii, seconds):
        self.agent = False
        self.exited = True

    def hbc_wait(self, wii, seconds):
        assert not self.agent


class SmokeTests(unittest.TestCase):
    def run_check(self, client):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"WII_BENCH_JOB_START": str(time.time())}), patch.object(bench.time, "sleep"):
            bench.check(client, "test-wii", Path(tmp))
            self.assertTrue((Path(tmp) / "results/pass.json").exists())

    def test_success_preserves_old_crash(self):
        client = Client()
        self.run_check(client)
        self.assertTrue(client.exited)

    def test_autoboot_refuses_upload(self):
        client = Client()
        client.autoboot = True
        with self.assertRaisesRegex(RuntimeError, "autoboot"):
            self.run_check(client)
        self.assertFalse(client.sent)

    def test_busy_refuses_upload(self):
        client = Client()
        client.agent = True
        with self.assertRaisesRegex(RuntimeError, "idle HBC"):
            self.run_check(client)
        self.assertFalse(client.sent)
        self.assertFalse(client.exited)

    def test_new_crash_fails(self):
        client = Client()
        client.new_crash = True
        with self.assertRaisesRegex(RuntimeError, "new crash"):
            self.run_check(client)

    def test_failure_still_returns_hbc(self):
        client = Client()
        client.guard = "disabled"
        with self.assertRaisesRegex(RuntimeError, "stack guard"):
            self.run_check(client)
        self.assertTrue(client.exited)

    def test_frozen_inputs_detect_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("release.dol", "release.elf", "client.py"):
                (root / name).write_bytes(b"fixture")
            bundle = root / "bundle"
            bench.prepare(root / "release.dol", root / "client.py", bundle)
            bench.verify(bundle)
            (bundle / "boot.dol").write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "changed"):
                bench.verify(bundle)


if __name__ == "__main__":
    unittest.main()
