import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import bench_achievement_games as bench


class ResultTests(unittest.TestCase):
    def fixture(self, output):
        cases = []
        for core in range(3):
            for enabled in (0, 1, 0):
                cases.append({"serial": f"ACHV0000{core + 1}", "enabled": enabled})
        cases += [{"serial": f"ACHV0000{i}", "enabled": 1, "frames": 209 if i < 7 else 240} for i in range(4, 8)]
        lines = []
        for index, case in enumerate(cases, 1):
            count = case["enabled"] * 3
            lines += ["wall: wall_us=1000000 vblanks=240",
                      f'achievement-bench: game={case["serial"]} frames={case.get("frames", 240)} count={count} earned={count} enabled={case["enabled"]}']
            if 10 <= index <= 12:
                core = index - 10
                lines += [f"achievement-bench-reset: core={core} earned=3 expected=3 count=3",
                          f"achievement-bench-swap: core={core} result=0 count=0 earned=0"]
            lines.append(f"=== chain {index}/13 end vblanks={case.get("frames", 240)} ===")
        (output / "perf.log").write_text("\n".join(lines) + "\n")
        for core in range(3):
            for name, bits in ((f"before{core}.st", 3), (f"after{core}.st", 7)):
                data = b"ACH1" + b"\0" * 4 + b"WAC1" + b"\0" * 16 + bytes([bits])
                (output / name).write_bytes(gzip.compress(data))
        for suffix in range(1, 7):
            (output / f"unlocks{suffix}.txt").write_text("digest 1 One\ndigest 2 Two\ndigest 3 Three\n")
        return cases

    def test_valid_and_missing_completion(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            cases = self.fixture(output)
            self.assertTrue(bench.judge(output, cases)["pass"])
            log = output / "perf.log"
            log.write_text(log.read_text().replace("=== chain 13/13", "incomplete"))
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                bench.judge(output, cases)

    def test_wrong_state_bits(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            cases = self.fixture(output)
            (output / "after0.st").write_bytes(gzip.compress(b"ACH1" + b"\0" * 4 + b"WAC1" + b"\0" * 17))
            with self.assertRaisesRegex(RuntimeError, "earned bits"):
                bench.judge(output, cases)

    def test_duplicate_journal(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            cases = self.fixture(output)
            (output / "unlocks1.txt").write_text("digest 1 One\ndigest 1 One\ndigest 3 Three\n")
            with self.assertRaisesRegex(RuntimeError, "Duplicate"):
                bench.judge(output, cases)


if __name__ == "__main__":
    unittest.main()
