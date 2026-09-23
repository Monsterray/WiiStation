#!/usr/bin/env python3
"""padtest_dx.py VRAM.BIN [--sweep]

Decode the status block that PadTest DX (github.com/Monsterray/padtest, include/dx.h)
writes to VRAM each frame, from a WiiStation 'dump <vblank>' (sd:/wiisxrx/vram.bin).
Prints, per port: the ID, the last poll reply with its /ACK pattern, the replies to the
config commands against a DualShock's (psx-spx), and the press and axis counters.
--sweep: also judge a padsweep run on port 1, the port the sweep drives (14 button bits,
one at a time; analog axes 0..255, finely graded). Exit status 1 if a check fails.
scripts/padtest_dx.sh builds the whole run.
"""
import struct
import sys

X, Y, W, H = 640, 256, 64, 8          # DX_VRAM_* in dx.h
MAGIC, VERSION = 0x58445450, 1
PORT = struct.Struct('<20s40sBBBxHxxII32xHxx4s4s128s80s')
PRESS = struct.Struct('<16H')
PRESS_AT = 76
PORT_SIZE = 328
NO_ACK = 0xFFFF

CFG = ['43 enter', '45 type', '46 act0', '46 act1', '47', '4C 0', '4C 1', '44 analog', '4D rumble', '43 exit']
# Bytes 1..8 of each reply from a DualShock; None = any. Same table as controllers.c.
WANT = [
    [None, 0x5A, None, None, None, None, None, None],
    [0xF3, 0x5A, None, 0x02, None, 0x02, 0x01, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A],
    [0xF3, 0x5A, 0x00, 0x00, 0x01, 0x01, 0x01, 0x14],
    [0xF3, 0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    [0xF3, 0x5A, None, None, None, None, None, None],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
]
BUTTONS = ['L2', 'R2', 'L1', 'R1', 'Tri', 'Cir', 'X', 'Sq',
           'Sel', 'L3', 'R3', 'Start', 'Up', 'Right', 'Down', 'Left']
AXES = ['LX', 'LY', 'RX', 'RY']
TYPES = {0x41: 'digital', 0x73: 'analog', 0x53: 'flight', 0x12: 'mouse', 0xFF: 'none'}


def block(vram):
    rows = [vram[((Y + r) * 1024 + X) * 2:((Y + r) * 1024 + X + W) * 2] for r in range(H)]
    return b''.join(rows)


def hexs(b):
    return ' '.join('%02X' % v for v in b)


def port(n, b, sweep):
    reply, ack, rlen, typ, cfg_state, buttons, polls, changes, multi, amin, amax, seen, cfg = \
        PORT.unpack(b)
    press = PRESS.unpack_from(b, PRESS_AT)
    ack = struct.unpack('<20H', ack)
    fails = []
    print('port %d: id %02X (%s)  polls %d  id changes %d  cfg state %d'
          % (n + 1, typ, TYPES.get(typ, '?'), polls, changes, cfg_state))
    if typ == 0xFF:
        return fails
    print('  last poll  %s' % hexs(reply[:max(rlen, 1)]))
    print('  /ACK wait  %s  (loops after each byte; -- = none)'
          % ' '.join('--' if a == NO_ACK else str(a) for a in ack[:max(rlen - 1, 1)]))
    print('  reply length %d' % rlen)
    ok = 0
    for i, name in enumerate(CFG):
        got = cfg[i * 8:i * 8 + 8]
        bad = [j for j, w in enumerate(WANT[i]) if w is not None and got[j] != w]
        ok += not bad
        want = ' '.join('..' if w is None else '%02X' % w for w in WANT[i])
        print('  cfg %-10s %s  %s' % (name, hexs(got), 'ok' if not bad else 'want ' + want))
    print('  cfg: %d/%d replies as a DualShock' % (ok, len(CFG)))
    if cfg[16] != 0xF3:
        print('  cfg: 46h did not answer F3h: the device takes no config commands (a digital pad)')
    bits = [BUTTONS[i] + ' %d' % c for i, c in enumerate(press) if c]
    print('  presses: %s  (multi-bit %d)' % (', '.join(bits) or 'none', multi))
    if typ == 0x73 or any(seen):
        for a in range(4):
            d = sum(bin(v).count('1') for v in seen[a * 32:a * 32 + 32])
            print('  %s %3d..%3d  %3d distinct' % (AXES[a], amin[a], amax[a], d))
            if sweep and (amin[a] != 0 or amax[a] != 255 or d < 150):
                fails.append('port %d %s' % (n + 1, AXES[a]))
    if sweep:
        if sum(1 for c in press if c) < 14:
            fails.append('port %d: %d button bits' % (n + 1, sum(1 for c in press if c)))
        if multi:
            fails.append('port %d: %d multi-bit presses' % (n + 1, multi))
    return fails


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    sweep = '--sweep' in sys.argv
    b = block(open(args[0], 'rb').read())
    magic, version, size, frame, vblank = struct.unpack_from('<IHHII', b)
    if magic != MAGIC:
        sys.exit('no PadTest DX status block at (%d,%d): magic %08X' % (X, Y, magic))
    if version != VERSION:
        sys.exit('status block version %d; this script reads %d' % (version, VERSION))
    print('PadTest DX: frame %d, vblank %d' % (frame, vblank))
    fails = []
    for n in range(2):
        fails += port(n, b[16 + n * PORT_SIZE:16 + (n + 1) * PORT_SIZE], sweep and n == 0)
    if sweep:
        print('sweep: ' + ('PASS' if not fails else 'FAIL: ' + '; '.join(fails)))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
