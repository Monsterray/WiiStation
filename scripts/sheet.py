"""sheet.py <out.png> <frames_dir> <start> <step> <end> [cols=6] [w=206]
Contact sheet of framedump_<n>.png thumbnails, frame number burned in.

Composed in-process with Pillow. It used to be one ffmpeg call with an -i and a
scale/drawtext block per frame, which Windows refuses past 32,767 characters of command
line (WinError 206) -- a few hundred frames of a long dump run."""
import sys, os
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_int  # noqa: E402

need(sys.argv, 6, "sheet.py <out.png> <frames_dir> <start> <step> <end> [cols=6] [w=206]")
out, d = sys.argv[1], sys.argv[2]
start, step, end = (req_int(sys.argv[3], '<start>'), req_int(sys.argv[4], '<step>'),
                    req_int(sys.argv[5], '<end>'))
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 6
w = int(sys.argv[7]) if len(sys.argv) > 7 else 206
h = w * 480 // 824

nums = [i for i in range(start, end + 1, step)
        if os.path.isfile(os.path.join(d, f"framedump_{i}.png"))]
if not nums:
    print(f"{out}: 0 thumbs")
    sys.exit(0)
sheet = Image.new("RGB", (cols * w, -(-len(nums) // cols) * h))
draw = ImageDraw.Draw(sheet, "RGBA")
font = ImageFont.load_default(size=13)
for k, i in enumerate(nums):
    x, y = (k % cols) * w, (k // cols) * h
    with Image.open(os.path.join(d, f"framedump_{i}.png")) as im:
        sheet.paste(im.convert("RGB").resize((w, h), Image.BICUBIC), (x, y))
    box = draw.textbbox((x + 3, y + 3), str(i), font=font)
    draw.rectangle(box, fill=(0, 0, 0, 153))          # black at 0.6, as ffmpeg's boxcolor was
    draw.text((x + 3, y + 3), str(i), font=font, fill=(255, 255, 0))
sheet.save(out)
print(f"{out}: {len(nums)} thumbs")
