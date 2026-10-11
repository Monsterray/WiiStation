#!/usr/bin/env bash
# llm_ask.sh "prompt" [--model NAME] [--file PATH]... [--lines N] [--out SLUG] [--timeout S]
#
# One question to a local model on ai-server (ssh + ollama API, temperature 0,
# non-streaming), saving the full Q+A transcript for later review instead of
# flooding the terminal:
#
#   bash scripts/llm_ask.sh "why does interp_exitearly fire here?" \
#       --file Gamecube/GamecubeMain.cpp --out interp-exit
#
# Prints three lines: model, response chars, transcript path. The transcript
# (.runs/llm/<slug>.md) holds prompt, attached files, model, timestamps and the
# full response. --file may repeat; each file is capped at --lines lines
# (default 120) with a truncation notice, so a perf.log cannot eat the context.
# Defaults: model qwen3.5:9b (fast triage; pass --model qwen3-coder:30b for code),
# timeout 300. Needs ai-server ssh (BatchMode, key auth) like every MCP server.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi

case "${1:-}" in -h|--help) sed -n '2,14p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;; esac
prompt="${1:-}"; shift || true
[ -n "$prompt" ] || { sed -n '2,14p' "$0" | sed -e 's/^# \{0,1\}//'; exit 2; }
model="qwen3.5:9b"; lines=120; slug=""; timeout=300; files=()
while [ $# -gt 0 ]; do
	case "$1" in
		--model) model="${2:?missing value}"; shift 2 ;;
		--file) files+=("${2:?missing value}"); shift 2 ;;
		--lines) lines="${2:?missing value}"; shift 2 ;;
		--out) slug="${2:?missing value}"; shift 2 ;;
		--timeout) timeout="${2:?missing value}"; shift 2 ;;
		-h|--help) sed -n '2,14p' "$0" | sed -e 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) echo "unexpected: $1 (the prompt comes first, quoted)"; exit 2 ;;
	esac
done
case "$lines$timeout" in ''|*[!0-9]*) echo "--lines/--timeout must be numbers"; exit 2;; esac
[ -n "$slug" ] || slug="ask-$(date +%Y%m%d-%H%M%S)"
case "$slug" in *..*|*/*) echo "--out: plain slug, no path"; exit 2;; esac
for f in ${files[@]+"${files[@]}"}; do
	[ -f "$f" ] || { echo "llm_ask.sh: no such file $f"; exit 2; }
done
outdir="$REPO/.runs/llm"; mkdir -p "$outdir"
out="$outdir/$slug.md"
# ClearAllForwardings: see llm_court.sh (concurrent ssh sessions fight over the
# ai-server config's local ollama forwards; these calls run curl ON ai-server).
SSH="ssh -o BatchMode=yes -o ClearAllForwardings=yes"
if ! $SSH -o ConnectTimeout=15 ai-server true 2>/dev/null; then
	echo "llm_ask.sh: ai-server unreachable (ssh BatchMode failed)"; exit 1
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
	echo "# llm ask: $slug"
	echo
	echo "- model: \`$model\`  asked: $(date '+%Y-%m-%d %H:%M:%S')"
	echo "- files: ${files[*]:-none}"
	echo
	echo '```'
	printf '%s\n' "$prompt"
	echo '```'
} > "$out"
payload="$("$PYTHON" -c "import json,sys; print(json.dumps({'model': sys.argv[1], 'prompt': sys.stdin.read(), 'stream': False, 'options': {'temperature': 0}}))" "$model" <<< "$full")"
resp="$(printf '%s' "$payload" | $SSH ai-server "curl -s -m $timeout -X POST http://localhost:11434/api/generate -H Content-Type:application/json -d @-")"
err="$("$PYTHON" -c "import json,sys; print(json.load(sys.stdin).get('error',''))" <<< "$resp" 2>/dev/null)"
if [ -n "$err" ]; then
	echo "llm_ask.sh: model error: $err" | tee -a "$out"; exit 1
fi
answer="$("$PYTHON" -c "import json,sys; print(json.load(sys.stdin).get('response',''))" <<< "$resp")"
[ -n "$answer" ] || { echo "llm_ask.sh: empty response (see $out)"; exit 1; }
{
	echo
	echo "## response ($model)"
	echo
	echo "$answer"
} >> "$out"
printf 'model: %s\nchars: %s\ntranscript: %s\n' "$model" "${#answer}" "$out"
