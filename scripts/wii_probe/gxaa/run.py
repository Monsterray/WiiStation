#!/usr/bin/env python3
"""gxaa run.py -- show GX anti-aliasing variants on the bench Wii and save what the TV shows.

Sends gxaa.dol (build.sh) with one wiiload argument per variant (gxaa.c lists the keys), takes
`hbc.py screen` pictures until every variant was captured twice, sends `hbc.py exit`, and writes
  .runs/NAME/vNN.png      the last picture of each variant (the XFB the VI shows)
  .runs/NAME/sheet.png    all of them, labelled, x1
  .runs/NAME/result.txt   per variant: captures, mean luma, readback texel
One job, about a minute for 8 variants. A queue job only:
  C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --name gxaa --timeout 300 \\
    --cwd <repo> -- C:/Python312/python.exe scripts/wii_probe/gxaa/run.py NAME [VARIANT ...]
No VARIANT: the default set below. Pictures: the CPU stamps row 0-7 (white, 8-bit variant
index, white) and rows 8-15 (16-bit RGB565 readback texel of the red square, expect f800)."""
import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "scripts"))
from wii_targets import bench_wii, hbc, hbc_ready, parser, wait_in_hbc, wiiload  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

DEFAULT = [
    "fmt=8,aa=0,tex=0",            # 0 control: RGB8, no AA (what 240p does today)
    "",                            # 1 WiiStation GxColor=2 as built (565 + AA + readback)
    "tex=0",                       # 2 same without the readback copy
    "aa=0,tex=0",                  # 3 565, copy filter without AA
    "pat=c,tex=0",                 # 4 565 + AA, centred samples
    "fmt=6,aa=0,tex=0",            # 5 RGBA6 control
    "w=512,h=216,tex=0",           # 6 CTR's 240p size
    "sync=0,tex=0",                # 7 format switched behind a queued frame
]

ap = parser(__doc__)
ap.add_argument("name")
ap.add_argument("variants", nargs="*")
ap.add_argument("--secs", type=int, default=90, help="capture time limit")
a = ap.parse_args()
variants = a.variants or DEFAULT
wii = bench_wii("wii_probe/gxaa/run.py")
out = REPO / ".runs" / a.name
out.mkdir(parents=True, exist_ok=True)
dol = HERE / "gxaa.dol"
if not hbc_ready(wii, 60) or not wait_in_hbc(wii, 60):
    sys.exit("the Wii is not in HBC")
wiiload(wii, dol, variants)


def decode(img):
    """(index, texel) from the stamp, or None when the sync blocks are not white."""
    g = img.convert("L")
    bit = lambda b, y: g.getpixel((16 * b + 8, y)) > 128
    if not (bit(0, 4) and bit(9, 4)):
        return None
    idx = sum(bit(b, 4) << (8 - b) for b in range(1, 9))
    tex = sum(bit(b, 12) << (15 - b) for b in range(16))
    return idx, tex


seen = {}
tmp = out / "_screen.png"
end = time.monotonic() + a.secs
time.sleep(4)
while time.monotonic() < end and not all(seen.get(i, (0,))[0] >= 2 for i in range(len(variants))):
    r = hbc(wii, "screen", tmp, timeout=30)
    if r.rc:
        print("screen:", r.text[-200:])
        time.sleep(1)
        continue
    img = Image.open(tmp)
    img.load()
    d = decode(img)
    if not d or d[0] >= len(variants):
        print("no stamp in this picture")
        continue
    i, tex = d
    n = seen.get(i, (0,))[0] + 1
    img.save(out / f"v{i:02d}.png")
    g = img.convert("L").crop((0, 16, img.width, img.height))
    luma = sum(g.getdata()) / (g.width * g.height)
    seen[i] = (n, tex, luma, img.size)
    print(f"v{i:02d} capture {n}: luma {luma:.1f}, texel {tex:04x}, {img.size[0]}x{img.size[1]}", flush=True)
    time.sleep(0.7)

hbc(wii, "exit", timeout=30, show=True)
back = wait_in_hbc(wii, 60)

lines = []
for i, v in enumerate(variants):
    n, tex, luma, size = seen.get(i, (0, 0, 0.0, (0, 0)))
    lines.append(f"v{i:02d}  [{v or 'default'}]  captures {n}  luma {luma:.1f}  texel {tex:04x}  {size[0]}x{size[1]}")
(out / "result.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))

tiles = [(i, Image.open(out / f"v{i:02d}.png").convert("RGB")) for i in range(len(variants))
         if (out / f"v{i:02d}.png").exists()]
if tiles:
    W = max(t.width for _, t in tiles)
    H = max(t.height for _, t in tiles) + 14
    cols = 2
    sheet = Image.new("RGB", (W * cols, H * ((len(tiles) + cols - 1) // cols)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for k, (i, t) in enumerate(tiles):
        x, y = (k % cols) * W, (k // cols) * H
        dr.text((x + 2, y + 1), f"v{i:02d} {variants[i] or 'default'}", fill=(255, 255, 0))
        sheet.paste(t, (x, y + 14))
    sheet.save(out / "sheet.png")
tmp.unlink(missing_ok=True)
print("HBC back" if back else "HBC NOT back within 60 s")
sys.exit(0 if back else 1)
