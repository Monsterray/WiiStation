#!/usr/bin/env python3
"""usbpad_read.py -- turn the USB Pad Test's log (usbpad.log) into a table.

  usbpad_read.py usbpad.log          the device, then one row per prompted button
  usbpad_read.py --selftest          check the parser on a built-in sample

The log comes from Settings > Input > Test USB Pad on the Wii (Gamecube/menu/UsbPadTestFrame.cpp),
written to <device>:/wiistation/usbpad.log. How to run the test and read this table:
Docs/CONTROLLER_TESTING.md, section 6.

Columns:
  changed   the report bytes that moved when the button was pressed: byte:rest>pressed (hex)
  gc        the GameCube-style buttons the controller ini decoded (controllers/VVVV_PPPP.ini)
  ps1       the PlayStation buttons that WiiStation's HID button config made of them
  verdict   OK; WRONG (another PlayStation button came out); NOT MAPPED (the pad sent something,
            WiiStation saw nothing); NO INI (the pad was read raw, nothing decodes it);
            for the sticks, the direction the stick values point
  ini       a starting point for the controller ini line (offset,mask in hex, as the ini takes
            them), from the bits that turned on. Check it against the "changed" column: a hat
            d-pad (one byte, a value per direction) needs DPAD=1 and the value as the mask.
"""
import argparse
import re
import sys

STICKS = {'LStickUp': (0, 1, 1), 'LStickDown': (0, 1, -1), 'LStickLeft': (0, 0, -1),
          'LStickRight': (0, 0, 1), 'RStickUp': (1, 1, 1), 'RStickDown': (1, 1, -1),
          'RStickLeft': (1, 0, -1), 'RStickRight': (1, 0, 1)}
# The PlayStation button -> the ini key it comes from, under the HID pad's default button config
# (controller-HidGC.c: Square=A, Cross=B, Circle=X, Triangle=Y, ...).
INI_KEY = {'Up': 'KeyUp', 'Down': 'KeyDown', 'Left': 'KeyLeft', 'Right': 'KeyRight',
           'Square': 'KeyA', 'Cross': 'KeyB', 'Circle': 'KeyX', 'Triangle': 'KeyY',
           'L1': 'KeyL1', 'R1': 'KeyR1', 'L2': 'KeyL2', 'R2': 'KeyR2',
           'Start': 'KeyStart', 'Select': 'KeySelect'}
STICK_KEY = {(0, 0): 'StickX', (0, 1): 'StickY', (1, 0): 'CStickX', (1, 1): 'CStickY'}
STEP = re.compile(r'^step (\d+) (\S+): (\S+)(.*)$')


def parse(lines):
    head, steps = {}, []
    for line in lines:
        line = line.rstrip('\r\n')
        m = STEP.match(line)
        if m:
            fields = dict(re.findall(r'(\w+)=(\S+)', m.group(4)))
            steps.append({'n': int(m.group(1)), 'name': m.group(2), 'result': m.group(3), **fields})
        elif ': ' in line and not line.startswith('#'):
            k, v = line.split(': ', 1)
            head.setdefault(k, []).append(v)
    return head, steps


def changes(s):
    """[(offset, rest, pressed)] from 'off:aa>bb,...'"""
    out = []
    for item in s.get('changed', '-').split(','):
        m = re.match(r'(\d+):([0-9A-F]{2})>([0-9A-F]{2})$', item)
        if m:
            out.append((int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)))
    return out


def verdict(s, raw):
    name, res = s['name'], s['result']
    if name == 'Nothing':
        return 'OK' if res == 'rest-ok' else 'DRIFT' if res == 'moved' else res
    if res != 'pressed':
        return res
    if raw or s.get('ps1', '-') == '-' and s.get('gc', '-') == '-' and 'sticks' not in s:
        return 'NO INI' if raw else 'NOT MAPPED'
    if name in STICKS:
        side, axis, sign = STICKS[name]
        v = [int(x) for x in s['sticks'].split(',')]
        x, y = v[2 * side], v[2 * side + 1]
        val = (x, y)[axis]
        if abs(x) < 30 and abs(y) < 30:
            return 'NOT MAPPED'
        return 'OK' if val * sign >= 30 else 'WRONG (%d,%d)' % (x, y)
    ps1 = [] if s.get('ps1', '-') == '-' else s['ps1'].split(',')
    if not ps1:
        return 'NOT MAPPED'
    if name in ps1:
        return 'OK' if len(ps1) == 1 else 'OK + ' + ','.join(p for p in ps1 if p != name)
    return 'WRONG (gives %s)' % ','.join(ps1)


def ini_hint(s):
    ch = changes(s)
    if s['result'] != 'pressed' or not ch:
        return ''
    if s['name'] in STICKS:
        side, axis, _ = STICKS[s['name']]
        off = max(ch, key=lambda c: abs(c[2] - c[1]))[0]
        return '%s=%X' % (STICK_KEY[(side, axis)], off)
    key = INI_KEY.get(s['name'])
    if not key:
        return ''
    off, rest, now = ch[0]
    on = now & ~rest
    return '%s=%X,%X' % (key, off, on if on and len(ch) == 1 else now)


def table(head, steps):
    out = []
    for k in ('version', 'hid', 'devices', 'device', 'ini', 'mode', 'reportlen', 'rest', 'noise'):
        for v in head.get(k, []):
            out.append('%-10s %s' % (k + ':', v))
    if 'reportdesc' in head:
        d = head['reportdesc'][0]
        out.append('%-10s %s' % ('reportdesc:', d if len(d) < 70 else d[:66] + '...'))
    raw = head.get('mode', [''])[0] == 'raw'
    out.append('')
    rows = [('#', 'prompt', 'result', 'changed', 'gc', 'ps1', 'verdict', 'ini')]
    for s in steps:
        if s['result'] == 'release-timeout':
            continue
        rows.append((str(s['n']), s['name'], s['result'], s.get('changed', ''), s.get('gc', ''),
                     s.get('ps1', ''), verdict(s, raw), ini_hint(s)))
    w = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    for r in rows:
        out.append('  '.join(c.ljust(w[i]) for i, c in enumerate(r)).rstrip())
    bad = [s['name'] for s in steps if verdict(s, raw).startswith(('WRONG', 'NOT MAPPED', 'DRIFT'))]
    out.append('')
    out.append('end: %s; %d steps, problems: %s' % (head.get('end', ['(none: the log stops early)'])[0],
               len(steps), ', '.join(bad) if bad else 'none'))
    return '\n'.join(out)


SAMPLE = """# WiiStation USB Pad Test
version: 5.13.1
hid: running
devices: 1 054C:05C4
device: vid=054C pid=05C4 product="Wireless Controller" class=03 subclass=00 protocol=00 interface=0 ep_in=84 ep_out=03 maxpacket=64
mode: mapped
reportlen: 64
rest: 0180807F7F08
noise: 1=7E-81
step 1 Up: pressed raw=0180807F7F00 changed=5:08>00 gc=D-Up ps1=Up buttons=0008 sticks=0,0,0,0 triggers=0,0
step 7 Cross: pressed raw=0180807F7F28 changed=5:08>28 gc=A ps1=Square buttons=0100 sticks=0,0,0,0 triggers=0,0
step 8 Square: pressed raw=0180807F7F18 changed=5:08>18 gc=- ps1=- buttons=0000 sticks=0,0,0,0 triggers=0,0
step 17 LStickUp: pressed raw=0180007F7F08 changed=2:80>00 gc=- ps1=- buttons=0000 sticks=0,100,0,0 triggers=0,0
step 18 LStickDown: timeout
step 25 Nothing: rest-ok
end: done
"""


def selftest():
    head, steps = parse(SAMPLE.splitlines())
    v = {s['name']: verdict(s, False) for s in steps}
    assert v == {'Up': 'OK', 'Cross': 'WRONG (gives Square)', 'Square': 'NOT MAPPED',
                 'LStickUp': 'OK', 'LStickDown': 'timeout', 'Nothing': 'OK'}, v
    h = {s['name']: ini_hint(s) for s in steps}
    assert h['Cross'] == 'KeyB=5,20' and h['LStickUp'] == 'StickY=2' and h['Up'] == 'KeyUp=5,0', h
    assert 'problems: Cross, Square' in table(head, steps)
    print('selftest ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split('\n\n', 1)[1])
    ap.add_argument('log', nargs='?', help='usbpad.log (from <device>:/wiistation/)')
    ap.add_argument('--selftest', action='store_true', help='check the parser on a built-in sample')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.log:
        ap.error('give usbpad.log')
    with open(a.log, encoding='ascii', errors='replace') as f:
        print(table(*parse(f)))


if __name__ == '__main__':
    sys.exit(main())
