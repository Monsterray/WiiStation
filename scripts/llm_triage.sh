#!/usr/bin/env bash
# llm_triage.sh RUN_DIR ["question"] [--model NAME] [--court] [--out SLUG]
#
# Pilot local-LLM integration: point a model at a finished run that went wrong
# and get a first-pass read, saved for review. Gathers the run's own account
# (run.log tail, perf.log tail, faults grep) plus your question, asks via
# scripts/llm_ask.sh (or --court for a panel), and prints the transcript path.
# Explicit opt-in only: nothing calls this automatically, so no run pays GPU
# time unless a human (or agent, deliberately) asks.
#
#   bash scripts/llm_triage.sh .runs/padtest_dx "why 0/12 with no vram files?"
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
case "${1:-}" in -h|--help) sed -n '2,10p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;; esac
dir="${1:-}"; shift || true
[ -n "$dir" ] && [ -d "$dir" ] || { sed -n '2,10p' "$0" | sed -e 's/^# \{0,1\}//'; exit 2; }
q=""
case "${1:-}" in ""|-*) ;; *) q="$1"; shift ;; esac
court=""; model="qwen3.5:9b"; slug=""
while [ $# -gt 0 ]; do
	case "$1" in
		--model) model="$2"; shift 2 ;;
		--court) court=1; shift ;;
		--out) slug="$2"; shift 2 ;;
		-h|--help) sed -n '2,10p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;;
		*) q="$q $1"; shift ;;
	esac
done
[ -n "$slug" ] || slug="triage-$(basename "$dir")-$(date +%Y%m%d-%H%M%S)"
[ -n "$q" ] || q="this run failed or looks wrong; say what the logs show and what to check next"
pack="$REPO/.runs/.triage-pack.txt"
{
	echo "=== run.log (tail 40) ==="
	tail -n 40 "$dir/run.log" 2>/dev/null || echo "(no run.log)"
	echo "=== perf.log (tail 15) ==="
	tail -n 15 "$dir/perf.log" 2>/dev/null || echo "(no perf.log)"
	echo "=== dolphin faults/panics ==="
	grep -a -E "faults in dolphin.log: [1-9]|PANIC|Unknown instruction|GFX FIFO|unknown opcode" \
		"$dir/run.log" "$dir/dolphin.log" 2>/dev/null | head -n 10 || true
	echo "(fault grep done)"
} > "$pack" 2>/dev/null
if [ -n "$court" ]; then
	bash "$REPO/scripts/llm_court.sh" "$q" --file "$pack" --out "$slug"
else
	bash "$REPO/scripts/llm_ask.sh" "$q" --model "$model" --file "$pack" --out "$slug"
fi
