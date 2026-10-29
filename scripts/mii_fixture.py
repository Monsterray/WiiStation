"""mii_fixture.py [N] [--out PATH] -- a Mii database of N fictional Miis for Dolphin runs.

Dolphin ships no RFL_DB.dat, so the Players menu (Docs/MII_PLAN.md) has nothing to show there.
This writes one, made the way hbc-reborn's tests/dolphin_mii.py makes its own (the RNOD block,
an empty RNHD hidden database, RFL's CRC), with Miis from tests/mii_fixture (the Mii SDK on the
PC: random faces, names from a list, every favorite color, every fifth a favorite).

Default output: the game library's NAND skeleton, C:/wiistation-lib/wii/shared2/menu/FaceLib/,
which scripts/profile.sh copies into every run profile. No faces: RFL_Res.dat is Nintendo's
and stays on the Wii, so Dolphin runs show the coins' initials.
"""
import argparse
import binascii
import pathlib
import struct
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
TOOL = REPO / ".runs" / "mii_fixture.exe"
CLANG = r"C:\Program Files\LLVM\bin\clang.exe"
SIZE, CRC_AT, MII, SLOTS = 0xBE6C0, 0x1F1DE, 74, 100


def build_tool():
    src = [REPO / "tests/mii_fixture/mii_fixture.c", REPO / "deps/mii/mii.c"]
    if TOOL.exists() and all(TOOL.stat().st_mtime > s.stat().st_mtime for s in src):
        return
    subprocess.run([CLANG, "-O1", "-DMII_NO_OGC", f"-I{REPO / 'deps/mii'}", *map(str, src),
                    "-o", str(TOOL)], check=True)


def database(miis):
    f = bytearray(SIZE)
    f[0:4] = b"RNOD"
    f[4:4 + len(miis)] = miis
    f[0x1D00:0x1D04] = b"RNHD"
    struct.pack_into(">hh", f, 0x1D04, -1, -1)
    f[0x1D08:0x1D08 + 10000 * 12] = (bytes(8) + bytes.fromhex("7fff7fff")) * 10000   # free nodes
    struct.pack_into(">H", f, CRC_AT, binascii.crc_hqx(bytes(f[:CRC_AT]), 0))
    return bytes(f)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("n", nargs="?", type=int, default=20)
    ap.add_argument("--out", default="C:/wiistation-lib/wii/shared2/menu/FaceLib/RFL_DB.dat")
    a = ap.parse_args()
    if not 0 <= a.n <= SLOTS:
        sys.exit(f"N must be 0-{SLOTS}")
    build_tool()
    miis = subprocess.run([str(TOOL), str(a.n)], check=True, capture_output=True).stdout
    assert len(miis) == a.n * MII, len(miis)
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(database(miis))
    print(f"{out}: {a.n} Miis")


if __name__ == "__main__":
    main()
