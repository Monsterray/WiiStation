#!/usr/bin/env bash
# Build WiiStation for Wii. Run scripts/setup-devkitpro-windows.sh once first.
#
# Run this from devkitPro's own MSYS2 shell (Start Menu -> devkitPro -> MSys2,
# or C:\devkitPro\msys2\msys2_shell.bat) -- that's what provides `make` and
# the rest of the standard devkitPro environment. A generic Git Bash works
# too as long as `make` is on PATH some other way; devkitPro's own shell is
# the one this is tested against.
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
# Pinned, not the pacman-managed $DEVKITPRO/devkitPPC -- see
# scripts/setup-devkitpro-windows.sh's header comment for why.
export DEVKITPPC="$DEVKITPRO/devkitPPC-r41-2"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"

if [ ! -x "$DEVKITPPC/bin/powerpc-eabi-gcc.exe" ]; then
	echo "devkitPPC r41-2 not found at $DEVKITPPC -- run scripts/setup-devkitpro-windows.sh first."
	exit 1
fi
if ! command -v make >/dev/null 2>&1; then
	echo "make not found on PATH. Run this from devkitPro's own MSYS2 shell"
	echo "(Start Menu -> devkitPro -> MSys2, or C:\\devkitPro\\msys2\\msys2_shell.bat)."
	exit 1
fi

cd "$REPO_ROOT"

# The five dependency archives (opengx, zstd, lzma, zlibstatic, chdr) build
# to their own deps/*/lib directories; Gamecube/Makefile_Wii links against
# those paths directly via -L, so no "make install" step is needed here.
# lightrec/lightning are not built at all -- they're prebuilt binaries
# already installed under devkitPPC-r41-2/powerpc-eabi/lib by the setup
# script (they came from this project's own bundled zip, not pacman).
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
