#!/usr/bin/env bash
# Isolated, credential-free feasibility probe using checked upstream Wii packages.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set the pinned WiiStation toolchain}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
output=${1:?Usage: build-achievements-https-test.sh OUTPUT_DIRECTORY}
packages=${WII_NETWORK_PACKAGES:-.runs/cache/wii-network}
ca=${WII_NETWORK_CA:-$packages/cacert.pem}
mkdir -p "$packages"
release='https://github.com/AndrewPiroli/wii-curl/releases/download/c8.16.0%2Bm3.6.4'
for package in libwiisocket-0.1-1-any.pkg.tar.gz wii-mbedtls-3.6.4-1-any.pkg.tar.gz wii-curl-8.16.0-1-any.pkg.tar.gz; do
  if [ ! -f "$packages/$package" ]; then
    curl -fL "$release/$package" -o "$packages/$package"
  fi
done
if [ ! -f "$ca" ]; then
  curl -fL 'https://raw.githubusercontent.com/AndrewPiroli/wii-curl/d9b21a24142c6bfbfff15cd4151ef154b9659c4e/sample-app/data/cacert.pem' -o "$ca"
fi
python3 - "$packages" "$ca" <<'PY'
import hashlib,sys
from pathlib import Path
p=Path(sys.argv[1]); checks={
'libwiisocket-0.1-1-any.pkg.tar.gz':'a77be1d6c0e3e5adb87fb751a9fdc89b325f7f9bdee77742b903b7aba15f20cc',
'wii-mbedtls-3.6.4-1-any.pkg.tar.gz':'0b0d3eb9aa93fd26f9caf2a52f8236354861daf4c558b95e0a3f67bc66c655de',
'wii-curl-8.16.0-1-any.pkg.tar.gz':'5aa5109b0baed29e516deef074f68d01798b24df6632a45cac0a8807dbdb0404'}
for n,h in checks.items(): assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h,n
assert hashlib.sha256(Path(sys.argv[2]).read_bytes()).hexdigest()=='189d3cf6d103185fba06d76c1af915263c6d42225481a1759e853b33ac857540','CA bundle'
PY
mkdir -p "$output"
output=$(cd "$output" && pwd)
task_build=$(mktemp -d /tmp/wiistation-https-test.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
for package in libwiisocket-0.1-1-any.pkg.tar.gz wii-mbedtls-3.6.4-1-any.pkg.tar.gz wii-curl-8.16.0-1-any.pkg.tar.gz; do
  tar -xzf "$packages/$package" -C "$task_build"
done
network="$task_build/opt/devkitpro/portlibs/wii"
socketlib="$task_build/opt/devkitpro/libogc/lib/wii"
# libogc2 defines these three helpers, but not inet_ntop/inet_pton.
"$DEVKITPPC/bin/powerpc-eabi-ar" d "$socketlib/libwiisocket.a" inet_addr.o inet_aton.o inet_ntoa.o
cp -R deps/hbc_agent "$task_build/agent"
make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
"$DEVKITPPC/bin/powerpc-eabi-gcc" -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -g \
  -Wall -Wextra -Werror -I"$DEVKITPRO/libogc2/include" -I"$network/include" \
  -I"$task_build/opt/devkitpro/libogc/include" tests/achievements_https_wii.c tests/https_wii_compat.c \
  -L"$task_build/agent/lib" -L"$network/lib" -L"$socketlib" -L"$DEVKITPRO/libogc2/lib/wii" \
  -L"$DEVKITPRO/portlibs/ppc/lib" -lhbcagent -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
  -lz -lwiisocket -lfat -lwiiuse -lbte -logc -lm -o "$output/boot.elf"
"$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
cp "$ca" "$output/cacert.pem"
