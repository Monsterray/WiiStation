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
MAGIC, VERSION = 0x58445450, 3
F_CPU = 33868800                      # ACK times are system clock ticks
PORT = struct.Struct('<20s40sBBBxHHII32xHxx4s4s128s128s16s')
PRESS = struct.Struct('<16H')
PRESS_AT = 76
PORT_SIZE = 392
NO_ACK = 0xFFFF

CFG = ['45 normal', '43 enter', '42 config', '45 type', '46 act0', '46 act1', '46 act2', '47',
       '48', '4C 0', '4C 1', '4F (DS2)', '44 analog', '4D map', '4D again', '43 exit']
# Bytes 1..8 and length of each reply from a PS1 DualShock (SCPH-1200); None = any.
# Same table as the ROM's controllers.c (psx-spx, DuckStation, MiSTer, PsxNewLib).
WANT = [
    [0xFF, None, None, None, None, None, None, None],
    [None, 0x5A, None, None, None, None, None, None],
    [0xF3, 0x5A, None, None, None, None, None, None],
    [0xF3, 0x5A, 0x01, 0x02, None, 0x02, 0x01, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A],
    [0xF3, 0x5A, 0x00, 0x00, 0x01, 0x01, 0x01, 0x14],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    [0xF3, 0x5A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF],
    [0xF3, 0x5A, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF],
    [0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
]
WANT_LEN = [2, None, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9]
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
    reply, ack, rlen, typ, cfg_state, buttons, byte_ticks, polls, changes, multi, amin, amax, seen, cfg, cfg_len = \
        PORT.unpack(b)
    press = PRESS.unpack_from(b, PRESS_AT)
    ack = struct.unpack('<20H', ack)
    fails = []
    print('port %d: id %02X (%s)  polls %d  id changes %d  cfg state %d'
          % (n + 1, typ, TYPES.get(typ, '?'), polls, changes, cfg_state))
    if typ == 0xFF:
        return fails
    print('  last poll  %s' % hexs(reply[:max(rlen, 1)]))
    print('  /ACK after %s  (us from the end of each byte; -- = none)'
          % ' '.join('--' if a == NO_ACK else '%.1f' % (a * 1e6 / F_CPU) for a in ack[:max(rlen - 1, 1)]))
    print('  reply length %d, byte 1 took %.1f us (hardware: 32 us at 250 kHz)' % (rlen, byte_ticks * 1e6 / F_CPU))
    digital = cfg[8] == 0xFF          # 43h (enter) unanswered: a digital pad
    ok = 0
    for i, name in enumerate(CFG):
        got = cfg[i * 8:i * 8 + 8]
        if digital:     # A digital pad answers 42h (a read) and nothing else: FFh, 2 bytes
            want, wlen = ([0x41, 0x5A] + [None] * 6, 5) if CFG[i].startswith('42') else ([0xFF] + [None] * 7, 2)
        else:
            want, wlen = WANT[i], WANT_LEN[i]
        bad = [j for j, w in enumerate(want) if w is not None and got[j] != w]
        if wlen is not None and cfg_len[i] != wlen:
            bad.append('len')
        ok += not bad
        shown = ' '.join('..' if w is None else '%02X' % w for w in want)
        print('  cfg %-10s %s  len %d  %s' % (name, hexs(got), cfg_len[i],
              'ok' if not bad else 'want %s len %s' % (shown, wlen)))
    print('  cfg: %d/%d replies as a %s' % (ok, len(CFG), 'digital pad (SCPH-1080)' if digital
                                              else 'PS1 DualShock (SCPH-1200)'))
    if sweep and ok != len(CFG):
        fails.append('port %d: config %d/%d' % (n + 1, ok, len(CFG)))
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
