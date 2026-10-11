#!/usr/bin/env python3
"""Generate an original, deterministic PS1 disc for achievement hardware tests.

No BIOS, copyrighted game data, PS1 SDK, or external compiler is needed. The
guest polls VBlank, increments RAM at 0x180000, and writes endian/scratch data.
"""
import argparse
from pathlib import Path
import struct


def build(directory):
    directory.mkdir(parents=True, exist_ok=False)
    words, labels, fixups = [], {}, []

    def ins(op, rs, rt, imm):
        words.append((op << 26) | (rs << 21) | (rt << 16) | (imm & 65535))

    def branch(op, rs, rt, label):
        fixups.append((len(words), label))
        ins(op, rs, rt, 0)
        words.append(0)

    ins(15, 0, 8, 0x8018)  # t0 = test RAM
    ins(15, 0, 9, 0x1f80)  # t1 = scratch/MMIO
    ins(43, 8, 0, 0)
    ins(43, 8, 0, 16)
    ins(43, 9, 0, 0)
    ins(43, 9, 0, 0x1814)  # reset GPU
    ins(15, 0, 10, 0x0300)
    ins(43, 9, 10, 0x1814)  # enable display
    ins(15, 0, 10, 0x0200)
    ins(13, 10, 10, 255)
    ins(43, 9, 10, 0x1810)  # red VRAM fill
    ins(43, 9, 0, 0x1810)
    ins(15, 0, 10, 0x00f0)
    ins(13, 10, 10, 320)
    ins(43, 9, 10, 0x1810)
    labels["wait"] = len(words)
    ins(35, 9, 10, 0x1070)
    words.append(0)  # R3000 load delay
    ins(12, 10, 10, 1)
    branch(4, 10, 0, "wait")
    ins(41, 9, 0, 0x1070)  # acknowledge VBlank
    ins(35, 8, 11, 0)
    words.append(0)
    ins(9, 11, 11, 1)
    ins(43, 8, 11, 0)
    ins(9, 0, 12, 10)
    branch(5, 11, 12, "wait")
    ins(15, 0, 12, 0x1234)
    ins(13, 12, 12, 0x5678)
    ins(43, 8, 12, 16)
    ins(9, 0, 12, 9)
    ins(43, 9, 12, 0)
    branch(4, 0, 0, "wait")
    for index, label in fixups:
        words[index] |= (labels[label] - index - 1) & 65535
    code = struct.pack("<" + "I" * len(words), *words).ljust(2048, b"\0")
    header = bytearray(2048)
    header[:8] = b"PS-X EXE"
    struct.pack_into("<I", header, 0x10, 0x80010000)
    struct.pack_into("<II", header, 0x18, 0x80010000, len(code))
    struct.pack_into("<I", header, 0x30, 0x801fff00)
    exe = bytes(header) + code

    def both32(n):
        return struct.pack("<I", n) + struct.pack(">I", n)

    def record(name, sector, size, flags=0):
        length = 33 + len(name) + (len(name) % 2 == 0)
        return (bytes([length, 0]) + both32(sector) + both32(size)
                + bytes([126, 10, 10, 0, 0, 0, 0, flags, 0, 0])
                + b"\x01\0\0\x01" + bytes([len(name)]) + name
                + (b"\0" if len(name) % 2 == 0 else b""))

    sectors = [bytearray(2048) for _ in range(32)]
    root = record(b"\0", 20, 2048, 2)
    pvd = sectors[16]
    pvd[:7] = b"\x01CD001\x01"
    pvd[8:40] = b"PLAYSTATION".ljust(32)
    pvd[40:72] = b"ACHIEVEMENT FIXTURE".ljust(32)
    pvd[80:88] = both32(len(sectors))
    pvd[120:128] = b"\x01\0\0\x01" * 2
    pvd[128:132] = b"\0\x08\x08\0"
    pvd[156:156 + len(root)] = root
    pvd[881] = 1
    sectors[17][:7] = b"\xffCD001\x01"
    cnf = b"BOOT = cdrom:\\ACHV_000.01;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801fff00\r\n"
    records = root + record(b"\x01", 20, 2048, 2) + record(b"SYSTEM.CNF;1", 21, len(cnf)) + record(b"ACHV_000.01;1", 22, len(exe))
    sectors[20][:len(records)] = records
    sectors[21][:len(cnf)] = cnf
    sectors[22][:] = exe[:2048]
    sectors[23][:] = exe[2048:]
    raw = bytearray()
    for i, sector in enumerate(sectors):
        m, rest = divmod(i + 150, 4500)
        s, f = divmod(rest, 75)
        bcd = lambda n: (n // 10) * 16 + n % 10
        raw += b"\0" + b"\xff" * 10 + b"\0" + bytes([bcd(m), bcd(s), bcd(f), 2])
        raw += b"\0\0\x08\0" * 2 + sector + b"\0" * 280
    (directory / "fixture.bin").write_bytes(raw)
    (directory / "fixture.cue").write_text('FILE "fixture.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    build(parser.parse_args().directory)
