"""Bench-Wii queue job: Quit WiiStation and report what happened: whether HBC answers again,
the crash report, the app's last output, a screen before the quit.

    C:/Python312/python.exe C:/tools/wii-bench/wiibench.py add --cwd C:/projects/WiiStation -- \\
        C:/Python312/python.exe scripts/wii_quit_test.py NAME [--dol DOL]
        [--game FOLDER ROM --secs N] [--set K=V ...]

Without --game: the menu's Quit button, pressed by the input script in
sd:/wiistation/autoinput.txt ("menukeys 60 UUA..LA": up, up, A on Quit, then left and A on
OK in its question; --direct: "menupage 90", the same code without the question); the file is
removed at the end. The agent's `key` presses do not reach WiiStation's menu.
With --game: the game boots (wiiload arguments FOLDER ROM), and after --secs an `hbc.py exit`
stops it; WiiStation then leaves as Quit does with a game loaded (SysClose, the fade, exit).
--set adds settings as K=V arguments (loadSettings reads them). Results: .runs/NAME/."""
import os, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wii_targets import bench_wii, existing_file, hbc, in_hbc, parser, wait_in_hbc  # noqa: E402

ap = parser(__doc__)
ap.add_argument("name", help="the run's folder: .runs/NAME")
ap.add_argument("--dol", type=existing_file, default="Gamecube/WiiSXRX_Release.dol",
                help="the DOL (default the release build); its .elf beside it gives source lines")
ap.add_argument("--game", nargs=2, metavar=("FOLDER", "ROM"), help="boot this game, then exit it")
ap.add_argument("--secs", type=int, default=30, help="with --game: seconds before the exit")
ap.add_argument("--set", nargs="*", default=[], metavar="K=V", help="settings, as wiiload arguments")
ap.add_argument("--direct", action="store_true", help="the Quit code without its question")
a = ap.parse_args()
wii = bench_wii("wii_quit_test.py")
dol = os.path.abspath(a.dol)
out = os.path.abspath(os.path.join(".runs", a.name))
os.makedirs(out, exist_ok=True)


def run(args, timeout=120):
    return hbc(wii, *args, timeout=timeout, show=True)


def wait_hbc(secs):
    return wait_in_hbc(wii, secs)


if not in_hbc(wii):                               # an app left running: back to HBC first
    run(["exit"])
    wait_hbc(120)
run(["crash", "--clear"])
if not a.game:
    script = os.path.join(tempfile.mkdtemp(), "autoinput.txt")
    open(script, "w").write("menupage 90\n" if a.direct else "menukeys 60 UUA..LA\n")
    run(["put", script, "sd:/wiistation/autoinput.txt"])
run(["send", dol] + (list(a.game) if a.game else []) + a.set)
t0 = time.monotonic()
if a.game:
    time.sleep(a.secs)
    run(["screen", os.path.join(out, "game.png")], timeout=20)
    run(["exit"])
else:
    time.sleep(2)
    run(["screen", os.path.join(out, "menu.png")], timeout=20)   # before the quit
back = wait_hbc(180)                              # back in HBC, or a crash screen
print(f"back in HBC: {back} after {time.monotonic() - t0:.0f} s", flush=True)
if not back:
    run(["screen", os.path.join(out, "stuck.png")], timeout=20)
r = run(["crash", "--elf", os.path.splitext(dol)[0] + ".elf"])
open(os.path.join(out, "crash.txt"), "w").write(r.text)
r = run(["lastlog"])
open(os.path.join(out, "lastlog.txt"), "w").write(r.text)
if not a.game and back:
    run(["rm", "sd:/wiistation/autoinput.txt"])
elif not back:
    print("NOT back in HBC: the Wii needs RESET" +
          ("" if a.game else "; sd:/wiistation/autoinput.txt is still there"), flush=True)
