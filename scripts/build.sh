#!/usr/bin/env bash
# Build WiiStation for Wii. Use setup-devkitpro-windows.sh on Windows or
# setup-devkitpro-local.sh for an isolated macOS/Linux toolchain.
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
#   LIBOGC=2 scripts/build.sh release  # libogc2 + devkitPPC r41-2; outputs get _ogc2
#                                      # (WiiSXRX_Release_ogc2.dol, build_release_ogc2/)
#
# The *-warn variants build with real compiler warnings enabled instead of
# the default fully-quiet (-w) build -- use them when auditing the codebase
# for real bugs; expect several hundred warnings that are legacy-C style
# noise (unused parameters in plugin callbacks, sign-compare, etc.) rather
# than a regression -- see Gamecube/Makefile_Wii's CWARNFLAGS comment.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$REPO_ROOT" == *" "* ]]; then
	if [ "${1:-debug}" = clean ]; then
		echo "Run clean from a checkout without spaces; isolated builds clean themselves." >&2
		exit 1
	fi
	# devkitPro's make rules cannot quote CURDIR. Snapshot source (including working
	# edits) into a space-free directory; ignored saves, quarantine and build caches
	# are excluded. Copy only successful build outputs back to the user's checkout.
	task_build=$(mktemp -d /tmp/wiistation-build.XXXXXX)
	trap 'rm -rf "$task_build"' EXIT
	echo "Building a source snapshot in $task_build (checkout path contains spaces)."
	git -C "$REPO_ROOT" ls-files --cached --others --exclude-standard -z |
	while IFS= read -r -d '' source_file; do
		if [ -f "$REPO_ROOT/$source_file" ] || [ -L "$REPO_ROOT/$source_file" ]; then
			printf '%s\0' "$source_file"
		fi
	done | tar -C "$REPO_ROOT" --null -T - -cf - | tar -C "$task_build" -xf -
	bash "$task_build/scripts/build.sh" "$@"
	for artifact in "$task_build"/Gamecube/*.dol "$task_build"/Gamecube/*.elf; do
		[ ! -f "$artifact" ] || cp "$artifact" "$REPO_ROOT/Gamecube/"
	done
	echo "Build outputs copied to $REPO_ROOT/Gamecube."
	exit 0
fi

# LIBOGC picks the libogc / devkitPPC pair the Makefiles build against:
#   3 (default)  devkitPro's libogc v3 + devkitPPC r50 ($DEVKITPRO/devkitPPC); dependency archives
#                are built in build_ogc3/deps (5.13.0).
#   2            libogc2 + devkitPPC-r41-2. Pinned, not the pacman-managed $DEVKITPRO/devkitPPC,
#                which is incompatible with libogc2 -- see scripts/setup-devkitpro-windows.sh. Its
#                outputs get the _ogc2 suffix (build dirs, .elf/.dol), so nothing mixes.
LIBOGC="${LIBOGC:-3}"
case "$LIBOGC" in
	2) DEVKITPPC_NAME=devkitPPC-r41-2 ;;
	3) DEVKITPPC_NAME=devkitPPC ;;
	*) echo "ERROR: LIBOGC must be 2 or 3 (got '$LIBOGC')."; exit 1 ;;
esac
export LIBOGC
# Windows: force-assign. The login shell here carries a stale DEVKITPRO=/opt/devkitpro and may carry
# the newer pacman devkitPPC; inheriting either breaks the build (AGENTS.md "Building").
# macOS/Linux: an isolated toolchain (setup-devkitpro-local.sh) may live elsewhere, so a set value wins.
case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*)
		export DEVKITPRO=/c/devkitPro
		export DEVKITPPC="$DEVKITPRO/$DEVKITPPC_NAME" ;;
	*)
		export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
		export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/$DEVKITPPC_NAME}" ;;
esac
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
OGC_SUFFIX="_ogc2"
DEPS_ROOT="deps"
if [ "$LIBOGC" = 3 ]; then
	OGC_SUFFIX=""
	DEPS_ROOT="build_ogc3/deps"
fi

if [ ! -x "$DEVKITPPC/bin/powerpc-eabi-gcc" ] && [ ! -x "$DEVKITPPC/bin/powerpc-eabi-gcc.exe" ]; then
	echo "$DEVKITPPC_NAME not found at $DEVKITPPC. Use setup-devkitpro-windows.sh on Windows or setup-devkitpro-local.sh on macOS/Linux."
	exit 1
fi
if [ "$LIBOGC" = 2 ]; then
	bash "$REPO_ROOT/scripts/check-wii-libraries.sh" "$DEVKITPRO/libogc2"
	if ! "$DEVKITPPC/bin/powerpc-eabi-gcc" --version | head -n 1 | grep -q 'release 41'; then
		echo "WiiStation's bundled libraries require devkitPPC release 41. Set DEVKITPPC to the pinned compiler." >&2
		exit 1
	fi
fi
if ! command -v make >/dev/null 2>&1; then
	echo "make not found on PATH. Run this from devkitPro's own MSYS2 shell"
	echo "(Start Menu -> devkitPro -> MSys2, or C:\\devkitPro\\msys2\\msys2_shell.bat)."
	exit 1
fi

cd "$REPO_ROOT"

# libogc v3 links two pad libraries that are not committed: libwupc (fetched at its pinned
# commit and patched, deps/wupc.mk) and libwiidrc (source in deps/wiidrc). Build them once.
if [ "$LIBOGC" = 3 ]; then
	[ -f deps/ext/lib/libwupc.a ] || { bash scripts/fetch_deps.sh wupc && make -f deps/wupc.mk; }
	[ -f deps/wiidrc/lib/libwiidrc.a ] || make -C deps/wiidrc
fi

# The version lives in Gamecube/version.h and in the HBC meta.xml; warn when they differ.
v_h=$(sed -n 's/^#define WIISTATION_VERSION "\(.*\)"/\1/p' Gamecube/version.h)
v_x=$(sed -n 's/.*<version>\(.*\)<\/version>.*/\1/p' Gamecube/release/apps/WiiStation/meta.xml)
[ "$v_h" = "$v_x" ] || echo "warning: version.h says $v_h, meta.xml says $v_x" >&2

# PROBES= selects which groups of debug probes the debug build compiles in. The
# profiler as a whole stays on; these turn off the expensive per-primitive and
# per-slice instrumentation, which is what makes a debug build slow to run.
#   PROBES=all    everything (the default, same as before)
#   PROBES=light  no primitive tracing, no GX sample rings -- keeps every counter
#   PROBES=min    also drops the per-slice CPU timing
#   PROBES=deep   light plus the GPU, GTE and SPU splits (chain_table.py --detail)
#   PROBES=pmc    light plus Broadway's performance counters (hardware sessions)
#   PROBES=hprof  light plus the sampling profiler (where the Wii's CPU is, per function)
#   PROBES="-DPERF_PROF_TRACE=0"   pass your own defines
# See the sub-gate comments in Gamecube/perf_prof.h for what each group costs.
case "${PROBES:-all}" in
	all)   PROBE_DEFINES="" ;;
	light) PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0" ;;
	min)   PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_CPU=0" ;;
	# deep: every split used to rank the subsystems (Docs/GPU_CPU_PLAN.md) -- the GPU, the GTE
	# and the SPU split on top of light. What chain_table.py --detail reads. Costs ~1% of wall.
	deep)  PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_GPUSPLIT=1 -DPERF_PROF_GTE=1 -DPERF_PROF_SPU=1" ;;
	# pmc: light plus Broadway's performance counters, for a hardware session.
	pmc)   PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_PMC=1" ;;
	# hprof: light plus the sampling profiler (Gamecube/hprof.c, hprof_NN.bin per game,
	# scripts/hprof_view.py). Uses PMC1, so not with pmc.
	hprof) PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_HPROF=1" ;;
	# texcheck: deep plus the texture-cache staleness oracle (gpuTexture.c, "texcheck:" in
	# perf.log): every cache hit is checked against VRAM. Slow; for correctness runs only.
	# Its own test: add -DTEXCHECK_SELFTEST=1 (a custom PROBES list); 1 hit in 64 then gets a
	# changed texel, and texcheck: stale must equal hits/64 (MediEvil: 6668 of 426764).
	texcheck) PROBE_DEFINES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_GPUSPLIT=1 -DPERF_PROF_GTE=1 -DPERF_PROF_SPU=1 -DPERF_PROF_TEXCHECK=1" ;;
	*)     PROBE_DEFINES="$PROBES" ;;
esac
[ -n "$PROBE_DEFINES" ] && echo "probe gates: $PROBE_DEFINES"

# make compares timestamps, not compiler flags, so changing PROBES on its own rebuilds
# nothing and you silently get the previous build back. Remember what the objects were
# compiled with and clean when it changes.
PROBE_STAMP="Gamecube/build_debug${OGC_SUFFIX}/.probe_defines"
# Debug builds only: a release build does not use the gates, and cleaning the debug tree
# from it threw away WiiSXRX_debug.dol whenever the last debug build had PROBES set.
if [ "${1:-debug}" != "${1#debug}" ] && [ -d "Gamecube/build_debug${OGC_SUFFIX}" ] && [ "$(cat "$PROBE_STAMP" 2>/dev/null)" != "$PROBE_DEFINES" ]; then
	echo "probe gates changed since the last build -- cleaning so the change takes effect"
	make -C Gamecube -f Makefile_Wii clean >/dev/null 2>&1 || true
	rm -rf "$DEPS_ROOT"/opengx/obj_prof   # the profiling OpenGX takes the same gates
fi

# The five dependency archives (opengx, zstd, lzma, zlibstatic, chdr) build
# to their own deps/*/lib directories; Gamecube/Makefile_Wii links against
# those paths directly via -L, so no "make install" step is needed here.
# lightrec (deps/lightrec) and, since 2026-09-30, GNU Lightning (deps/lightning) are built here;
# the prebuilt liblightning.a still in devkitPPC-r41-2/powerpc-eabi/lib is no longer linked
# (deps/lightning/lib comes first in the -L list).
# The Gamecube Makefiles make the .elf depend only on its own objects, not
# on the dependency archives, so an edit confined to e.g. deps/opengx rebuilds
# libOpengx.a but never relinks -- and elf2dol then regenerates a fresh-looking
# .dol from the stale .elf. Drop the .elf when any archive is newer than it.
relink_if_deps_newer() {
	local elf="$1" a
	[ -f "$elf" ] || return 0
	for a in "$DEPS_ROOT"/*/lib/*.a; do
		if [ "$a" -nt "$elf" ]; then
			echo "$a is newer than $elf -- forcing relink"
			rm -f "$elf"
			return 0
		fi
	done
}

case "${1:-debug}" in
	debug)
		# PROBE_DEFINES reaches the profiling OpenGX library too: its per-draw probes are
		# gated like the plugin's, and a library built with other gates than the plugin
		# reads state the plugin never writes.
		make opengx.a lightrecWithLog.a lightning.a zstd.a lzma.a zlibstatic.a chdrstatic.a hbcagent.a smb2.a PROBE_DEFINES="$PROBE_DEFINES"
		relink_if_deps_newer Gamecube/WiiSXRX_debug${OGC_SUFFIX}.elf
		make -C Gamecube -f Makefile_Wii EXTRA_CFLAGS="$PROBE_DEFINES"
		mkdir -p Gamecube/build_debug${OGC_SUFFIX} && printf '%s' "$PROBE_DEFINES" > "$PROBE_STAMP"
		echo "Output: Gamecube/WiiSXRX_debug${OGC_SUFFIX}.dol"
		;;
	debug-warn)
		make opengx.a lightrecWithLog.a lightning.a zstd.a lzma.a zlibstatic.a chdrstatic.a hbcagent.a smb2.a
		relink_if_deps_newer Gamecube/WiiSXRX_debug${OGC_SUFFIX}.elf
		make -C Gamecube -f Makefile_Wii WARN=1
		echo "Output: Gamecube/WiiSXRX_debug${OGC_SUFFIX}.dol"
		;;
	release)
		make opengx.a lightrecNoLog.a lightning.a zstd.a lzma.a zlibstatic.a chdrstatic.a hbcagent.a smb2.a
		relink_if_deps_newer Gamecube/WiiSXRX_Release${OGC_SUFFIX}.elf
		make -C Gamecube -f Makefile_Wii_Release
		echo "Output: Gamecube/WiiSXRX_Release${OGC_SUFFIX}.dol"
		;;
	release-warn)
		make opengx.a lightrecNoLog.a lightning.a zstd.a lzma.a zlibstatic.a chdrstatic.a hbcagent.a smb2.a
		relink_if_deps_newer Gamecube/WiiSXRX_Release${OGC_SUFFIX}.elf
		make -C Gamecube -f Makefile_Wii_Release WARN=1
		echo "Output: Gamecube/WiiSXRX_Release${OGC_SUFFIX}.dol"
		;;
	clean)
		make clean
		;;
	*)
		echo "Usage: $0 [debug|release|debug-warn|release-warn|clean]"
		exit 1
		;;
esac
