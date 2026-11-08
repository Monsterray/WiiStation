#!/usr/bin/env python3
"""cover_pack.py -- build the offline cover pack for WiiStation's explorer / ISO info page. (DRAFT)

Everything is local: it reads directories the user already cloned and writes a folder laid out
like the SD card. No network access, nothing is downloaded.

Sources
  --libretro DIR   libretro-thumbnails "Sony_-_PlayStation" checkout (Named_Boxarts/*.png, named by
                   the exact Redump game name with  & * / : \\ ` < > ? |  replaced by '_').
                   Needs --dat, because the file name comes from the game name.
  --psxcovers DIR  xlenore/psx-covers checkout (covers/default/<SERIAL-with-dash>.jpg,
                   covers/3d/<serial>.png).
  --dat FILE       libretro-database metadat/redump "Sony - PlayStation.dat" (clrmamepro format):
                   gives serial <-> game name.
  --disc-dir DIR   optional user-made disc art. Matching rule: a file belongs to the serial of the
                   dat game whose name it carries (exact, case-folded or letters-and-digits-only
                   match of the game name, with or without the libretro '_' mapping), else to the
                   serial its name STARTS with (SLUS-00506.png, slus00506 foo.jpg). A serial found
                   only this way (not in the dat, not in psx-covers) still gets a pack row, with
                   box '-'; for that, the serial needs a known prefix (SLUS SCUS SLES SCES SLPS
                   SCPS SLPM), so 'FIFA 2000.png' is not read as a serial. (A serial the dat or
                   psx-covers already lists takes any prefix.) Files matching nothing are listed in unmatched.txt (written only when
                   there are some), on the console and in report.txt.

Output (<out>/<root>/, root defaults to "covers"; --root . puts it directly under <out>)
  index.txt        tab separated, sorted by serial, no header, one row per serial that has a file:
                     SERIAL  box_path  disc_path  source  3d_path      ('-' = none)
                   paths are relative to the covers folder: box/SLUS00506.jpg, disc/SLUS00506.png,
                   3d/SLUS00506.png.  source = psxcovers|libretro|- (of the box cover)
  box/<SERIAL>.jpg|png    disc/<SERIAL>.<ext>    3d/<SERIAL>.png
  missing.txt      serials with no box cover (SERIAL<TAB>name, '-' for a serial not in the dat)
  unmatched.txt    disc-dir file names that match no serial (only when there are any)
  report.txt       the coverage table (only with --report)
Serials are written without the dash, upper case (SLUS00506): the CdromId form. Dashed input is
accepted; suffixes ("SCED-04009/PR") are dropped; several serials in one value are all used.

Modes: --jpg-first (default): psx-covers by serial, then libretro by name only where psx-covers
has nothing. --png-first: the other way round. A multi-disc game (the discs differ only in a
"(Disc N)" tag) shares one cover: a disc without its own cover gets the one of the first disc
that has one, one box file per serial. Disc art is per disc.

--games DIR|FILE keeps only the games of one card: a folder (searched recursively) or a list of
file names; names that match no game go to unmatched_games.txt. Pictures that are the same file
content (the discs of one game) are written ONCE and every serial's index row points at that
file (FAT has no links: index.txt is the alias table); --no-share writes one file per serial.

--resize N shrinks images whose longest side is over N (needs Pillow, never upscales); without it
every file is copied byte for byte.  --dry-run writes nothing.
"""
import argparse
import collections
import pathlib
import re
import shutil
import sys

PREFIXES = ("SLUS", "SCUS", "SLES", "SCES", "SLPS", "SCPS", "SLPM")
FORBIDDEN = re.compile(r"[&*/:\\`<>?|]")
SERIAL_RE = re.compile(r"^([A-Z]{2,5})[-_ ]?(\d{3,6})")
DISC_TAG = re.compile(r"\s*\((?:Disc|CD)\s*\d+[^)]*\)", re.I)
IMG_EXTS = (".jpg", ".jpeg", ".png")
LIBRETRO_SUBDIRS = (("Named_Boxarts",), ("Sony - PlayStation", "Named_Boxarts"),
                    ("Sony_-_PlayStation", "Named_Boxarts"))
PSX_DEFAULT_SUBDIRS = (("covers", "default"), ("default",))
PSX_3D_SUBDIRS = (("covers", "3d"), ("3d",))


class PackError(Exception):
    """A problem the user can fix (bad path, missing Pillow, ...)."""


# ---------------------------------------------------------------- names and serials

def libretro_name(name):
    """The libretro-thumbnails file stem for a game name."""
    return FORBIDDEN.sub("_", name)


def base_name(name):
    """Game name without the "(Disc N)" tag: equal for all discs of one game."""
    return DISC_TAG.sub("", name).strip()


def _alnum(text):
    return re.sub(r"[^0-9a-z]+", "", text.casefold())


def normalise_serial(raw):
    """'SLUS-00506' / 'slus00506' -> 'SLUS00506'; None when it is not a serial."""
    if raw is None:
        return None
    m = SERIAL_RE.match(str(raw).strip().upper())
    return m.group(1) + m.group(2) if m else None


def parse_serials(raw):
    """All serials in one dat value: 'SCED-04009/PR', 'SLES-1234, SLES-1235' ..."""
    out = []
    for tok in re.split(r"[,;\s]+", raw or ""):
        s = normalise_serial(tok.split("/")[0])
        if s and s not in out:
            out.append(s)
    return out


def prefix_of(serial):
    p = serial[:4]
    return p if p in PREFIXES else "other"


# ---------------------------------------------------------------- clrmamepro dat

_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([()])|([^\s()"]+)')


def _tokenize(text):
    toks = []
    for m in _TOKEN.finditer(text):
        if m.group(1) is not None:
            toks.append(("s", m.group(1)))
        elif m.group(2):
            toks.append((m.group(2), m.group(2)))
        else:
            toks.append(("w", m.group(3)))
    return toks


def _parse_block(toks, i):
    """toks[i] is the token after '('. Returns ({key: [values]}, index after ')')."""
    block = {}
    n = len(toks)
    while i < n:
        kind, val = toks[i]
        if kind == ")":
            return block, i + 1
        if kind == "(":                      # stray block without a key
            _, i = _parse_block(toks, i + 1)
        elif kind == "w":
            i += 1
            if i < n and toks[i][0] == "(":
                sub, i = _parse_block(toks, i + 1)
                block.setdefault(val.lower(), []).append(sub)
            elif i < n and toks[i][0] in ("s", "w"):
                block.setdefault(val.lower(), []).append(toks[i][1])
                i += 1
        else:
            i += 1
    return block, i                           # unbalanced file: keep what we have


def parse_dat(text):
    """-> list of game blocks (dicts) from clrmamepro text; the header block is ignored."""
    toks = _tokenize(text)
    games = []
    i = 0
    while i < len(toks):
        kind, val = toks[i]
        if kind == "w" and i + 1 < len(toks) and toks[i + 1][0] == "(":
            block, i = _parse_block(toks, i + 2)
            if val.lower() in ("game", "machine", "resource"):
                games.append(block)
        else:
            i += 1
    return games


def read_dat(path):
    """-> ({serial: game name}, stats). First name (sorted) wins when a serial repeats."""
    try:
        text = pathlib.Path(path).read_text(encoding="utf-8-sig", errors="replace")
    except OSError as e:
        raise PackError("cannot read dat %s: %s" % (path, e))
    stats = collections.Counter()
    pairs = []
    for g in parse_dat(text):
        stats["games"] += 1
        names = [v for v in g.get("name", []) if isinstance(v, str) and v.strip()]
        raws = [v for v in g.get("serial", []) if isinstance(v, str)]
        for rom in g.get("rom", []):
            if isinstance(rom, dict):
                raws += [v for v in rom.get("serial", []) if isinstance(v, str)]
        serials = []
        for raw in raws:
            serials += [s for s in parse_serials(raw) if s not in serials]
        if not names:
            stats["no_name"] += 1
        elif not serials:
            stats["no_serial"] += 1
        else:
            pairs += [(s, names[0].strip()) for s in serials]
    entries = {}
    for serial, name in sorted(pairs):
        entries.setdefault(serial, name)
    stats["serials"] = len(entries)
    return entries, stats


# ---------------------------------------------------------------- source directories

def resolve_stub(path, hops=4):
    """libretro-thumbnails keeps alias names as git symlinks; a checkout without symlink support
    (Windows) turns each into a tiny text file holding the target's name, in the same folder.
    -> the real image file; path itself for anything that is not such a stub; None for a stub whose
    target is missing (or loops)."""
    for _ in range(hops):
        try:
            if path.stat().st_size > 512:
                return path
            head = path.read_bytes()
        except OSError:
            return None
        if head[:4] == b"\x89PNG" or head[:3] == b"\xff\xd8\xff":
            return path
        try:
            text = head.decode("utf-8").strip().replace("\\\\", "/")
        except UnicodeDecodeError:
            return path
        if not text or "\n" in text or not text.lower().endswith(IMG_EXTS):
            return path                  # not a pointer: some other small file, left to the caller
        target = path.parent / text
        if not target.is_file():
            return None
        path = target
    return None


class FileIndex:
    """Image files of one directory, looked up by name (three tiers) or by serial."""

    def __init__(self, directory, exts=IMG_EXTS):
        rank = {e: i for i, e in enumerate(exts)}
        named = [p for p in directory.iterdir() if p.is_file() and p.suffix.lower() in rank]
        named.sort(key=lambda p: (rank[p.suffix.lower()], p.name))   # extension priority, then name
        # (name used for matching, real file that holds the picture): a stub keeps its own name
        pairs = [(p, resolve_stub(p)) for p in named]
        pairs = [(n, r) for n, r in pairs if r is not None]
        files = [r for _, r in pairs]
        self.count = len(files)
        self.files = files
        self.exact, self.fold, self.key, self._serial = {}, {}, {}, {}
        for n, p in pairs:
            self.exact.setdefault(n.stem, p)
            self.fold.setdefault(n.stem.casefold(), p)
            if _alnum(n.stem):
                self.key.setdefault(_alnum(n.stem), p)
            s = normalise_serial(n.stem)
            if s:
                self._serial.setdefault(s, p)

    def find(self, stem):
        """-> (path, tier) with tier exact|fold|key, or (None, None)."""
        if stem in self.exact:
            return self.exact[stem], "exact"
        if stem.casefold() in self.fold:
            return self.fold[stem.casefold()], "fold"
        k = _alnum(stem)
        if k and k in self.key:
            return self.key[k], "key"
        return None, None

    def by_serial(self, serial):
        return self._serial.get(serial)

    def serials(self):
        return set(self._serial)


def resolve_dir(base, label, subdirs, fallback_to_base=True):
    base = pathlib.Path(base)
    if not base.is_dir():
        raise PackError("%s: not a directory: %s" % (label, base))
    for sub in subdirs:
        cand = base.joinpath(*sub)
        if cand.is_dir():
            return cand
    if fallback_to_base:
        return base
    raise PackError("%s: none of %s found under %s" % (
        label, ", ".join("/".join(s) for s in subdirs), base))


# ---------------------------------------------------------------- planning

def disc_serials(discs):
    """Serials a disc-dir may add on its own. Game names such as 'FIFA 2000' look like a serial,
    so a file only counts when its serial has a known PlayStation prefix (SLUS, SCES, ...)."""
    return {s for s in discs.serials() if s[:4] in PREFIXES}


Box = collections.namedtuple("Box", "path source")
Row = collections.namedtuple("Row", "serial name box disc d3")


def plan(entries, psx, lib, discs, d3, mode="jpg"):
    """-> sorted list of Row for every known serial (box/disc/d3 may be None).

    Known serials = the dat, the psx-covers files and the disc-dir files whose name starts with a
    serial (a disc-only serial gets a row with box '-')."""
    universe = set(entries) | (psx.serials() if psx else set()) | (disc_serials(discs) if discs else set())
    groups = collections.defaultdict(list)
    for s in universe:
        name = entries.get(s)
        groups[base_name(name).casefold() if name else "\0" + s].append(s)

    def own_box(s):
        order = ("psx", "lib") if mode == "jpg" else ("lib", "psx")
        for src in order:
            if src == "psx" and psx:
                p = psx.by_serial(s)
                if p:
                    return Box(p, "psxcovers")
            elif src == "lib" and lib and entries.get(s):
                name = entries[s]
                for cand in (name, base_name(name)):
                    p, _ = lib.find(libretro_name(cand))
                    if p:
                        return Box(p, "libretro")
        return None

    boxes = {}
    for members in groups.values():
        members.sort()
        own = {s: own_box(s) for s in members}
        shared = next((own[s] for s in members if own[s]), None)    # first disc that has one
        for s in members:
            boxes[s] = own[s] or shared

    rows = []
    for s in sorted(universe):
        name = entries.get(s)
        disc = None
        if discs:
            if name:
                disc = discs.find(libretro_name(name))[0] or discs.find(name)[0]
            disc = disc or discs.by_serial(s)
        rows.append(Row(s, name, boxes[s], disc, d3.by_serial(s) if d3 else None))
    return rows


def _ext(path):
    return ".jpg" if path.suffix.lower() in (".jpg", ".jpeg") else ".png"


def unmatched_discs(rows, discs):
    """Names of disc-dir files that no row uses: no dat name, no serial in the name (a lower
    priority duplicate of a used file, same stem, is not reported)."""
    if not discs:
        return []
    used = {r.disc.stem for r in rows if r.disc}
    return sorted(p.name for p in discs.files if p.stem not in used)


def _content_key(path):
    import hashlib
    return hashlib.sha1(pathlib.Path(path).read_bytes()).hexdigest()


def assign_paths(rows, share=True):
    """-> {(serial, kind): path relative to the covers folder}, kind box|disc|3d.

    share: two serials whose picture is the same file content (the discs of one game, a re-release
    with the same cover) get ONE file, the first serial's, and both index rows point at it: FAT has no
    links, so index.txt is the pack's alias table (WiiStation only ever reads the path a row gives)."""
    paths, first = {}, {}
    for r in rows:
        for kind, src, ext in (("box", r.box.path if r.box else None, _ext(r.box.path) if r.box else ""),
                               ("disc", r.disc, _ext(r.disc) if r.disc else ""),
                               ("3d", r.d3, ".png")):
            if not src:
                continue
            rel = "%s/%s%s" % (kind, r.serial, ext)
            if share:
                rel = first.setdefault((kind, ext, _content_key(src)), rel)
            paths[(r.serial, kind)] = rel
    return paths


def index_lines(rows, paths=None):
    if paths is None:
        paths = assign_paths(rows, share=False)
    lines = []
    for r in rows:
        if not (r.box or r.disc or r.d3):
            continue
        lines.append("\t".join((
            r.serial,
            paths.get((r.serial, "box"), "-"),
            paths.get((r.serial, "disc"), "-"),
            r.box.source if r.box else "-",
            paths.get((r.serial, "3d"), "-"))))
    return lines


def missing_lines(rows):
    return ["%s\t%s" % (r.serial, r.name or "-") for r in rows if not r.box]


def coverage(rows):
    """-> ordered {prefix: Counter(total, box, disc, d3, missing)} plus 'TOTAL'."""
    table = collections.OrderedDict((p, collections.Counter()) for p in PREFIXES + ("other",))
    total = collections.Counter()
    for r in rows:
        for c in (table[prefix_of(r.serial)], total):
            c["total"] += 1
            c["box"] += bool(r.box)
            c["disc"] += bool(r.disc)
            c["d3"] += bool(r.d3)
            c["missing"] += not r.box
    table["TOTAL"] = total
    return table


def report_text(rows, unmatched=()):
    out = ["%-6s %7s %7s %7s %7s %8s" % ("prefix", "serials", "box", "disc", "3d", "no box")]
    for p, c in coverage(rows).items():
        out.append("%-6s %7d %7d %7d %7d %8d" % (p, c["total"], c["box"], c["disc"], c["d3"], c["missing"]))
    if unmatched:
        out.append("")
        out.append("unmatched disc art (%d, no serial found): %s" % (len(unmatched), ", ".join(unmatched)))
    return "\n".join(out)


# ---------------------------------------------------------------- only the games on a card

TRACK_TAG = re.compile(r"\s*\(Track\s*\d+\)", re.I)
GAME_EXTS = (".bin", ".cue", ".iso", ".img", ".chd", ".pbp", ".cbn", ".z", ".znx", ".ecm", ".m3u", ".mdf", ".ccd")


def game_names(src):
    """-> sorted file stems of the games in src: a folder (searched recursively for game files) or a
    text file with one name or path per line ('#' starts a comment)."""
    p = pathlib.Path(src)
    if p.is_dir():
        found = [f.stem for f in p.rglob("*") if f.is_file() and f.suffix.lower() in GAME_EXTS]
    elif p.is_file():
        found = []
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.split("#", 1)[0].strip().replace("\\", "/")
            if line:
                name = line.rsplit("/", 1)[-1]
                stem, dot, ext = name.rpartition(".")
                found.append(stem if dot and ("." + ext.lower()) in GAME_EXTS else name)
    else:
        raise PackError("--games: no such file or folder: %s" % src)
    return sorted(set(found))


REV_TAG = re.compile(r"\s*\((?:Rev\s*\w+|v\d+(?:\.\d+)*)\)", re.I)
ANY_SERIAL = re.compile(r"[\[(]\s*([A-Z]{4})[-_ ]?(\d{3,5})\s*[\])]", re.I)


def _no_rev(name):
    return REV_TAG.sub("", name).strip()


def select_games(rows, names):
    """-> (rows of the named games only, names that matched no game). A name matches, in this order:
    a dat game by its full name (case folded, or letters and digits only); every disc of the game
    whose name without "(Disc N)" it is (a multi-disc PBP); the same two without "(Rev N)"/"(v1.1)"
    tags on either side; a serial at its start (SLUS-00506.iso) or in brackets anywhere
    ("Ape Escape [U] [SCUS-94423]"), where a serial no game has (a typo: SLUS-94244) is taken by its
    number when exactly one known serial has that number."""
    tables = [collections.defaultdict(set) for _ in range(4)]   # full, base, full no-rev, base no-rev
    by_number = collections.defaultdict(set)
    for r in rows:
        by_number[r.serial[4:]].add(r.serial)
        if r.name:
            forms = (r.name, base_name(r.name), _no_rev(r.name), base_name(_no_rev(r.name)))
            for t, form in zip(tables, forms):
                for k in (form.casefold(), _alnum(form)):
                    t[k].add(r.serial)
    known = {r.serial for r in rows}

    def by_serial(n):
        cands = []
        s = normalise_serial(n)
        if s:
            cands.append(s)
        cands += [(m.group(1) + m.group(2)).upper() for m in ANY_SERIAL.finditer(n)]
        for s in cands:
            if s in known:
                return {s}
        for s in cands:
            same = by_number.get(s[4:], set())
            if len(same) == 1:
                return set(same)
        return set()

    keep, unmatched = set(), []
    for n in names:
        n2 = TRACK_TAG.sub("", n).strip()
        forms = (n2, base_name(n2), _no_rev(n2), base_name(_no_rev(n2)))
        hit = set()
        for t, form in zip(tables, forms):
            for key in (form.casefold(), _alnum(form)):
                if key and key in t:
                    hit = t[key]
                    break
            if hit:
                break
        hit = hit or by_serial(n2)
        if hit:
            keep |= hit
        else:
            unmatched.append(n)
    return [r for r in rows if r.serial in keep], unmatched


# ---------------------------------------------------------------- writing

def load_pillow():
    try:
        from PIL import Image
    except ImportError:
        raise PackError("--resize needs Pillow, which is not installed (py -m pip install Pillow)")
    return Image


def place(src, dst, resize=0, Image=None):
    """Copy src to dst byte for byte, or shrink it when resize is set and the image is larger."""
    if resize and Image is not None:
        with Image.open(src) as im:
            w, h = im.size
            if max(w, h) > resize:
                scale = resize / float(max(w, h))
                size = (max(1, round(w * scale)), max(1, round(h * scale)))
                resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
                small = im.resize(size, resample)
                if dst.suffix.lower() == ".jpg":
                    small.convert("RGB").save(dst, "JPEG", quality=90, optimize=True)
                else:
                    small.save(dst, "PNG", optimize=True)
                return
    shutil.copyfile(str(src), str(dst))


def write_pack(rows, pack, resize=0, report=False, unmatched=(), share=True):
    Image = load_pillow() if resize else None
    for sub in ("box", "disc", "3d"):
        (pack / sub).mkdir(parents=True, exist_ok=True)
    paths = assign_paths(rows, share)
    written = set()
    for r in rows:
        for kind, src in (("box", r.box.path if r.box else None), ("disc", r.disc), ("3d", r.d3)):
            rel = paths.get((r.serial, kind))
            if src and rel and rel not in written:     # a shared picture is written once
                place(src, pack / rel, resize, Image)
                written.add(rel)

    def write(name, lines):
        with open(str(pack / name), "w", encoding="utf-8", newline="") as f:
            f.write("".join(l + "\n" for l in lines))
    write("index.txt", index_lines(rows, paths))
    write("missing.txt", missing_lines(rows))
    if unmatched:
        write("unmatched.txt", list(unmatched))
    if report:
        write("report.txt", report_text(rows, unmatched).split("\n"))


# ---------------------------------------------------------------- command line

def parse_args(argv):
    ap = argparse.ArgumentParser(prog="cover_pack.py", description=__doc__.split("\n")[0])
    ap.add_argument("--libretro", metavar="DIR")
    ap.add_argument("--psxcovers", metavar="DIR")
    ap.add_argument("--dat", metavar="FILE")
    ap.add_argument("--disc-dir", metavar="DIR")
    ap.add_argument("--out", metavar="DIR", default="covers_pack")
    ap.add_argument("--root", default="covers", help="folder under --out (the on-card root); '.' for none")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--jpg-first", dest="mode", action="store_const", const="jpg",
                      help="psx-covers JPG by serial, then libretro PNG by name (default)")
    mode.add_argument("--png-first", dest="mode", action="store_const", const="png",
                      help="libretro PNG by name, then psx-covers by serial")
    ap.add_argument("--with-3d", action="store_true", help="also copy covers/3d/<serial>.png into 3d/")
    ap.add_argument("--resize", type=int, default=0, metavar="N",
                    help="shrink images whose longest side exceeds N (Pillow)")
    ap.add_argument("--games", metavar="DIR|FILE",
                    help="only the games in this folder (searched recursively) or list file: a pack for one card")
    ap.add_argument("--no-share", action="store_true",
                    help="one file per serial even when two serials have the same picture")
    ap.add_argument("--dry-run", action="store_true", help="write nothing")
    ap.add_argument("--report", action="store_true", help="also write report.txt")
    ap.set_defaults(mode="jpg")
    return ap.parse_args(argv)


def run(args):
    """-> (rows, pack path). Raises PackError."""
    if not (args.libretro or args.psxcovers):
        raise PackError("give at least one of --libretro, --psxcovers")
    if args.libretro and not args.dat:
        raise PackError("--libretro needs --dat (the file name is built from the game name)")
    if args.with_3d and not args.psxcovers:
        raise PackError("--with-3d needs --psxcovers")
    if args.resize < 0:
        raise PackError("--resize must be positive")
    root = pathlib.PurePosixPath(args.root.replace("\\", "/"))
    if root.is_absolute() or ".." in root.parts:
        raise PackError("--root must be a relative folder name: %s" % args.root)

    entries, stats = ({}, collections.Counter())
    if args.dat:
        if not pathlib.Path(args.dat).is_file():
            raise PackError("--dat: not a file: %s" % args.dat)
        entries, stats = read_dat(args.dat)
        print("dat: %d games, %d serials, %d without serial, %d without name" % (
            stats["games"], stats["serials"], stats["no_serial"], stats["no_name"]))
    psx = lib = discs = d3 = None
    if args.psxcovers:
        psx = FileIndex(resolve_dir(args.psxcovers, "--psxcovers", PSX_DEFAULT_SUBDIRS))
        if args.with_3d:
            d3 = FileIndex(resolve_dir(args.psxcovers, "--psxcovers", PSX_3D_SUBDIRS, False), (".png",))
        print("psx-covers: %d covers%s" % (psx.count, ", %d 3d" % d3.count if d3 else ""))
    if args.libretro:
        lib = FileIndex(resolve_dir(args.libretro, "--libretro", LIBRETRO_SUBDIRS), (".png", ".jpg", ".jpeg"))
        print("libretro: %d boxarts" % lib.count)
    if args.disc_dir:
        discs = FileIndex(resolve_dir(args.disc_dir, "--disc-dir", ()))
        print("disc art: %d files" % discs.count)
    if args.resize:
        load_pillow()                       # fail before anything is written

    rows = plan(entries, psx, lib, discs, d3, args.mode)
    unmatched_games = []
    if args.games:
        names = game_names(args.games)
        rows, unmatched_games = select_games(rows, names)
        print("games: %d names, %d serials matched, %d names matched nothing" % (
            len(names), len(rows), len(unmatched_games)))
        for n in unmatched_games:
            print("  no serial for: %s" % n)
    pack = pathlib.Path(args.out)
    if root.parts and root != pathlib.PurePosixPath("."):
        pack = pack.joinpath(*root.parts)
    unmatched = unmatched_discs(rows, discs)
    print(report_text(rows, unmatched))
    if args.dry_run:
        print("dry run: would write %d index rows, %d missing to %s" % (
            len(index_lines(rows)), len(missing_lines(rows)), pack))
    else:
        write_pack(rows, pack, args.resize, args.report, unmatched, share=not args.no_share)
        if unmatched_games:
            with open(str(pack / "unmatched_games.txt"), "w", encoding="utf-8", newline="") as f:
                f.write("".join(n + "\n" for n in unmatched_games))
        print("wrote %d index rows, %d missing to %s" % (
            len(index_lines(rows)), len(missing_lines(rows)), pack))
    return rows, pack


def main(argv=None):
    try:
        run(parse_args(sys.argv[1:] if argv is None else argv))
    except PackError as e:
        print("cover_pack: error: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("cover_pack: error: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
