#!/bin/bash
# build-ra-https.sh -- Official mode's HTTPS stack for the product build: Mbed TLS 3.6.4 (NV seed
# configuration: all entropy from the Wii's secret seed file) and curl 8.16.0, built for libogc v3
# + devkitPPC r50 by scripts/build-achievements-native-https-test.sh (proven on the bench Wii,
# job 20261023-135603-294ade), installed into build_ogc3/deps/ra_https, where Makefile_Wii and
# Makefile_Wii_Release find it and build Gamecube/ra_transport.c with RA_HTTPS.
#   usage: scripts/build-ra-https.sh [--rebuild]     (run from the devkitPro shell, as build.sh)
# Without --rebuild it installs the last build in .runs/https-ogc3 if there is one.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
src=.runs/https-ogc3
dst=build_ogc3/deps/ra_https
if [ "${1:-}" = "--rebuild" ] || [ ! -f "$src/lib/libcurl.a" ] || [ ! -f "$src/tls/lib/libmbedtls.a" ]; then
	DEVKITPRO=/c/devkitPro   # set outright, as below
	export DEVKITPRO
	bash scripts/build-achievements-native-https-test.sh "$src"
fi
rm -rf "$dst"
mkdir -p "$dst/lib" "$dst/include"
cp "$src/lib/libcurl.a" "$src"/tls/lib/libmbed*.a "$dst/lib/"
cp -R "$src/include/." "$src/tls/include/." "$dst/include/"
cp "$src/cacert.pem" "$dst/cacert.pem"   # the CA bundle the build was tested with
# libsmb2 (in WiiStation's link, after curl) has its own poll() for its raw IOS sockets
# (deps/libsmb2/lib/compat.c). curl's sockets are libogc v3's (newlib descriptors through the
# "soc" devoptab), so with libsmb2's poll curl polled the wrong IOS socket and every connect
# timed out (curl 28; Dolphin and the bench Wii, 2026-10-23). curl gets libogc's own poll,
# under another name: its reference renamed, and libogc's soc_poll.o renamed to match.
DEVKITPRO=/c/devkitPro   # set outright: the ambient environment may hold a stale /opt/devkitpro
bin="$DEVKITPRO/devkitPPC/bin"
"$bin/powerpc-eabi-objcopy" --redefine-sym poll=ws_soc_poll "$dst/lib/libcurl.a"
tmp=$(mktemp -d)
( cd "$tmp" && "$bin/powerpc-eabi-ar" x "$DEVKITPRO/libogc/lib/wii/libogc.a" soc_poll.o )
"$bin/powerpc-eabi-objcopy" --redefine-sym poll=ws_soc_poll "$tmp/soc_poll.o"
"$bin/powerpc-eabi-ar" rcs "$dst/lib/libwssocpoll.a" "$tmp/soc_poll.o"
rm -rf "$tmp"
echo "build-ra-https: $(ls "$dst/lib" | tr '\n' ' ')-> $dst"
