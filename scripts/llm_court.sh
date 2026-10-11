#!/usr/bin/env bash
# llm_court.sh "prompt" [--models A,B,C] [--file PATH]... [--lines N] [--out SLUG] [--timeout S]
#
# The same question to several local models at once (ai-server ollama, temperature 0,
# non-streaming), gathered into one transcript for later review:
#
#   bash scripts/llm_court.sh "is this FIFO stall theory sound?" \
#       --file .runs/dbgx/run.log --out fifo-stall
#
# Prints one line per model (chars or error) plus the transcript path; the transcript
# (.runs/llm/<slug>.md) holds prompt, files, and every response in full. A panel is a
# second opinion, not an oracle: disagreements between models are themselves the signal,
# verify disputed points against measurements before acting on any of them.
# Defaults: models qwen3-coder:30b,gemma4:31b,muse-glimmer:30b; 120 lines per file;
# timeout 600 s per model (they run in parallel, so wall time is the slowest).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi

case "${1:-}" in -h|--help) sed -n '2,16p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;; esac
prompt="${1:-}"; shift || true
[ -n "$prompt" ] || { sed -n '2,16p' "$0" | sed -e 's/^# \{0,1\}//'; exit 2; }
models="qwen3-coder:30b,gemma4:31b,muse-glimmer:30b"; lines=120; slug=""; timeout=600; files=()
while [ $# -gt 0 ]; do
	case "$1" in
		--models) models="${2:?missing value}"; shift 2 ;;
		--file) files+=("${2:?missing value}"); shift 2 ;;
		--lines) lines="${2:?missing value}"; shift 2 ;;
		--out) slug="${2:?missing value}"; shift 2 ;;
		--timeout) timeout="${2:?missing value}"; shift 2 ;;
		-h|--help) sed -n '2,16p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) echo "unexpected: $1 (the prompt comes first, quoted)"; exit 2 ;;
	esac
done
case "$lines$timeout" in ''|*[!0-9]*) echo "--lines/--timeout must be numbers"; exit 2;; esac
[ -n "$slug" ] || slug="court-$(date +%Y%m%d-%H%M%S)"
case "$slug" in *..*|*/*) echo "--out: plain slug, no path"; exit 2;; esac
for f in ${files[@]+"${files[@]}"}; do
	[ -f "$f" ] || { echo "llm_court.sh: no such file $f"; exit 2; }
done
outdir="$REPO/.runs/llm"; mkdir -p "$outdir"
out="$outdir/$slug.md"
# ClearAllForwardings: the ai-server ssh config forwards ollama ports locally, and two
# concurrent ssh sessions fight over binding them (second call dies). These calls run
# curl ON ai-server, so local forwards are useless anyway. Same flag as every MCP server.
SSH="ssh -o BatchMode=yes -o ClearAllForwardings=yes"
if ! $SSH -o ConnectTimeout=15 ai-server true 2>/dev/null; then
	echo "llm_court.sh: ai-server unreachable (ssh BatchMode failed)"; exit 1
fi
full="$prompt"
for f in ${files[@]+"${files[@]}"}; do
	body="$(head -n "$lines" "$f")"
	n=$(wc -l < "$f")
	[ "$n" -gt "$lines" ] && body="$body
[... $((n - lines)) further lines truncated (--lines N raises it)]"
	full="$full
--- attached: $f (first $lines lines) ---
$body"
done
{
	echo "# llm court: $slug"
	echo
	echo "- models: \`$(echo "$models" | tr ',' ' ')\`  asked: $(date '+%Y-%m-%d %H:%M:%S')"
	echo "- files: ${files[*]:-none}"
	echo
	echo '```'
	printf '%s\n' "$prompt"
	echo '```'
} > "$out"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/llmcourt.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
trap 'exit 130' INT TERM
printf '%s' "$full" > "$tmp/prompt.txt"
i=0
for m in $(echo "$models" | tr ',' ' '); do
	i=$((i + 1))
	(
	payload="$("$PYTHON" -c "import json,sys; print(json.dumps({'model': sys.argv[1], 'prompt': open(sys.argv[2], encoding='utf-8', errors='replace').read(), 'stream': False, 'options': {'temperature': 0}}))" "$m" "$tmp/prompt.txt")"
	printf '%s' "$payload" | $SSH ai-server "curl -s -m $timeout -X POST http://localhost:11434/api/generate -H Content-Type:application/json -d @-" > "$tmp/resp_$i.json" 2>"$tmp/err_$i.txt" || echo "transport failed" > "$tmp/err_$i.txt"
	echo "$m" > "$tmp/model_$i.txt"
	) &
done
wait
i=0
ok=0
for m in $(echo "$models" | tr ',' ' '); do
	i=$((i + 1))
	mname="$(cat "$tmp/model_$i.txt" 2>/dev/null || echo "$m")"
	# FAILED only when the output is missing or not JSON; ssh writing to stderr alone is not a failure
	if ! "$PYTHON" -c "import json,sys; json.load(open(sys.argv[1]))" "$tmp/resp_$i.json" 2>/dev/null; then
		why="$(head -c 120 "$tmp/err_$i.txt" 2>/dev/null)"
		why="${why:-no output or invalid JSON}"
		{
			echo
			echo "## response ($mname): FAILED"
			echo
			echo "$why"
		} >> "$out"
		echo "$mname: FAILED ($why)"
		continue
	fi
	err="$("$PYTHON" -c "import json,sys; print(json.load(open(sys.argv[1])).get('error',''))" "$tmp/resp_$i.json" 2>/dev/null)"
	answer="$("$PYTHON" -c "import json,sys; print(json.load(open(sys.argv[1])).get('response',''))" "$tmp/resp_$i.json" 2>/dev/null)"
	if [ -n "$err" ] || [ -z "$answer" ]; then
		{
			echo
			echo "## response ($mname): FAILED"
			echo
			echo "${err:-empty response}"
		} >> "$out"
		echo "$mname: FAILED (${err:-empty response})"
		continue
	fi
	{
		echo
		echo "## response ($mname)"
		echo
		echo "$answer"
	} >> "$out"
	echo "$mname: ${#answer} chars"
	ok=$((ok + 1))
done
echo "transcript: $out"
[ "$ok" -gt 0 ] || { echo "llm_court.sh: no model answered"; exit 1; }
