#!/usr/bin/env python3
"""Where this PC's tools are. One file per PC, ~/wii-tools.env: NAME=path lines, # comments.

An environment variable of the same name wins over the file. See SETUP.md.

    python scripts/tools.py setup   find the tools in the usual places, write the lines found
    python scripts/tools.py check   every name, its path, whether it exists, which levels work
    python scripts/tools.py NAME    print one path, or exit 1 saying how to set it
    import tools; tools.get("PYTHON")   the same from Python (required=False returns None)

Bash scripts read the file through scripts/tools_env.sh.
"""
import glob
import os
import pathlib
import shutil
import sys

# name: (level, usual places: files, globs and PATH names). Levels: 1 build, 2 Dolphin, 3 real Wii.
NAMES = {
    "DEVKITPRO": ("1", ["C:/devkitPro"]),
    "PYTHON": ("2", ["C:/Python3*/python.exe", "python"]),
    "DOLPHIN": ("2", ["C:/tools/Dolphin*/Dolphin.exe", "C:/Program Files/Dolphin/Dolphin.exe", "Dolphin", "dolphin-emu"]),
    "GAME_LIBRARY": ("2", []),
    "WII_IP": ("3", []),
    "WII_BENCH": ("extra", ["C:/tools/wii-bench/wiibench.py"]),
    "WIICAP": ("extra", ["C:/tools/wii-capture/wiicap.py"]),
    "DOLPHIN_MOVIE": ("extra", []),
    "PADTEST": ("extra", []),
    "PS1_TESTS": ("extra", ["C:/tools/ps1-tests"]),
    "CHDMAN": ("extra", ["C:/Program Files/*/chdman.exe", "chdman"]),
    "FFMPEG": ("extra", ["C:/Program Files*/ffmpeg/bin/ffmpeg.exe", "ffmpeg"]),
    "SEVENZIP": ("extra", ["C:/Program Files/7-Zip/7z.exe", "7z"]),
}
NOT_A_PATH = ("WII_IP",)


def _home():
    try:
        return pathlib.Path.home()
    except RuntimeError:  # devkitPro's MSYS2 login shell clears USERPROFILE
        import ctypes
        buf = ctypes.create_unicode_buffer(1024)
        ctypes.windll.shell32.SHGetFolderPathW(None, 0x28, None, 0, buf)
        return pathlib.Path(buf.value)


def env_file():
    return pathlib.Path(os.environ.get("WII_TOOLS_ENV") or _home() / "wii-tools.env")


def read():
    out = {}
    try:
        lines = env_file().read_text(encoding="utf-8").splitlines()
    except OSError:
        return out
    for line in lines:
        k, eq, v = line.strip().partition("=")
        if eq and not k.startswith("#"):
            out[k.strip()] = v.strip().strip('"')
    return out


def missing_msg(name):
    return f"{name} is not set: run python scripts/tools.py setup (SETUP.md, {_level_name(name)})"


def _level_name(name):
    lv = NAMES[name][0]
    return "extras" if lv == "extra" else f"level {lv}"


def _env(name):
    v = os.environ.get(name, "").strip()
    # a login shell can carry a stale DEVKITPRO=/opt/devkitpro: ignore it when the folder is not there
    return "" if name == "DEVKITPRO" and v and not os.path.isdir(v) else v


def get(name, required=True):
    v = _env(name) or read().get(name, "")
    if not v and required:
        raise SystemExit(missing_msg(name))
    return v or None


def _find(places):
    for p in places:
        hits = sorted(glob.glob(p)) or ([shutil.which(p)] if "/" not in p and shutil.which(p) else [])
        if hits:
            return hits[0].replace("\\", "/")


def _ok(name):
    v = get(name, False)
    return bool(v) and (name in NOT_A_PATH or os.path.exists(v))


def check():
    for n, (lv, _) in NAMES.items():
        v = get(n, False)
        print(f"{n:14} {'ok' if _ok(n) else 'MISSING' if not v else 'NOT FOUND':9} {v or ''}  ({'extra' if lv == 'extra' else 'level ' + lv})")
    print("levels working:", " ".join(str(i) for i in (1, 2, 3)
          if all(_ok(n) for n, (lv, _) in NAMES.items() if lv.isdigit() and int(lv) <= i)) or "none")


def setup():
    have, lines, absent = read(), [], []
    for n, (lv, places) in NAMES.items():
        if n in have:
            continue
        found = _env(n) or _find(places) or (sys.executable.replace("\\", "/") if n == "PYTHON" else None)
        if found:
            lines.append(f'{n}="{found}"')
            print(f"found   {n} = {found}")
        else:
            absent.append(n)
            print(f"missing {n} (SETUP.md, {_level_name(n)})")
    if lines:
        with open(env_file(), "a", encoding="utf-8", newline="\n") as f:
            f.write("".join(l + "\n" for l in lines))
    lib = get("GAME_LIBRARY", False)
    if lib:
        for d in ("bios", "games"):
            os.makedirs(os.path.join(lib, d), exist_ok=True)
    print(f"{env_file()}: {len(lines)} lines added, {len(absent)} still missing; add them by hand as NAME=path")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["setup"]:
        setup()
    elif a == ["check"]:
        check()
    elif len(a) == 1 and a[0] in NAMES:
        v = get(a[0], False)
        sys.exit(print(v) if v else (print(missing_msg(a[0]), file=sys.stderr) or 1))
    else:
        sys.exit(__doc__)
