#!/usr/bin/env python3
"""padtest_dx.py -- PadTest DX status block: decode it, and judge the controller matrix.

  padtest_dx.py show VRAM.BIN            what the ROM received, per port and slot
  padtest_dx.py chain OUT.TXT [CELLS]    write the matrix chain (one boot runs every cell)
  padtest_dx.py judge RUN_DIR [CELLS]    one PASS/FAIL line per cell, from RUN_DIR/vram_NN.bin
  padtest_dx.py cells                    list the cells

PadTest DX is github.com/Monsterray/padtest (built in C:\\projects\\padtest). Each frame it
copies a status block (its include/dx.h) to VRAM; a chained run leaves each game's VRAM in
sd:/wiistation/vram_NN.bin. scripts/padtest_dx.sh does the whole run. CELLS is a comma list of
cell names (default: all). The protocol and what each check rests on: the fork's
docs/PROTOCOL.md (psx-spx, Mednafen's multitap notes, DuckStation).
"""
import os
import struct
import sys

X, Y, W, H = 640, 256, 64, 32          # DX_VRAM_* in dx.h
MAGIC, VERSION = 0x58445450, 4
F_CPU = 33868800                       # /ACK times are system clock ticks
NO_ACK = 0xFFFF
NO_FRAME = 0xFFFF
SLOTS = 4
CFG_N = 16
PROBE_N = 12
LONG_LEN = 36

SLOT = struct.Struct('<20s40sBBBBHHII32s32sHHI8s4s4s128s128s16s')
PORT = struct.Struct('<BBBBHHHHI36s12s12s12s')
HEADER = struct.Struct('<IHHIIHHHH')
assert SLOT.size == 436 and PORT.size == 88

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
# Bits of the ROM's button word (dx.h: ~((byte3 << 8) | byte4), 1 = pressed)
BUTTONS = ['L2', 'R2', 'L1', 'R1', 'Tri', 'Cir', 'X', 'Sq',
           'Sel', 'L3', 'R3', 'Start', 'Up', 'Right', 'Down', 'Left']
DPAD = {12, 13, 14, 15}
AXES = ['LX', 'LY', 'RX', 'RY']
TYPES = {0x41: 'digital', 0x73: 'analog', 0x53: 'flight', 0x12: 'mouse', 0x23: 'negcon',
         0x31: 'konami gun', 0x63: 'guncon', 0xF3: 'config', 0xFF: 'none'}
REPLY_LEN = {0x41: 5, 0x73: 9, 0x53: 9, 0x12: 7, 0x63: 9}   # bytes clocked, HiZ included

# The order PadWiiSX.c's "sweep" presses buttons in (PSX mask bits), and the swap that makes
# each script port's order its own: port k > 0 swaps the pair at k - 1 and k.
SWEEP_ORDER = [0x0001, 0x0008, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
               0x0010, 0x0020, 0x0040, 0x0080]
SCRIPT_PORTS = ['p1', 'p2', 'p1b', 'p1c', 'p1d', 'p2b', 'p2c', 'p2d']


def rom_bit(mask):
    """A PSX mask bit (Start 0008) as the ROM's button bit (wire bytes swapped)."""
    b = mask.bit_length() - 1
    return b + 8 if b < 8 else b - 8


def sweep_order(sp):
    order = list(SWEEP_ORDER)
    if sp > 0:
        order[sp - 1], order[sp] = order[sp], order[sp - 1]
    return [rom_bit(m) for m in order]


def script_port(port, slot):
    """The script port that plays PlayStation port/slot (PadSSSPSX.c script_port)."""
    if slot == 0:
        return port
    return (2 if port == 0 else 5) + slot - 1


# ---------------------------------------------------------------------------------------
# The matrix. A cell is one chain game: PadTest DX with one controller setup. Each port is
# 'none', 'pad' (a controller on the port) or 'tap' (a multitap) with the slots listed
# filled ('ABCD', 'B-D' ...). GameCube pad 1 (the only one Dolphin's test profile has)
# plays port 1, or multitap 1's slot A, through the real driver ("padsweep 1 fast"); every
# other port and slot gets the script's own sweep, in an order of its own.
# ---------------------------------------------------------------------------------------
CELLS = [
    dict(name='pads_ct1', ct=1, ports=[('pad', ''), ('pad', '')]),
    dict(name='pad_ct0', ct=0, ports=[('pad', ''), ('none', '')]),
    dict(name='tap1_ct1', ct=1, ports=[('tap', 'ABCD'), ('none', '')]),
    dict(name='taps_ct1', ct=1, ports=[('tap', 'ABCD'), ('tap', 'ABCD')]),
    dict(name='taps_ct0', ct=0, ports=[('tap', 'ABCD'), ('tap', 'ABCD')]),
    dict(name='tap1_bd_ct1', ct=1, ports=[('tap', 'B-D'), ('pad', '')]),
    dict(name='tap2_ac_ct2', ct=2, ports=[('none', ''), ('tap', 'A-C')]),
    dict(name='tap1_empty_ct0', ct=0, ports=[('tap', ''), ('pad', '')]),
    dict(name='pad_tap2_ab_ct2', ct=2, ports=[('pad', ''), ('tap', 'AB')]),
    # Multitap -> GameCube pad -> multitap on port 1 while the ROM runs ("padtype" lines):
    # the ROM must see the multitap go and come back, with the unplug gap each time.
    dict(name='switch_ct1', ct=1, ports=[('tap', 'ABCD'), ('none', '')],
         switch=[(1, 1), (1, 4)]),
    # SioTiming Accurate (sio.c): the same checks, and a byte and its /ACK timed as on a PS1
    dict(name='pads_ct1_sio', ct=1, ports=[('pad', ''), ('pad', '')], sets=['SioTiming=1']),
    dict(name='taps_ct1_sio', ct=1, ports=[('tap', 'ABCD'), ('tap', 'ABCD')], sets=['SioTiming=1']),
]
# SioTiming Accurate, in system clock ticks as the ROM measures them: a byte is 8 bits at the
# BIOS's 250 kHz (0x88 x 8 = 1088 ticks, 32 us), and a controller's /ACK comes 6.8..13.7 us
# after it on hardware (DuckStation's measurement; sio.c uses 450 ticks). The ROM's own
# polling adds a little to both.
BYTE_TICKS = (1060, 1160)
ACK_TICKS = (230, 520)
BOOT = 10           # vblanks before the ROM's main loop: under 1 (frame = vblank = chain vblanks - 1)
SWEEP_PERIOD = 214  # PadWiiSX.c sweep and controller-GC.c fast padsweep: 84 + 2 * 65
CONFIG = 40         # two config tests (digital, then analog) of 17 frames each, and margin
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'autoinput')


def switch_vblanks():
    """The vblanks of the switch cell's padtype lines (scripts/autoinput/padtest_dx_switch.txt)."""
    with open(os.path.join(SCRIPTS, 'padtest_dx_switch.txt')) as f:
        return [int(l.split()[1]) for l in f if l.startswith('padtype ')]


def cell_vblanks(c):
    if c.get('switch'):
        return switch_vblanks()[-1] + CONFIG + 120   # back as a multitap: config test again, then a look
    return BOOT + CONFIG + SWEEP_PERIOD + 40


def filled(c, port, slot):
    kind, slots = c['ports'][port]
    if kind == 'pad':
        return slot == 0
    if kind == 'tap':
        return 'ABCD'[slot] in slots
    return False


def settings(c):
    """The chain line's settings: manual assignment, every port and slot type."""
    ptype = {'none': 0, 'pad': 1, 'tap': 4}
    # GameCube pad 1 on port 1 or slot 1A only: multitap 2's slot A would take it too by default
    s = ['PadAutoAssign=0', 'ControllerType=%d' % c['ct'], 'PadAssign1=0', 'PadAssign3=0', 'PadAssign7=1']
    s += c.get('sets', [])
    for port in range(2):
        kind = c['ports'][port][0]
        s.append('PadType%d=%d' % (port + 1, ptype[kind]))
        for slot in range(SLOTS):
            on = kind == 'tap' and filled(c, port, slot)
            s.append('PadType%d=%d' % (3 + 4 * port + slot, 1 if on else 0))
    return s


def cell_script(c):
    return 'padtest_dx_switch.txt' if c.get('switch') else 'padtest_dx_matrix.txt'


def write_chain(path, cells):
    with open(path, 'w', newline='\n') as f:
        f.write('CHAIN\n# PadTest DX controller matrix (scripts/padtest_dx.py chain)\n')
        for c in cells:
            f.write('# %s\n%d sd:/wiistation/%s %s\nsd:/wiisxrx/isos/PadTestDX\npadtest.cue\n'
                    % (c['name'], cell_vblanks(c), cell_script(c), ' '.join(settings(c))))


# ---------------------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------------------
def block(vram):
    rows = [vram[((Y + r) * 1024 + X) * 2:((Y + r) * 1024 + X + W) * 2] for r in range(H)]
    return b''.join(rows)


def decode(vram):
    b = block(vram)
    magic, version, size, frame, vblank, read1, read2, lines, late = HEADER.unpack_from(b, 0)
    if magic != MAGIC:
        raise ValueError('no PadTest DX status block at (%d,%d): magic %08X' % (X, Y, magic))
    if version != VERSION:
        raise ValueError('status block version %d; this script reads %d' % (version, VERSION))
    st = dict(frame=frame, vblank=vblank, lines_read=(read1, read2), lines_frame=lines, late=late,
              ports=[], slots=[[], []])
    off = HEADER.size
    for _ in range(2):
        f = PORT.unpack_from(b, off)
        off += PORT.size
        st['ports'].append(dict(tap=f[0], en_slot=f[1], long_len=f[2], probes=f[3], tap_on=f[4],
                                tap_off=f[5], empty_frames=f[6], plain_frames=f[7],
                                long_reads=f[8], long_reply=f[9], probe_len=f[10],
                                probe_id=f[11], probe_last=f[12]))
    for p in range(2):
        for _ in range(SLOTS):
            f = SLOT.unpack_from(b, off)
            off += SLOT.size
            st['slots'][p].append(dict(
                reply=f[0], ack=struct.unpack('<20H', f[1]), reply_len=f[2], type=f[3],
                cfg_state=f[4], flags=f[5], buttons=f[6], byte_ticks=f[7], polls=f[8],
                type_changes=f[9], press=struct.unpack('<16H', f[10]),
                first=struct.unpack('<16H', f[11]), multi=f[12], long_diff=f[13],
                long_polls=f[14], long_reply=f[15], axis_min=f[16], axis_max=f[17],
                seen=f[18], cfg=f[19], cfg_len=f[20]))
    return st


def hexs(b):
    return ' '.join('%02X' % v for v in b)


def axis_values(s, a):
    return sum(bin(v).count('1') for v in s['seen'][a * 32:a * 32 + 32])


def cfg_check(s):
    """(replies that match, total, 'dualshock'|'digital', [(name, got, len, ok, want)])"""
    digital = s['cfg'][8] == 0xFF          # 43h (enter) unanswered: a digital pad
    rows, ok = [], 0
    for i, name in enumerate(CFG):
        got = s['cfg'][i * 8:i * 8 + 8]
        if digital:     # A digital pad answers 42h (a read) and nothing else: FFh, 2 bytes
            want, wlen = ([0x41, 0x5A] + [None] * 6, 5) if name.startswith('42') else ([0xFF] + [None] * 7, 2)
        else:
            want, wlen = WANT[i], WANT_LEN[i]
        bad = [j for j, w in enumerate(want) if w is not None and got[j] != w]
        if wlen is not None and s['cfg_len'][i] != wlen:
            bad.append('len')
        ok += not bad
        rows.append((name, got, s['cfg_len'][i], not bad,
                     ' '.join('..' if w is None else '%02X' % w for w in want) + ' len %s' % wlen))
    return ok, len(CFG), 'digital' if digital else 'dualshock', rows


def show_slot(name, s, verbose):
    t = s['type']
    if t == 0xFF and not s['polls']:
        print('  %s: empty (address byte unanswered: %d byte)' % (name, s['reply_len']))
        return
    print('  %s: id %02X (%s)  polls %d  id changes %d  cfg state %d'
          % (name, t, TYPES.get(t, '?'), s['polls'], s['type_changes'], s['cfg_state']))
    n = max(s['reply_len'], 1)
    print('     last read  %s' % hexs(s['reply'][:n]))
    print('     /ACK after %s  (us; -- = none)'
          % ' '.join('--' if a == NO_ACK else '%.1f' % (a * 1e6 / F_CPU) for a in s['ack'][:n]))
    print('     %d bytes, byte 1 took %.1f us (hardware: 32 us at 250 kHz)'
          % (s['reply_len'], s['byte_ticks'] * 1e6 / F_CPU))
    ok, tot, kind, rows = cfg_check(s)
    if verbose:
        for nm, got, ln, good, want in rows:
            print('     cfg %-10s %s  len %d  %s' % (nm, hexs(got), ln, 'ok' if good else 'want ' + want))
    print('     cfg: %d/%d replies as a %s' % (ok, tot, 'digital pad (SCPH-1080)' if kind == 'digital'
                                                 else 'PS1 DualShock (SCPH-1200)'))
    bits = [BUTTONS[i] + ' %d' % c for i, c in enumerate(s['press']) if c]
    print('     presses: %s  (multi-bit %d)' % (', '.join(bits) or 'none', s['multi']))
    order = sorted((f, b) for b, f in enumerate(s['first']) if f != NO_FRAME)
    if order:
        print('     first presses: %s' % ' '.join(BUTTONS[b] for _, b in order))
    if t == 0x73 or any(s['seen']):
        print('     axes: %s' % '  '.join('%s %d..%d (%d)' % (AXES[a], s['axis_min'][a], s['axis_max'][a],
                                                             axis_values(s, a)) for a in range(4)))
    if s['long_polls'] or s['long_reply'] != b'\xff' * 8:
        print('     long read block %s  (%d reads, %d with other buttons)'
              % (hexs(s['long_reply']), s['long_polls'], s['long_diff']))


PROBES = ['read, TAP 1', 'long, TAP 1', 'long, TAP 1', 'long, 00h blocks', 'long after 00h',
          'long, TAP 0', 'read, TAP 0', 'address 00h', 'address 05h', 'read, TAP 1',
          'long, cmd 43h', 'read, TAP 0']


def show(st, verbose=True):
    print('PadTest DX: frame %d, vblank %d; last frame %d scanlines, reading the ports %d + %d; %d frames late'
          % (st['frame'], st['vblank'], st['lines_frame'], st['lines_read'][0], st['lines_read'][1], st['late']))
    for p in range(2):
        q = st['ports'][p]
        if q['tap']:
            print('port %d: MULTITAP  long read %d bytes: %s' % (p + 1, q['long_len'], hexs(q['long_reply'][:q['long_len']])))
        else:
            print('port %d: %s' % (p + 1, 'no multitap' if q['long_len'] > 1 else 'nothing answers'))
        print('  long reads %d, multitap on %d off %d, empty frames %d, plain frames %d, request to slot %s'
              % (q['long_reads'], q['tap_on'], q['tap_off'], q['empty_frames'], q['plain_frames'], 'ABCD'[q['en_slot'] & 3]))
        if q['probes']:
            print('  probes (%d): %s' % (q['probes'], ', '.join(
                '%s %d/%02X/%02X' % (PROBES[i], q['probe_len'][i], q['probe_id'][i], q['probe_last'][i])
                for i in range(PROBE_N))))
        for s in range(SLOTS):
            show_slot('ABCD'[s] if q['tap'] or s else 'pad', st['slots'][p][s], verbose)


# ---------------------------------------------------------------------------------------
# Judging
# ---------------------------------------------------------------------------------------
# Distinct values an axis must give: the scripted sweep walks 0, 4, .. 252, 255 (65); the
# GameCube pad's fast padsweep walks the raw stick in steps of 3, and its C-stick, which has
# less travel than the 96 swept, reaches its ends early (59 measured).
LEAST_AXIS = {'script': 65, 'gc': 50}


def judge_timing(where, s):
    fails = []
    n = s['reply_len']
    if not BYTE_TICKS[0] <= s['byte_ticks'] <= BYTE_TICKS[1]:
        fails.append('%s: byte took %.1f us, want 32' % (where, s['byte_ticks'] * 1e6 / F_CPU))
    late = [a for a in s['ack'][:n - 1] if not ACK_TICKS[0] <= a <= ACK_TICKS[1]]
    if late:
        fails.append('%s: /ACK after %s us, want 6.8..13.7' % (where, ' '.join('%.1f' % (a * 1e6 / F_CPU) for a in late[:3])))
    return fails


def judge_device(where, s, ct, sp, tap, checks):
    """A filled port or slot. sp: the script port whose sweep order it must show, or None
    for GameCube pad 1 (the driver's padsweep)."""
    fails = []
    want = 0x73 if ct == 1 else 0x41
    if s['type'] != want:
        return ['%s: id %02X, want %02X' % (where, s['type'], want)]
    n = REPLY_LEN[want]
    if s['reply_len'] != n:
        fails.append('%s: read %d bytes, want %d' % (where, s['reply_len'], n))
    elif any(a == NO_ACK for a in s['ack'][:n - 1]) or s['ack'][n - 1] != NO_ACK:
        fails.append('%s: /ACK pattern %s' % (where, ' '.join('-' if a == NO_ACK else 'a' for a in s['ack'][:n])))
    ok, tot, kind, _ = cfg_check(s)
    if (kind == 'dualshock') != (ct == 1) or ok != tot:
        fails.append('%s: config %d/%d as %s' % (where, ok, tot, kind))
    bits = [b for b in range(16) if s['press'][b]]
    if len(bits) < 14:
        fails.append('%s: %d button bits' % (where, len(bits)))
    if s['multi']:
        fails.append('%s: %d multi-bit presses' % (where, s['multi']))
    if ct == 1 and 'axes' in checks:
        for a in range(4):
            d = axis_values(s, a)
            if s['axis_min'][a] != 0 or s['axis_max'][a] != 255 or d < LEAST_AXIS['gc' if sp is None else 'script']:
                fails.append('%s %s %d..%d %d values' % (where, AXES[a], s['axis_min'][a], s['axis_max'][a], d))
    if sp is not None and 'order' in checks:
        skip = DPAD if ct == 2 else set()
        want_o = [b for b in sweep_order(sp) if b not in skip]
        got = [b for f, b in sorted((f, b) for b, f in enumerate(s['first']) if f != NO_FRAME) if b not in skip]
        if sorted(got) != sorted(want_o):
            fails.append('%s: first presses of %d of %d buttons' % (where, len(got), len(want_o)))
        else:
            succ = lambda o: {o[i]: o[(i + 1) % len(o)] for i in range(len(o))}
            if succ(got) != succ(want_o):
                match = [SCRIPT_PORTS[k] for k in range(8)
                         if succ([b for b in sweep_order(k) if b not in skip]) == succ(got)]
                fails.append('%s: buttons in %s order, want %s' % (where, '/'.join(match) or 'no script port\'s',
                                                                  SCRIPT_PORTS[sp]))
    if tap:
        lr = s['long_reply']
        if not s['long_polls'] or lr[0] != want or lr[1] != 0x5A:
            fails.append('%s: long read block %s' % (where, hexs(lr)))
        elif want == 0x41 and lr[4:8] != b'\xff' * 4:
            fails.append('%s: digital pad block not padded with FFh: %s' % (where, hexs(lr)))
        if s['long_diff']:
            fails.append('%s: %d frames the long read had other buttons' % (where, s['long_diff']))
    return fails


def judge_empty(where, s, tap):
    fails = []
    if s['polls'] or s['type'] != 0xFF:
        fails.append('%s: should be empty, id %02X, %d reads' % (where, s['type'], s['polls']))
    if s['reply_len'] != 1:
        fails.append('%s: empty, but %d bytes clocked (want 1: no /ACK)' % (where, s['reply_len']))
    if tap and s['long_reply'] != b'\xff' * 8:
        fails.append('%s: empty slot block %s, want FFh x8' % (where, hexs(s['long_reply'])))
    return fails


def judge_probes(where, q, slot_a):
    """The multitap's request rules (controllers.c Probe; docs/PROTOCOL.md)."""
    fails = []
    ln, idb, last = q['probe_len'], q['probe_id'], q['probe_last']
    if not q['probes']:
        return ['%s: no probe sequence ran' % where]
    want = {1: 35, 2: 35, 3: 35, 4: 4, 5: 35, 7: 1, 8: 1}
    for i, n in want.items():
        if ln[i] != n:
            fails.append('%s probe %d (%s): %d bytes, want %d' % (where, i, PROBES[i], ln[i], n))
    for i in (1, 2, 3, 4, 5, 10):
        if ln[i] >= 2 and idb[i] != 0x80:
            fails.append('%s probe %d (%s): id %02X, want 80' % (where, i, PROBES[i], idb[i]))
    for i in (0, 6, 9, 11):
        if ln[i] < 5 or idb[i] == 0x80:
            fails.append('%s probe %d (%s): %d bytes id %02X, want a single-slot reply'
                         % (where, i, PROBES[i], ln[i], idb[i]))
    if ln[4] == 4 and last[4] not in (slot_a, 0xF3):
        fails.append('%s probe 4: last byte %02X, want slot A\'s ID %02X' % (where, last[4], slot_a))
    if ln[10] not in (3, 4):
        fails.append('%s probe 10 (%s): %d bytes, want 4 (Mednafen) or 3 (DuckStation)' % (where, PROBES[10], ln[10]))
    return fails


def judge(st, c):
    fails = []
    checks = {'axes', 'order'}
    if c.get('switch'):
        checks = set()      # the second config test eats into the sweep; the switch is the point
    for p in range(2):
        kind, _ = c['ports'][p]
        q = st['ports'][p]
        tap = kind == 'tap'
        any_slot = any(filled(c, p, s) for s in range(SLOTS))
        port = 'port %d' % (p + 1)
        if tap and any_slot:
            if not q['tap']:
                fails.append('%s: no multitap (long read %d bytes, id %02X)' % (port, q['long_len'], q['long_reply'][1]))
            elif q['long_len'] != 35:
                fails.append('%s: long read %d bytes, want 35' % (port, q['long_len']))
            first = min(s for s in range(SLOTS) if filled(c, p, s))
            if q['en_slot'] != first:
                fails.append('%s: request went to slot %s, want %s' % (port, 'ABCD'[q['en_slot'] & 3], 'ABCD'[first]))
            slot_a = (0x73 if c['ct'] == 1 else 0x41) if filled(c, p, 0) else 0xFF
            fails += judge_probes(port, q, slot_a)
        else:
            if q['tap'] or q['tap_on']:
                fails.append('%s: a multitap answered (%d times)' % (port, q['tap_on']))
        if kind == 'pad':
            s0 = st['slots'][p][0]
            if q['long_len'] != REPLY_LEN.get(s0['type'], -1) or q['long_reply'][1] != s0['type']:
                fails.append('%s: the long read should be a plain read: %d bytes %s'
                             % (port, q['long_len'], hexs(q['long_reply'][:q['long_len']])))
        if not any_slot and not q['empty_frames']:
            fails.append('%s: should answer nothing' % port)
        if c.get('switch') and p == 0:
            if (q['tap_on'], q['tap_off']) != (2, 1):
                fails.append('%s: multitap on %d off %d, want 2 and 1' % (port, q['tap_on'], q['tap_off']))
            if q['empty_frames'] < 40:
                fails.append('%s: %d empty frames, want the two unplug gaps (2 x 30)' % (port, q['empty_frames']))
            if not q['plain_frames']:
                fails.append('%s: never saw the GameCube pad on its own' % port)
        for s in range(SLOTS):
            where = '%s%s' % (port, 'ABCD'[s] if tap else ('' if s == 0 else ' address %02X' % (s + 1)))
            sd = st['slots'][p][s]
            if filled(c, p, s):
                gc = p == 0 and s == 0      # GameCube pad 1: the driver's padsweep, its own order
                fails += judge_device(where, sd, c['ct'], None if gc else script_port(p, s), tap, checks)
                if 'SioTiming=1' in c.get('sets', []):
                    fails += judge_timing(where, sd)
            else:
                fails += judge_empty(where, sd, tap)
    return fails


def pick(names):
    if not names:
        return CELLS
    want = names.split(',')
    out = [c for c in CELLS if c['name'] in want]
    if len(out) != len(want):
        sys.exit('unknown cell(s): %s' % ', '.join(set(want) - {c['name'] for c in out}))
    return out


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd = a[0]
    if cmd == 'cells':
        for c in CELLS:
            print('%-16s %5d vblanks  %s' % (c['name'], cell_vblanks(c), ' '.join(settings(c))))
        return 0
    if cmd == 'chain':
        write_chain(a[1], pick(a[2] if len(a) > 2 else ''))
        return 0
    if cmd == 'show':
        show(decode(open(a[1], 'rb').read()))
        return 0
    if cmd == 'judge':
        run, cells = a[1], pick(a[2] if len(a) > 2 else '')
        bad = 0
        for i, c in enumerate(cells):
            path = os.path.join(run, 'vram_%02d.bin' % (i + 1))
            try:
                st = decode(open(path, 'rb').read())
                fails = judge(st, c)
                if '-v' in a:
                    show(st, verbose=False)
            except (OSError, ValueError) as e:
                fails = [str(e)]
            bad += bool(fails)
            print('%-16s %s' % (c['name'], 'PASS' if not fails else 'FAIL: ' + '; '.join(fails)))
        print('matrix: %d/%d cells PASS' % (len(cells) - bad, len(cells)))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
