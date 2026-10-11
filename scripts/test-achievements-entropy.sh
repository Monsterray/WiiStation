#!/usr/bin/env bash
# Credential-free contract test; no secret or Wii provisioning.
set -euo pipefail
cd "$(dirname "$0")/.."
target=${1:-host}
if [ "$target" != host ] && [ "$target" != --wii ]; then
  echo 'Usage: test-achievements-entropy.sh [--wii OUTPUT_DIRECTORY]' >&2
  exit 1
fi
if [ "$target" = --wii ]; then
  : "${DEVKITPRO:?Set the pinned WiiStation toolchain}"
  : "${DEVKITPPC:=$DEVKITPRO/devkitPPC-r41-2}"
  output=${2:?Choose an output directory}
  mkdir -p "$output"
  output=$(cd "$output" && pwd)
fi
archive=.runs/cache/mbedtls-3.6.4.tar.bz2
mkdir -p .runs/cache
if [ ! -f "$archive" ]; then
  curl -fL 'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2' -o "$archive"
fi
python3 - "$archive" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()=='ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110'
PY
task_build=$(mktemp -d /tmp/wiistation-entropy-test.XXXXXX)
trap 'rm -rf "$task_build"' EXIT
tar -xjf "$archive" -C "$task_build" --strip-components=1
python3 - "$task_build/include/mbedtls/mbedtls_config.h" "$target" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
for option in ('MBEDTLS_ENTROPY_NV_SEED','MBEDTLS_PLATFORM_NV_SEED_ALT','MBEDTLS_NO_PLATFORM_ENTROPY',
               'MBEDTLS_PLATFORM_TIME_ALT','MBEDTLS_PLATFORM_GMTIME_R_ALT'):
    assert '//#define '+option in s
    s=s.replace('//#define '+option,'#define '+option)
if sys.argv[2] == '--wii':
    s=s.replace('//#define MBEDTLS_PLATFORM_MS_TIME_ALT', '#define MBEDTLS_PLATFORM_MS_TIME_ALT')
    # The upstream timing module supports only Unix/Windows. This probe has no
    # DTLS timers; millisecond timing uses the Wii callback in the test harness.
    s=s.replace('#define MBEDTLS_TIMING_C', '//#define MBEDTLS_TIMING_C')
    # curl owns sockets; do not compile the upstream Unix socket convenience API.
    s=s.replace('#define MBEDTLS_NET_C', '//#define MBEDTLS_NET_C')
p.write_text(s)
PY
# Use the checked release's generated PSA wrappers; the entropy-only switches
# do not change them. Avoid requiring generator-only Python dependencies.
if [ "$target" = host ]; then
  cc=${CC:-cc}
  flags=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
  make_args=(CC="$cc")
else
  cc="$DEVKITPPC/bin/powerpc-eabi-gcc"
  flags=(-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -g -fno-lto
         -I"$DEVKITPRO/libogc2/include")
  make_args=(CC="$cc" AR="$DEVKITPPC/bin/powerpc-eabi-ar" APPLE_BUILD=0)
fi
libraries=(libmbedcrypto.a libmbedx509.a)
if [ "$target" = --wii ]; then libraries+=(libmbedtls.a); fi
make -s -j4 -C "$task_build/library" -o psa_crypto_driver_wrappers.h \
  -o psa_crypto_driver_wrappers_no_static.c "${make_args[@]}" \
  CFLAGS="${flags[*]}" "${libraries[@]}"
if [ "$target" = host ]; then
  "$cc" -std=c99 "${flags[@]}" -Wall -Wextra -Werror \
    -I"$task_build/include" tests/entropy_seed_test.c "$task_build/library/libmbedcrypto.a" -o "$task_build/check"
  UBSAN_OPTIONS=halt_on_error=1 "$task_build/check"
  "$cc" -std=c99 "${flags[@]}" -Wall -Wextra -Werror -I"$task_build/include" \
    achievement_seed.c tests/entropy_storage_test.c "$task_build/library/libmbedcrypto.a" -o "$task_build/storage"
  UBSAN_OPTIONS=halt_on_error=1 "$task_build/storage" "$task_build"
  cat "$task_build/storage.log"
  "$cc" -std=c99 "${flags[@]}" -Wall -Wextra -Werror -DACHIEVEMENT_CLOCK_HOST_TEST \
    -I"$task_build/include" achievement_clock.c tests/achievement_clock_test.c \
    "$task_build/library/libmbedx509.a" "$task_build/library/libmbedcrypto.a" -o "$task_build/clock"
  UBSAN_OPTIONS=halt_on_error=1 "$task_build/clock"
else
  cp -R deps/hbc_agent "$task_build/agent"
  make -s -C "$task_build/agent" DEVKITPRO="$DEVKITPRO" DEVKITPPC="$DEVKITPPC"
  "$cc" -std=c99 "${flags[@]}" -Wall -Wextra -Werror -I"$task_build/include" \
    achievement_seed.c achievement_clock.c tests/achievement_clock_test.c tests/entropy_storage_test.c \
    "$task_build/library/libmbedx509.a" "$task_build/library/libmbedcrypto.a" \
    -L"$task_build/agent/lib" -L"$DEVKITPRO/libogc2/lib/wii" -L"$DEVKITPRO/portlibs/ppc/lib" \
    -lhbcagent -lfat -lwiiuse -lbte -logc -lz -lm -o "$output/boot.elf"
  "$DEVKITPRO/tools/bin/elf2dol" "$output/boot.elf" "$output/boot.dol"
  # Matched headers and libraries for the curl source build. Never combine these
  # headers with the old binary packages: the NV option changes context layout.
  mkdir -p "$output/lib"
  cp "$task_build/library/"libmbed*.a "$output/lib/"
  cp -R "$task_build/include" "$output/"
fi
