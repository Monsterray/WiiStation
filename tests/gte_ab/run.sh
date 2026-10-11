#!/bin/bash
# Host A/B of two gte.c versions: the same seeded register states and opcodes through each,
# every register read back through MFC2/CFC2, then per command and input kind the share of
# cases that differ, which registers, and ns per command. Cases use the PsyQ SDK encodings
# (sf=1, what games issue) and random encodings (sf/lm/mx/v/cv random) separately.
#
#   bash tests/gte_ab/run.sh [A_GTE_C] [B_GTE_C] [CASES_PER_OP]
#
# A defaults to `git show HEAD:gte.c`, B to the working gte.c. Needs clang (C:/Program Files/LLVM).
# shim/ holds the few libogc/zlib headers psxcommon.h pulls in; gte.c uses none of them.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); T="$R/tests/gte_ab"; O="${TMP:-/tmp}/gte_ab"; mkdir -p "$O"
CC="${CC:-/c/Program Files/LLVM/bin/clang.exe}"
if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi
command -v "$CC" >/dev/null 2>&1 || [ -x "$CC" ] || { echo "run.sh: no C compiler at $CC (set CC=)" >&2; exit 2; }
A="${1:-}"; [ -n "$A" ] || { git -C "$R" show HEAD:gte.c > "$O/gte_a.c" || { echo "run.sh: no HEAD:gte.c (not a git checkout?)" >&2; exit 2; }; A="$O/gte_a.c"; }
B="${2:-$R/gte.c}"; N="${3:-30000}"
case "$N" in ''|*[!0-9]*) echo "run.sh: CASES_PER_OP must be a number, got '$N'" >&2; exit 2;; esac
[ -f "$A" ] || { echo "run.sh: no such A file $A" >&2; exit 2; }
[ -f "$B" ] || { echo "run.sh: no such B file $B" >&2; exit 2; }
F=(-O2 -w -I"$R" -I"$T/shim" -include "$T/shim/gctypes.h")
for s in a:"$A" b:"$B"; do
	"$CC" "${F[@]}" "$T/gte_ab.c" "${s#*:}" "$R/gte_divider.c" -o "$O/gte_${s%%:*}.exe"
	"$O/gte_${s%%:*}.exe" "$O/${s%%:*}.bin" "$N" bench > "$O/${s%%:*}_t.txt"
done
"$PYTHON" "$T/gte_ab_cmp.py" "$O/a.bin" "$O/b.bin" ex
echo; echo "ns per command (game-like states, SDK encodings), host:"
paste "$O/a_t.txt" "$O/b_t.txt" | awk '{p=($3==0?"n/a":sprintf("%+.0f%%",($7/$3-1)*100)); printf "  %-6s A %5s  B %5s  %4s\n",$2,$3,$7,p}'
