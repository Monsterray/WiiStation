#!/usr/bin/env python3
"""vram2png.py VRAM.bin OUT.png [--crop X,Y,W,H] [--scale N]
Render a 1024x512 little-endian 15-bit PSX VRAM dump (sd:/wiistation/vram.bin
from the debug build's 'dump' directive) as a PNG through ffmpeg. The whole
VRAM is drawn by default; --crop selects a window such as a display buffer
(e.g. 0,240,512,240). Needs numpy and ffmpeg."""
import os
import shutil
import sys, subprocess, numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, parse_opts, req_file, req_int  # noqa: E402
FF = shutil.which("ffmpeg") or r"C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe"

def main():
    need(sys.argv, 3, "vram2png.py VRAM.bin OUT.png [--crop X,Y,W,H] [--scale N]")
    args, opts = parse_opts(sys.argv, valued=('--crop', '--scale'))
    if len(args) != 2:
        sys.exit("need exactly a VRAM dump and an output PNG")
    src, out = req_file(args[0], 'VRAM dump'), args[1]
    crop = None
    if opts.get('--crop'):
        try:
            crop = [int(v) for v in opts['--crop'].split(',')]
        except ValueError:
            sys.exit(f"--crop: need X,Y,W,H, got {opts['--crop']!r}")
        if len(crop) != 4:
            sys.exit(f"--crop: need X,Y,W,H, got {opts['--crop']!r}")
    scale = req_int(opts.get('--scale', 1), '--scale')
    if os.path.getsize(src) != 512 * 1024 * 2:
        sys.exit(f"{src}: {os.path.getsize(src)} bytes, want a 1 MiB 1024x512 dump")
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
