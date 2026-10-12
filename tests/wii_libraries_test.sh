#!/usr/bin/env bash
# A directory's existence must not hide a missing controller library/header.
set -euo pipefail
cd "$(dirname "$0")/.."
task_libraries=$(mktemp -d)
trap 'rm -rf "$task_libraries"' EXIT
if bash scripts/check-wii-libraries.sh "$task_libraries" >/dev/null 2>&1; then
  echo "Empty library directory incorrectly accepted" >&2; exit 1
fi
for required in include/gccore.h include/SDL/SDL.h include/wupc/wupc.h \
  include/wiidrc/wiidrc.h lib/wii/libogc.a lib/wii/libSDL.a \
  lib/wii/libwupc.a lib/wii/libwiidrc.a wii_rules gamecube_rules; do
  mkdir -p "$task_libraries/$(dirname "$required")"
  touch "$task_libraries/$required"
done
bash scripts/check-wii-libraries.sh "$task_libraries"
rm "$task_libraries/include/wupc/wupc.h"
if bash scripts/check-wii-libraries.sh "$task_libraries" >/dev/null 2>&1; then
  echo "Incomplete WUPC installation incorrectly accepted" >&2; exit 1
fi
echo "Wii library preflight: empty, complete, and partial installations checked"
