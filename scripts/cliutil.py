"""cliutil.py -- one-line argv hygiene for the reader scripts.

Tracebacks on missing/bad arguments waste everyone's context window: these turn
them into one-line usage errors instead. Import pattern (same as the wii_* tools)::

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from cliutil import need, flag, parse_opts, req_file, req_int

FPS_OVERLAY is the debug-overlay rectangle every dump reader masks, as
(x0, y0, x1, y1) in dump pixels. One constant: frame_compare's 430x80 is the
conservative one (frame_cycle/dot_flicker masked only 200x60 and reported
false diffs on overlay pixels); convert at the use site, never re-derive.
"""
import os
import sys

# Debug overlay (FPS + memory figures): host-timing dependent, never part of
# the picture. (x0, y0, x1, y1) in dump pixels.
FPS_OVERLAY = (0, 0, 430, 80)


def need(argv, n, usage):
    """argv[0] is the program; require n total entries or exit with usage."""
    if len(argv) < n:
        sys.exit("usage: " + usage)


def flag(argv, name, default=None):
    """Value of --name VALUE or --name=VALUE in argv, else default. A present
    but valueless flag is a clean error, not an IndexError."""
    for i, a in enumerate(argv[1:], 1):
        if a == name:
            if i + 1 >= len(argv):
                sys.exit(f"{name}: needs a value")
            return argv[i + 1]
        if a.startswith(name + "="):
            return a[len(name) + 1:]
    return default


def parse_opts(argv, valued=(), bools=()):
    """Split argv[1:] into (positionals, {flag: value|True}). Unknown --flags
    and valueless valued flags are clean errors. Values are taken verbatim
    (even a negative number), so option values can never be mistaken for
    positionals the way substring tests did."""
    pos, opts, i = [], {}, 1
    valued, bools = set(valued), set(bools)
    while i < len(argv):
        a = argv[i]
        if a in valued:
            if i + 1 >= len(argv):
                sys.exit(f"{a}: needs a value")
            opts[a] = argv[i + 1]
            i += 2
        elif a in bools:
            opts[a] = True
            i += 1
        elif a.startswith("--"):
            sys.exit(f"unknown option {a}")
        else:
            pos.append(a)
            i += 1
    return pos, opts


def req_int(s, what):
    """int(s) or a clean error naming what was expected."""
    try:
        return int(s)
    except (TypeError, ValueError):
        sys.exit(f"{what}: needs an integer, got {s!r}")


def req_float(s, what):
    """float(s) or a clean error naming what was expected."""
    try:
        return float(s)
    except (TypeError, ValueError):
        sys.exit(f"{what}: needs a number, got {s!r}")


def req_file(path, what="file"):
    """Clean exit when path is not a file, instead of a traceback mid-read."""
    if not os.path.isfile(path or ""):
        sys.exit(f"{path}: no such {what}")
    return path
