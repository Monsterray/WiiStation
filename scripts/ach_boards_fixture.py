#!/usr/bin/env python3
"""ach_boards_fixture.py -- a seeded Local leaderboards file for test runs (Docs/ACHIEVEMENTS.md, "Local leaderboards file").

    python scripts/ach_boards_fixture.py --game-id 9000002 --out tests/fixtures/ach_boards

Writes DIR/achievements/boards/<game id>.lb for tests/fixtures/ach_boards' set (boards 1 and 2):
board 1 (a time, lower is better) has 13 Miis that all beat the 7380 the set's trigger gives, so the
seated Miis A and B land below the table's 8 rows; board 2 (a score) has 5, so they land inside it.
Guest is one of them. Written field by field in big endian, independent of the C code, so the
run also proves the format.
"""
import argparse
import pathlib
import struct


def key(n):
    return b"%08x" % (0x82630000 + n) + b"c2123456" if n else bytes(16)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-id", type=int, required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    recs = []
    day = 1760000000  # 2025-10-09
    for i in range(12):
        recs.append((1, key(0x2000 + i), 6000 + i * 100, day + i * 86400))
    recs.append((1, key(0), 6500, day + 13 * 86400))   # Guest
    for i in range(4):
        recs.append((2, key(0x3000 + i), 1500 + i * 700, day + i * 3600))
    recs.append((2, key(0), 900, day + 5 * 86400))      # Guest
    out = pathlib.Path(a.out) / "achievements" / "boards"
    out.mkdir(parents=True, exist_ok=True)
    data = b"WLB1" + struct.pack(">HHI", 1, 28, len(recs))
    for board, k, value, date in recs:
        data += struct.pack(">I", board) + k + struct.pack(">iI", value, date)
    (out / ("%d.lb" % a.game_id)).write_bytes(data)
    print("wrote %s: %d records, %d bytes" % (out / ("%d.lb" % a.game_id), len(recs), len(data)))


if __name__ == "__main__":
    main()
