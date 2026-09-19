#!/usr/bin/env python3
"""sdimage_read.py IMAGE PATH/IN/IMAGE [OUT]
Read one file out of a FAT32 SD-card image (e.g. Dolphin's WiiSD.raw) by
walking the cluster chain directly. Unlike 7-Zip it tolerates a file whose
directory entry was updated before the last clusters were flushed (the
state a killed emulator leaves behind): it returns as many bytes as the
chain holds and reports the shortfall on stderr instead of failing.
Path components are matched case-insensitively against long or short
names. Output goes to OUT or stdout."""
import struct, sys

def read_bpb(f):
    f.seek(0); b = f.read(512)
    bps, spc, rsv, nfats = struct.unpack_from("<HBHB", b, 11)
    spf = struct.unpack_from("<I", b, 36)[0]
    root = struct.unpack_from("<I", b, 44)[0]
    if bps == 0 or spf == 0:            # maybe an MBR: use the first partition
        lba = struct.unpack_from("<I", b, 0x1C6)[0]
        f.seek(lba * 512); b = f.read(512)
        bps, spc, rsv, nfats = struct.unpack_from("<HBHB", b, 11)
        spf = struct.unpack_from("<I", b, 36)[0]
        root = struct.unpack_from("<I", b, 44)[0]
        base = lba * 512
    else:
        base = 0
    fat_off = base + rsv * bps
    data_off = fat_off + nfats * spf * bps
    return dict(bps=bps, spc=spc, fat=fat_off, data=data_off, root=root, cl=bps * spc)

def chain(f, g, first):
    out, c, seen = [], first, set()
    while 2 <= c < 0x0FFFFFF8 and c not in seen:
        seen.add(c); out.append(c)
        f.seek(g["fat"] + c * 4); c = struct.unpack("<I", f.read(4))[0] & 0x0FFFFFFF
    return out

def read_chain(f, g, first, size=None):
    parts = []
    for c in chain(f, g, first):
        f.seek(g["data"] + (c - 2) * g["cl"]); parts.append(f.read(g["cl"]))
    d = b"".join(parts)
    return d if size is None else d[:size]

def entries(raw):
    lfn = []
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if e[0] == 0: break
        if e[0] == 0xE5: lfn = []; continue
        attr = e[11]
        if attr == 0x0F:
            seq = e[0] & 0x1F
            name = e[1:11] + e[14:26] + e[28:32]
            lfn.insert(0, (seq, name.decode("utf-16le", "ignore").split("\x00")[0]))
            continue
        short = (e[0:8].decode("ascii", "ignore").strip() + "." + e[8:11].decode("ascii", "ignore").strip()).rstrip(".")
        long = "".join(n for _, n in sorted(lfn)) if lfn else ""
        lfn = []
        first = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        yield short, long, attr, first, size

def main():
    img, path = sys.argv[1], sys.argv[2].replace("\\", "/").strip("/")
    out = sys.argv[3] if len(sys.argv) > 3 else None
    with open(img, "rb") as f:
        g = read_bpb(f)
        cur = g["root"]
        parts = path.split("/")
        for k, p in enumerate(parts):
            raw = read_chain(f, g, cur)
            hit = None
            for short, long, attr, first, size in entries(raw):
                if p.lower() in (short.lower(), long.lower()):
                    hit = (attr, first, size); break
            if hit is None:
                sys.exit(f"not found: {p}")
            attr, first, size = hit
            if k < len(parts) - 1:
                cur = first
            else:
                data = read_chain(f, g, first)
                if len(data) < size:
                    print(f"warning: directory says {size} bytes, chain holds {len(data)}", file=sys.stderr)
                data = data[:size]
                if out:
                    open(out, "wb").write(data); print(f"{out}: {len(data)} bytes", file=sys.stderr)
                else:
                    sys.stdout.buffer.write(data)

if __name__ == "__main__":
    main()
