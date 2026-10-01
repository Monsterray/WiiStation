"""Read the E/F/V/T colours of AmiDog psxtest_gte's result grid from an xfb.png (xfb2png.py output, soft GPU).
Prints per text row the class of each letter: G ok, R error, Y warning, C info, n n/a. The row
order is the order on screen (skill: references/test-roms.md). usage: python scripts/amidog_grid.py xfb.png"""
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); W, H = im.size
def cls(c):
    r, g, b = c
    if r > 150 and g < 80 and b < 80: return 'R'   # error
    if g > 150 and r < 100 and b < 100: return 'G' # ok
    if r > 150 and g > 150 and b < 100: return 'Y' # warning
    if b > 150 and g > 150 and r < 100: return 'C' # info
    if r > 80 and g > 40 and b < 60 and r > g: return 'n'  # brown n/a
    return None
def cells(x0, x1, y0, y1):
    """per letter cell (8 px wide) the majority class of the coloured pixels"""
    out = []
    for cx in range(x0, x1, 8):
        cnt = {}
        for x in range(cx, cx + 8):
            for y in range(y0, y1):
                k = cls(im.getpixel((x, y)))
                if k: cnt[k] = cnt.get(k, 0) + 1
        out.append(max(cnt, key=cnt.get) if cnt else '.')
    return ''.join(out)
# find text rows: runs of y with any lit pixel in the given column band
def rows(x0, x1):
    lit = [any(sum(im.getpixel((x, y))) > 200 for x in range(x0, x1)) for y in range(H)]
    rs, y = [], 0
    while y < H:
        if lit[y]:
            s = y
            while y < H and lit[y]: y += 1
            rs.append((s, y))
        y += 1
    return rs
for name, (tx0, tx1, lx0) in {'col3': (210, 270, 274), 'col4': (315, 375, 380)}.items():
    for (y0, y1) in rows(lx0, lx0 + 32):
        print(name, y0, cells(lx0, lx0 + 32, y0, y1))
