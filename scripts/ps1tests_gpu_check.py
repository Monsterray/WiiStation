"""ps1tests_gpu_check.py RUN [--png DIR] [--detail] -- PASS/FAIL per ps1-tests GPU test and renderer.

RUN is a chain run of scripts/chains/ps1tests_gpu.txt (or any chain of ps1t_* GPU tests).
Each chained game is matched to its real-PS1 result in $PS1_TESTS/bin/gpu/<test>/ (PS1_TESTS: the ps1-tests folder, no default;
unset skips the check):
  psx.log  console text: every line must be in the game's part of RUN/tty.log (matched in
           order: the k-th "gpu/<test>" banner is the k-th chain entry of that test).
           bandwidth prints timings: a line passes when its hblank count is within 10%.
  vram.png VRAM at the end: RUN/vram_NN.bin (the chain's end-of-game dump) must have the same
           15-bit colour in every pixel (bit 15 is not in the picture); BOX limits a test whose
           picture holds VRAM the test never drew. Use ChainDump=1 on the chain lines.
A column is one renderer and setting set (the chain line's set=, e.g. gpuPlugin=0,GpuSoftMode=0).
--png DIR writes DIR/<test>_<column>.png with differing pixels in magenta; --detail prints
the failing lines and the bounding box of differing pixels.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import parse_opts  # noqa: E402
import tools  # noqa: E402

PS1_TESTS = tools.get("PS1_TESTS", False) or ""
REF = os.path.join(PS1_TESTS, "bin", "gpu")
# Where a reference picture holds pixels the test never draws (VRAM the real console had
# before the test ran), only the test's own area is compared: (x0, y0, x1, y1), end excluded.
BOX = {"transparency": (0, 0, 320, 240)}
# Pixels whose colour the test leaves undefined: gpu/lines sets RGB2 twice and never RGB3 of
# its LINE_G4 polylines, so the last segment (the diagonal back to the start, (x, y) to
# (x + 32, y + 32)) fades to whatever was on the PS1's stack. (x, y, length) per diagonal.
DIAG = {"lines": [(150, 140, 33), (210, 140, 33)]}
W, H = 1024, 512
END = re.compile(r"=== chain (\d+)/\d+ end vblanks=\d+(?: set=(\S+))? at=\S+ rom=(\S+) ===")


def entries(run):
    out = []
    for line in open(os.path.join(run, "perf.log"), errors="replace"):
        m = END.search(line)
        if m:
            out.append((int(m.group(1)), m.group(2) or "-", m.group(3).removesuffix(".exe")))
    return out


def tty_sections(run, tests):
    """{test: [lines of the 1st run, of the 2nd, ...]} cut at each "gpu/<test>" banner"""
    p = os.path.join(run, "tty.log")
    lines = [l.rstrip() for l in open(p, errors="replace")] if os.path.isfile(p) else []
    secs, cur = {}, None
    for l in lines:
        m = re.fullmatch(r"gpu/(\S+)", l.strip())
        if m and m.group(1) in tests:
            cur = []
            secs.setdefault(m.group(1), []).append(cur)
        elif cur is not None and l.strip():
            cur.append(l.strip())
    return secs


def check_log(test, got, detail):
    want = [l.strip() for l in open(os.path.join(REF, test, "psx.log"), errors="replace")]
    want = [l for l in want if l and not l.startswith("gpu/")]
    if got is None:
        return "NO LOG", []
    bad = []
    if test == "bandwidth":
        hb = {}
        for l in got:
            m = re.match(r"(.*?)\s+dT:.*hblanks:\s*(\d+)", l)
            if m:
                hb[m.group(1).strip()] = int(m.group(2))
        for l in want:
            m = re.match(r"(.*?)\s+dT:.*hblanks:\s*(\d+)", l)
            if not m:
                continue
            k, ref = m.group(1).strip(), int(m.group(2))
            g = hb.get(k)
            if g is None or abs(g - ref) > ref // 10:
                bad.append(f"{k}: PS1 {ref} hblanks, emu {g}")
    else:
        for l in want:
            if l.rstrip(".") not in [g.rstrip(".") for g in got]:
                bad.append(f"PS1: {l}")
    return ("PASS" if not bad else f"FAIL {len(bad)}/{len(want)}"), bad


def check_vram(test, path, png):
    x0, y0, x1, y1 = BOX.get(test, (0, 0, W, H))
    skip = {(dx + i, dy + i) for dx, dy, n in DIAG.get(test, []) for i in range(n)}
    if not os.path.isfile(path):
        return "NO VRAM", []
    from PIL import Image
    d = open(path, "rb").read()
    px = struct.unpack_from(f"<{W * H}H", d)
    ref = Image.open(os.path.join(REF, test, "vram.png")).convert("RGB").load()
    bad, box = 0, [W, H, -1, -1]
    out = Image.new("RGB", (W, H)) if png else None
    op = out.load() if png else None
    for y in range(H):
        for x in range(W):
            p = px[y * W + x]
            g = ((p & 31) << 3, ((p >> 5) & 31) << 3, ((p >> 10) & 31) << 3)
            r = ref[x, y]
            diff = g != (r[0] & 0xF8, r[1] & 0xF8, r[2] & 0xF8) and x0 <= x < x1 and y0 <= y < y1 and (x, y) not in skip
            if diff:
                bad += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
            if png:
                op[x, y] = (255, 0, 255) if diff else g
    if png:
        out.save(png)
    return ("PASS" if not bad else f"FAIL {bad}px"), ([f"box {box[0]},{box[1]}-{box[2]},{box[3]}"] if bad else [])


def main():
    pos, opts = parse_opts(sys.argv, valued=("--png",), bools=("--detail", "-h", "--help"))
    if not pos or "-h" in opts or "--help" in opts:
        sys.exit(__doc__.strip())
    if not PS1_TESTS:
        print("ps1tests_gpu_check: PS1_TESTS is not set (the ps1-tests folder with bin/gpu): skipped")
        return
    run = pos[0]
    ents = entries(run)
    if not ents:
        sys.exit(f"{run}: no '=== chain N/M end' lines in perf.log")
    tests = sorted({t for _, _, t in ents})
    secs = tty_sections(run, tests)
    seen, cols, rows, details = {}, [], {}, []
    png = opts.get("--png")
    if png:
        os.makedirs(png, exist_ok=True)
    for i, sets, test in ents:
        k = seen.get(test, 0)
        seen[test] = k + 1
        col = ",".join(k for k in sets.split(",") if not k.startswith("ChainDump=")) or "-"
        if col not in cols:
            cols.append(col)
        if os.path.isfile(os.path.join(REF, test, "psx.log")):
            lines = secs.get(test, [])
            res, why = check_log(test, lines[k] if k < len(lines) else None, opts.get("--detail"))
        elif os.path.isfile(os.path.join(REF, test, "vram.png")):
            res, why = check_vram(test, os.path.join(run, f"vram_{i:02d}.bin"),
                                  png and os.path.join(png, f"{test}_{col.replace(',', '_').replace('=', '')}.png"))
        else:
            res, why = "NO REF", []
        rows.setdefault(test, {})[col] = res
        details += [f"  {test} [{col}] {w}" for w in why]
    tw = max(len(t) for t in rows) + 2
    cw = [max(len(c), 12) + 2 for c in cols]
    print("test".ljust(tw) + "".join(c.ljust(w) for c, w in zip(cols, cw)))
    for t in rows:
        print(t.ljust(tw) + "".join(rows[t].get(c, "-").ljust(w) for c, w in zip(cols, cw)))
    passed = {c: sum(1 for t in rows if rows[t].get(c) == "PASS") for c in cols}
    print("passed".ljust(tw) + "".join(f"{passed[c]}/{sum(1 for t in rows if c in rows[t])}".ljust(w) for c, w in zip(cols, cw)))
    if opts.get("--detail") and details:
        print("\n".join(details))


if __name__ == "__main__":
    main()
