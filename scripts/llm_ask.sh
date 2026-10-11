#!/usr/bin/env bash
# llm_ask.sh "prompt" [--model NAME] [--file PATH]... [--out SLUG] [--timeout S] [--think]
#
# One question to a local model on ai-server, through the local-llm MCP server's
# llm_complete tool (scripts/llm.py), saving the full Q+A transcript for later review:
#
#   bash scripts/llm_ask.sh "why does interp_exitearly fire here?" \
#       --file Gamecube/GamecubeMain.cpp --out interp-exit
#
# Prints three lines: model, response chars, transcript path. The transcript
# (.runs/llm/<slug>.md) holds prompt, attached files, model, timestamps and the
# full response. --file may repeat; each file is attached IN FULL (no line cap).
# Defaults: model qwen3.5:9b (fast; pass --model NAME for another, e.g. qwen3-coder:30b
# for code), reasoning off for short answers (--think turns it on), max 4096 tokens,
# timeout 900 s. --lines is accepted for old callers and ignored.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi
LLM="$PYTHON $REPO/scripts/llm.py"

usage() { sed -n '2,15p' "$0" | sed -e 's/^# \{0,1\}//'; }
case "${1:-}" in -h|--help) usage; exit 0 ;; esac
prompt="${1:-}"; shift || true
[ -n "$prompt" ] || { usage; exit 2; }
model="qwen3.5:9b"; slug=""; timeout=900; think=false; files=(); max_tokens=4096
while [ $# -gt 0 ]; do
	case "$1" in
		--model) model="${2:?missing value}"; shift 2 ;;
		--file) files+=("${2:?missing value}"); shift 2 ;;
		--lines) echo "llm_ask.sh: --lines is ignored (files are sent whole)" >&2; shift 2 ;;
		--out) slug="${2:?missing value}"; shift 2 ;;
		--timeout) timeout="${2:?missing value}"; shift 2 ;;
		--think) think=true; max_tokens=8192; shift ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) echo "unexpected: $1 (the prompt comes first, quoted)"; exit 2 ;;
	esac
done
case "$timeout" in ''|*[!0-9]*) echo "--timeout must be a number"; exit 2;; esac
[ -n "$slug" ] || slug="ask-$(date +%Y%m%d-%H%M%S)"
case "$slug" in *..*|*/*) echo "--out: plain slug, no path"; exit 2;; esac
for f in ${files[@]+"${files[@]}"}; do
	[ -f "$f" ] || { echo "llm_ask.sh: no such file $f"; exit 2; }
done
outdir="$REPO/.runs/llm"; mkdir -p "$outdir"
out="$outdir/$slug.md"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/llmask.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

{
	printf '%s\n' "$prompt"
	for f in ${files[@]+"${files[@]}"}; do
		printf '\n--- attached: %s ---\n' "$f"
		cat "$f"
		printf '\n'
	done
} > "$tmp/prompt.txt"

{
	echo "# llm ask: $slug"
	echo
	echo "- model: \`$model\`  thinking: $think  asked: $(date '+%Y-%m-%d %H:%M:%S')"
	echo "- files: ${files[*]:-none}"
	echo
	echo '```'
	printf '%s\n' "$prompt"
	echo '```'
} > "$out"

args=(llm_complete --file "prompt=$tmp/prompt.txt" --arg "model=$model"
	--arg "max_tokens=$max_tokens" --arg "enable_thinking=$think" --timeout "$timeout")
if ! resp="$($LLM "${args[@]}" 2>"$tmp/err")"; then
	msg="$(cat "$tmp/err") $resp"
	echo "llm_ask.sh: model error: $(echo "$msg" | tr -s ' \n' '  ')" | tee -a "$out"
	exit 1
fi
jget() { printf '%s' "$resp" | $PYTHON -c "import json,sys; v=json.load(sys.stdin).get(sys.argv[1]); print(v if isinstance(v,str) else json.dumps(v))" "$1"; }
answer="$(jget content)"
used="$(jget model)"
truncated="$(jget truncated)"
[ -n "$answer" ] || { echo "llm_ask.sh: empty response (see $out)" | tee -a "$out"; exit 1; }
{
	echo
	echo "## response ($used)"
	echo
	printf '%s\n' "$answer"
} >> "$out"
printf 'model: %s\nchars: %s\ntranscript: %s\n' "$used" "${#answer}" "$out"
[ "$truncated" = "true" ] && echo "warning: response hit max_tokens (truncated); raise --think or split the question"
exit 0
