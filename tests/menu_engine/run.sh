#!/bin/sh
# Builds the menu engine's host test and runs it against the real font.
#
#     sh tests/menu_engine/run.sh            # clang++ if there is one, else g++
#     CXX=g++ sh tests/menu_engine/run.sh
#
# Nothing is written into the repository: the binary goes to a temporary file.
# Exit status is the number of failed checks (0 = pass), or 2 if the build or font is missing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

cxx=${CXX:-}
if [ -z "$cxx" ]; then
	if command -v clang++ >/dev/null 2>&1; then cxx=clang++; else cxx=g++; fi
fi

bin=$(mktemp "${TMPDIR:-/tmp}/menu_engine_test.XXXXXX") || exit 2
trap 'rm -f "$bin"' EXIT

"$cxx" -std=c++11 -Wall -Wextra -Wno-unused-parameter -O2 \
	-o "$bin" \
	"$root/tests/menu_engine_test.cpp" \
	"$root/Gamecube/menu/MenuEngine.cpp" || exit 2

"$bin" "$root/fonts/En.dat"
