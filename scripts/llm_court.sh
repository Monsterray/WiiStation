#!/usr/bin/env bash
# llm_court.sh "prompt" [--models A,B,C] [--file PATH]... [--out SLUG] [--timeout S]
#
# The same question to several local models at once, through the local-llm MCP server's
# llm_court tool (scripts/llm.py). Gathered into one transcript for later review:
#
#   bash scripts/llm_court.sh "is this FIFO stall theory sound?" \
#       --file .runs/dbgx/run.log --out fifo-stall
#
# Prints one line per model (chars or FAILED) plus the transcript path; the transcript
# (.runs/llm/<slug>.md) holds prompt, files, and every response in full. A panel is a
# second opinion, not an oracle: disagreements between models are themselves the signal,
# verify disputed points against measurements before acting on any of them.
# --models omitted: the server picks the panel (families, scored from the benchmark data;
# it may shrink while a benchmark is running). Files are attached whole (no line cap).
# --lines is accepted for old callers and ignored. Timeout 600 s per model.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi
LLM="$PYTHON $REPO/scripts/llm.py"

usage() { sed -n '2,17p' "$0" | sed -e 's/^# \{0,1\}//'; }
case "${1:-}" in -h|--help) usage; exit 0 ;; esac
prompt="${1:-}"; shift || true
[ -n "$prompt" ] || { usage; exit 2; }
models=""; slug=""; timeout=600; files=()
while [ $# -gt 0 ]; do
	case "$1" in
		--models) models="${2:?missing value}"; shift 2 ;;
		--file) files+=("${2:?missing value}"); shift 2 ;;
		--lines) echo "llm_court.sh: --lines is ignored (files are sent whole)" >&2; shift 2 ;;
		--out) slug="${2:?missing value}"; shift 2 ;;
		--timeout) timeout="${2:?missing value}"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) echo "unexpected: $1 (the prompt comes first, quoted)"; exit 2 ;;
	esac
done
case "$timeout" in ''|*[!0-9]*) echo "--timeout must be a number"; exit 2;; esac
[ -n "$slug" ] || slug="court-$(date +%Y%m%d-%H%M%S)"
case "$slug" in *..*|*/*) echo "--out: plain slug, no path"; exit 2;; esac
for f in ${files[@]+"${files[@]}"}; do
	[ -f "$f" ] || { echo "llm_court.sh: no such file $f"; exit 2; }
done
outdir="$REPO/.runs/llm"; mkdir -p "$outdir"
out="$outdir/$slug.md"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/llmcourt.XXXXXX")"
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
	echo "# llm court: $slug"
	echo
	echo "- models: \`${models:-server panel}\`  asked: $(date '+%Y-%m-%d %H:%M:%S')"
	echo "- files: ${files[*]:-none}"
	echo
	echo '```'
	printf '%s\n' "$prompt"
	echo '```'
} > "$out"

args=(llm_court --file "prompt=$tmp/prompt.txt" --arg "timeout_s=$timeout" --timeout $((timeout * 4)))
[ -n "$models" ] && args+=(--arg "models=$models")
if ! resp="$($LLM "${args[@]}" 2>"$tmp/err")"; then
	msg="$(cat "$tmp/err") $resp"
	echo "llm_court.sh: tool error: $(echo "$msg" | tr -s ' \n' '  ')" | tee -a "$out"
	exit 1
fi
printf '%s' "$resp" > "$tmp/resp.json"
# one line per opinion; answers appended to the transcript in full
$PYTHON - "$tmp/resp.json" "$out" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
out = sys.argv[2]
ok = 0
lines = []
with open(out, "a", encoding="utf-8") as f:
    for op in d.get("opinions", []):
        name = op.get("model", "?")
        if op.get("status") == "ok" and (op.get("answer") or "").strip():
            ok += 1
            f.write("\n## response (%s)\n\n%s\n" % (name, op["answer"].strip()))
            lines.append("%s: %d chars" % (name, len(op["answer"])))
        else:
            why = op.get("detail") or op.get("status") or "no answer"
            f.write("\n## response (%s): FAILED\n\n%s\n" % (name, why))
            lines.append("%s: FAILED (%s)" % (name, why))
    for s in d.get("skipped", []):
        lines.append("skipped: %s" % (s if isinstance(s, str) else json.dumps(s)))
print("\n".join(lines))
print("transcript: " + out)
sys.exit(0 if ok else 3)
PYEOF
rc=$?
[ "$rc" -eq 0 ] || echo "llm_court.sh: no model answered"
exit "$rc"
