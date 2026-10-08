#!/usr/bin/env python3
"""vram2png.py VRAM.bin OUT.png [--crop X,Y,W,H] [--scale N]
Render a 1024x512 little-endian 15-bit PSX VRAM dump (sd:/wiistation/vram.bin
from the debug build's 'dump' directive) as a PNG through ffmpeg. The whole
VRAM is drawn by default; --crop selects a window such as a display buffer
(e.g. 0,240,512,240). Needs numpy and ffmpeg."""
import sys, subprocess, numpy as np
FF = r"C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe"

def main():
    src, out = sys.argv[1], sys.argv[2]
    crop, scale = None, 1
    args = sys.argv[3:]
    while args:
        a = args.pop(0)
        if a == "--crop": crop = [int(v) for v in args.pop(0).split(",")]
        elif a == "--scale": scale = int(args.pop(0))
    v = np.fromfile(src, dtype="<u2").reshape(512, 1024)
    if crop:
        x, y, w, h = crop; v = v[y:y+h, x:x+w]
    r = ((v & 0x1F) << 3).astype(np.uint8); g = (((v >> 5) & 0x1F) << 3).astype(np.uint8); b = (((v >> 10) & 0x1F) << 3).astype(np.uint8)
    rgb = np.dstack([r, g, b])
    h, w = v.shape
    cmd = [FF, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{w}x{h}", "-i", "-",
           "-vf", f"scale={w*scale}:{h*scale}:flags=neighbor", out]
    subprocess.run(cmd, input=rgb.tobytes(), check=True)
    print(f"{out}: {w}x{h} (x{scale})")

if __name__ == "__main__":
    main()
