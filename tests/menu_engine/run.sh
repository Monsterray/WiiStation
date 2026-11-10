#!/bin/sh
# Builds the menu engine's host test and runs it against the real font.
#
#     sh tests/menu_engine/run.sh            # clang++ if there is one, else g++
#     CXX=g++ sh tests/menu_engine/run.sh
#
# Nothing is written into the repository: the binary goes to a temporary file.
# Exit status: 0 = pass, 1 = a check failed, 2 = the build or font is missing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

cxx=${CXX:-}
if [ -z "$cxx" ]; then
	if command -v clang++ >/dev/null 2>&1; then cxx=clang++; else cxx=g++; fi
fi

bin=$(mktemp "${TMPDIR:-/tmp}/menu_engine_test.XXXXXX") || exit 2
bin2=$(mktemp "${TMPDIR:-/tmp}/explorer_info_test.XXXXXX") || exit 2
bin3=$(mktemp "${TMPDIR:-/tmp}/recent_list_test.XXXXXX") || exit 2
bin4=$(mktemp "${TMPDIR:-/tmp}/cover_index_test.XXXXXX") || exit 2
bin5=$(mktemp "${TMPDIR:-/tmp}/cover_art_test.XXXXXX") || exit 2
bin6=$(mktemp "${TMPDIR:-/tmp}/menu_pages_test.XXXXXX") || exit 2
scratch=$(mktemp -d "${TMPDIR:-/tmp}/recent_list_dir.XXXXXX") || exit 2
trap 'rm -f "$bin" "$bin2" "$bin3" "$bin4" "$bin5" "$bin6"; rm -rf "$scratch"' EXIT

"$cxx" -std=c++11 -Wall -Wextra -Wno-unused-parameter -O0 \
	-o "$bin" \
	"$root/tests/menu_engine_test.cpp" \
	"$root/Gamecube/menu/MenuEngine.cpp" || exit 2

"$bin" "$root/fonts/En.dat"
rc=$?

# The shared page tables (Gamecube/menu/pages/*.h), as the Wii build compiles them, with empty hooks.
"$cxx" -std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -O0 -o "$bin6" \
	"$root/tests/menu_pages_test.cpp" "$root/Gamecube/menu/MenuEngine.cpp" || exit 2
"$bin6" "$root" "$root/fonts/En.dat"
rc=$((rc + $?))

# The explorer's format/region/disc-count classifiers (plain C, the code the Wii runs).
case "$cxx" in *clang*) cc=clang ;; *) cc=gcc ;; esac
"$cc" -std=c11 -Wall -Wextra -O2 -D_CRT_SECURE_NO_WARNINGS -o "$bin2" \
	"$root/tests/explorer_info_test.c" \
	"$root/Gamecube/fileBrowser/explorer_info.c" || exit 2
"$bin2" "$root/tests/fixtures/explorer"
rc=$((rc + $?))

# The Recent record (recent.ini): add, move to front, cap, damaged and missing files.
"$cc" -std=c11 -Wall -Wextra -O2 -D_CRT_SECURE_NO_WARNINGS -o "$bin3" \
	"$root/tests/recent_list_test.c" \
	"$root/Gamecube/fileBrowser/recent_list.c" || exit 2
"$bin3" "$scratch"
rc=$((rc + $?))

# The cover pack index lookup (binary search of index.txt); the real pack's index too when it is here.
"$cc" -std=c11 -Wall -Wextra -O2 -D_CRT_SECURE_NO_WARNINGS -o "$bin4" \
	"$root/tests/cover_index_test.c" \
	"$root/Gamecube/fileBrowser/cover_index.c" || exit 2
real="${GAME_LIBRARY:-}/cover_pack/covers/index.txt"   # GAME_LIBRARY: ~/wii-tools.env, when exported
if [ -n "${GAME_LIBRARY:-}" ] && [ -f "$real" ]; then "$bin4" "$scratch" "$real"; else "$bin4" "$scratch"; fi
rc=$((rc + $?))

# Decoding a cover (stb_image, JPEG and PNG) into the explorer's RGB5A3 slot; the real pack's covers when it is here.
"$cc" -std=c11 -Wall -Wextra -O2 -D_CRT_SECURE_NO_WARNINGS -o "$bin5" \
	"$root/tests/cover_art_test.c" \
	"$root/Gamecube/cover_art.c" || exit 2
if [ -f "$real" ]; then "$bin5" "$(dirname "$real")"; else "$bin5"; fi
rc=$((rc + $?))
# Centered labels: the ink of every label lands within 1 px of the centre it is given (needs Pillow).
py=$(command -v python || command -v python3)
if [ -n "$py" ] && "$py" -c "import PIL" 2>/dev/null; then
	"$py" "$root/tests/menu_text_center.py" --scales 1.0,0.75,0.5 > "$scratch/center.txt"; crc=$?
	tail -1 "$scratch/center.txt"; echo "0 checks failed: menu_text_center exit $crc" | grep -q "^0 checks failed: .* exit 0$" || rc=$((rc + 1))
fi
exit $((rc != 0))	# a sum of failure counts can wrap to 0 at 256
