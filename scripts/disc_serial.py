"""disc_serial.py -- read a PS1 game's serial from the disc itself: the BOOT line of SYSTEM.CNF
(cue/bin, iso, img/ccd, mdf/mds, toc/bin, chd via chdman), or DISC_ID in a PBP's PARAM.SFO.

    python scripts/disc_serial.py FILE...      prints "SERIAL  FILE" (or "-" when none is found)

Used by disc_art_fetch.py for games with no serial in their name."""

import pathlib
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import tools  # noqa: E402

BOOT = re.compile(rb"BOOT\s*=\s*cdrom:\\?([A-Z]{4})[_-]?(\d{3})\.?(\d{2})", re.I)
# (sector size, offset of the user data in the sector): ISO, raw MODE2, raw MODE1, raw + subchannel
LAYOUTS = ((2048, 0), (2352, 24), (2352, 16), (2448, 24), (2448, 16), (2336, 8))


def _data_file(path):
    """The track file behind a sheet (cue, toc, ccd, mds), else the file itself."""
    ext = path.suffix.lower()
    if ext in (".cue", ".toc"):
        m = re.search(r'(?:FILE|DATAFILE)\s+"([^"]+)"', path.read_text(errors="replace"))
        return path.parent / m.group(1) if m else None
    if ext in (".ccd", ".mds"):
        return path.with_suffix(".img" if ext == ".ccd" else ".mdf")
    return path


def _from_image(f):
    def read(layout, lba, size):
        sec, off = layout
        out = b""
        while len(out) < size:
            f.seek(lba * sec + off)
            out += f.read(2048)
            lba += 1
        return out[:size]

    for layout in LAYOUTS:
        pvd = read(layout, 16, 2048)
        if pvd[1:6] != b"CD001":
            continue
        root_lba, root_size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
        d, i = read(layout, root_lba, root_size), 0
        while i < len(d):
            n = d[i]
            if n == 0:                      # records do not cross a sector: skip to the next one
                i = (i // 2048 + 1) * 2048
                continue
            name = d[i + 33:i + 33 + d[i + 32]]
            if name.upper().startswith(b"SYSTEM.CNF"):
                lba, size = struct.unpack_from("<I", d, i + 2)[0], struct.unpack_from("<I", d, i + 10)[0]
                m = BOOT.search(read(layout, lba, min(size, 2048)))
                return (m.group(1) + m.group(2) + m.group(3)).decode().upper() if m else None
            i += n
        return None
    return None


def _from_pbp(path):
    with open(path, "rb") as f:
        head = f.read(40)
        if head[:4] != b"\0PBP":
            return None
        sfo_at, icon_at = struct.unpack_from("<II", head, 8)
        f.seek(sfo_at)
        sfo = f.read(icon_at - sfo_at)
    if sfo[:4] != b"\0PSF":
        return None
    keys, data, count = struct.unpack_from("<III", sfo, 8)
    for k in range(count):
        koff, _, length, _, doff = struct.unpack_from("<HHIII", sfo, 20 + 16 * k)
        if sfo[keys + koff:sfo.index(b"\0", keys + koff)] == b"DISC_ID":
            v = sfo[data + doff:data + doff + length].rstrip(b"\0").decode(errors="replace")
            return v if re.fullmatch(r"[A-Z]{4}\d{5}", v) else None
    return None


def _from_chd(path):
    chdman = tools.get("CHDMAN", required=False)
    if not chdman:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        cue, bin_ = pathlib.Path(tmp) / "d.cue", pathlib.Path(tmp) / "d.bin"
        r = subprocess.run([chdman, "extractcd", "-i", str(path), "-o", str(cue), "-ob", str(bin_)],
                           capture_output=True)
        return serial_of(cue) if r.returncode == 0 else None


def serial_of(path):
    """-> 'SCUS94467' or None."""
    path = pathlib.Path(path)
    try:
        ext = path.suffix.lower()
        if ext == ".pbp":
            return _from_pbp(path)
        if ext == ".chd":
            return _from_chd(path)
        data = _data_file(path)
        if not data or not data.is_file():
            return None
        with open(data, "rb") as f:
            return _from_image(f)
    except (OSError, struct.error, ValueError, IndexError):
        return None


if __name__ == "__main__":
    for a in sys.argv[1:]:
        print(serial_of(a) or "-", a)
