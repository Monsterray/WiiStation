#!/usr/bin/env python3
"""A GameCube pad for Dolphin that needs no keyboard, mouse or window focus.

Dolphin's DSU client (cemuhook protocol, InputCommon/ControllerInterface/DualShockUDPClient)
reads a pad from a UDP server. This is that server, for one pad on 127.0.0.1:26760, plus
the commands that press its buttons. Nothing goes through the user's keyboard or focus.

Usage:
  scripts/dsu_pad.py profile <Dolphin user dir>   map GC pad 1 and the savestate hotkeys
                                                  to this pad only; no keyboard anywhere
  scripts/dsu_pad.py serve                        run the server (until killed)
  scripts/dsu_pad.py keys "A,A:400,START@1500,SU:300,SAVE"
                                                  press in order: NAME[:hold ms][@gap ms]

Buttons: A B X Y Z L R START, D-pad DU DD DL DR, main stick SU SD SL SR, C-stick CU CD CL CR.
SAVE and LOAD press Dolphin's Save/Load State Slot 1 hotkeys (Share and PS on the pad), SAVE2
and LOAD2 slot 2 (L3, R3), and SHOT takes a Dolphin screenshot (<user dir>/ScreenShots).
The profile turns on Dolphin's background input, so the pad works while Dolphin is behind
other windows; that is safe only because the profile maps no keyboard input at all.
"""
import argparse
import configparser
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

DSU_PORT, CONTROL_PORT = 26760, 26761
NAME = "wiistation"

# Pad fields: name -> (offset in PadDataResponse after the header and type, kind)
BUTTON1 = {"SHARE": 0x01, "L3": 0x02, "R3": 0x04, "OPTIONS": 0x08}
ANALOG = {"DL": 8, "DD": 9, "DR": 10, "DU": 11, "X": 12, "A": 13, "B": 14, "Y": 15,
          "R": 16, "L": 17, "Z": 19}       # Square Cross Circle Triangle, R1 L1 R2 L2
STICK = {"SL": (4, 0), "SR": (4, 255), "SU": (5, 255), "SD": (5, 0),
         "CL": (6, 0), "CR": (6, 255), "CU": (7, 255), "CD": (7, 0)}


def message(kind, body, uid=0x57363421):
    """A DSUS message: header (CRC over the whole message with the CRC zeroed)."""
    payload = struct.pack("<I", kind) + body
    head = struct.pack("<4sHHII", b"DSUS", 1001, len(payload), 0, uid)
    crc = zlib.crc32(head + payload) & 0xFFFFFFFF
    return struct.pack("<4sHHII", b"DSUS", 1001, len(payload), crc, uid) + payload


PORT_INFO = struct.pack("<BBBB6sBB", 0, 2, 2, 1, b"\x00\x00\x00\x00\x00\x01", 5, 0)


def pad_data(state, counter):
    b = bytearray(68)                   # buttons, sticks, analogs, touches
    b[4:8] = bytes([128, 128, 128, 128])
    for name, on in state.items():
        if not on:
            continue
        if name in BUTTON1:
            b[0] |= BUTTON1[name]
        elif name == "PS":
            b[2] = 1
        elif name == "TOUCH":
            b[3] = 1
        elif name in ANALOG:
            b[ANALOG[name]] = 255
        elif name in STICK:
            offset, value = STICK[name]
            b[offset] = value
    body = PORT_INFO[:11] + b"\x01" + struct.pack("<I", counter) + bytes(b[:20]) + bytes(12) + \
        struct.pack("<Q6f", int(time.monotonic() * 1e6), 0, 1, 0, 0, 0, 0)
    return message(0x100002, body)


def serve(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", DSU_PORT))
    control = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    control.bind(("127.0.0.1", CONTROL_PORT))
    sock.setblocking(False)
    control.setblocking(False)
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        sock.ioctl(socket.SIO_UDP_CONNRESET, False)
    state, clients, counter, seen = {}, {}, 0, set()
    print(f"DSU pad '{NAME}' on 127.0.0.1:{DSU_PORT}; commands on {CONTROL_PORT}", flush=True)
    while True:
        for s in (sock, control):
            try:
                data, addr = s.recvfrom(1024)
            except (BlockingIOError, ConnectionResetError):
                continue    # Windows reports a gone client's ICMP "port unreachable" here
            if s is control:            # "NAME=1 NAME=0 ..."
                for item in data.decode().split():
                    name, _, value = item.partition("=")
                    state[name.upper()] = value == "1"
                continue
            if len(data) < 20 or data[:4] != b"DSUC":
                continue
            kind = struct.unpack_from("<I", data, 16)[0]
            if (addr, kind) not in seen:
                seen.add((addr, kind))
                print(f"client {addr[0]}:{addr[1]} asks {kind:#x}", flush=True)
            if kind == 0x100000:
                sock.sendto(message(0x100000, struct.pack("<H2x", 1001)), addr)
            elif kind == 0x100001:
                count = struct.unpack_from("<I", data, 20)[0]
                if 0 in data[24:24 + count]:
                    sock.sendto(message(0x100001, PORT_INFO), addr)
            elif kind == 0x100002:
                clients[addr] = time.monotonic() + 5
        now = time.monotonic()
        counter += 1
        for addr, until in list(clients.items()):
            if until < now:
                del clients[addr]
            else:
                try:
                    sock.sendto(pad_data(state, counter), addr)
                except OSError:
                    del clients[addr]
        time.sleep(0.004)


def send(text):
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(text.encode(), ("127.0.0.1", CONTROL_PORT))


ALIASES = {"START": "OPTIONS", "SAVE": "SHARE", "LOAD": "PS", "SAVE2": "L3", "LOAD2": "R3", "SHOT": "TOUCH"}
VALID = set(ANALOG) | set(STICK) | set(ALIASES)


def keys(args):
    for item in args.sequence.split(","):
        item, _, gap = item.partition("@")
        name, _, hold = item.partition(":")
        name = name.strip().upper()
        if name not in VALID:
            sys.exit(f"dsu_pad: unknown button {name!r}; see --help")
        name = ALIASES.get(name, name)
        send(f"{name}=1")
        time.sleep(int(hold or 120) / 1000)
        send(f"{name}=0")
        time.sleep(int(gap or 350) / 1000)


GCPAD = f"""[GCPad1]
Device = DSUClient/0/{NAME}
Buttons/A = Cross
Buttons/B = Circle
Buttons/X = Square
Buttons/Y = Triangle
Buttons/Z = L2
Buttons/Start = Options
Main Stick/Up = `Left Y+`
Main Stick/Down = `Left Y-`
Main Stick/Left = `Left X-`
Main Stick/Right = `Left X+`
C-Stick/Up = `Right Y+`
C-Stick/Down = `Right Y-`
C-Stick/Left = `Right X-`
C-Stick/Right = `Right X+`
Triggers/L = L1
Triggers/R = R1
Triggers/L-Analog = L1
Triggers/R-Analog = R1
D-Pad/Up = `Pad N`
D-Pad/Down = `Pad S`
D-Pad/Left = `Pad W`
D-Pad/Right = `Pad E`
[GCPad2]
Device = DSUClient/0/{NAME}
[GCPad3]
Device = DSUClient/0/{NAME}
[GCPad4]
Device = DSUClient/0/{NAME}
"""
HOTKEYS = f"""[Hotkeys]
Device = DSUClient/0/{NAME}
Load State/Load State Slot 1 = PS
Save State/Save State Slot 1 = Share
Load State/Load State Slot 2 = R3
Save State/Save State Slot 2 = L3
General/Take Screenshot = `Touch Button`
"""


def profile(args):
    config = Path(args.userdir) / "Config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "GCPadNew.ini").write_text(GCPAD)
    (config / "Hotkeys.ini").write_text(HOTKEYS)
    (config / "GCKeyNew.ini").write_text("")
    (config / "DSUClient.ini").write_text(
        f"[Server]\nEnabled = True\nEntries = {NAME}:127.0.0.1:{DSU_PORT};\n")
    wiimote = "".join(f"[Wiimote{i}]\nSource = 0\n" for i in range(1, 5)) + "[BalanceBoard]\nSource = 0\n"
    (config / "WiimoteNew.ini").write_text(wiimote)
    ini = configparser.ConfigParser(interpolation=None, strict=False)
    ini.optionxform = str
    ini.read(config / "Dolphin.ini")
    if not ini.has_section("Input"):
        ini.add_section("Input")
    ini["Input"]["BackgroundInput"] = "True"
    if not ini.has_section("General"):
        ini.add_section("General")
    ini["General"]["HotkeysRequireFocus"] = "False"   # SAVE/LOAD work in the background
    with open(config / "Dolphin.ini", "w") as f:
        ini.write(f)
    print(f"{config}: GC pads and hotkeys on DSU pad '{NAME}', Wii Remotes off, background input on")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("serve", help="run the DSU server").set_defaults(run=serve)
    k = sub.add_parser("keys", help="press buttons in order")
    k.add_argument("sequence")
    k.set_defaults(run=keys)
    p = sub.add_parser("profile", help="point a Dolphin user dir's pads at this server")
    p.add_argument("userdir")
    p.set_defaults(run=profile)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
