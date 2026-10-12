#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set DEVKITPRO to the isolated WiiStation toolchain}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
if ! "$DEVKITPPC/bin/powerpc-eabi-gcc" --version | head -1 | grep -q 'release 41'; then
  echo 'Set DEVKITPPC to WiiStation’s pinned release 41 compiler.' >&2
  exit 1
fi
output=${1:?Usage: build-achievements-wii-test.sh OUTPUT_DIRECTORY}
mkdir -p "$output"
output=$(cd "$output" && pwd)
task_build=$(mktemp -d /tmp/achievement-wii-test.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
# The SDK Makefile needs a space-free checkout; build a private copy.
cp -R deps/hbc_agent "$task_build/agent"
make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
cc="$DEVKITPPC/bin/powerpc-eabi-gcc"
flags=(-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -g -Wall -Wextra -Werror
  -I"$DEVKITPRO/libogc2/include" -Ideps/rcheevos/include -fno-fast-math -fno-lto)
for source in achievements.c deps/rcheevos/src/runtime/*.c deps/rcheevos/src/util/*.c tests/achievements_wii.c; do
  "$cc" "${flags[@]}" -c "$source" -o "$task_build/$(basename "${source%.c}").o"
done
"$cc" "${flags[@]}" -Dmain=achievements_test_main -c tests/achievements_test.c -o "$task_build/test.o"
"$cc" -mrvl -mcpu=750 -meabi -mhard-float "$task_build"/*.o \
  -L"$task_build/agent/lib" -L"$DEVKITPRO/libogc2/lib/wii" \
  -L"$DEVKITPRO/portlibs/ppc/lib" -lhbcagent -lfat -lwiiuse -lbte -logc -lz -lm \
  -o "$output/boot.elf"
"$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
