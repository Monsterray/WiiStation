#!/usr/bin/env python3
"""ds_gdb.py -- drive the test DuckStation (C:/tools/DuckStation, settings.ini [Debug]
EnableGDBServer = true, GDBServerPort = 2345) through its GDB server: read and write PS1 RAM,
find a byte pattern, hold a pad button. Nothing touches the user's keyboard, mouse or focus
(user rule 2026-10-12); DuckStation runs minimized (`start /min`).

  ds_gdb.py find HEX            addresses in RAM (0x80000000..0x801FFFFF) holding HEX
  ds_gdb.py read ADDR N         N bytes at ADDR (hex)
  ds_gdb.py press ADDR HEX N     overwrite ADDR with HEX at each of the next N stores to it (a
                                write watchpoint): a button held for N pad refreshes
  ds_gdb.py hold ADDR HEX MS    write HEX at ADDR as fast as the socket allows for MS ms: a
                                held button (the BIOS refills a pad buffer once per vblank)
"""
import socket
import sys
import time

RAM, RAM_SIZE = 0x80000000, 0x200000


class Gdb:
    def __init__(self, port=2345):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.buf = b""

    def _byte(self):
        while not self.buf:
            self.buf = self.s.recv(65536)
            if not self.buf:
                raise ConnectionError("GDB server closed")
        b, self.buf = self.buf[:1], self.buf[1:]
        return b

    def cmd(self, data):
        pkt = data.encode()
        self.s.sendall(b"$" + pkt + b"#%02x" % (sum(pkt) & 0xFF))
        while True:                     # skip acks, read one $...#cs reply
            c = self._byte()
            if c == b"$":
                break
        out = b""
        while True:
            c = self._byte()
            if c == b"#":
                self._byte(); self._byte()
                break
            out += c
        self.s.sendall(b"+")
        return out.decode(errors="replace")

    def read(self, addr, n):
        out = b""
        for a in range(addr, addr + n, 2048):
            r = self.cmd("m%x,%x" % (a, min(2048, addr + n - a)))
            if r.startswith("E"):
                raise RuntimeError("read %x: %s" % (a, r))
            out += bytes.fromhex(r)
        return out

    def write(self, addr, data):
        r = self.cmd("M%x,%x:%s" % (addr, len(data), data.hex()))
        if r != "OK":
            raise RuntimeError("write %x: %s" % (addr, r))


def main():
    a = sys.argv[1:]
    g = Gdb()
    try:
        if a[0] == "find":
            pat = bytes.fromhex(a[1])
            ram = g.read(RAM, RAM_SIZE)
            i = ram.find(pat)
            while i >= 0:
                print(hex(RAM + i))
                i = ram.find(pat, i + 1)
        elif a[0] == "read":
            print(g.read(int(a[1], 16), int(a[2])).hex(" "))
        elif a[0] == "press":
            # press ADDR HEX FRAMES: a write watchpoint on ADDR; each time the PS1 stores there
            # (the BIOS refilling the pad buffer, once per vblank) the emulator stops, HEX is
            # written over it, and it runs on. Deterministic, unlike "hold".
            addr, data, frames = int(a[1], 16), bytes.fromhex(a[2]), int(a[3])
            g.cmd("Z2,%x,%x" % (addr, len(data)))
            for _ in range(frames):
                g.cmd("c")              # returns at the next stop (the watchpoint)
                g.write(addr, data)
            g.cmd("z2,%x,%x" % (addr, len(data)))
            print("pressed for %d stores" % frames)
        elif a[0] == "hold":
            addr, data, ms = int(a[1], 16), bytes.fromhex(a[2]), int(a[3])
            end = time.perf_counter() + ms / 1000
            n = 0
            while time.perf_counter() < end:
                g.write(addr, data)
                n += 1
                time.sleep(0.002)   # let the emulator run: a stub busy with writes stalls it
            print("%d writes" % n)
    finally:
        try:
            g.s.sendall(b"$c#63")       # leave the emulator running if the stub paused it
        except OSError:
            pass


if __name__ == "__main__":
    main()
