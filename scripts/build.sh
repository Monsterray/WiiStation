#!/usr/bin/env bash
# Build WiiStation for Wii. Run scripts/setup-devkitpro-windows.sh once first.
#
# Usage:
#   scripts/build.sh            # debug build (WiiSXRX_debug.dol)
#   scripts/build.sh release    # release build (WiiSXRX_Release.dol)
#   scripts/build.sh clean      # clean all build artifacts
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$REPO_ROOT" == *" "* ]]; then
	echo "ERROR: this repo is checked out at a path containing a space:"
	echo "  $REPO_ROOT"
	echo "devkitPro's Makefiles break on spaces in the path (unquoted \$(CURDIR))."
	echo "Move or clone the repo to a path with no spaces, e.g. C:/projects/WiiStation."
	exit 1
fi

export DEVKITPRO=/c/devkitPro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"

if [ ! -x "$DEVKITPPC/bin/powerpc-eabi-gcc.exe" ]; then
	echo "devkitPPC not found at $DEVKITPPC -- run scripts/setup-devkitpro-windows.sh first."
	exit 1
fi

cd "$REPO_ROOT"

# The five dependency archives (opengx, zstd, lzma, zlibstatic, chdr) build
# to their own deps/*/lib directories; Gamecube/Makefile_Wii links against
# those paths directly via -L, so no "make install" step is needed here.
# lightrec/lightning are not built at all -- they're prebuilt binaries
# already installed under devkitPPC/powerpc-eabi/lib by the setup script.
case "${1:-debug}" in
	debug)
		make opengx.a lightrecWithLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		make -C Gamecube -f Makefile_Wii
		echo "Output: Gamecube/WiiSXRX_debug.dol"
		;;
	release)
		make opengx.a lightrecNoLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		make -C Gamecube -f Makefile_Wii_Release
		echo "Output: Gamecube/WiiSXRX_Release.dol"
		;;
	clean)
		make clean
		;;
	*)
		echo "Usage: $0 [debug|release|clean]"
		exit 1
		;;
esac
