import json
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import bench_achievements


class FailedSuiteEvidence(unittest.TestCase):
    def test_result_cannot_be_supplied_as_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('test.dol', 'test.elf', 'client.py', 'result.json'):
                (root / name).write_text('fixture')
            argv = ['bench_achievements.py', 'prepare', '--dol', str(root/'test.dol'),
                    '--client', str(root/'client.py'), '--bundle', str(root/'bundle'),
                    '--payload', str(root/'result.json')]
            with patch.object(sys, 'argv', argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    bench_achievements.main()
            self.assertEqual(error.exception.code, 2)
            self.assertFalse((root/'bundle/result.json').exists())

    def test_completed_startup_without_json_fails_immediately(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory)
            (bundle / "manifest.json").write_text(json.dumps({"timeout": 180}))
            client = Mock()
            client.HBCError = OSError
            client.status.return_value = {"agent": False}
            client.list_dir.return_value = ([('f', 8, 'probe.log')], False)
            client.get_file.return_value = b"boundary"
            with patch.object(bench_achievements.time, "monotonic", side_effect=[0, 0]):
                with self.assertRaisesRegex(RuntimeError, "without a result"):
                    bench_achievements.unit(client, "test-wii", bundle)
            self.assertEqual((bundle / "results/probe.log").read_bytes(), b"boundary")
            client.remove_tree.assert_called_once()

    def test_missing_result_preserves_diagnostics_without_collecting_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory)
            (bundle / "manifest.json").write_text(json.dumps({"timeout": 1}))
            client = Mock()
            client.HBCError = RuntimeError
            client.status.return_value = {"agent": False}
            client.list_dir.return_value = ([('f', 8, 'probe.log'), ('f', 64, 'private-seed')], False)
            client.get_file.return_value = b"boundary"
            with patch.object(bench_achievements.time, "monotonic", side_effect=[0, 2]):
                with self.assertRaisesRegex(RuntimeError, "fresh result"):
                    bench_achievements.unit(client, "test-wii", bundle)
            self.assertEqual((bundle / "results/probe.log").read_bytes(), b"boundary")
            self.assertFalse((bundle / "results/private-seed").exists())
            client.get_file.assert_called_once()
            client.remove_tree.assert_called_once()


if __name__ == '__main__':
    unittest.main()
