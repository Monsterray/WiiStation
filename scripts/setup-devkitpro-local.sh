#!/usr/bin/env bash
# Isolated pinned toolchain for macOS/Linux; needs an existing devkitPro tools/portlibs install.
# Usage: bash scripts/setup-devkitpro-local.sh /tmp/wiistation-devkitpro [/opt/devkitpro]
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
task_toolchain=${1:?Choose a new directory without spaces for the isolated toolchain}
system_devkitpro=${2:-/opt/devkitpro}
case "$task_toolchain" in /*) ;; *) echo "Choose an absolute toolchain path." >&2; exit 1 ;; esac
case "$task_toolchain" in *' '*) echo "Toolchain path must have no spaces." >&2; exit 1 ;; esac
if [ -e "$task_toolchain" ]; then
  echo "Choose a new directory; refusing to modify an existing toolchain: $task_toolchain" >&2
  exit 1
fi
case "$(uname -s)-$(uname -m)" in
  Darwin-x86_64) platform=osx_x86_64 ;;
  Darwin-arm64) platform=osx_arm64 ;;
  Linux-x86_64) platform=linux_x86_64 ;;
  Linux-aarch64) platform=linux_aarch64 ;;
  *) echo "Unsupported host for the pinned compiler." >&2; exit 1 ;;
esac
for required in tools/bin/elf2dol devkitPPC/base_tools portlibs/ppc/include/zlib.h; do
  [ -f "$system_devkitpro/$required" ] || { echo "Missing base devkitPro component: $required" >&2; exit 1; }
done
task_unpack=$(mktemp -d)
trap 'rm -rf "$task_unpack"' EXIT
curl -fL --retry 2 -o "$task_unpack/compiler.tar.xz" \
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r41%20(2022-05-31)/devkitPPC-r41-2-${platform}.pkg.tar.xz"
tar -xf "$task_unpack/compiler.tar.xz" -C "$task_unpack"
unzip -oq "$repo_root/lightrec+Libogc2.zip" -d "$task_unpack"
mkdir -p "$task_toolchain"
cp -R "$task_unpack/opt/devkitpro/devkitPPC" "$task_toolchain/devkitPPC-r41-2"
cp -R "$task_unpack/lightrec+Libogc2/devkitPPC/." "$task_toolchain/devkitPPC-r41-2/"
cp -R "$task_unpack/lightrec+Libogc2/libogc2" "$task_toolchain/libogc2"
ln -s "$system_devkitpro/tools" "$task_toolchain/tools"
ln -s "$system_devkitpro/portlibs" "$task_toolchain/portlibs"
cp "$task_toolchain/libogc2/wii_rules" "$task_toolchain/devkitPPC-r41-2/wii_rules"
cp "$task_toolchain/libogc2/gamecube_rules" "$task_toolchain/devkitPPC-r41-2/gamecube_rules"
sed 's#$(DEVKITPATH)/devkitPPC/bin#$(DEVKITPPC)/bin#' \
  "$system_devkitpro/devkitPPC/base_tools" > "$task_toolchain/devkitPPC-r41-2/base_tools"
bash "$repo_root/scripts/check-wii-libraries.sh" "$task_toolchain/libogc2"
echo "Ready. Build with:"
printf 'DEVKITPRO=%q DEVKITPPC=%q bash scripts/build.sh release\n' \
  "$task_toolchain" "$task_toolchain/devkitPPC-r41-2"
