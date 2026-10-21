"""card_sync.py CARD_ROOT [--apply]: make a mounted SD card's wiistation/isos/ match the game
library (C:/wiistation-lib/games, or $WSX_LIBRARY/games): copies every library file that is
missing on the card or has another size. Never deletes: a card file the library lacks is only
listed. Dry run without --apply. For the BENCH Wii's card in a card reader (2026-10-21: it
carries everything, past the HBC agent's 512 MB put limit); never a production Wii's card.

    python scripts/card_sync.py D:/            (list)
    python scripts/card_sync.py D:/ --apply    (copy)
"""
import os
import shutil
import sys

lib = os.path.join(os.environ.get("WSX_LIBRARY", "C:/wiistation-lib"), "games")
if len(sys.argv) < 2:
    sys.exit(__doc__)
card = os.path.join(sys.argv[1], "wiistation", "isos")
apply = "--apply" in sys.argv
if not os.path.isdir(card):
    sys.exit(f"{card}: not there (is the card mounted?)")
if os.path.isdir(os.path.join(sys.argv[1], "hbcbench")) is False:
    print("note: no hbcbench/ on this card -- check it is the bench Wii's card before --apply")

SKIP = (".orig", ".ibg", ".bmp", ".jpg", ".png", ".nfo", ".sfv", ".md5")   # not for the Wii
todo, total = [], 0
for root, _, files in os.walk(lib):
    rel = os.path.relpath(root, lib)
    for f in files:
        if f.lower().endswith(SKIP):
            continue
        src = os.path.join(root, f)
        dst = os.path.join(card, rel, f)
        size = os.path.getsize(src)
        if not os.path.exists(dst) or os.path.getsize(dst) != size:
            todo.append((src, dst, size))
            total += size
extra = []
for root, _, files in os.walk(card):
    rel = os.path.relpath(root, card)
    for f in files:
        if not os.path.exists(os.path.join(lib, rel, f)):
            extra.append(os.path.join(rel, f))

print(f"{len(todo)} file(s) to copy, {total / 1e9:.2f} GB; {len(extra)} card file(s) not in the library (kept)")
for p in extra[:20]:
    print("  card only:", p)
if not apply:
    for s, d, n in todo[:40]:
        print(f"  copy {n / 1e6:9.1f} MB  {os.path.relpath(s, lib)}")
    sys.exit(0)
done = 0
for s, d, n in todo:
    os.makedirs(os.path.dirname(d), exist_ok=True)
    shutil.copyfile(s, d)
    done += n
    print(f"  {done / 1e9:6.2f} / {total / 1e9:.2f} GB  {os.path.relpath(s, lib)}", flush=True)
print("done")
