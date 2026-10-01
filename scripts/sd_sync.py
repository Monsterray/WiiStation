"""sd_sync.py LOAD_DIR [--dry-run]

Decides whether the play window's Dolphin must repack its SD folder at this start, which
costs 27-35 s at every boot for a 6 GB folder (and 7 s more unpacking at exit). Dolphin must
not be running. LOAD_DIR is the profile's Load folder, holding WiiSDSync/ and WiiSD.raw.

Compares the folder with the image, path and size, file by file:
  - WiiStation's own files (everything under wiisxrx/ except isos/, bios/ and fonts/: memory
    cards, save states, settings, logs): the IMAGE is the master -- WiiStation writes them there
    while it runs. Any that differ are copied from the image into the folder first.
  - every other file (games, BIOS, fonts, apps): the FOLDER is the master. If any was added,
    removed or changed size, this start must sync ("on"); if none, it can skip the repack
    ("off"), and Dolphin uses the image as it is.
Prints "on" or "off" as its last line (scripts/wiistation_play.sh reads it).

With folder sync off, WiiStation's writes stay in the image only; they reach the folder at the
next start that runs this (the copy above), before any repack could overwrite them. A file the
user deletes from wiisxrx/saves while the image still has it comes back the same way.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sdimage_read import read_bpb, read_chain, entries

OURS_SKIP = ("wiisxrx/isos/", "wiisxrx/bios/", "wiisxrx/fonts/")

def ours(rel):
    r = rel.lower()
    return r.startswith("wiisxrx/") and not r.startswith(OURS_SKIP)

def image_tree(f, g):
    """{lower relpath: (relpath, first cluster, size)} for every file in the image"""
    out, stack = {}, [("", g["root"])]
    while stack:
        prefix, cl = stack.pop()
        for short, long, attr, first, size in entries(read_chain(f, g, cl)):
            name = long or short
            # entries() turns "." and ".." into "" (its rstrip(".")): skip them, or the walk
            # descends into "." for ever
            if name in ("", ".", "..") or attr & 0x08:
                continue
            rel = prefix + name
            if attr & 0x10:
                if first:
                    stack.append((rel + "/", first))
            else:
                out[rel.lower()] = (rel, first, size)
    return out

def folder_tree(root):
    out = {}
    for d, _, files in os.walk(root):
        for n in files:
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace("\\", "/")
            out[rel.lower()] = (rel, os.path.getsize(p))
    return out

def main():
    load, dry = sys.argv[1], "--dry-run" in sys.argv
    folder, image = os.path.join(load, "WiiSDSync"), os.path.join(load, "WiiSD.raw")
    if not os.path.isdir(folder) or not os.path.isfile(image):
        print("no folder or no image yet: sync")
        print("on")
        return
    with open(image, "rb") as f:
        g = read_bpb(f)
        img = image_tree(f, g)
        fol = folder_tree(folder)
        theirs = [k for k in set(img) | set(fol) if not ours(k)
                  and (k not in img or k not in fol or img[k][2] != fol[k][1])]
        back = []
        for k, (rel, first, size) in img.items():
            if not ours(k):
                continue
            if k in fol and fol[k][1] == size:
                with open(os.path.join(folder, fol[k][0]), "rb") as h:
                    if h.read() == read_chain(f, g, first, size):
                        continue
            back.append((fol[k][0] if k in fol else rel, first, size))   # keep the folder's spelling
        for rel, first, size in back:
            print(f"from the image: {rel} ({size} bytes)")
            if not dry:
                dst = os.path.join(folder, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as h:
                    h.write(read_chain(f, g, first, size))
    for k in sorted(theirs)[:10]:
        print(f"changed in the folder: {(fol.get(k) or img.get(k))[0]}")
    if len(theirs) > 10:
        print(f"... and {len(theirs) - 10} more")
    print(f"{len(img)} files in the image, {len(fol)} in the folder; {len(back)} copied back;"
          f" {len(theirs)} of the user's files changed")
    print("on" if theirs else "off")

if __name__ == "__main__":
    main()
