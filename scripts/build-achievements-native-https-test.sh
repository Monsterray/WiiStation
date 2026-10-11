#!/usr/bin/env bash
# Credential-free source build. All seed material in this DOL is public dummy data.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DEVKITPRO:?Set the pinned WiiStation toolchain}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
output=${1:?Usage: build-achievements-native-https-test.sh OUTPUT_DIRECTORY}
mkdir -p "$output" .runs/cache
output=$(cd "$output" && pwd)
packages=${WII_NETWORK_PACKAGES:-.runs/cache/wii-network}
ca=${WII_NETWORK_CA:-$packages/cacert.pem}
mkdir -p "$packages"
archive=.runs/cache/curl-8.16.0.tar.xz
socket=$packages/libwiisocket-0.1-1-any.pkg.tar.gz
if [ ! -f "$archive" ]; then curl -fL https://curl.se/download/curl-8.16.0.tar.xz -o "$archive"; fi
if [ ! -f "$socket" ]; then
  curl -fL 'https://github.com/AndrewPiroli/wii-curl/releases/download/c8.16.0%2Bm3.6.4/libwiisocket-0.1-1-any.pkg.tar.gz' -o "$socket"
fi
if [ ! -f "$ca" ]; then
  curl -fL 'https://raw.githubusercontent.com/AndrewPiroli/wii-curl/d9b21a24142c6bfbfff15cd4151ef154b9659c4e/sample-app/data/cacert.pem' -o "$ca"
fi
python3 - "$archive" "$socket" "$ca" <<'PY'
import hashlib,sys
expected=['40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d',
          'a77be1d6c0e3e5adb87fb751a9fdc89b325f7f9bdee77742b903b7aba15f20cc',
          '189d3cf6d103185fba06d76c1af915263c6d42225481a1759e853b33ac857540']
for path,sha in zip(sys.argv[1:],expected):
    assert hashlib.sha256(open(path,'rb').read()).hexdigest()==sha,path
PY
# Rebuild the matched Mbed TLS headers, libraries and storage test every time.
bash scripts/test-achievements-entropy.sh --wii "$output/tls"
task_build=$(mktemp -d /tmp/wiistation-native-https.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
tar -xJf "$archive" -C "$task_build"
tar -xzf "$socket" -C "$task_build"
socket_root="$task_build/opt/devkitpro/libogc"
"$DEVKITPPC/bin/powerpc-eabi-ar" d "$socket_root/lib/wii/libwiisocket.a" inet_addr.o inet_aton.o inet_ntoa.o
tls="$task_build/tls"
cp -R "$output/tls" "$tls"
cc="$DEVKITPPC/bin/powerpc-eabi-gcc"
flags="-mrvl -mcpu=750 -meabi -mhard-float -O2 -g -fno-lto"
export PATH="$DEVKITPPC/bin:$PATH"
cd "$task_build/curl-8.16.0"
CC="$cc" AR="$DEVKITPPC/bin/powerpc-eabi-ar" RANLIB="$DEVKITPPC/bin/powerpc-eabi-ranlib" \
 CFLAGS="$flags" \
 CPPFLAGS="-DGEKKO -I$socket_root/include -I$DEVKITPRO/libogc2/include -I$tls/include -I$DEVKITPRO/portlibs/ppc/include" \
 LDFLAGS="$flags -L$tls/lib -L$socket_root/lib/wii -L$DEVKITPRO/libogc2/lib/wii -L$DEVKITPRO/portlibs/ppc/lib" \
 LIBS='-lmbedtls -lmbedx509 -lmbedcrypto -lwiisocket -logc -lz -lm' \
 ./configure --prefix="$task_build/install" --host=powerpc-eabi \
 --disable-shared --enable-static --disable-ipv6 --disable-unix-sockets \
 --disable-manual --disable-threaded-resolver --disable-socketpair \
 --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp \
 --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap \
 --disable-smtp --disable-gopher --disable-mqtt --disable-smb \
 --without-libpsl --without-libidn2 --without-brotli --without-zstd \
 --without-libssh2 --without-nghttp2 --without-nghttp3 --without-ngtcp2 \
 --with-mbedtls="$tls" --with-zlib="$DEVKITPRO/portlibs/ppc"
make -s -j4 -C lib
make -s -C lib install
make -s -C include install
cd - >/dev/null
cp -R deps/hbc_agent "$task_build/agent"
make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
"$cc" -DGEKKO -DACHIEVEMENTS_NV_PROBE -mrvl -mcpu=750 -meabi -mhard-float -O2 -g \
 -Wall -Wextra -Werror -I"$socket_root/include" -I"$DEVKITPRO/libogc2/include" \
 -I"$task_build/install/include" -I"$tls/include" tests/achievements_https_wii.c achievement_seed.c achievement_clock.c tests/achievement_clock_test.c \
 -L"$task_build/agent/lib" -L"$task_build/install/lib" -L"$tls/lib" \
 -L"$socket_root/lib/wii" -L"$DEVKITPRO/libogc2/lib/wii" -L"$DEVKITPRO/portlibs/ppc/lib" \
 -lhbcagent -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lwiisocket -lfat -lwiiuse -lbte -logc -lm \
 -o "$output/boot.elf"
"$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
cp "$ca" "$output/cacert.pem"
mkdir -p "$output/lib" "$output/include"
cp "$task_build/install/lib/libcurl.a" "$socket_root/lib/wii/libwiisocket.a" "$output/lib/"
cp -R "$task_build/install/include/." "$socket_root/include/." "$output/include/"
python3 - "$output" "$cc" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1])
paths=[out/'tls/include/mbedtls/mbedtls_config.h',
       *sorted((out/'tls/lib').glob('*.a')), *sorted((out/'lib').glob('*.a'))]
inputs=['achievement_clock.c','achievement_clock.h','tests/achievement_clock_test.c','tests/achievement_clock_test.h','achievement_seed.c','achievement_seed.h','tests/achievements_https_wii.c',
        'scripts/test-achievements-entropy.sh','scripts/build-achievements-native-https-test.sh']
info={'compiler':subprocess.check_output([sys.argv[2],'--version'],text=True).splitlines()[0],
      'fixture':'public dummy seed; no authenticated traffic',
      'artifacts':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
      'sources':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in inputs}}
(out/'build-info.json').write_text(json.dumps(info,indent=2)+'\n')
PY
