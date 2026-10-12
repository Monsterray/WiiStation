#!/usr/bin/env bash
# Run pinned upstream runtime/API/client regression tests with our Wii byte fix.
set -euo pipefail
cd "$(dirname "$0")/.."
revision=683197d8dc92555a51e8280a4123d071ed66bff3
archive=.runs/cache/rcheevos-$revision.tar.gz
mkdir -p .runs/cache
if [ ! -f "$archive" ]; then
  curl -fL "https://codeload.github.com/RetroAchievements/rcheevos/tar.gz/$revision" -o "$archive"
fi
python3 - "$archive" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest() == '173679fb2fb1bb4a281a3d5167792ef23efef65c64a8ad0b15fc34a05e557324', 'Upstream archive checksum mismatch'
PY
task_dir=$(mktemp -d /tmp/wiistation-client-tests.XXXXXX)
trap 'rm -rf "$task_dir"' EXIT
tar -xzf "$archive" -C "$task_dir"
source_root="$task_dir/rcheevos-$revision"
cp deps/rcheevos/src/runtime/rc_memref.c "$source_root/src/runtime/rc_memref.c"
# Upstream uses null-based offset expressions: ASan upstream, UBSan in our glue suite.
make -s -j4 -C "$source_root/test/client" ARCH=x64 HAVE_HASH=0 CC="${CC:-cc}" \
  CFLAGS='-std=c99 -g -O1 -fsanitize=address -fno-omit-frame-pointer -DRC_HASH_NO_ROM -DRC_HASH_NO_DISC -DRC_HASH_NO_ZIP -DRC_HASH_NO_ENCRYPTED' \
  LDFLAGS='-fsanitize=address' rc_client-test
"$source_root/test/client/rc_client-test"
