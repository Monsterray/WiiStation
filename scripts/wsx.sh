#!/bin/bash
# wsx.sh -- the short way round the build / run / read loop, for people and for models
# working with a small context: one command per step, one screen of output per step.
#
#   scripts/wsx.sh build debug|release [all|light|min|deep|pmc]  build through the devkitPro shell;
#                                                         prints the DOL and its time, or the errors
#   scripts/wsx.sh run NAME [options]                     one unattended Dolphin run, then its summary
#        --dol debug|release|PATH   what to boot (default debug). The DOL is COPIED into the
#                                   run directory first, so a rebuild between queued runs
#                                   cannot change what boots.
#        --secs N                   run length (default 170)
#        --input speech|title|FILE  autoinput script: scripts/autoinput/spyro_<name>.txt or a file
#        --set K=V[,K=V...]         settings on top of the base (gpuPlugin=2 FPS=1 PadType1=1 PadAutoAssign=0)
#        --dsp                      DSP sound path: SoundHwAccel=1 and Dolphin DSP LLE
#        --nodump                   no audio dump
#        --autoboot spyro|bios|none which autoboot file (default spyro)
#        --env VAR=VALUE            extra environment for dolphin_run.sh (CACHE=1, XFB_RAM=1 ...)
#        --card FILE                put FILE in the card's wiisxrx/ folder for the run (repeatable):
#                                   the per-game input scripts a chained autoboot names
#   scripts/wsx.sh chain NAME CHAINFILE [options]         a chained autoboot (several games, one
#                                   boot) from scripts/chains/; stages the input scripts it names,
#                                   runs until the console powers off, prints scripts/chain_table.py.
#                                   Same options as run; --secs is the limit for the whole chain.
#   scripts/wsx.sh summary NAME...                        re-read finished runs (run_summary.py)
#   scripts/wsx.sh table NAME [--detail gpu|tex|cpu|lc|pmc]  a chain's per-game table (chain_table.py)
#   scripts/wsx.sh compare A [B] [--detail GROUP]         two chains, or one A/B chain's pairs (chain_compare.py)
#   scripts/wsx.sh frames A B [--crop OUT.png]            two runs' frame dumps, pixel by pixel (frame_compare.py)
#   scripts/wsx.sh audio A B                              two runs' audio dumps, sample by sample (wav_compare.py --exact)
#   scripts/wsx.sh last                                   summary of the newest run
#   scripts/wsx.sh runs                                   list runs
#
# Runs live in $WSX_RUNS (default <repo>/.runs, gitignored); each holds boot.dol, settings.cfg,
# autoinput.txt, run.info, run.log and everything dolphin_run.sh collects. Several runs in one
# shell line -- `wsx.sh run a ...; wsx.sh run b ...; wsx.sh summary a b` -- give one table.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
RUNS="${WSX_RUNS:-$REPO/.runs}"
P="/c/tools/Dolphin-x64/User/Load/WiiSDSync_paused_by_claude"
DKP_BASH="/c/devkitPro/msys2/usr/bin/bash.exe"
cmd="${1:-}"; shift || true

# Only a run of OUR OWN is a reason not to build: the build would take CPU from it. The
# user's own Dolphin, and another project's runs, have their own profile and are not ours
# to stop for. dolphin_run.sh uses the same directory.
PROFILE="${WSX_PROFILE:-$REPO/.dolphin}"
dolphin_running() {
	powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -eq 'Dolphin.exe' -and \$_.CommandLine -like '*$(basename "$PROFILE")*' }" 2>/dev/null | grep -q .
}

case "$cmd" in
build)
	mode="${1:-debug}"; probes="${2:-light}"
	if dolphin_running && [ -z "${FORCE:-}" ]; then
		echo "one of our runs is going: a build now would skew its timing (FORCE=1 to build anyway)"; exit 2
	fi
	mkdir -p "$RUNS"
	log="$RUNS/build_$mode.log"
	case "$mode" in
		debug*)   out="Gamecube/WiiSXRX_debug.dol"; env="PROBES=$probes" ;;
		release*) out="Gamecube/WiiSXRX_Release.dol"; env="" ;;
		*) echo "build debug|release"; exit 2 ;;
	esac
	before=$(stat -c %Y "$REPO/$out" 2>/dev/null || echo 0)
	"$DKP_BASH" -lc "cd '$REPO' && $env bash scripts/build.sh $mode" > "$log" 2>&1
	rc=$?
	after=$(stat -c %Y "$REPO/$out" 2>/dev/null || echo 0)
	if [ $rc -ne 0 ] || [ "$after" = "$before" ]; then
		echo "BUILD FAILED ($mode, exit $rc, dol unchanged: $([ "$after" = "$before" ] && echo yes || echo no)); first errors:"
		grep -n -B1 -A4 -E "error:|Error [0-9]|undefined reference" "$log" | head -30
		echo "(full log: $log)"; exit 1
	fi
	w=$(grep -c "warning:" "$log")
	echo "built $out $(date -d @"$after" +%T 2>/dev/null) ($w warnings, log $log)"
	grep -E "warning:" "$log" | grep -E "ratectl|sdl\.c|cube\.c|oldGpuFps|perf_prof" | head -5
	;;

run)
	name="${1:-}"; shift || true
	[ -n "$name" ] || { echo "run NAME [options]"; exit 2; }
	dol="debug"; secs=170; input="speech"; sets=""; dsp=""; dump=1; aboot="spyro"; extra=(); cards=()
	while [ $# -gt 0 ]; do
		case "$1" in
			--dol) dol="$2"; shift 2 ;;
			--secs) secs="$2"; shift 2 ;;
			--input) input="$2"; shift 2 ;;
			--set) sets="${sets:+$sets,}$2"; shift 2 ;;
			--dsp) dsp=1; shift ;;
			--nodump) dump=0; shift ;;
			--autoboot) aboot="$2"; shift 2 ;;
			--env) extra+=("$2"); shift 2 ;;
			--card) cards+=("$2"); shift 2 ;;
			*) echo "unknown option $1"; exit 2 ;;
		esac
	done
	case "$dol" in
		debug)   dolsrc="$REPO/Gamecube/WiiSXRX_debug.dol" ;;
		release) dolsrc="$REPO/Gamecube/WiiSXRX_Release.dol" ;;
		*)       dolsrc="$dol" ;;
	esac
	[ -f "$dolsrc" ] || { echo "no such dol: $dolsrc"; exit 2; }
	case "$input" in
		speech|title) insrc="$REPO/scripts/autoinput/spyro_$input.txt" ;;
		*) insrc="$input" ;;
	esac
	[ -f "$insrc" ] || { echo "no such input script: $insrc"; exit 2; }
	case "$aboot" in
		spyro) ab="$P/autoboot_spyro.txt" ;;
		bios)  ab="$P/autoboot_bios.txt" ;;
		none)  ab="" ;;
		*)     ab="$aboot" ;;
	esac
	dir="$RUNS/$name"; mkdir -p "$dir"
	cp "$dolsrc" "$dir/boot.dol"
	cp "$insrc" "$dir/autoinput.txt"
	rm -rf "$dir/card"
	for c in "${cards[@]:-}"; do [ -n "$c" ] && mkdir -p "$dir/card" && cp "$c" "$dir/card/"; done
	{
		printf 'gpuPlugin = 2\nFPS = 1\nPadType1 = 1\nPadAutoAssign = 0\n'
		[ -n "$dsp" ] && printf 'SoundHwAccel = 1\n'
		[ -n "$sets" ] && echo "$sets" | tr ',' '\n' | sed -E 's/^ *([^= ]+) *= *(.*)$/\1 = \2/'
	} > "$dir/settings.cfg"
	envline="AUDIO_DUMP=$dump${dsp:+ DSP_LLE=1}${extra[@]:+ ${extra[*]}}"
	{
		echo "dol=$dol ($(basename "$dolsrc"), $(stat -c %y "$dolsrc" | cut -c1-19)) secs=$secs input=$(basename "$insrc") autoboot=$aboot"
		echo "settings: $(grep -v -E '^(gpuPlugin|FPS|PadType1|PadAutoAssign) ' "$dir/settings.cfg" | tr '\n' ' ')"
		echo "env: $envline  commit: $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null)$(git -C "$REPO" diff --quiet 2>/dev/null || echo '+dirty')"
	} > "$dir/run.info"
	echo "run $name: $(head -1 "$dir/run.info")"
	if [ "$dump" = 1 ]; then export AUDIO_DUMP=1; else unset AUDIO_DUMP; fi
	[ -n "$dsp" ] && export DSP_LLE=1
	for e in "${extra[@]:-}"; do [ -n "$e" ] && export "$e"; done
	DOL="$dir/boot.dol" bash "$REPO/scripts/dolphin_run.sh" "$dir" "$secs" "$dir/autoinput.txt" "$dir/settings.cfg" $ab > "$dir/run.log" 2>&1
	rc=$?
	grep -E "refusing|no such|faults in dolphin.log: [1-9]|PANIC|Unknown instruction" "$dir/run.log" | head -5
	[ $rc -ne 0 ] && echo "dolphin_run.sh exit $rc (see $dir/run.log)"
	python "$REPO/scripts/run_summary.py" "$dir"
	;;

chain)
	# A chain file names its input scripts as sd:/wiisxrx/<file>; each is scripts/autoinput/<file>.
	name="${1:-}"; cf="${2:-}"; shift 2 || { echo "chain NAME CHAINFILE [options]"; exit 2; }
	[ -f "$cf" ] || cf="$REPO/scripts/chains/$cf"
	[ -f "$cf" ] || { echo "no such chain file: $cf"; exit 2; }
	cardargs=()
	for f in $(tr -d '\r' < "$cf" | sed -n -E 's|^[0-9]+[[:space:]]+sd:/wiisxrx/([^[:space:]]+).*|\1|p' | sort -u); do
		[ -f "$REPO/scripts/autoinput/$f" ] || { echo "chain names sd:/wiisxrx/$f: no scripts/autoinput/$f"; exit 2; }
		cardargs+=(--card "$REPO/scripts/autoinput/$f")
	done
	bash "$0" run "$name" --autoboot "$cf" --input "$P/autoinput_none.txt" --nodump --env FRAMES_DUMP=False ${cardargs[@]+"${cardargs[@]}"} "$@" | grep -v "^  "
	python "$REPO/scripts/chain_table.py" "$RUNS/$name"
	;;

table)
	n="$1"; shift; python "$REPO/scripts/chain_table.py" "$RUNS/$n" "$@"
	;;
compare)
	a=(); for x in "$@"; do case "$x" in --*|gpu|tex|cpu|lc|pmc) a+=("$x") ;; *) a+=("$RUNS/$x") ;; esac; done
	python "$REPO/scripts/chain_compare.py" "${a[@]}"
	;;
frames)
	a="$1"; b="$2"; shift 2; python "$REPO/scripts/frame_compare.py" "$RUNS/$a" "$RUNS/$b" "$@"
	;;
audio)
	wa=$(ls "$RUNS/$1"/*dspdump1.wav 2>/dev/null | head -1); wb=$(ls "$RUNS/$2"/*dspdump1.wav 2>/dev/null | head -1)
	[ -n "$wa" ] && [ -n "$wb" ] || { echo "no *dspdump1.wav in both runs (run them without --nodump)"; exit 2; }
	python "$REPO/scripts/wav_compare.py" "$wa" "$wb" --exact | tail -3
	;;

summary)
	dirs=(); for n in "$@"; do [ -d "$n" ] && dirs+=("$n") || dirs+=("$RUNS/$n"); done
	python "$REPO/scripts/run_summary.py" "${dirs[@]}"
	;;

last)
	d=$(ls -td "$RUNS"/*/ 2>/dev/null | head -1)
	[ -n "$d" ] && python "$REPO/scripts/run_summary.py" "$d" || echo "no runs in $RUNS"
	;;

runs)
	ls -t "$RUNS" 2>/dev/null | grep -v '\.log$' | while read -r n; do
		printf '%-28s %s\n' "$n" "$(head -1 "$RUNS/$n/run.info" 2>/dev/null)"
	done
	;;

*)
	sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
	;;
esac
