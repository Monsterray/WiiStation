#!/usr/bin/env bash
# Check the legacy layout WiiStation's pinned build uses, not merely the directory.
set -euo pipefail
library_root=${1:?Usage: check-wii-libraries.sh LIBOGC2_DIRECTORY}
missing=0
for required in include/gccore.h include/SDL/SDL.h include/wupc/wupc.h \
  include/wiidrc/wiidrc.h lib/wii/libogc.a lib/wii/libSDL.a \
  lib/wii/libwupc.a lib/wii/libwiidrc.a wii_rules gamecube_rules; do
  if [ ! -f "$library_root/$required" ]; then
    echo "Missing WiiStation dependency: $library_root/$required" >&2
    missing=1
  fi
done
exit "$missing"
