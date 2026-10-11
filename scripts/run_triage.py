#!/usr/bin/env python3
"""run_triage.py -- condense one run's logs (or a compare table) with the local LLM, for a reader
with a small context. Calls the local-llm server (llm_condense_log / llm_summarize) through
scripts/llm.py; only the log tails below leave this machine, nothing else.

  run_triage.py RUN_DIR [QUESTION ...]
        always: the verdict and evidence in <= 15 lines, saved to RUN_DIR/triage.txt and printed.
        (wsx.sh triage NAME [question])
  run_triage.py --auto RUN_DIR [--rc N]
        after a run: only when something looks wrong (non-zero exit, a crash/fatal/fault/hang
        line, a game with no perf block, or a dump that is almost all silence).
        Prints a "triage:" block of <= 10 lines. Never fails: an LLM error prints
        "triage unavailable" and exits 0. WSX_NO_LLM=1 turns it off. The exit code comes from
        --rc, else from RUN_DIR/dolphin.rc (wsx.sh run writes it).
  run_triage.py --explain TABLE_FILE RUN_DIR [RUN_DIR ...]
        the reading under a wsx compare table: <= 8 lines on what changed and whether it is beyond
        noise. The table text is read from TABLE_FILE; each RUN_DIR adds its run.info.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from chain_table import games, row  # noqa: E402
from run_summary import read_wav  # noqa: E402

LLM = [sys.executable, os.path.join(HERE, "llm.py")]
# the tails sent to the server: (file, lines kept)
TAILS = [("run.log", 60), ("dolphin.log", 60), ("tty.log", 60), ("perf.log", 40),
         ("lab.log", 30), ("lastlog.txt", 40), ("crash.txt", 80)]
# case-sensitive on purpose: "Crash Bash" and "CRASH BANDICOOT" are game names, not crashes
HIT = re.compile(r"\b(fatal|FATAL|panic|PANIC|hang|hung|crash|CRASH|crashed|DSI|ISI|fault|FAULT|"
                 r"Unknown instruction|GFX FIFO|refusing|no results after)\b"
                 r"|faults in dolphin\.log: [1-9]")
MAX_PACK = 50000
LINE_CAP = 240
TIMEOUT_AUTO = 240   # one call measured 60-80 s
TIMEOUT_MANUAL = 300

FOCUS_TRIAGE = ("Condense these logs of one emulator run for the developer who will read them. "
                "Reply with at most 15 short plain lines, no markdown, no headers, no emojis. "
                "Line 1: the verdict (ok, or what failed first; the first real failure, not the noise after it). "
                "Then the evidence: short quoted fragments, each with its log file name, skipping repeats. "
                "Last line: the one thing to check next. Use only the text given.")
FOCUS_EXPLAIN = ("Below is a table comparing two runs, A and B, column by column (B - A is the change), "
                 "and each run's run.info. Reply with at most 8 short plain lines, no markdown, no headers, "
                 "no emojis: what changed, which columns moved most, and whether each change is beyond noise. "
                 "Under Dolphin two runs of the same build agree within about 0.1 percent of wall, so a larger "
                 "move is real. For hardware runs the noise level is not known from this table: say so. "
                 "Use only the numbers given.")


def short(e, n=160):
    return " ".join(str(e).split())[:n] or type(e).__name__


def tail_lines(path, n):
    buf = collections.deque(maxlen=n)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            buf.append(line.rstrip("\r\n")[:LINE_CAP])
    return list(buf)


def read_rc(d):
    p = os.path.join(d, "dolphin.rc")
    try:
        return int(open(p).read().strip())
    except (OSError, ValueError):
        return None


def triggers(d, rc):
    """Why this run looks wrong: a list of short reasons, empty when it does not."""
    out = []
    if rc not in (None, 0):
        out.append(f"exit code {rc}")
    for name in ("run.log", "dolphin.log", "tty.log", "lab.log", "perf.log", "lastlog.txt", "crash.txt"):
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            continue
        hits, first = 0, None
        with open(p, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f, 1):
                if HIT.search(line):
                    hits += 1
                    if first is None:
                        first = f"{name}:{i}: {line.strip()[:110]}"
        if first:
            out.append(f"{first} ({hits} such lines)")
    perf = os.path.join(d, "perf.log")
    if not os.path.isfile(perf):
        out.append("no perf.log")
    else:
        text = open(perf, encoding="utf-8", errors="replace").read()
        if "=== chain" in text:
            gs = games(perf)
            totals = re.findall(r"=== chain \d+/(\d+) end", text)
            total = int(totals[-1]) if totals else len(gs)
            if len(gs) < total:
                out.append(f"chain: {len(gs)} of {total} games have an end line")
            for n, rom, vbl, b in gs:
                if not row(b, vbl):
                    out.append(f"no perf block for game {n} ({rom})")
                    break
        elif not re.search(r"^wall:", text, re.M):
            out.append("no perf block (no wall: line in perf.log)")
    try:
        wv = read_wav(d, 3.0, 400.0)
    except Exception:
        wv = None
    if wv and "error" not in wv and wv["dur_s"] >= 10:
        silent = (wv["zero_ms"] / 1000.0 + wv["long_s"]) / wv["dur_s"]
        if silent >= 0.95:
            out.append(f"audio: {100 * silent:.0f}% of the {wv['dur_s']:.0f} s dump is exact silence")
    return out


def run_pack(d, trig):
    parts = ["=== triggers (why this was looked at) ===\n" + ("\n".join(trig) or "(none: asked by hand)")]
    ip = os.path.join(d, "run.info")
    if os.path.isfile(ip):
        parts.append("=== run.info ===\n" + "\n".join(tail_lines(ip, 12)))
    for name, n in TAILS:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            parts.append(f"=== {name} (last {n} lines) ===\n" + "\n".join(tail_lines(p, n)))
    return "\n\n".join(parts)[-MAX_PACK:]


def call_llm(tool, key, text, args, timeout):
    """One call through scripts/llm.py; returns the response's content string or raises."""
    fd, path = tempfile.mkstemp(prefix="wsx_triage_", suffix=".txt")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        cmd = LLM + [tool, "--file", f"{key}={path}", "--field", "content", "--timeout", str(timeout)]
        for k, v in args.items():
            cmd += ["--arg", f"{k}={v}"]
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)   # TimeoutExpired -> the caller's error path
    finally:
        os.unlink(path)
    if p.returncode != 0:
        raise RuntimeError(short(p.stderr.decode("utf-8", "replace")) or f"llm.py exit {p.returncode}")
    out = p.stdout.decode("utf-8", "replace").strip()
    if not out:
        raise RuntimeError("empty answer")
    return out


def plain(text, n):
    return [l.rstrip() for l in text.splitlines() if l.strip()][:n]


def save(d, lines):
    with open(os.path.join(d, "triage.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def cmd_manual(d, question):
    trig = triggers(d, read_rc(d))
    focus = FOCUS_TRIAGE + (" Question from the developer: " + question if question else "")
    text = call_llm("llm_condense_log", "log_text", run_pack(d, trig),
                    {"focus": focus, "extract_issues": "false"}, TIMEOUT_MANUAL)
    lines = plain(text, 15)
    save(d, lines)
    print("\n".join(lines))
    return 0


def cmd_auto(d, rc):
    stale = os.path.join(d, "triage.txt")
    if os.path.isfile(stale):
        os.unlink(stale)                     # a triage of an earlier run must not outlive this one
    if os.environ.get("WSX_NO_LLM") or not os.path.isdir(d):
        return 0
    trig = triggers(d, read_rc(d) if rc is None else rc)
    if not trig:
        return 0
    head = trig[0] + (f" (+{len(trig) - 1} more)" if len(trig) > 1 else "")
    print("triage: " + short(head, 150))
    try:
        text = call_llm("llm_condense_log", "log_text", run_pack(d, trig),
                        {"focus": FOCUS_TRIAGE, "extract_issues": "false"}, TIMEOUT_AUTO)
    except Exception as e:
        print(f"triage unavailable ({short(e, 100)})")
        return 0
    lines = plain(text, 15)
    save(d, lines)
    for l in lines[:9]:
        print(l)
    return 0


def cmd_explain(table_file, dirs):
    with open(table_file, encoding="utf-8", errors="replace") as f:
        table = f.read()
    parts = ["=== compare table (A, B, B - A per game and column) ===\n" + table]
    for d in dirs:
        ip = os.path.join(d, "run.info")
        info = "\n".join(tail_lines(ip, 10)) if os.path.isfile(ip) else "(no run.info)"
        parts.append(f"=== run.info {os.path.basename(d)} ===\n{info}")
    try:
        text = call_llm("llm_summarize", "text", "\n\n".join(parts)[-MAX_PACK:],
                        {"target_words": "120", "focus": FOCUS_EXPLAIN}, TIMEOUT_MANUAL)
    except Exception as e:
        print(f"explain unavailable ({short(e, 100)})")
        return 1
    print("explain:")
    for l in plain(text, 8):
        print(l)
    return 0


def main(argv):
    sys.stdout.reconfigure(errors="replace")
    sys.stderr.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", nargs="?")
    ap.add_argument("question", nargs="*")
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--rc", type=int, default=None)
    ap.add_argument("--explain", metavar="TABLE_FILE")
    a = ap.parse_args(argv)
    if a.explain:
        dirs = [os.path.abspath(x) for x in [a.run_dir] + a.question if x]
        return cmd_explain(a.explain, dirs)
    if not a.run_dir:
        ap.print_usage(sys.stderr)
        return 2
    d = os.path.abspath(a.run_dir)
    if a.auto:
        return cmd_auto(d, a.rc)
    if not os.path.isdir(d):
        sys.stderr.write(f"run_triage.py: no run directory {d}\n")
        return 2
    try:
        return cmd_manual(d, " ".join(a.question).strip())
    except Exception as e:
        print(f"triage unavailable ({short(e, 200)})")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
