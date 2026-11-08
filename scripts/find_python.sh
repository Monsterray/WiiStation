# Source this file: it sets PYTHON, the Python for WiiStation's scripts.
#
# Order: $PYTHON from the environment; else the Python wii_paths.py names ($PYTHON in
# ~/.wii-paths.json, else the Python that runs it), asked through the `py` launcher
# (on PATH even in devkitPro's MSYS2 shell, where plain `python` is not), python3 or
# python; else a bare `python`, then `python3`.
if [ -z "${PYTHON:-}" ]; then
	_fp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	for _fp_py in py python3 python; do
		command -v "$_fp_py" >/dev/null 2>&1 || continue
		PYTHON="$("$_fp_py" "$_fp_dir/wii_paths.py" python 2>/dev/null | tr -d '\r')"
		[ -n "$PYTHON" ] && break
	done
	PYTHON="${PYTHON//\\//}"   # C:\x\python.exe -> C:/x/python.exe: safe to leave unquoted
	if [ -z "$PYTHON" ]; then
		if command -v python >/dev/null 2>&1; then PYTHON=python; else PYTHON=python3; fi
	fi
	unset _fp_dir _fp_py
fi
