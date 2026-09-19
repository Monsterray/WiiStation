"""sheet.py <out.png> <frames_dir> <start> <step> <end> [cols=6] [w=206]
Contact sheet of framedump_<n>.png thumbnails, frame number burned in."""
import sys, os, subprocess
FF = r"C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe"
out, d, start, step, end = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 6
w = int(sys.argv[7]) if len(sys.argv) > 7 else 206
h = w * 480 // 824
ins, flt, k = [], [], 0
for i in range(start, end + 1, step):
    f = os.path.join(d, f"framedump_{i}.png")
    if not os.path.isfile(f): continue
    ins += ["-i", f]
    flt.append(f"[{k}:v]scale={w}:{h},drawtext=text='{i}':x=3:y=3:fontsize=13:fontcolor=yellow:box=1:boxcolor=black@0.6[p{k}]")
    k += 1
layout = "|".join(f"{(j % cols) * w}_{(j // cols) * h}" for j in range(k))
flt.append("".join(f"[p{j}]" for j in range(k)) + f"xstack=inputs={k}:layout={layout}:fill=black")
r = subprocess.run([FF, "-y", "-loglevel", "error", *ins, "-filter_complex", ";".join(flt), out], capture_output=True, text=True)
err = "\n".join(l for l in r.stderr.splitlines() if "Fontconfig" not in l)
print(f"{out}: {k} thumbs" + (f"  ERR {err[:300]}" if r.returncode else ""))
