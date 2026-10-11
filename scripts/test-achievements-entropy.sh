#!/usr/bin/env bash
# Credential-free contract test; no secret or Wii provisioning.
set -euo pipefail
cd "$(dirname "$0")/.."
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
python3 - "$task_build/include/mbedtls/mbedtls_config.h" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
for option in ('MBEDTLS_ENTROPY_NV_SEED','MBEDTLS_PLATFORM_NV_SEED_ALT','MBEDTLS_NO_PLATFORM_ENTROPY'):
    assert '//#define '+option in s
    s=s.replace('//#define '+option,'#define '+option)
p.write_text(s)
PY
# Use the checked release's generated PSA wrappers; the entropy-only switches
# do not change them. Avoid requiring generator-only Python dependencies.
make -s -j4 -C "$task_build/library" -o psa_crypto_driver_wrappers.h \
  -o psa_crypto_driver_wrappers_no_static.c CC="${CC:-cc}" \
  CFLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' libmbedcrypto.a
"${CC:-cc}" -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$task_build/include" tests/entropy_seed_test.c "$task_build/library/libmbedcrypto.a" -o "$task_build/check"
"$task_build/check"
