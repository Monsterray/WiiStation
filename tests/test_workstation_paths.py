"""No paths of one workstation: no tracked text file names a project, tool or home
folder of one PC (C:/projects/..., /c/tools/..., /mnt/c/..., a C:/Python3xx), so
the project works on anyone's workstation. Tools are found through
~/wii-tools.env (scripts/tools.py) instead. Saved logs and history keep the paths they were recorded
with: list their folders in EXEMPT.

Tracked scripts (scripts/ and tests/, .py .sh .ps1 .bat) are held to more: no absolute
drive or MSYS path at all (C:/x, /c/x, Program Files), no ../sibling-repo path (a ../ name that
is not a folder of this repository), no LAN address (192.168.x.y). Allowed there: the search
locations in tools.py, the installer's default folder in setup-devkitpro-windows.sh, the
addresses in wii_targets.py.

Allowed: the bench queue's standard folder on Windows (C:/tools/wii-bench, as
devkitPro's is C:/devkitPro), and placeholders such as C:/projects/... or
C:\\Users\\<user name>.

This file comes from the wii-bench kit (template/tests/test_workstation_paths.py).
Fix it there and copy it back; only EXEMPT is the project's own.

Run as a script, it lists every finding: python tests/test_workstation_paths.py
"""

import fnmatch
import pathlib
import re
import shutil
import subprocess
import unittest

# Files kept as a record, with the paths they were made with. fnmatch patterns
# against the path from the repository root; "*" crosses folders.
EXEMPT = (
    "baselines/*",
    "scripts/autoinput/*",   # recorded play files; headers name the machine they were made on
    "scripts/runfiles/*",
)

_FOLDER = r"(?:projects|tools|users|ai)[\\/]+(?!wii-bench\b)[\w-]"
_PATTERN = re.compile(
    r"\b[a-z]:[\\/]+" + _FOLDER                          # C:/projects/x, C:\Users\name
    + r"|/mnt/[a-z]/" + _FOLDER                          # /mnt/c/projects/x (WSL)
    + r"|(?<![\w.~/-])/[a-z]/" + _FOLDER                 # /c/tools/x (MSYS2, Git Bash)
    + r"|(?:\b[a-z]:|(?<![\w.~/-])/[a-z])[\\/]+python3\d+\b",  # C:/Python312
    re.IGNORECASE)

# Checks for tracked scripts only. Per-file allowances: path -> the checks it is exempt from.
SCRIPT_EXT = (".py", ".sh", ".ps1", ".bat")
SCRIPT_DIRS = ("scripts/", "tests/")
SCRIPT_ALLOW = {
    "scripts/tools.py": ("absolute",),                      # the usual install places it searches
    "scripts/setup-devkitpro-windows.sh": ("absolute",),    # devkitPro's installer default folder
    "scripts/wii_targets.py": ("address",),                 # the Wiis and the protected-Wii guard
    "tests/bench_gameplay_test.py": ("sibling",),           # ../ in test data for path handling
    "tests/cover_pack_test.py": ("sibling",),
}
_SCRIPT_CHECKS = {
    "absolute": re.compile(r"(?<![\w/.~-])(?:[a-z]:[\\/]+(?=[\w.$])|/[a-z]/(?=[\w.$]))|Program Files", re.IGNORECASE),
    "address": re.compile(r"\b192\.168\.\d+\.\d+"),
}
_SIBLING = re.compile(r"(?<![\w.])\.\.[\\/]([\w-]+)")   # ../name: ../.. and ../.runs do not match

here = pathlib.Path(__file__).resolve().parent
# This test, in any project: its own pattern and examples would match.
SELF = ("test_workstation_paths.py", "workstation_paths_test.py")


def repo_root():
    if not shutil.which("git"):
        return None
    r = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=here,
                       capture_output=True, text=True)
    return pathlib.Path(r.stdout.strip()) if r.returncode == 0 else None


root = repo_root()


def findings():
    """(path, line number, line) for every match outside EXEMPT."""
    out = subprocess.run(["git", "-c", f"safe.directory={root.as_posix()}", "ls-files", "-z"],
                         cwd=root, capture_output=True, check=True).stdout
    for name in filter(None, out.decode("utf-8", "replace").split("\0")):
        if any(fnmatch.fnmatch(name, p) for p in EXEMPT) or name.rsplit("/", 1)[-1] in SELF:
            continue
        try:
            data = (root / name).read_bytes()
        except OSError:  # a file deleted but not yet staged
            continue
        if b"\0" in data[:8192]:
            continue
        for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            if _PATTERN.search(line):
                yield name, n, line.strip()


def script_findings():
    """(path, line number, check, line) for every script line that names a path or address of one PC."""
    out = subprocess.run(["git", "-c", f"safe.directory={root.as_posix()}", "ls-files", "-z"],
                         cwd=root, capture_output=True, check=True).stdout
    names = [n for n in out.decode("utf-8", "replace").split("\0") if n]
    own = {n.split("/")[0] for n in names}   # folders and files of this repository: ../scripts is fine
    for name in names:
        if (not name.startswith(SCRIPT_DIRS) or not name.endswith(SCRIPT_EXT)
                or name.rsplit("/", 1)[-1] in SELF or any(fnmatch.fnmatch(name, p) for p in EXEMPT)):
            continue
        try:
            text = (root / name).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for check, rx in _SCRIPT_CHECKS.items():
                if check not in SCRIPT_ALLOW.get(name, ()) and rx.search(line):
                    yield name, n, check, line.strip()
            if "sibling" not in SCRIPT_ALLOW.get(name, ()) and any(m.group(1) not in own for m in _SIBLING.finditer(line)):
                yield name, n, "sibling", line.strip()


@unittest.skipUnless(root, "needs a git checkout")
class WorkstationPaths(unittest.TestCase):
    def test_no_workstation_paths(self):
        bad = [f"{p}:{n}: {line[:160]}" for p, n, line in findings()]
        self.assertEqual(bad, [], "paths of one workstation; find the tool through ~/wii-tools.env, "
                                  "or add a folder of saved logs to EXEMPT")

    def test_scripts_name_no_pc_paths_or_addresses(self):
        bad = [f"{p}:{n}: [{c}] {line[:140]}" for p, n, c, line in script_findings()]
        self.assertEqual(bad, [], "a script names an absolute path, a ../sibling repo or a LAN address; "
                                  "use tools.get(NAME) / ~/wii-tools.env (scripts/tools.py), or deps/")

    def test_script_patterns(self):
        hit = lambda c, s: bool(_SCRIPT_CHECKS[c].search(s))
        for s in ("C:/devkitPro", "c:\\x", "/c/devkitPro/tools", "C:\\Program Files\\x", "D:/emu", '"/c/Python312"'):
            self.assertTrue(hit("absolute", s), s)
        for s in ("sd:/wiistation", "http://x.org/a/b", "s/a/b/", "a/c/tools", "~/c/x", "$HOME/c/x", "C:/", "r'[A-Z]:[\\/]'"):
            self.assertFalse(hit("absolute", s), s)
        self.assertTrue(hit("address", "ip = '192.168.8.200'"))
        self.assertFalse(hit("address", "10.0.0.1"))
        self.assertTrue(_SIBLING.search("../hbc-reborn/tools/hbc.py"))
        self.assertFalse(_SIBLING.search("$here/../.runs"))

    def test_pattern(self):
        for s in ("C:/projects/hbc-reborn", "c:\\tools\\Dolphin-x64", "/c/tools/Dolphin-x64",
                  "/mnt/c/projects/Wii64", "C:/Python312/python.exe", "/c/Python312/python",
                  "C:\\Users\\Someone\\AppData", "C:/ai/venvs/x"):
            self.assertRegex(s, _PATTERN)
        for s in ("C:/tools/wii-bench/wiibench.py", "/c/tools/wii-bench", "C:/projects/...",
                  "C:\\Users\\<user name>", "mounts `C:\\Users` on", "C:/devkitPro/msys2",
                  "/opt/devkitpro/tools/bin", "~/c/tools/x", "a/c/tools/x", "https://x.org/c/tools/y"):
            self.assertNotRegex(s, _PATTERN)


if __name__ == "__main__":
    if root is None:
        raise SystemExit("needs a git checkout")
    found = list(findings())
    for p, n, line in found:
        print(f"{p}:{n}: {line[:160]}")
    for p, n, c, line in script_findings():
        found.append(p)
        print(f"{p}:{n}: [{c}] {line[:140]}")
    raise SystemExit(1 if found else 0)
