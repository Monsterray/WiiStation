#!/usr/bin/env python3
"""disc_convert.py SRC.cue OUTROOT [--only FMT,FMT]   write SRC as every disc format cdriso.c loads
   disc_convert.py --check OUTROOT [--only FMT,FMT]    re-read each copy the way cdriso.c does

Folders: OUTROOT/"<game> [FMT]"/ for FMT in CUE1 CCD TOC MDS PBP CBN ISO CHD. Stdlib + zlib (CHD: chdman).
Every writer follows what cdriso.c READS, not the file-format spec; the line numbers in the comments are
cdriso.c's. --check is a line-by-line port of that parsing (Cdriso class) and compares what it returns with
the source image.

Boot file per format (what to pick in the browser): CUE1 .cue, CCD .ccd, TOC .toc, MDS .mdf (not .mds:
ISOopen's alt-file list has no .mdf), PBP .pbp, CBN .cbn (its .cue sidecar is what gives it tracks), ISO .iso, CHD .chd.
"""
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

SS = 2352
CHDMAN = "C:/tools/chdman/chdman.exe"
FORMATS = ["CUE1", "CCD", "TOC", "MDS", "PBP", "CBN", "ISO", "CHD"]
BOOT = {"CUE1": ".cue", "CCD": ".ccd", "TOC": ".toc", "MDS": ".mdf", "PBP": ".pbp",
        "CBN": ".cbn", "ISO": ".iso", "CHD": ".chd"}
DATA, CDDA = 1, 2   # cdriso.c's trackinfo.type


def msf(n):
    return "%02d:%02d:%02d" % (n // 4500, n // 75 % 60, n % 75)


def msf2sec(s):
    m, sec, f = (int(x) for x in s.split(":"))
    return (m * 60 + sec) * 75 + f


def bcd(n):
    return n // 10 * 16 + n % 10


def unbcd(b):
    return (b >> 4) * 10 + (b & 15)


def bcd_msf(n):
    return bytes([bcd(n // 4500), bcd(n // 75 % 60), bcd(n % 75)])


def swap16(b):
    b = bytearray(b)
    b[0::2], b[1::2] = b[1::2], b[0::2]
    return bytes(b)


def _crctab():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return t


CRCTAB = _crctab()


def calc_crc(d):   # misc.c calcCrc: CRC16-CCITT, inverted
    crc = 0
    for x in d:
        crc = CRCTAB[(crc >> 8) ^ x] ^ ((crc << 8) & 0xFFFF)
    return (~crc) & 0xFFFF


# ---------------------------------------------------------------- source

class Disc:
    """The source cue as one image: sectors 0..n-1 (LBA), tracks with INDEX 00/01 as LBA."""

    def __init__(self, cue):
        d = os.path.dirname(os.path.abspath(cue))
        self.name = os.path.splitext(os.path.basename(cue))[0]
        self.files, self.tracks, base, fbase = [], [], 0, 0
        for line in open(cue, encoding="latin-1"):
            w = line.split()
            if not w:
                continue
            k = w[0].upper()
            if k == "FILE":
                p = os.path.join(d, re.match(r'\s*FILE\s+"(.*)"', line).group(1))
                size = os.path.getsize(p)
                assert size % SS == 0, p
                self.files.append(p)
                fbase = base
                base += size // SS
            elif k == "TRACK":
                audio = w[2].upper() == "AUDIO"
                assert audio or w[2].upper() in ("MODE1/2352", "MODE2/2352"), w[2]
                self.tracks.append(dict(num=int(w[1]), audio=audio, i0=None, i1=None))
            elif k == "INDEX":
                self.tracks[-1]["i%d" % int(w[1])] = fbase + msf2sec(w[2])
            elif k in ("PREGAP", "POSTGAP"):
                raise SystemExit("%s: %s not supported (pregap data must be in the file)" % (cue, k))
        self.n = base
        assert not self.tracks[0]["audio"] and all(t["audio"] for t in self.tracks[1:]), "data track first, then audio"
        for t in self.tracks:
            t["start"] = t["i0"] if t["i0"] is not None else t["i1"]
        for i, t in enumerate(self.tracks):
            t["end"] = self.tracks[i + 1]["start"] if i + 1 < len(self.tracks) else self.n
        self.img = b"".join(open(p, "rb").read() for p in self.files)
        assert len(self.img) == self.n * SS

    def sector(self, lba):
        return self.img[lba * SS:(lba + 1) * SS]

    def audio_swapped(self):
        """The image with every audio sector byte-swapped (cdrdao/CHD keep CD audio big-endian)."""
        b = bytearray(self.img)
        for t in self.tracks:
            if t["audio"]:
                seg = b[t["start"] * SS:t["end"] * SS]
                seg[0::2], seg[1::2] = seg[1::2], seg[0::2]
                b[t["start"] * SS:t["end"] * SS] = seg
        return bytes(b)

    def cue_text(self, binname):
        s = 'FILE "%s" BINARY\r\n' % binname
        for t in self.tracks:
            s += "  TRACK %02d %s\r\n" % (t["num"], "AUDIO" if t["audio"] else "MODE2/2352")
            if t["i0"] is not None:
                s += "    INDEX 00 %s\r\n" % msf(t["i0"])
            s += "    INDEX 01 %s\r\n" % msf(t["i1"])
        return s


# ---------------------------------------------------------------- writers

def w(path, data, mode="wb"):
    with open(path, mode, **({} if "b" in mode else {"newline": ""})) as f:
        f.write(data)


def write_cue1(D, d):          # parsecue 643: one FILE, INDEX 00/01 kept, offsets = LBA * 2352
    w(d + D.name + ".bin", D.img)
    w(d + D.name + ".cue", D.cue_text(D.name + ".bin"), "w")


def write_ccd(D, d):           # parseccd 819: [TRACK n] MODE= (0 = audio) and INDEX 1= (LBA); .img + .sub beside it
    t1 = D.tracks
    ent = [(0xA0, 0, 0x04, 1, 0x20, 0), (0xA1, 0, 0x04 if not t1[-1]["audio"] else 0, t1[-1]["num"], 0, 0),
           (0xA2, 0, 0, D.n + 150, None, None)]
    s = "[CloneCD]\r\nVersion=3\r\n[Disc]\r\nTocEntries=%d\r\nSessions=1\r\nDataTracksScrambled=0\r\nCDTextLength=0\r\n" \
        "[Session 1]\r\nPreGapMode=2\r\nPreGapSubC=0\r\n" % (len(t1) + 3)
    rows = []
    for point, tno, ctl, a, b, c in ent:
        if point == 0xA2:
            lo = D.n + 150
            pm, ps, pf, plba = lo // 4500, lo // 75 % 60, lo % 75, D.n
        else:
            pm, ps, pf = a, b, c
            plba = (pm * 60 + ps) * 75 + pf - 150
        rows.append((point, ctl, pm, ps, pf, plba))
    for t in t1:
        a = t["i1"] + 150
        rows.append((t["num"], 0 if t["audio"] else 4, a // 4500, a // 75 % 60, a % 75, t["i1"]))
    for i, (point, ctl, pm, ps, pf, plba) in enumerate(rows):
        s += "[Entry %d]\r\nSession=1\r\nPoint=0x%02x\r\nADR=0x01\r\nControl=0x%02x\r\nTrackNo=0\r\n" \
             "AMin=0\r\nASec=0\r\nAFrame=0\r\nALBA=-150\r\nZero=0\r\nPMin=%d\r\nPSec=%d\r\nPFrame=%d\r\nPLBA=%d\r\n" \
             % (i, point, ctl, pm, ps, pf, plba)
    for t in t1:
        s += "[TRACK %d]\r\nMODE=%d\r\n" % (t["num"], 0 if t["audio"] else 2)
        if t["i0"] is not None:
            s += "INDEX 0=%d\r\n" % t["i0"]
        s += "INDEX 1=%d\r\n" % t["i1"]
    w(d + D.name + ".ccd", s, "w")
    w(d + D.name + ".img", D.img)
    # .sub: ISOgetBufferSub 2107 reads 96 bytes at lba*96; cdrom.c UpdateSubq 591 uses Q at byte 12 only if its CRC is good
    sub = bytearray(D.n * 96)
    for lba in range(D.n):
        t = [t for t in D.tracks if lba >= t["start"]][-1]
        idx = 1 if lba >= t["i1"] else 0
        rel = lba - t["i1"] if idx else t["i1"] - 1 - lba
        q = bytes([0x01 if t["audio"] else 0x41, bcd(t["num"]), bcd(idx)]) + bcd_msf(rel) + b"\0" + bcd_msf(lba + 150)
        sub[lba * 96 + 12:lba * 96 + 24] = q + struct.pack(">H", calc_crc(q))
    w(d + D.name + ".sub", bytes(sub))


def write_toc(D, d):           # parsetoc 496: lines must start in column 0 (sscanf literal); audio kept big-endian
    s = "CD_ROM_XA\n"
    b = D.name + ".bin"
    for t in D.tracks:
        s += "\n// Track %d\n" % t["num"]
        if not t["audio"]:
            s += 'TRACK MODE2_RAW\nDATAFILE "%s" %s\n' % (b, msf(t["end"] - t["start"]))
        else:   # length = to the end of the track from INDEX 01, so ISOgetTD(0) (start + length) is the real lead-out
            s += 'TRACK AUDIO\nFILE "%s" #%d 0 %s\n' % (b, t["start"] * SS, msf(t["end"] - t["i1"]))
            if t["i0"] is not None:
                s += "START %s\n" % msf(t["i1"] - t["i0"])
    w(d + D.name + ".toc", s, "w")
    w(d + b, D.audio_swapped())


def write_mds(D, d):           # parsemds 898; user picks the .mdf (alt-file list at ISOopen 1851 has no .mdf)
    ntr = len(D.tracks)
    hdr = bytearray(0x58)
    hdr[0:16] = b"MEDIA DESCRIPTOR"
    hdr[0x10:0x12] = bytes([1, 3])
    struct.pack_into("<HH", hdr, 0x12, 0x02, 1)
    struct.pack_into("<I", hdr, 0x50, 0x58)
    sess = struct.pack("<iiHBBHHII", -150, D.n - 1, 1, 3 + ntr, 3, 1, ntr, 0, 0x70)
    extra0 = 0x70 + (3 + ntr) * 0x50
    footer = extra0 + 8 * ntr

    def blk(mode, ctl, tno, point, abs_lba, extra, start_lba, start_off, length):
        b = bytearray(0x50)
        b[0], b[1], b[2], b[3], b[4] = mode, 0, ctl, tno, point
        b[9:12] = bytes([abs_lba // 4500, abs_lba // 75 % 60, abs_lba % 75])   # binary MSF, read at 1000
        struct.pack_into("<I", b, 12, extra)
        struct.pack_into("<H", b, 16, SS)
        struct.pack_into("<I", b, 0x24, start_lba)
        struct.pack_into("<Q", b, 0x28, start_off)
        struct.pack_into("<II", b, 0x30, 1, footer)
        return b, struct.pack("<II", 0, length)

    blocks, extras = bytearray(), bytearray()
    for point, m in ((0xA0, 1), (0xA1, ntr), (0xA2, None)):   # lead-in entries skipped at 962 (point >= 0xA0)
        b, _ = blk(0xAB, 0x04, 0, point, (D.n + 150) if m is None else m * 4500 + 0, 0, 0, 0, 0)
        if m is not None:
            b[9:12] = bytes([m, 0x20 if point == 0xA0 else 0, 0])
        blocks += b
    for i, t in enumerate(D.tracks):
        b, ex = blk(0xA9 if t["audio"] else 0xAB, 0x01 if t["audio"] else 0x41, t["num"], t["num"], t["i1"] + 150,
                    extra0 + 8 * i, t["i1"], t["i1"] * SS, t["end"] - t["i1"])   # pregap 0: it is in the .mdf (pregapOffset stays 0)
        blocks += b
        extras += ex
    foot = struct.pack("<II8x", footer + 16, 0) + b"*.mdf\0"
    w(d + D.name + ".mds", bytes(hdr) + sess + bytes(0x70 - 0x58 - len(sess)) + bytes(blocks) + bytes(extras) + foot)
    w(d + D.name + ".mdf", D.img)


def write_pbp(D, d):           # handlepbp 1015
    title = D.name.encode()
    keys, vals = b"TITLE\0\0\0", title + b"\0" * (4 - len(title) % 4 or 4)
    ktab = 20 + 16
    dtab = ktab + len(keys)
    sfo = b"\0PSF" + struct.pack("<IIII", 0x101, ktab, dtab, 1) + struct.pack("<HHIII", 0, 0x0204, len(title) + 1, len(vals), 0) \
        + keys + vals
    e = 0x28 + len(sfo)
    psar = (e + 0xFFFF) & ~0xFFFF
    hdr = b"\0PBP" + struct.pack("<9I", 0x10000, 0x28, e, e, e, e, e, e, psar)
    assert len(hdr) == 0x28
    pre = bytearray(0x100000)
    pre[0:12] = b"PSISOIMG0000"
    ent = [struct.pack("BBB3sB3s", 0x41, 0, 0xA0, b"\0\0\0", 0, bytes([1, 0x20, 0])),            # 1119: entry 0 skipped
           struct.pack("BBB3sB3s", 0x01, 0, 0xA1, b"\0\0\0", 0, bytes([bcd(len(D.tracks)), 0, 0])),   # numtracks (BCD)
           # lead-out: cdriso uses it as a sector COUNT (cd_length - start_offset/2352, 1135), so N, not N + 150
           struct.pack("BBB3sB3s", 0x01, 0, 0xA2, b"\0\0\0", 0, bcd_msf(D.n))]
    for t in D.tracks:   # index0 is only read as start_offset = LBA of INDEX 01 (1130), index1 = absolute MSF
        ent.append(struct.pack("BBB3sB3s", 1 if t["audio"] else 0x41, 0, bcd(t["num"]), bcd_msf(t["i1"]), 0, bcd_msf(t["i1"] + 150)))
    pre[0x800:0x800 + 10 * len(ent)] = b"".join(ent)
    comp, off, idx = [], 0, bytearray()
    for b in range(0, D.n, 16):
        blk = D.img[b * SS:(b + 16) * SS]
        blk += bytes(16 * SS - len(blk))
        c = zlib.compressobj(6, zlib.DEFLATED, -15)
        z = c.compress(blk) + c.flush()
        idx += struct.pack("<II24x", off, len(z))
        comp.append(z)
        off += len(z)
    idx += struct.pack("<II24x", off, 0)   # terminator: size 0, offset = end (1182 builds the last table slot from it)
    assert 0x4000 + len(idx) <= 0x100000
    pre[0x4000:0x4000 + len(idx)] = idx
    w(d + D.name + ".pbp", hdr + sfo + bytes(psar - e) + bytes(pre) + b"".join(comp))


def write_cbn(D, d):           # handlecbin 1209; tracks come from the .cue sidecar (parsecue runs on x.cbn -> x.cue)
    # one phantom zero sector at the end: cdriso reads index_len entries but uses index_len + 1 (1262 vs 1275), so the
    # last entry is uninitialised memory; with the phantom the unreadable sector is not a real one.
    img = D.img + bytes(SS)
    n = len(img) // SS
    start = 24 + 4 * (n + 1)
    idx, body, off = [], [], start
    for i in range(n):
        raw = img[i * SS:(i + 1) * SS]
        c = zlib.compressobj(6, zlib.DEFLATED, -15)
        z = c.compress(raw) + c.flush()
        if len(z) < SS:
            idx.append(off)
            body.append(z)
            off += len(z)
        else:
            idx.append(off | 0x80000000)
            body.append(raw)
            off += SS
    idx.append(off)
    assert off < 0x80000000
    w(d + D.name + ".cbn", b"CISO" + struct.pack("<IQIBB2x", 24, n * SS, SS, 1, 0) + struct.pack("<%dI" % (n + 1), *idx) + b"".join(body))
    w(d + D.name + ".cue", D.cue_text(D.name + ".cbn"), "w")


def write_iso(D, d):           # ISOopen 1821: no sheet at all, the data track as 2352-byte sectors
    t = D.tracks[0]
    w(d + D.name + ".iso", D.img[t["start"] * SS:t["end"] * SS])


def write_chd(D, d, src_cue):
    out = d + D.name + ".chd"
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([CHDMAN, "createcd", "-i", src_cue, "-o", out], check=True)


def convert(src, root, only):
    D = Disc(src)
    for fmt in FORMATS:
        if only and fmt not in only:
            continue
        d = "%s/%s [%s]/" % (root, D.name, fmt)
        os.makedirs(d, exist_ok=True)
        print("writing", d, flush=True)
        if fmt == "CHD":
            write_chd(D, d, os.path.abspath(src))
        else:
            {"CUE1": write_cue1, "CCD": write_ccd, "TOC": write_toc, "MDS": write_mds, "PBP": write_pbp,
             "CBN": write_cbn, "ISO": write_iso}[fmt](D, d)
    return D


# ---------------------------------------------------------------- the cdriso.c port

def rd(path, off, n):
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(n)


def toks(line):   # strtok(line, " "): a token may end in "\n"
    return [t for t in line.split(" ") if t]


def atoi(s):
    m = re.match(r"\s*([+-]?\d+)", s)
    return int(m.group(1)) if m else 0


def tok2msf(s):   # 144: "m:s:f" -> sectors
    p = s.split(":")
    return (atoi(p[0]) * 60 + (atoi(p[1]) if len(p) > 1 else 0)) * 75 + (atoi(p[2]) if len(p) > 2 else 0)


class Cdriso:
    """ISOopen 1785 for a file picked in the browser. ti[i] = dict(type, start (sector, absolute MSF), length,
    start_offset (bytes; CHD: sectors), handle (path or None))."""

    def __init__(self, iso):
        self.iso = iso
        self.ti = {}
        self.n = 0
        self.cddaBE, self.subMixed, self.pregap, self.multifile = True, False, 0, False
        self.cd = iso                    # cdHandle
        self.mode = "normal"
        self.log = []
        self.compr = self.chd = self.subfile = None
        if self.parsetoc(iso) == 0:
            pass
        elif self.parseccd(iso) == 0:
            pass
        elif self.parsemds(iso) == 0:
            pass
        else:
            self.parsecue(iso)
        if self.handlepbp(iso) == 0:
            self.mode = "compr"
        elif self.handlecbin(iso) == 0:
            self.mode = "compr"
        elif self.handlechd(iso) == 0:
            self.mode = "chd"
        base = iso[:-4]
        if not self.subMixed and os.path.exists(base + ".sub"):    # 1396 opensubfile
            self.subfile = base + ".sub"
        if os.path.getsize(self.cd) < SS * 0x10:                   # 1844: user picked the metadata file
            for e in (".bin", ".BIN", ".img", ".IMG"):
                if os.path.exists(base + e):
                    self.cd = base + e
                    break
        if self.mode == "normal" and os.path.getsize(self.cd) % 2048 == 0 and rd(self.cd, 0, 4) != b"\0\xff\xff\xff":
            self.mode = "2048"
        if self.n > 1 and self.ti[1].get("handle") is None:
            self.ti[1]["handle"] = self.cd

    def T(self, i):
        return self.ti.setdefault(i, dict(type=0, start=0, length=0, handle=None, start_offset=0))

    # --- parsers
    def parsetoc(self, iso):          # 496
        toc = iso[:-4] + ".toc"
        if not os.path.exists(toc):
            toc = iso[:-4] + ".cue"
            if not os.path.exists(toc):
                return -1
            t = toks(open(toc, encoding="latin-1").readline())
            if t and not t[0].startswith("CD"):
                return -1
        self.ti, self.n, self.cddaBE = {}, 0, False
        size, offs, gap = SS, 150, 0
        for line in open(toc, encoding="latin-1", newline=""):
            t = toks(line)
            if not t:
                continue
            if t[0] == "TRACK":
                offs += gap
                gap = 0
                self.n += 1
                tr = self.T(self.n)
                if t[1].startswith("MODE2_RAW"):
                    tr["type"], tr["start"] = DATA, 150
                    if len(t) > 2 and t[2].startswith("RW"):
                        size, self.subMixed = SS + 96, True
                elif t[1].startswith("AUDIO"):
                    tr["type"] = CDDA
            elif t[0] == "DATAFILE":
                tr = self.T(self.n)
                if tr["type"] == CDDA:
                    m = re.match(r'DATAFILE "([^"]*)" #(\d+) (\S{1,8})', line)
                    tr["start_offset"] = int(m.group(2))
                    tr["start"] = int(m.group(2)) // size + offs
                    tr["length"] = tok2msf(m.group(3))
                else:
                    m = re.match(r'DATAFILE "([^"]*)" (\S{1,8})', line)
                    if not m:
                        self.log.append("TOC: DATAFILE line did not scan (indented?)")
                        continue
                    tr["length"] = tok2msf(m.group(2))
            elif t[0] == "FILE":
                m = re.match(r'FILE "([^"]*)" #(-?\d+) (\S{1,8}) (\S{1,8})', line)
                if not m:
                    self.log.append("TOC: FILE line did not scan (indented?)")
                    continue
                tr = self.T(self.n)
                off = int(m.group(2)) + tok2msf(m.group(3)) * size
                tr["start_offset"] = off
                tr["start"] = off // size + offs
                tr["length"] = tok2msf(m.group(4))
            elif t[0] in ("ZERO", "SILENCE"):
                raise NotImplementedError("ZERO/SILENCE not used by the writer")
            elif t[0] == "START":
                tr = self.T(self.n)
                v = tok2msf(t[1]) if len(t) > 1 and ":" in t[1] else None
                if v is not None:
                    tr["start_offset"] += (v - gap) * size
                    tr["start"] += v
        return 0

    def parsecue(self, iso):          # 643
        cue = iso[:-4] + ".cue" if not iso[-4:].lower().startswith(".cd") else iso
        if not os.path.exists(cue):
            return -1
        lines = open(cue, encoding="latin-1", newline="").read().splitlines(True)
        if lines and lines[0].startswith("CD_ROM_XA"):
            return self.parsetoc(iso)
        d = os.path.dirname(cue)
        self.ti, self.n = {}, 0
        file_len, offs, size = 0, 150, SS
        for line in lines:
            t = toks(line)
            if not t:
                continue
            if t[0] == "TRACK":
                self.n += 1
                tr = self.T(self.n)
                m = re.match(r"\s*TRACK (\d+) MODE(\d+)/(\d+)", line)
                if "AUDIO" in line:
                    tr["type"], size = CDDA, SS
                elif m:
                    tr["type"], size = DATA, int(m.group(3))
                else:
                    tr["type"] = DATA if self.n == 1 else CDDA
                    size = SS
            elif t[0] == "INDEX":
                m = re.match(r"\s*INDEX (\d+) (\S{1,8})", line)
                tr = self.T(self.n)
                sec = tok2msf(m.group(2))
                tr["start_offset"] = sec * size
                tr["start"] = sec + offs
                tr["length"] = file_len - tr["start_offset"] // size
                if self.n > 1 and tr["handle"] is None:
                    self.T(self.n - 1)["length"] = tr["start"] - self.T(self.n - 1)["start"]
            elif t[0] == "PREGAP":
                raise NotImplementedError("PREGAP not used by the writer")
            elif t[0] == "FILE":
                m = re.match(r'\s*FILE "([^"]*)"', line)
                name = re.split(r"[\\/]", m.group(1))[-1]
                p = os.path.join(d, name)
                nxt = self.T(self.n + 1)
                nxt["handle"] = p if os.path.exists(p) else None
                if self.n + 1 > 1:
                    self.multifile = True
                    offs += file_len
                file_len = 0
                if nxt["handle"] is None:
                    continue
                file_len = os.path.getsize(p) // SS
                if self.n == 0 and iso[-4:].lower() == ".cue":
                    self.cd = p
        return 0 if self.n else -1

    def parseccd(self, iso):          # 819
        ccd = iso[:-4] + ".ccd"
        if not os.path.exists(ccd):
            return -1
        self.ti, self.n = {}, 0
        for line in open(ccd, encoding="latin-1"):
            if line.startswith("[TRACK"):
                self.n += 1
            elif line.startswith("MODE="):
                self.T(self.n)["type"] = CDDA if atoi(line[5:]) == 0 else DATA
            elif line.startswith("INDEX 1="):
                t = atoi(line[8:])
                tr = self.T(self.n)
                tr["start"], tr["start_offset"] = t + 150, t * SS
                if self.n > 1:
                    self.T(self.n - 1)["length"] = tr["start"] - self.T(self.n - 1)["start"]
        if self.n >= 1:
            size = -1
            for e in (".img", ".IMG", ".bin", ".BIN"):
                if size < 0 and os.path.exists(iso[:-4] + e):
                    size = os.path.getsize(iso[:-4] + e)
            if size < 0:
                size = os.path.getsize(self.cd)
            self.T(self.n)["length"] = size // SS - self.T(self.n)["start"] + 150
        return 0

    def parsemds(self, iso):          # 898
        mds = iso[:-4] + ".mds"
        if not os.path.exists(mds):
            return -1
        b = open(mds, "rb").read()
        u32 = lambda o: struct.unpack_from("<I", b, o)[0]
        if u32(0) != 0x4944454D:
            return -1
        self.ti = {}
        off = u32(0x50)
        self.n = struct.unpack_from("<H", b, off + 14)[0]
        off = u32(off + 14 + 2 + 4)
        while b[off + 4] >= 0xA0:
            off += 0x50
        self.subMixed = b[off + 1] != 0
        for i in range(1, self.n + 1):
            tr = self.T(i)
            tr["type"] = CDDA if b[off] == 0xA9 else DATA
            tr["start"] = (b[off + 9] * 60 + b[off + 10]) * 75 + b[off + 11]
            extra = u32(off + 12)
            tr["start_offset"] = u32(off + 0x28)
            if u32(extra) != 0 and i > 1:
                self.pregap = tr["start"]
            tr["length"] = u32(extra + 4)
            off += 0x50
        return 0

    def handlepbp(self, iso):         # 1015
        if iso[-4:].lower() != ".pbp" or iso[-4:] not in (".pbp", ".PBP"):
            return -1
        f = open(iso, "rb")
        hdr = f.read(40)
        psar = struct.unpack_from("<I", hdr, 36)[0]
        f.seek(psar)
        sig = f.read(11)[:10]
        base = psar
        if sig == b"PSTITLEIMG":
            raise NotImplementedError("multidisk")
        if sig != b"PSISOIMG00":
            return -1
        f.seek(base + 0x800 + 10)
        e = lambda: f.read(10)
        x = e()
        self.n = unbcd(x[7])
        x = e()
        cd_length = unbcd(x[7]) * 4500 + unbcd(x[8]) * 75 + unbcd(x[9])
        self.ti = {}
        for i in range(1, self.n + 1):
            x = e()
            tr = self.T(i)
            tr["type"] = CDDA if x[0] == 1 else DATA
            tr["start_offset"] = (unbcd(x[3]) * 4500 + unbcd(x[4]) * 75 + unbcd(x[5])) * SS
            tr["start"] = unbcd(x[7]) * 4500 + unbcd(x[8]) * 75 + unbcd(x[9])
            if i > 1:
                self.T(i - 1)["length"] = tr["start"] - self.T(i - 1)["start"]
        self.T(self.n)["length"] = cd_length - self.T(self.n)["start_offset"] // SS
        f.seek(base + 0x4000)
        cnt = (0x100000 - 0x4000) // 32
        cb = base + 0x100000
        tab, last = [], None
        for i in range(cnt):
            o, s = struct.unpack("<II", f.read(32)[:8])
            last = (o, s)
            if s == 0:
                break
            tab.append(cb + o)
        tab.append(cb + last[0] + last[1])
        self.compr = dict(shift=4, tab=tab, plain=set(), len=cnt, path=iso)
        return 0

    def handlecbin(self, iso):        # 1209
        if iso[-4:].lower() != ".cbn" and iso[-5:].lower() != ".cbin":
            return -1
        h = open(iso, "rb").read(24)
        magic, hsz, total, bsz, ver, align = struct.unpack_from("<4sIQIBB", h)
        if magic != b"CISO" or total <= 0 or bsz <= 0:
            return -1
        n = total // bsz
        raw = open(iso, "rb").read(hsz + 4 * n)[hsz:]
        vals = list(struct.unpack("<%dI" % n, raw)) + [None]   # [n]: never read (1262), uninitialised malloc in C
        tab, plain = [], set()
        for i, v in enumerate(vals):
            if v is None:
                tab.append(None)
                continue
            if v & 0x80000000:
                plain.add(i)
            tab.append((v & 0x7FFFFFFF) << align)
        self.compr = dict(shift=0, tab=tab, plain=plain, len=n, path=iso)
        return 0

    def handlechd(self, iso):         # 1294, metadata through chdman instead of libchdr
        if iso[-4:].lower() != ".chd":
            return -1
        out = subprocess.run([CHDMAN, "info", "-i", iso, "-v"], capture_output=True, text=True).stdout
        tr = re.findall(r"TRACK:(\d+) TYPE:(\S+) SUBTYPE:(\S+) FRAMES:(\d+) PREGAP:(\d+) PGTYPE:(\S+) PGSUB:(\S+) POSTGAP:(\d+)", out)
        if not tr:
            return -1
        self.cddaBE, self.ti, self.n = False, {}, 0
        frame, fileoff, padded, lay = 150, 0, 0, []
        self.log.append("chd: " + "; ".join(" ".join(t) for t in tr))
        for t in tr:
            num, typ, sub, frames, pregap = int(t[0]), t[1], t[2], int(t[3]), int(t[4])
            r = self.T(num)
            r["type"] = CDDA if typ.startswith("AUDIO") else DATA
            r["start"] = frame + pregap
            r["length"] = frames
            r["start_offset"] = fileoff + pregap
            r["frame0"] = fileoff
            lay.append((padded, fileoff, frames, r["type"] == CDDA))   # chdman pads every track to a multiple of 4 frames
            padded += (frames + 3) & ~3
            frame += frames
            fileoff += frames
            self.n += 1
        self.chd = dict(path=iso, bin=None, lay=lay)
        return 0

    # --- reads (return 2352 bytes or None)
    def chd_bin(self):
        if self.chd["bin"] is None:
            tmp = tempfile.mkdtemp()
            subprocess.run([CHDMAN, "extractcd", "-i", self.chd["path"], "-o", tmp + "/x.cue", "-ob", tmp + "/x.bin"],
                           check=True, capture_output=True)
            self.chd["bin"] = tmp + "/x.bin"
        return self.chd["bin"]

    def read(self, path, base, sector):   # cdimg_read_func
        if self.mode == "chd":
            sector += base   # a CHD frame number, padding frames included
            for pad0, un0, frames, audio in self.chd["lay"]:
                if pad0 <= sector < pad0 + ((frames + 3) & ~3):
                    break
            else:
                return None
            if sector - pad0 >= frames:
                return bytes(SS)
            d = rd(self.chd_bin(), (un0 + sector - pad0) * SS, SS)   # extractcd: audio little-endian again,
            if audio:                                                # the CHD itself (and so cdriso) holds it big-endian
                d = swap16(d)
            return d
        if self.mode == "compr":
            c = self.compr
            sector += base // SS
            blk = sector >> c["shift"]
            if sector >= c["len"] * 16:
                return None
            if c["tab"][blk] is None or c["tab"][blk + 1] is None:
                self.log.append("block %d has an uninitialised end offset" % blk)
                return None
            start, size = c["tab"][blk], c["tab"][blk + 1] - c["tab"][blk]
            if size < 0 or size > SS * 16 + 100:
                return None
            data = rd(c["path"], start, size)
            if blk not in c["plain"]:
                z = zlib.decompressobj(-15)
                data = z.decompress(data)
                if not z.eof or len(data) != SS << c["shift"]:
                    return None
            return data[(sector & ((1 << c["shift"]) - 1)) * SS:][:SS]
        if self.mode == "2048":
            return None
        pos = base + sector * (SS + 96 if self.subMixed else SS)
        d = rd(path, pos, SS)
        return d if len(d) == SS else None

    def read_track(self, lba):            # 2060 ISOreadTrack
        if self.pregap and lba >= self.pregap:
            lba -= 150
        return self.read(self.cd, 0, lba)

    def read_cdda(self, abs_sector):      # 2146 ISOreadCDDA (abs_sector = m*4500+s*75+f)
        track = self.n
        while True:
            if self.ti[track]["start"] <= abs_sector or track == 1:
                break
            track -= 1
        t = self.ti[track]
        if t["type"] != CDDA:
            return bytes(SS)
        h = self.cd
        if self.multifile:
            for f in range(track, 1, -1):
                if self.ti[f]["handle"]:
                    h = self.ti[f]["handle"]
                    break
        d = self.read(h, t["start_offset"], abs_sector - t["start"])
        if d is None:
            return None
        return swap16(d) if self.cddaBE else d

    def sub_q(self, lba):                 # 2107 ISOgetBufferSub + cdrom.c UpdateSubq CRC test
        if not self.subfile:
            return None
        d = rd(self.subfile, lba * 96, 96)
        q = d[12:24]
        return q if len(d) == 96 and calc_crc(q[:10]) == struct.unpack(">H", q[10:12])[0] else False


def find_boot(folder, name, fmt):
    for f in sorted(os.listdir(folder)):
        if f.lower().endswith(BOOT[fmt]) and f.startswith(name):
            return os.path.join(folder, f)


def check(root, only):
    games = sorted({m.group(1) for f in os.listdir(root) for m in [re.match(r"(.*) \[(?:%s)\]$" % "|".join(FORMATS), f)] if m})
    bad = 0
    for name in games:
        src = "%s/%s/%s.cue" % (root, name, name)
        D = Disc(src)
        print("game:", name, "(%d sectors, tracks %s)" % (D.n, [(t["num"], "A" if t["audio"] else "D", t["i1"]) for t in D.tracks]))
        for fmt in FORMATS:
            if only and fmt not in only:
                continue
            folder = "%s/%s [%s]" % (root, name, fmt)
            boot = find_boot(folder, name, fmt) if os.path.isdir(folder) else None
            if not boot:
                print("FAIL %-4s no %s in %s" % (fmt, BOOT[fmt], folder))
                bad += 1
                continue
            errs, notes = [], []
            try:
                C = Cdriso(boot)
                want = D.tracks[:1] if fmt == "ISO" else D.tracks
                if fmt != "ISO" and C.n != len(want):
                    errs.append("numtracks %d != %d" % (C.n, len(want)))
                for t in want if fmt != "ISO" else []:
                    r = C.ti.get(t["num"], {})
                    if r.get("type") != (CDDA if t["audio"] else DATA):
                        errs.append("track %d type %s != %s" % (t["num"], r.get("type"), CDDA if t["audio"] else DATA))
                    if r.get("start") != t["i1"] + 150:
                        errs.append("track %d start %s != %d" % (t["num"], r.get("start"), t["i1"] + 150))
                lo = C.ti[C.n]["start"] + C.ti[C.n]["length"] if C.n else None
                if fmt != "ISO" and lo != D.n + 150:
                    (notes if fmt in ("CBN", "CHD") else errs).append("lead-out (ISOgetTD 0) %s != %d" % (lo, D.n + 150))
                d16 = C.read_track(16)
                if d16 != D.sector(16):
                    errs.append("LBA 16 differs")
                elif d16[25:30] != b"CD001":
                    errs.append("no CD001 in LBA 16")
                for lba in (0, 1, D.tracks[0]["end"] - 1):
                    if C.read_track(lba) != D.sector(lba):
                        errs.append("data LBA %d differs" % lba)
                for t in want:
                    if t["audio"]:
                        for lba in (t["i1"], t["i1"] + (t["end"] - t["i1"]) // 2, t["end"] - 1):
                            if C.read_cdda(lba + 150) != swap16(D.sector(lba)):
                                errs.append("audio LBA %d differs" % lba)
                if fmt == "CCD":
                    for lba in (0, 16, D.tracks[1]["i1"] - 5, D.tracks[1]["i1"] + 7):
                        q = C.sub_q(lba)
                        if not q or q[7:10] != bcd_msf(lba + 150):
                            errs.append("sub Q bad at LBA %d" % lba)
                if fmt == "CHD":
                    v = subprocess.run([CHDMAN, "verify", "-i", boot], capture_output=True, text=True)
                    if v.returncode:
                        errs.append("chdman verify failed")
                notes += C.log
                if fmt == "CBN" and C.read_track(D.n) is None:
                    notes.append("phantom last sector unreadable (cdriso.c:1262/1275, expected)")
            except Exception as e:   # a port that crashes is a failed format
                errs.append("%s: %s" % (type(e).__name__, e))
            if fmt == "ISO":
                errs.append("no sheet: ISOgetStatus 2140 reports ti[1].type = 0, cdrom.c:1338 then answers CdlID as 'no disc'")
            msg = "; ".join(errs) if errs else "tracks %s, LBA 16 + audio sectors identical" % [(i, "A" if r["type"] == CDDA else "D", r["start"]) for i, r in sorted(C.ti.items()) if 1 <= i <= C.n]
            print("%s %-4s %s%s" % ("FAIL" if errs else "PASS", fmt, msg, "  [" + " | ".join(notes) + "]" if notes else ""))
            bad += bool(errs)
    return bad


if __name__ == "__main__":
    a = sys.argv[1:]
    only = set(a.pop(a.index("--only") + 1).upper().split(",")) if "--only" in a else None
    if "--only" in a:
        a.remove("--only")
    if a and a[0] == "--check" and len(a) == 2:
        sys.exit(1 if check(a[1], only) else 0)
    if len(a) == 2 and not a[0].startswith("--"):
        convert(a[0], a[1], only)
    else:
        sys.exit(__doc__)
