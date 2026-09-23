#!/usr/bin/env bash
# padtest_dx.sh [--ct 0|1] [--frames] [--rom DIR]
#
# Run PadTest DX (github.com/Monsterray/padtest, built in C:\projects\padtest) in WiiStation
# with the scripted pad sweep, then decode what the ROM received (scripts/padtest_dx.py).
#   --ct N     ControllerType: 0 = Standard (digital), 1 = Analog. Default 1.
#   --frames   also dump Dolphin frames, to .runs/padtest_dx_ctN/frames.
#   --rom DIR  folder with padtest.bin + padtest.cue. Default C:/projects/padtest/build.
# The ROM goes on this repo's test profile card only (.dolphin), never the shared SD folder.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ct=1; rom=/c/projects/padtest/build; extra=()
while [ $# -gt 0 ]; do
	case "$1" in
		--ct) ct="$2"; shift ;;
		--frames) extra=(--env FRAMES_DUMP=True --env KEEP=100000) ;;
		--rom) rom="$2"; shift ;;
		*) echo "unknown option $1"; exit 2 ;;
	esac
	shift
done
name=padtest_dx_ct$ct
card="${WSX_PROFILE:-$REPO/.dolphin}/Load/WiiSDSync/wiisxrx/isos/PadTestDX"
mkdir -p "$card" "$REPO/.runs"
cp "$rom/padtest.bin" "$rom/padtest.cue" "$card/"
chain="$REPO/.runs/$name.chain.txt"
printf 'CHAIN\n2400 sd:/wiisxrx/padtest_dx_sweep.txt PadAutoAssign=1 ControllerType=%s\nsd:/wiisxrx/isos/PadTestDX\npadtest.cue\n' "$ct" > "$chain"
rm -rf "$REPO/.runs/$name"
bash "$REPO/scripts/wsx.sh" chain "$name" "$chain" --secs 500 ${extra[@]+"${extra[@]}"} > "$REPO/.runs/$name.out" 2>&1
python "$REPO/scripts/sdimage_read.py" "${WSX_PROFILE:-$REPO/.dolphin}/Load/WiiSD.raw" wiisxrx/vram.bin "$REPO/.runs/$name/vram.bin" > /dev/null
python "$REPO/scripts/padtest_dx.py" "$REPO/.runs/$name/vram.bin" --sweep
