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
#   scripts/build.sh              # debug build (WiiSXRX_debug.dol)
#   scripts/build.sh release      # release build (WiiSXRX_Release.dol)
#   scripts/build.sh debug-warn   # debug build with -Wall -Wextra (WARN=1)
#   scripts/build.sh release-warn # release build with -Wall -Wextra (WARN=1)
#   scripts/build.sh clean        # clean all build artifacts
#
# The *-warn variants build with real compiler warnings enabled instead of
# the default fully-quiet (-w) build -- use them when auditing the codebase
# for real bugs; expect several hundred warnings that are legacy-C style
# noise (unused parameters in plugin callbacks, sign-compare, etc.) rather
# than a regression -- see Gamecube/Makefile_Wii's CWARNFLAGS comment.
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

# PROBES= selects which groups of debug probes the debug build compiles in. The
# profiler as a whole stays on; these turn off the expensive per-primitive and
# per-slice instrumentation, which is what makes a debug build slow to run.
#   PROBES=all    everything (the default, same as before)
#   PROBES=light  no primitive tracing, no GX sample rings -- keeps every counter
#   PROBES=min    also drops the per-slice CPU timing
#   PROBES="-DPERF_PROF_TRACE=0"   pass your own defines
# See the sub-gate comments in Gamecube/perf_prof.h for what each group costs.
case "${PROBES:-all}" in
	all)   PROBE_DEFINES="" ;;
	light) PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0" ;;
	min)   PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0" ;;
	*)     PROBE_DEFINES="$PROBES" ;;
esac
[ -n "$PROBE_DEFINES" ] && echo "probe gates: $PROBE_DEFINES"

# The five dependency archives (opengx, zstd, lzma, zlibstatic, chdr) build
# to their own deps/*/lib directories; Gamecube/Makefile_Wii links against
# those paths directly via -L, so no "make install" step is needed here.
# lightrec/lightning are not built at all -- they're prebuilt binaries
# already installed under devkitPPC-r41-2/powerpc-eabi/lib by the setup
# script (they came from this project's own bundled zip, not pacman).
# The Gamecube Makefiles make the .elf depend only on its own objects, not
# on the dependency archives, so an edit confined to e.g. deps/opengx rebuilds
# libOpengx.a but never relinks -- and elf2dol then regenerates a fresh-looking
# .dol from the stale .elf. Drop the .elf when any archive is newer than it.
relink_if_deps_newer() {
	local elf="$1" a
	[ -f "$elf" ] || return 0
	for a in deps/*/lib/*.a; do
		if [ "$a" -nt "$elf" ]; then
			echo "$a is newer than $elf -- forcing relink"
			rm -f "$elf"
			return 0
		fi
	done
}

case "${1:-debug}" in
	debug)
		make opengx.a lightrecWithLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		relink_if_deps_newer Gamecube/WiiSXRX_debug.elf
		make -C Gamecube -f Makefile_Wii EXTRA_CFLAGS="$PROBE_DEFINES"
		echo "Output: Gamecube/WiiSXRX_debug.dol"
		;;
	debug-warn)
		make opengx.a lightrecWithLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		relink_if_deps_newer Gamecube/WiiSXRX_debug.elf
		make -C Gamecube -f Makefile_Wii WARN=1
		echo "Output: Gamecube/WiiSXRX_debug.dol"
		;;
	release)
		make opengx.a lightrecNoLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		relink_if_deps_newer Gamecube/WiiSXRX_Release.elf
		make -C Gamecube -f Makefile_Wii_Release
		echo "Output: Gamecube/WiiSXRX_Release.dol"
		;;
	release-warn)
		make opengx.a lightrecNoLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
		relink_if_deps_newer Gamecube/WiiSXRX_Release.elf
		make -C Gamecube -f Makefile_Wii_Release WARN=1
		echo "Output: Gamecube/WiiSXRX_Release.dol"
		;;
	clean)
		make clean
		;;
	*)
		echo "Usage: $0 [debug|release|debug-warn|release-warn|clean]"
		exit 1
		;;
esac
