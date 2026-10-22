"""marks.py -- point at a problem in a recording, and trace exactly what is drawn there.

While recording a movie (scripts/movie_capture.sh), press the Shot button combo at each moment
that looks wrong. Each screenshot becomes a "# mark: VBLANK WxH FILE" line in the movie's header
(the vblank comes from perf.log's "screenshot: ... vbl=" line).

  marks.py page NAME [--dol DOL]
      Replays scripts/autoinput/NAME_play.txt to the last mark with every frame kept, picks the
      frame on screen at each mark and writes .runs/marks_NAME/marks.html: drag a box (or
      several) around what is wrong on each picture, then "Copy marks" and paste the JSON into
      .runs/marks_NAME/marks.json (or into the chat).
  marks.py trace NAME [--dol DOL] [--json FILE]
      For each box: replays the movie with "trace VBLANK" and "tracebox" (the box in PS1 screen
      coordinates) and writes .runs/marks_NAME/mark_VBLANK_N.txt, the primitives drawn there in
      order (scripts/ptrace_summary.py). The DOL must carry the trace: build it with
      `wsx.sh build debug all` (default .runs/marks_trace.dol, made then).

The replays use the build given (default Gamecube/WiiSXRX_debug.dol). A recording replays
exactly on the build it was made with ("# build:" line); probes do not change the guest.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from cliutil import flag  # noqa: E402
import vtl_view  # noqa: E402

PY = sys.executable
# A POSIX bash that sees C:/ (wsx.sh needs one): Git's (what wsx.sh is run from; devkitPro's MSYS2 sed broke its -E substitution), else MSYS2. A bare "bash" from
# Python on Windows is System32's WSL bash, which cannot see C:/ at all.
BASH = next((b for b in ("C:/Program Files/Git/usr/bin/bash.exe", "C:/devkitPro/msys2/usr/bin/bash.exe",
                         "C:/Program Files/Git/bin/bash.exe") if os.path.isfile(b)), shutil.which("bash") or "bash")


def header(name):
    path = os.path.join(HERE, "autoinput", f"{name}_play.txt")
    if not os.path.isfile(path):
        sys.exit(f"{path}: no such recording")
    h = {"marks": []}
    for line in open(path, encoding="utf-8", errors="replace"):
        if not line.startswith("#"):
            continue
        k, _, v = line[1:].strip().partition(": ")
        if k == "mark":
            vbl, size, file = (v.split(" ", 2) + ["", ""])[:3]
            w, _, hgt = size.partition("x")
            h["marks"].append({"vbl": int(vbl), "w": int(w or 320), "h": int(hgt or 240), "file": file})
        elif k in ("folder", "cue", "chain line"):
            h[k] = v
    if "chain line" not in h:
        sys.exit(f"{path}: no '# chain line:' header (re-record with scripts/movie_capture.sh)")
    return path, h


def run_chain(run, name, h, vbls, dol, extra_input=None):
    """Replays the movie to max(vbls) + 30 in one chained game; returns the run directory."""
    out = os.path.join(REPO, ".runs", f"marks_{name}")
    os.makedirs(out, exist_ok=True)
    play = f"{name}_play.txt"
    if extra_input:   # the movie with trace lines on top, under its own name
        play = f"{name}_mark.txt"
        src = os.path.join(HERE, "autoinput", f"{name}_play.txt")
        with open(os.path.join(HERE, "autoinput", play), "w", newline="\n") as f:
            f.write(extra_input)
            f.writelines(l for l in open(src, encoding="utf-8") if not l.startswith("#"))
    fields = h["chain line"].split()
    fields[0] = str(max(vbls) + 30)
    fields[1] = f"sd:/wiistation/{play}"
    chain = os.path.join(out, f"{run}.txt")
    with open(chain, "w", newline="\r\n") as f:
        f.write("CHAIN\n# marks.py: the movie replayed to its marks\n" + " ".join(fields) + "\n"
                + h["folder"] + "\n" + h["cue"] + "\n")
    fw = lambda p: p.replace(chr(92), "/")   # bash takes C:/..., not C:\... (it ate the backslashes)
    cmd = [BASH, fw(os.path.join(HERE, "wsx.sh")), "chain", run, fw(chain), "--dol", fw(dol), "--fast",
           "--secs", "1800", "--env", "FRAMES_DUMP=True", "--env", "KEEP=100000"]
    subprocess.run(cmd, cwd=REPO)
    return out, os.path.join(REPO, ".runs", run)


def frame_at(rundir, vbl):
    """The dump on screen at a vblank: menu frames before the game, then one per present."""
    rows = vtl_view.load(os.path.join(rundir, "vtl_01.bin"))
    frames = len([f for f in os.listdir(os.path.join(rundir, "frames")) if f.endswith(".png")])
    offset = frames - sum(r[5] for r in rows)
    n = offset + sum(r[5] for r in rows[:vbl + 1])
    return os.path.join(rundir, "frames", f"framedump_{max(1, n)}.png")


PAGE = """<!doctype html><meta charset=utf-8><title>Marks: %(name)s</title>
<style>body{font:14px sans-serif;background:#222;color:#ddd;margin:16px}
.m{margin:0 0 24px} canvas{cursor:crosshair;border:1px solid #555;max-width:100%%}
textarea{width:100%%;height:120px;background:#111;color:#cfc} button{margin:4px 8px 4px 0}</style>
<h2>%(name)s: drag a box around what is wrong (as many as you like)</h2>
<p>Each picture is the frame on screen when you pressed Shot. Undo removes the last box on that picture.
Then Copy marks and paste them into .runs/marks_%(name)s/marks.json or into the chat.</p>
<div id=list></div><button onclick=copyOut()>Copy marks</button><textarea id=out readonly></textarea>
<script>
const marks=%(marks)s, key='marks_%(name)s';
let boxes={}; try{boxes=JSON.parse(localStorage.getItem(key))||{}}catch(e){}
function save(){try{localStorage.setItem(key,JSON.stringify(boxes))}catch(e){}
 document.getElementById('out').value=JSON.stringify(marks.map(m=>({vbl:m.vbl,w:m.w,h:m.h,img_w:m.img_w,img_h:m.img_h,boxes:boxes[m.vbl]||[]})))}
function copyOut(){save();const t=document.getElementById('out');t.select();navigator.clipboard&&navigator.clipboard.writeText(t.value)}
for(const m of marks){const d=document.createElement('div');d.className='m';
 d.innerHTML='<b>vblank '+m.vbl+'</b> <button>Undo</button><br>';const c=document.createElement('canvas');d.appendChild(c);
 document.getElementById('list').appendChild(d);const img=new Image();img.src=m.img;
 const draw=(t)=>{const g=c.getContext('2d');g.drawImage(img,0,0);g.lineWidth=3;g.strokeStyle='#f33';
  for(const b of (boxes[m.vbl]||[]))g.strokeRect(b[0],b[1],b[2]-b[0],b[3]-b[1]);if(t){g.strokeStyle='#ff0';g.strokeRect(t[0],t[1],t[2]-t[0],t[3]-t[1])}};
 img.onload=()=>{c.width=img.width;c.height=img.height;m.img_w=img.width;m.img_h=img.height;draw();save()};
 let s=null;const at=e=>{const r=c.getBoundingClientRect();return[(e.clientX-r.left)*c.width/r.width,(e.clientY-r.top)*c.height/r.height]};
 c.onmousedown=e=>{s=at(e)};c.onmousemove=e=>{if(s){const p=at(e);draw([Math.min(s[0],p[0]),Math.min(s[1],p[1]),Math.max(s[0],p[0]),Math.max(s[1],p[1])])}};
 c.onmouseup=e=>{if(!s)return;const p=at(e);const b=[Math.min(s[0],p[0]),Math.min(s[1],p[1]),Math.max(s[0],p[0]),Math.max(s[1],p[1])].map(Math.round);s=null;
  if(b[2]-b[0]>3&&b[3]-b[1]>3)(boxes[m.vbl]=boxes[m.vbl]||[]).push(b);draw();save()};
 d.querySelector('button').onclick=()=>{(boxes[m.vbl]||[]).pop();draw();save()};}
save();
</script>"""


def cmd_page(name):
    path, h = header(name)
    if not h["marks"]:
        sys.exit(f"{path}: no '# mark:' lines -- press the Shot combo while recording")
    dol = flag(sys.argv, "--dol", os.path.join(REPO, "Gamecube", "WiiSXRX_debug.dol"))
    out, rundir = run_chain(f"marks_{name}_replay", name, h, [m["vbl"] for m in h["marks"]], dol)
    for m in h["marks"]:
        src = frame_at(rundir, m["vbl"])
        dst = os.path.join(out, f"mark_{m['vbl']}.png")
        shutil.copyfile(src, dst)
        m["img"] = os.path.basename(dst)
    page = os.path.join(out, "marks.html")
    with open(page, "w", encoding="utf-8") as f:
        f.write(PAGE % {"name": name, "marks": json.dumps(h["marks"])})
    print(f"open {page}: draw the boxes, Copy marks, save as {os.path.join(out, 'marks.json')}")


def cmd_trace(name):
    path, h = header(name)
    out = os.path.join(REPO, ".runs", f"marks_{name}")
    jpath = flag(sys.argv, "--json", os.path.join(out, "marks.json"))
    if not os.path.isfile(jpath):
        sys.exit(f"{jpath}: no marks yet -- run 'marks.py page {name}', draw, save the JSON there")
    dol = flag(sys.argv, "--dol", os.path.join(REPO, ".runs", "marks_trace.dol"))
    if not os.path.isfile(dol):
        sys.exit(f"{dol}: build one with the trace: wsx.sh build debug all, then copy it there")
    for m in json.load(open(jpath, encoding="utf-8")):
        for k, b in enumerate(m.get("boxes", [])):
            sx, sy = m["w"] / m["img_w"], m["h"] / m["img_h"]   # dump pixels -> PS1 screen
            box = [int(b[0] * sx), int(b[1] * sy), int(b[2] * sx + 0.999), int(b[3] * sy + 0.999)]
            run = f"marks_{name}_{m['vbl']}_{k}"
            extra = (f"# marks.py: box {k} of the mark at vblank {m['vbl']}\n"
                     f"trace {m['vbl']}\ntracebox {box[0]} {box[1]} {box[2]} {box[3]}\n")
            _, rundir = run_chain(run, name, h, [m["vbl"]], dol, extra)
            rep = os.path.join(out, f"mark_{m['vbl']}_{k}.txt")
            res = subprocess.run([PY, os.path.join(HERE, "ptrace_summary.py"), os.path.join(rundir, "ptrace.log")],
                                 capture_output=True, text=True)
            with open(rep, "w", encoding="utf-8") as f:
                f.write(f"mark vblank {m['vbl']}, box {k}: dump {b} -> PS1 screen {box}\n\n" + res.stdout)
                # every primitive of the marked frame in draw order: what a sorting bug needs
                raw = [l.rstrip() for l in open(os.path.join(rundir, "ptrace.log"), encoding="utf-8", errors="replace")
                       if l.startswith("pt: +1 ")]
                f.write("\n--- the marked frame: every primitive in the box, in draw order ---\n" + "\n".join(raw) + "\n")
            print(f"{rep}: {len(res.stdout.splitlines())} lines")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("page", "trace"):
        sys.exit(__doc__)
    (cmd_page if sys.argv[1] == "page" else cmd_trace)(sys.argv[2])


if __name__ == "__main__":
    main()
