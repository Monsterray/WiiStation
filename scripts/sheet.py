"""sheet.py <out.png> <frames_dir> <start> <step> <end> [cols=6] [w=206]
Contact sheet of framedump_<n>.png thumbnails, frame number burned in."""
import shutil
import sys, os, subprocess, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, req_int  # noqa: E402

FF = shutil.which("ffmpeg") or r"C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe"
need(sys.argv, 6, "sheet.py <out.png> <frames_dir> <start> <step> <end> [cols=6] [w=206]")
out, d = sys.argv[1], sys.argv[2]
start, step, end = (req_int(sys.argv[3], '<start>'), req_int(sys.argv[4], '<step>'),
                    req_int(sys.argv[5], '<end>'))
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 6
w = int(sys.argv[7]) if len(sys.argv) > 7 else 206
h = w * 480 // 824

# A dump run can leave thousands of frames, and Windows refuses a process command
# line over 32,767 characters outright (CreateProcess, WinError 206) -- one -i plus
# its scale/drawtext block per frame reaches that at a few hundred. Compose such
# runs in batches well under the cap, then stack the batch sheets: every batch is
# whole rows of the final `cols` grid, so the stack still reads in frame order.
# (A small sheet is one call, exactly as before.)
CMD_BUDGET = 24000


def compose(dst, paths, labels=None):
    """One ffmpeg call: the given images, `cols` per row, a row-major xstack to dst.
    `labels` (the frame numbers) burns each thumbnail's number in."""
    ins, flt, lay = [], [], []
    for k, p in enumerate(paths):
        ins += ["-i", p]
        if labels is None:
            flt.append(f"[{k}:v]null[p{k}]")
        else:
            flt.append(f"[{k}:v]scale={w}:{h},drawtext=text='{labels[k]}':x=3:y=3:"
                       f"fontsize=13:fontcolor=yellow:box=1:boxcolor=black@0.6[p{k}]")
        lay.append(f"{(k % cols) * w}_{(k // cols) * h}")
    flt.append("".join(f"[p{k}]" for k in range(len(paths))) +
               f"xstack=inputs={len(paths)}:layout={'|'.join(lay)}:fill=black")
    return subprocess.run([FF, "-y", "-loglevel", "error", *ins,
                           "-filter_complex", ";".join(flt), dst],
                          capture_output=True, text=True)


def report(r, k):
    err = "\n".join(l for l in (r.stderr or "").splitlines() if "Fontconfig" not in l)
    print(f"{out}: {k} thumbs" + (f"  ERR {err[:300]}" if r.returncode else ""))


nums = [i for i in range(start, end + 1, step)
        if os.path.isfile(os.path.join(d, f"framedump_{i}.png"))]
paths = [os.path.join(d, f"framedump_{i}.png") for i in nums]
if not nums:
    print(f"{out}: 0 thumbs")
    sys.exit(0)

# fps + "-i " + the per-input filter block, plus quoting slack: measured, not guessed.
# Whole rows only, so a batch boundary never splits a row and the stack has no gaps.
per_item = len(os.path.join(d, "framedump_000000.png")) + 160
rows = max(1, min(64, CMD_BUDGET // (per_item * cols)))
batch = rows * cols

if len(nums) <= batch:
    report(compose(out, paths, nums), len(nums))
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix="sheet_")
try:
    chunks, counts = [], []
    for s in range(0, len(nums), batch):
        cp = os.path.join(tmp, f"chunk_{len(chunks):03d}.png")
        r = compose(cp, paths[s:s + batch], nums[s:s + batch])
        if r.returncode:
            report(r, 0)
            sys.exit(1)
        chunks.append(cp)
        counts.append(len(paths[s:s + batch]))
    # The batch sheets are already the final width; stack them. y advances by each
    # batch's full row count (its last row may be partly empty -- filled black).
    ins, flt, lay, y = [], [], [], 0
    for k, cp in enumerate(chunks):
        ins += ["-i", cp]
        flt.append(f"[{k}:v]null[p{k}]")
        lay.append(f"0_{y}")
        y += ((counts[k] + cols - 1) // cols) * h
    flt.append("".join(f"[p{k}]" for k in range(len(chunks))) +
               f"xstack=inputs={len(chunks)}:layout={'|'.join(lay)}:fill=black")
    r = subprocess.run([FF, "-y", "-loglevel", "error", *ins,
                        "-filter_complex", ";".join(flt), out],
                       capture_output=True, text=True)
    report(r, len(nums))
    sys.exit(r.returncode)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
