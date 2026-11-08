#!/usr/bin/env python3
"""cover_unstub.py -- make a libretro-thumbnails checkout readable on Windows.

libretro-thumbnails stores alias names as git symlinks. A checkout without symlink support (Windows,
core.symlinks=false) turns each into a tiny text file that holds the target's name, so about half of
a folder such as Named_Logos does not open as a picture. This replaces every such stub with a HARD
LINK to the picture it points to (NTFS: no extra space, no Developer Mode or admin needed, and every
program sees an ordinary file); where a hard link cannot be made (another volume, FAT) it copies.

Which files are aliases: in a git checkout, git's own list (index entries of mode 120000, whose
content is the target's name), so files an older run already replaced with copies are found too;
elsewhere, the text-stub test (cover_pack.resolve_stub).

    python scripts/cover_unstub.py DIR [DIR ...]      (a folder, searched one level deep: Named_Logos)
    python scripts/cover_unstub.py --dry-run DIR
    python scripts/cover_unstub.py --copy DIR         (copies instead of hard links)

Prints how many aliases it fixed and lists the ones whose target is missing (left as they are).
The pack builder (cover_pack.py) resolves stubs by itself; this is only for browsing the folders.
git status shows the fixed files as modified; that is expected and harmless. """
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import cover_pack as cp  # noqa: E402


def git_aliases(folder):
    """-> {file name: target name} of the symlink entries git records for folder, or None when folder
    is not in a git checkout (or git is missing)."""
    try:
        top = subprocess.run(["git", "-C", str(folder), "rev-parse", "--show-toplevel"], capture_output=True,
                             text=True, check=True).stdout.strip()
        ls = subprocess.run(["git", "-C", str(folder), "ls-files", "-s", "-z", "--", "."], capture_output=True,
                            check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    entries = []
    for rec in ls.split(b"\0"):
        if not rec.startswith(b"120000 "):
            continue
        meta, _, path = rec.partition(b"\t")
        entries.append((meta.split()[1].decode(), path.decode("utf-8")))
    if not entries:
        return {}
    blobs = "".join(sha + "\n" for sha, _ in entries).encode()
    out = subprocess.run(["git", "-C", top, "cat-file", "--batch"], input=blobs, capture_output=True, check=True).stdout
    aliases, pos = {}, 0
    for _, path in entries:
        nl = out.index(b"\n", pos)
        size = int(out[pos:nl].split()[2])
        target = out[nl + 1:nl + 1 + size].decode("utf-8").strip()
        pos = nl + 1 + size + 1
        aliases[pathlib.PurePosixPath(path).name] = target
    return aliases


def _target(folder, name, aliases, hops=8):
    """the real picture behind an alias name, following alias chains; None when it is missing or loops"""
    for _ in range(hops):
        if name not in aliases:
            p = folder / name
            return p if p.is_file() else None
        name = aliases[name]
    return None


def _same_file(a, b):
    try:
        return os.path.samefile(a, b)
    except OSError:
        return False


def _replace(p, real, copy):
    tmp = p.with_name(p.name + ".unstub-tmp")
    if tmp.exists():
        tmp.unlink()
    if not copy:
        try:
            os.link(real, tmp)
        except OSError:
            copy = True
    if copy:
        shutil.copyfile(str(real), str(tmp))
    os.replace(tmp, p)
    return "copy" if copy else "link"


def unstub(folder, dry_run=False, copy=False):
    """-> (fixed, broken name list, images seen, how {link|copy: n})"""
    fixed, broken, seen, how = 0, [], 0, {"link": 0, "copy": 0}
    aliases = git_aliases(folder)
    for p in sorted(folder.iterdir()):
        if not p.is_file() or p.suffix.lower() not in cp.IMG_EXTS or p.name.endswith(".unstub-tmp"):
            continue
        seen += 1
        if aliases:
            if p.name not in aliases:
                continue
            real = _target(folder, p.name, aliases)
        else:
            real = cp.resolve_stub(p)
            if real == p:
                continue
        if real is None:
            broken.append(p.name)
            continue
        if _same_file(p, real):
            continue                       # already a hard link to its picture
        if not dry_run:
            how[_replace(p, real, copy)] += 1
        fixed += 1
    return fixed, broken, seen, how


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dirs", nargs="+", type=pathlib.Path)
    ap.add_argument("--dry-run", action="store_true", help="count only, change nothing")
    ap.add_argument("--copy", action="store_true", help="copy the pictures instead of hard-linking them")
    args = ap.parse_args(argv)
    code = 0
    for d in args.dirs:
        if not d.is_dir():
            print("%s: not a folder" % d)
            code = 2
            continue
        fixed, broken, seen, how = unstub(d, args.dry_run, args.copy)
        done = "to fix" if args.dry_run else "fixed (%d hard links, %d copies)" % (how["link"], how["copy"])
        print("%s: %d pictures, %d aliases %s, %d broken" % (d, seen, fixed, done, len(broken)))
        for name in broken[:20]:
            print("  target missing: %s" % name)
        if len(broken) > 20:
            print("  ... and %d more" % (len(broken) - 20))
    return code


if __name__ == "__main__":
    sys.exit(main())
