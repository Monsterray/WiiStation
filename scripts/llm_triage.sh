#!/usr/bin/env bash
# llm_triage.sh RUN_DIR ["question"] [--model NAME] [--court] [--out SLUG]
#
# First-pass read of a finished run that went wrong: gathers the run's own account
# (run.log tail, or tty.log tail when there is no run.log; perf.log tail; faults grep)
# and asks the local-llm MCP server about it, saved for review. The default path is the
# server's llm_condense_log with your question as its focus (errors/warnings that matter
# plus a narrative); --court sends the same pack to a model panel via llm_court.sh.
# Explicit opt-in only: nothing calls this automatically.
#
#   bash scripts/llm_triage.sh .runs/padtest_dx "why 0/12 with no vram files?"
#
# Prints the model, response chars and transcript path (.runs/llm/<slug>.md).
# The pack is ai-server-side text only: nothing else from the run is sent.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi
LLM="$PYTHON $REPO/scripts/llm.py"

usage() { sed -n '2,15p' "$0" | sed -e 's/^# \{0,1\}//'; }
case "${1:-}" in -h|--help) usage; exit 0 ;; esac
dir="${1:-}"; shift || true
[ -n "$dir" ] && [ -d "$dir" ] || { usage; exit 2; }
q=""
case "${1:-}" in ""|-*) ;; *) q="$1"; shift ;; esac
court=""; model=""; slug=""
while [ $# -gt 0 ]; do
	case "$1" in
		--model) model="${2:?missing value}"; shift 2 ;;
		--court) court=1; shift ;;
		--out) slug="${2:?missing value}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) q="$q $1"; shift ;;
	esac
done
[ -n "$slug" ] || slug="triage-$(basename "$dir")-$(date +%Y%m%d-%H%M%S)"
case "$slug" in *..*|*/*) echo "--out: plain slug, no path"; exit 2;; esac
[ -n "$q" ] || q="this run failed or looks wrong; say what the logs show and what to check next"
mkdir -p "$REPO/.runs"
pack="$REPO/.runs/.triage-pack.txt"
{
	if [ -f "$dir/run.log" ]; then
		echo "=== run.log (tail 40) ==="
		tail -n 40 "$dir/run.log"
	else
		echo "=== tty.log (tail 40; no run.log) ==="
		tail -n 40 "$dir/tty.log" 2>/dev/null || echo "(no tty.log)"
	fi
	echo "=== perf.log (tail 15) ==="
	tail -n 15 "$dir/perf.log" 2>/dev/null || echo "(no perf.log)"
	echo "=== dolphin faults/panics ==="
	grep -a -E "faults in dolphin.log: [1-9]|PANIC|Unknown instruction|GFX FIFO|unknown opcode" \
		"$dir/run.log" "$dir/dolphin.log" 2>/dev/null | head -n 10 || true
	echo "(fault grep done)"
} > "$pack" 2>/dev/null
q="$(echo "$q" | sed -e 's/^ *//')"

if [ -n "$court" ]; then
	court_args=("$q" --file "$pack" --out "$slug")
	bash "$REPO/scripts/llm_court.sh" "${court_args[@]}"
	exit $?
fi

out="$REPO/.runs/llm/$slug.md"
mkdir -p "$REPO/.runs/llm"
args=(llm_condense_log --file "log_text=$pack" --arg "focus=$q" --timeout 900)
[ -n "$model" ] && args+=(--arg "model=$model")
if ! resp="$($LLM "${args[@]}" 2>"$pack.err")"; then
	echo "llm_triage.sh: tool error: $(tr -s ' \n' '  ' < "$pack.err") $resp" >&2
	rm -f "$pack.err"; exit 1
fi
rm -f "$pack.err"
printf '%s' "$resp" > "$pack.json"
$PYTHON - "$pack.json" "$out" "$slug" "$dir" "$q" <<'PYEOF'
import json, os, sys, time
d = json.load(open(sys.argv[1], encoding="utf-8"))
out, slug, run_dir, q = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
answer = (d.get("content") or "").strip()
with open(out, "w", encoding="utf-8") as f:
    f.write("# llm triage: %s\n\n- run: `%s`\n- question: %s\n- asked: %s\n\n" % (
        slug, run_dir, q, time.strftime("%Y-%m-%d %H:%M:%S")))
    f.write("## response (%s)\n\n%s\n" % (d.get("model", "?"), answer))
    issues = d.get("issues") or []
    if issues:
        f.write("\n## issues flagged\n\n")
        for i in issues:
            f.write("- [%s] line %s: %s\n" % (i.get("severity", "?"), i.get("line_number", "?"),
                                              i.get("line_text", "")))
print("model: %s" % d.get("model", "?"))
print("chars: %d" % len(answer))
print("issues: %d" % len(d.get("issues") or []))
print("transcript: %s" % out)
if d.get("truncated"):
    print("warning: response hit max_tokens (truncated)")
sys.exit(0 if answer else 1)
PYEOF
rc=$?
rm -f "$pack.json"
exit "$rc"
