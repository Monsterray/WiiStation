#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set the pinned WiiStation toolchain}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
output=${1:?Usage: build-achievements-client-test.sh OUTPUT_DIRECTORY}
archive=.runs/cache/rcheevos-683197d8dc92555a51e8280a4123d071ed66bff3.tar.gz
python3 - "$archive" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()=='173679fb2fb1bb4a281a3d5167792ef23efef65c64a8ad0b15fc34a05e557324'
PY
mkdir -p "$output";output=$(cd "$output" && pwd)
task_build=$(mktemp -d /tmp/wiistation-client-wii.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
tar -xzf "$archive" -C "$task_build"
upstream="$task_build/rcheevos-683197d8dc92555a51e8280a4123d071ed66bff3"
cp -R deps/hbc_agent "$task_build/agent"
make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
cc="$DEVKITPPC/bin/powerpc-eabi-gcc"
flags=(-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -g -Wall -Wextra -Werror -fno-fast-math -fno-lto
       -I"$DEVKITPRO/libogc2/include" -Ideps/rcheevos/include -I"$upstream/test")
for source in deps/rcheevos/src/runtime/*.c deps/rcheevos/src/util/*.c deps/rcheevos/src/api/*.c deps/rcheevos/src/client/*.c tests/achievements_client_wii.c "$upstream/test/client/test_rc_client.c"; do
  "$cc" "${flags[@]}" -c "$source" -o "$task_build/$(basename "${source%.c}").o"
done
"$cc" -mrvl -mcpu=750 -meabi -mhard-float "$task_build"/*.o \
  -L"$task_build/agent/lib" -L"$DEVKITPRO/libogc2/lib/wii" -L"$DEVKITPRO/portlibs/ppc/lib" \
  -lhbcagent -lfat -lwiiuse -lbte -logc -lz -lm -o "$output/boot.elf"
"$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
