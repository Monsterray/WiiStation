"""disc_serial.py: the serial from SYSTEM.CNF (raw cue/bin) and from a PBP's PARAM.SFO."""
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "scripts"))
import achievement_fixture  # noqa: E402
import disc_serial  # noqa: E402


def pbp(disc_id):
    keys = b"DISC_ID\0"
    value = disc_id.encode() + b"\0"
    entry = struct.pack("<HHIII", 0, 0x0204, len(value), 16, 0)
    key_at = 20 + len(entry)
    sfo = b"\0PSF" + struct.pack("<IIII", 0x101, key_at, key_at + len(keys), 1) + entry + keys + value
    return b"\0PBP" + struct.pack("<I", 0x10000) + struct.pack("<8I", 40, 40 + len(sfo), *([40 + len(sfo)] * 6)) + sfo


class DiscSerial(unittest.TestCase):
    def test_cue_bin(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp) / "disc"
            achievement_fixture.build(d)
            self.assertEqual(disc_serial.serial_of(d / "fixture.cue"), "ACHV00001")

    def test_pbp(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "g.pbp"
            p.write_bytes(pbp("SCUS94467"))
            self.assertEqual(disc_serial.serial_of(p), "SCUS94467")

    def test_not_a_disc(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "x.iso"
            p.write_bytes(b"\0" * 50000)
            self.assertIsNone(disc_serial.serial_of(p))
            self.assertIsNone(disc_serial.serial_of(pathlib.Path(tmp) / "missing.cue"))


if __name__ == "__main__":
    unittest.main()
