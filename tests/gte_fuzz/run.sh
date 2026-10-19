#!/bin/bash
# gte.c against a real PS1: the gte-fuzz log from C:/tools/ps1-tests/tests.zip, through the
# interpreter's transfers (i) and Lightrec's (l). bash tests/gte_fuzz/run.sh [LOG] [i|l]
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O="${TMP:-/tmp}/gte_fuzz"; mkdir -p "$O"
CC="${CC:-/c/Program Files/LLVM/bin/clang.exe}"
LOG="${1:-$O/gte_valid_0xc0ffee_50.log}"
[ -f "$LOG" ] || unzip -o -q -j /c/tools/ps1-tests/tests.zip gte-fuzz/gte_valid_0xc0ffee_50.log -d "$O"
"$CC" -O1 -w -I"$R" -I"$R/tests/gte_ab/shim" -include "$R/tests/gte_ab/shim/gctypes.h" \
	"$R/tests/gte_fuzz/gte_fuzz.c" "$R/gte.c" "$R/gte_divider.c" -o "$O/gte_fuzz.exe"
for m in ${2:-i l}; do echo "== transfers: $m"; "$O/gte_fuzz.exe" "$LOG" "$m"; done
