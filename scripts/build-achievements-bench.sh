#!/usr/bin/env bash
# Bench DOL for the achievement hardware tests: the debug build with the test-only
# ACHIEVEMENTS_BENCH hooks (Gamecube/achievements_bench.cpp: card isolation and the
# achievement-bench perf.log lines). Product builds never define it.
#   PROBES='-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0'  optional -D list; the default is the light set
set -euo pipefail
cd "$(dirname "$0")/.."
probes=${PROBES:-"-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0"}
case "$probes" in
	-D*) ;;
	*) echo "PROBES must be a list of -D flags for the bench build, not a preset: $probes" >&2; exit 2 ;;
esac
export PROBES="$probes -DACHIEVEMENTS_BENCH"
exec bash scripts/build.sh debug
