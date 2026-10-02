#!/usr/bin/env bash
# padtest_dx.sh [--cells a,b,..] [--frames] [--rom DIR] [-v] [wsx.sh run options]
#
# The controller matrix: PadTest DX (github.com/Monsterray/padtest, built in
# C:\projects\padtest) once per controller setup -- ports None / GameCube pad / Multitap,
# multitap slots filled or empty, ControllerType 0/1/2, and a multitap -> pad -> multitap
# switch while the ROM runs -- all in ONE Dolphin boot (a CHAIN autoboot), then one PASS/FAIL
# line per cell from what the ROM received (scripts/padtest_dx.py; `padtest_dx.py cells`
# lists them). About two minutes.
#   --cells    only these cells (comma list of names)
#   --frames   also dump Dolphin frames, to .runs/padtest_dx/frames
#   --rom DIR  folder with padtest.bin + padtest.cue. Default C:/projects/padtest/build.
#   -v         print what each cell's ROM received, not just the verdict
# Other options go to wsx.sh run (e.g. --dol release). The ROM goes on the test profile's
# card only (.dolphin), never the shared SD folder.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUNS="${WSX_RUNS:-$REPO/.runs}"
cells=""; rom=/c/projects/padtest/build; extra=(); verbose=""
while [ $# -gt 0 ]; do
	case "$1" in
		--cells) cells="$2"; shift ;;
		--frames) extra+=(--env FRAMES_DUMP=True --env KEEP=100000) ;;
		--rom) rom="$2"; shift ;;
		-v) verbose=-v ;;
		*) extra+=("$1") ;;
	esac
	shift
done
name=padtest_dx
card="${WSX_PROFILE:-$REPO/.dolphin}/Load/WiiSDSync/wiisxrx/isos/PadTestDX"
mkdir -p "$card" "$RUNS"
cp "$rom/padtest.bin" "$rom/padtest.cue" "$card/"
chain="$RUNS/$name.chain.txt"
python "$REPO/scripts/padtest_dx.py" chain "$chain" "$cells"
# Wall time: Dolphin runs the ROM at about full speed; the chain's own loads add ~3 s a cell
vbl=$(awk '/^[0-9]+ sd:/ {s += $1; n++} END {print s, n}' "$chain")
secs=$(( ${vbl% *} / 50 + ${vbl#* } * 5 + 60 ))
rm -rf "$RUNS/$name"
t0=$(date +%s)
bash "$REPO/scripts/wsx.sh" chain "$name" "$chain" --secs "$secs" ${extra[@]+"${extra[@]}"} > "$RUNS/$name.out" 2>&1 || true
echo "dolphin: $(( $(date +%s) - t0 )) s for ${vbl#* } cells, ${vbl% *} vblanks (log $RUNS/$name.out)"
python "$REPO/scripts/padtest_dx.py" judge "$RUNS/$name" "$cells" $verbose
