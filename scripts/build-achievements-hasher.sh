#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output=${1:?Usage: build-achievements-hasher.sh OUTPUT_PATH}
mkdir -p "$(dirname "$output")"
"${CC:-cc}" -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DRC_HASH_NO_ROM -DRC_HASH_NO_ZIP -DRC_HASH_NO_ENCRYPTED \
  -Ideps/rcheevos/include tests/hash_ps1.c deps/rcheevos/src/hash/*.c \
  deps/rcheevos/src/util/*.c deps/rcheevos/src/runtime/rc_consoleinfo.c \
  -lm -o "$output"
