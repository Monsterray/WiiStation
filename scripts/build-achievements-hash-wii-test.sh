#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set the pinned WiiStation toolchain}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
output=${1:?Usage: build-achievements-hash-wii-test.sh OUTPUT_DIRECTORY}
mkdir -p "$output";output=$(cd "$output" && pwd)
task_build=$(mktemp -d /tmp/wiistation-hash-wii.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
cp -R deps/hbc_agent "$task_build/agent"
make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
cc="$DEVKITPPC/bin/powerpc-eabi-gcc"
flags=(-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -g -Wall -Wextra -Werror -fno-fast-math -fno-lto
       -DRC_HASH_NO_ROM -DRC_HASH_NO_ZIP -DRC_HASH_NO_ENCRYPTED -I"$DEVKITPRO/libogc2/include" -Ideps/rcheevos/include)
for source in deps/rcheevos/src/hash/*.c deps/rcheevos/src/util/*.c deps/rcheevos/src/runtime/rc_consoleinfo.c tests/hash_ps1_wii.c; do
  "$cc" "${flags[@]}" -c "$source" -o "$task_build/$(basename "${source%.c}").o"
done
"$cc" -mrvl -mcpu=750 -meabi -mhard-float "$task_build"/*.o \
  -L"$task_build/agent/lib" -L"$DEVKITPRO/libogc2/lib/wii" -L"$DEVKITPRO/portlibs/ppc/lib" \
  -lhbcagent -lfat -lwiiuse -lbte -logc -lz -lm -o "$output/boot.elf"
"$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
