#!/bin/bash
# Build gxaa.dol (the GX anti-aliasing test bench, gxaa.c) with WiiStation's toolchain:
# devkitPPC r50 + libogc v3, and the HBC agent (deps/hbc_agent) so `hbc.py screen`/`exit` work.
# Run from Git Bash or the devkitPro MSYS2 bash:  bash scripts/wii_probe/gxaa/build.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
. "$repo/scripts/tools_env.sh"; need DEVKITPRO "level 1"   # ~/wii-tools.env
export DEVKITPRO
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$DEVKITPRO/msys2/usr/bin:$PATH"
WTMP=$(cygpath -w "${TMPDIR:-$here}" 2>/dev/null || echo "$here")
make -s -C "$repo/deps/hbc_agent" LIBOGC=3 DEVKITPRO=$DEVKITPRO DEVKITPPC=$DEVKITPPC TMP="$WTMP" TEMP="$WTMP"
cd "$here"
powerpc-eabi-gcc -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -Wall \
	-I$DEVKITPRO/libogc/include -I"$repo/deps/hbc_agent/sdk" gxaa.c -o gxaa.elf \
	-L$DEVKITPRO/libogc/lib/wii -L$DEVKITPRO/portlibs/ppc/lib -L"$repo/deps/hbc_agent/lib" \
	-lhbcagent -lwiiuse -lbte -lfat -logc -lz -lm
elf2dol gxaa.elf gxaa.dol
ls -l gxaa.dol
