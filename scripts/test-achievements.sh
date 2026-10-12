#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_dir=$(mktemp -d)
trap 'rm -rf "$task_dir"' EXIT
# Upstream's parser uses null-based member-offset expressions which Clang UBSan
# diagnoses. Keep it unmodified; ASan covers upstream, both sanitizers cover our glue.
for source in deps/rcheevos/src/runtime/*.c deps/rcheevos/src/util/*.c; do
  "${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address -fno-omit-frame-pointer -Ideps/rcheevos/include \
    -c "$source" -o "$task_dir/$(basename "${source%.c}").o"
done
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ideps/rcheevos/include achievements.c tests/achievements_test.c \
  "$task_dir"/*.o -lm -o "$task_dir/test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/test" "$task_dir"
