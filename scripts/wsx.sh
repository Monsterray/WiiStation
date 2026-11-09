#!/bin/bash
# wsx.sh -- the short way round the build / run / read loop, for people and for models
# working with a small context: one command per step, one screen of output per step.
#
#   scripts/wsx.sh build debug|release [all|light|ach|speed|min|deep|pmc|hprof|play]  build through the devkitPro shell;
#                                                         prints the DOL and its time, or the errors
#                                                         (play: WiiSXRX_play.dol, Docs/DEBUG_PLAY.md)
#   scripts/wsx.sh run NAME [options]                     one unattended Dolphin run, then its summary
#        --dol debug|play|release|PATH   what to boot (default debug). The DOL is COPIED into the
#                                   run directory first, so a rebuild between queued runs
#                                   cannot change what boots.
#        --secs N                   run length (default 170)
#        --input speech|title|FILE  autoinput script: scripts/autoinput/spyro_<name>.txt or a file
#        --set K=V[,K=V...]         settings on top of the base (gpuPlugin=2 FPS=1 PadType1=1 PadAutoAssign=0)
#        --dsp                      DSP sound path: SoundDriver=1 and Dolphin DSP LLE
#        --nodump                   no audio dump
#        --fast                     Dolphin as fast as the PC allows (about 2x): frames and guest
#                                   time are unchanged, and SoundRateControl=0 SoundTempo=0 make
#                                   the audio dump bit-identical to a normal-speed run (with the
#                                   rate control on, two Dolphin runs agree for a minute or two at
#                                   best: skill references/measuring.md, scripts/aq_cmp.py).
#                                   --secs is wall time, so a fast run covers about twice the game.
#        --autoboot spyro|bios|none which autoboot file (default spyro)
#        --env VAR=VALUE            extra environment for dolphin_run.sh (CACHE=1, XFB_RAM=1 ...)
#        --card FILE|DIR            put FILE (or the folder DIR) in the card's wiistation/ folder for
#                                   the run (repeatable): the per-game input scripts a chained
#                                   autoboot names, or a settings/ folder with a game's own file
#        --settings FILE            the lines of FILE after the base and --set (a whole settings.ini)
#        --base NAME                the verdict also compares with an earlier run of the same test:
#                                   speed per game, frames and audio
#   scripts/wsx.sh chain NAME CHAINFILE [options]         a chained autoboot (several games, one
#                                   boot) from scripts/chains/; stages the input scripts it names,
#                                   runs until the console powers off, prints scripts/chain_table.py.
#                                   Same options as run; --secs is the limit for the whole chain.
#   scripts/wsx.sh state NAME GAME --at V [--rec REC] [--ct 0|1] [options]
#                                   boot GAME (part of its folder name in chains/all.txt, or the game
#                                   of recording REC), play REC if given, and save the game at vblank V
#                                   as save state NAME on the test card (Docs/SAVE_STATES.md)
#   scripts/wsx.sh abstate RUN STATE VBL SETS... [--rec REC] [options]
#                                   one boot: each SETS ("K=V,K=V", or - for none) runs VBL vblanks
#                                   from save state STATE; prints the table. --rec plays REC on.
#   Every run, chain and menu comparison ENDS with its verdict: "VERDICT NAME: OK|CHECK|
#                                   NO-RESULTS|DIFFERS | speed per game" and <= 6 lines (where to
#                                   look). Also .runs/NAME/verdict.txt and verdict.json. Read it first.
#   scripts/wsx.sh wait NAME [--timeout S]                wait for a run started in the background
#                                   and print its verdict (default 540 s; exit 4 = still running)
#   scripts/wsx.sh summary NAME...                        re-read finished runs (run_summary.py)
#   scripts/wsx.sh table NAME [--detail gpu|tex|cpu|lc|pmc|upl]  a chain's per-game table (chain_table.py)
#   scripts/wsx.sh compare A [B] [--detail GROUP] [--explain]  two chains, or one A/B chain's pairs
#                                   (chain_compare.py); --explain adds <= 8 lines on what changed (llm)
#   scripts/wsx.sh triage NAME [question]                 condense a run's logs with the local LLM into
#                                   <= 15 lines, saved to .runs/NAME/triage.txt (run_triage.py)
#                                   run/chain/wii_lab/wii_crash_job do this by themselves when a run
#                                   looks wrong (a "triage:" block); WSX_NO_LLM=1 turns that off
#   scripts/wsx.sh frames A B [--crop OUT.png]            two runs' frame dumps, pixel by pixel (frame_compare.py)
#   scripts/wsx.sh menu NAME --dol A.dol [--dol2 B.dol] (--page N | --input FILE) [--secs S] [--set K=V,...] [--pad]
#                                   guarded menu comparison (menu_compare.py): one or two builds,
#                                   every frame kept; Wiimote Source=0 during the run and restored
#                                   byte for byte after. --page compares the first settled frame;
#                                   --input walks frame by frame and warns if the menu never moved.
#                                   Output: .runs/menu_NAME/ (before_after.png, input.txt)
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
P="$REPO/scripts/runfiles"                # the autoboot, input and settings presets
LIB="${WSX_LIBRARY:-/c/wiistation-lib}"   # the game library; save states are in $LIB/states
DKP_BASH="/c/devkitPro/msys2/usr/bin/bash.exe"
cmd="${1:-}"; shift || true

# Only a run of OUR OWN is a reason not to build: the build would take CPU from it. The user's
# own Dolphin, and another project's runs, have their own profile and are not ours to stop for.
dolphin_running() { bash "$REPO/scripts/profile.sh" dolphin-busy ANY; }

# The summarise/compare steps below are Python. Plain `python` does not exist in the
# devkitPro MSYS2 bash on a stock machine (`python3` there is a dead WindowsApps stub),
# so without this every post-processing step silently no-ops. An exported PYTHON wins.
. "$(dirname "${BASH_SOURCE[0]}")/find_python.sh"   # $PYTHON, else wii_paths.py, else python

# The automatic triage after a run (run_triage.py --auto: only when the run looks wrong, a
# "triage:" block of <= 10 lines). It never changes this script's exit code; WSX_NO_LLM=1 is off.
auto_triage() {
	[ -n "${WSX_NO_LLM:-}" ] && return 0
	"$PYTHON" "$REPO/scripts/run_triage.py" --auto "$1" || true
}

# The last lines of every run, chain and menu comparison: "VERDICT NAME: OK|CHECK|NO-RESULTS|
# DIFFERS | speed per game" and at most 6 lines under it (run_verdict.py), also in
# RUN_DIR/verdict.txt and verdict.json. Read it before any log. verdict DIR [BASE] [args...];
# BASE is a run name or directory (--base): speed, frames and audio against that run.
verdict() {
	local d="$1" b="${2:-}"; shift 2 || shift $#
	if [ -n "$b" ] && [ ! -d "$b" ]; then b="$RUNS/$b"; fi
	"$PYTHON" "$REPO/scripts/run_verdict.py" "$d" ${b:+--base "$b"} "$@" || true
}

# menu: put the profile's WiimoteNew.ini back from its backup and check it by checksum. The
# menu branch sets MENU_INI, MENU_BAK and MENU_SHA (the checksum of the original) before it
# changes anything, and this runs on every way out of the script (trap EXIT).
menu_restore_ini() {
	[ -n "${MENU_BAK:-}" ] && [ -e "$MENU_BAK" ] || return 0
	if cp -f "$MENU_BAK" "$MENU_INI" && [ "$(sha256sum "$MENU_INI" | cut -d' ' -f1)" = "$MENU_SHA" ]; then
		rm -f "$MENU_BAK"; echo "WiimoteNew.ini restored (sha256 ${MENU_SHA:0:12}...)"
	else
		echo "WARNING: WiimoteNew.ini is not restored; the original is in $MENU_BAK"
	fi
}

case "$cmd" in
build)
	mode="${1:-debug}"; probes="${2:-light}"
	if dolphin_running && [ -z "${FORCE:-}" ]; then
		echo "one of our runs is going: a build now would skew its timing (FORCE=1 to build anyway)"; exit 2
	fi
	mkdir -p "$RUNS"
	# LIBOGC=2 (environment): the old libogc2 build, outputs WiiSXRX_*_ogc2.dol (scripts/build.sh)
	sfx=""; [ "${LIBOGC:-3}" = 2 ] && sfx="_ogc2"
	log="$RUNS/build_$mode$sfx.log"
	case "$mode" in
		debug*)   out="Gamecube/WiiSXRX_$([ "$probes" = play ] && echo play || echo debug)$sfx.dol"; env="LIBOGC=${LIBOGC:-3} PROBES='$probes'" ;;   # quoted: a custom list of -D flags has spaces
		release*) out="Gamecube/WiiSXRX_Release$sfx.dol"; env="LIBOGC=${LIBOGC:-3}" ;;
		*) echo "build debug|release"; exit 2 ;;
	esac
	# The menu's host tests (engine layout, page tables, explorer, covers) run before every build; a failure stops it.
	# WSX_NO_HOSTTEST=1 skips them.
	if [ -z "${WSX_NO_HOSTTEST:-}" ]; then
		sh "$REPO/tests/menu_engine/run.sh" > "$RUNS/hosttest.log" 2>&1
		hrc=$?
		if [ $hrc -ne 0 ]; then
			echo "HOST TESTS FAILED (exit $hrc, WSX_NO_HOSTTEST=1 skips): see $RUNS/hosttest.log"; grep -E "FAIL|fail|error" "$RUNS/hosttest.log" | head -10; exit 1
		fi
		echo "host tests: $(grep -cE '0 failed|0 checks failed' "$RUNS/hosttest.log") suites passed"
	fi
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
	dol="debug"; secs=170; input="speech"; sets=""; dsp=""; dump=1; aboot="spyro"; extra=(); cards=(); fast=""; setfile=""; base=""
	while [ $# -gt 0 ]; do
		case "$1" in
			--base) base="$2"; shift 2 ;;
			--dol) dol="$2"; shift 2 ;;
			--secs) secs="$2"; shift 2 ;;
			--input) input="$2"; shift 2 ;;
			--set) sets="${sets:+$sets,}$2"; shift 2 ;;
			--dsp) dsp=1; shift ;;
			--nodump) dump=0; shift ;;
			--fast) fast=1; shift ;;
			--autoboot) aboot="$2"; shift 2 ;;
			--env) extra+=("$2"); shift 2 ;;
			--card) cards+=("$2"); shift 2 ;;
			--settings) setfile="$2"; shift 2 ;;
			*) echo "unknown option $1"; exit 2 ;;
		esac
	done
	[ -z "$setfile" ] || [ -f "$setfile" ] || { echo "no such settings file: $setfile"; exit 2; }
	case "$dol" in
		debug)   dolsrc="$REPO/Gamecube/WiiSXRX_debug.dol" ;;
		play)    dolsrc="$REPO/Gamecube/WiiSXRX_play.dol" ;;
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
	rm -rf "$dir/card" "$dir/frames"
	# what the last run under this name collected: a result this run does not produce must not stay behind
	rm -f "$dir"/*.log "$dir"/vram*.bin "$dir"/vram*.png "$dir"/xfb*.bin "$dir"/*.wav
	rm -f "$dir/dolphin.rc" "$dir/triage.txt" "$dir/verdict.json" "$dir/verdict.txt"
	date +%s > "$dir/running"   # until run_verdict.py writes verdict.json (wsx.sh wait)
	for c in "${cards[@]:-}"; do [ -n "$c" ] && mkdir -p "$dir/card" && cp -r "$c" "$dir/card/"; done
	{
		printf 'gpuPlugin = 2\nFPS = 1\nPadType1 = 1\nPadAutoAssign = 0\n'
		[ -n "$dsp" ] && printf 'SoundDriver = 1\n'
		[ -n "$fast" ] && printf 'SoundRateControl = 0\nSoundTempo = 0\n'   # before --set, which may override
		[ -n "$sets" ] && echo "$sets" | tr ',' '\n' | sed -E 's/^ *([^= ]+) *= *(.*)$/\1 = \2/'
		[ -n "$setfile" ] && cat "$setfile"
	} > "$dir/settings.cfg"
	envline="AUDIO_DUMP=$dump${dsp:+ DSP_LLE=1}${fast:+ FAST=1}${extra[@]:+ ${extra[*]}}"
	{
		echo "dol=$dol ($(basename "$dolsrc"), $(stat -c %y "$dolsrc" | cut -c1-19)) secs=$secs input=$(basename "$insrc") autoboot=$aboot"
		echo "settings: $(grep -v -E '^(gpuPlugin|FPS|PadType1|PadAutoAssign) ' "$dir/settings.cfg" | tr '\n' ' ')"
		echo "env: $envline  commit: $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null)$(git -C "$REPO" diff --quiet 2>/dev/null || echo '+dirty')"
	} > "$dir/run.info"
	echo "run $name: $(head -1 "$dir/run.info")"
	if [ "$dump" = 1 ]; then export AUDIO_DUMP=1; else unset AUDIO_DUMP; fi
	[ -n "$dsp" ] && export DSP_LLE=1
	[ -n "$fast" ] && export FAST=1
	for e in "${extra[@]:-}"; do [ -n "$e" ] && export "$e"; done
	DOL="$dir/boot.dol" bash "$REPO/scripts/dolphin_run.sh" "$dir" "$secs" "$dir/autoinput.txt" "$dir/settings.cfg" ${ab:+"$ab"} > "$dir/run.log" 2>&1   # quoted: a checkout path with spaces
	rc=$?
	echo "$rc" > "$dir/dolphin.rc"
	grep -E "refusing|no such|STOPPED: dolphin.log|faults in dolphin.log: [1-9]|PANIC|Unknown instruction" "$dir/run.log" | head -5
	[ $rc -ne 0 ] && echo "dolphin_run.sh exit $rc (see $dir/run.log)"
	"$PYTHON" "$REPO/scripts/run_summary.py" "$dir"
	st=$?
	if [ -z "${WSX_TRIAGE_DEFER:-}" ]; then   # a chain does these once, at its end
		auto_triage "$dir"
		verdict "$dir" "$base"
	fi
	exit $st
	;;

chain)
	# A chain file names its input scripts as sd:/wiistation/<file>; each is scripts/autoinput/<file>.
	name="${1:-}"; cf="${2:-}"; shift 2 || { echo "chain NAME CHAINFILE [options]"; exit 2; }
	[ -f "$cf" ] || cf="$REPO/scripts/chains/$cf"
	[ -f "$cf" ] || { echo "no such chain file: $cf"; exit 2; }
	# the same test as WiiStation's parser (GamecubeMain.cpp): the very first line, CR/LF cut, CHAIN in any case
	head -1 "$cf" | tr -d '\r\n' | grep -qix 'CHAIN' || { echo "$cf: the first line is not CHAIN -- WiiStation would sit on a blocking message box (measured 2026-10-17)"; exit 2; }
	cardargs=()
	for f in $(tr -d '\r' < "$cf" | sed -n -E 's|^[0-9]+[[:space:]]+sd:/wiistation/([^[:space:]]+).*|\1|p' | sort -u); do
		[ -f "$REPO/scripts/autoinput/$f" ] || { echo "chain names sd:/wiistation/$f: no scripts/autoinput/$f"; exit 2; }
		cardargs+=(--card "$REPO/scripts/autoinput/$f")
	done
	WSX_TRIAGE_DEFER=1 bash "$0" run "$name" --autoboot "$cf" --input "$P/autoinput_none.txt" --nodump --env FRAMES_DUMP=False ${cardargs[@]+"${cardargs[@]}"} "$@" | grep -v "^  "
	"$PYTHON" "$REPO/scripts/chain_table.py" "$RUNS/$name"
	st=$?
	"$PYTHON" "$REPO/scripts/benchdb.py" ingest "$RUNS/$name" >/dev/null 2>&1   # baselines/bench.sqlite
	auto_triage "$RUNS/$name"
	base=""; prev=""; for x in "$@"; do [ "$prev" = --base ] && base="$x"; prev="$x"; done
	verdict "$RUNS/$name" "$base"
	exit $st
	;;

state)
	# Save a game at a vblank: a one-game chain whose input script is "state save NAME V", plus
	# the pad lines of a recording if one is given. The state stays on the test card.
	name="${1:-}"; game="${2:-}"; shift 2 || { echo "state NAME GAME --at V [--rec REC] [--ct 0|1]"; exit 2; }
	at=""; rec=""; ct=0; rest=()
	while [ $# -gt 0 ]; do
		case "$1" in
			--at) at="$2"; shift 2 ;;
			--rec) rec="$2"; shift 2 ;;
			--ct) ct="$2"; shift 2 ;;
			*) rest+=("$1"); shift ;;
		esac
	done
	[ -n "$at" ] || { echo "state: --at VBLANK is needed"; exit 2; }
	if [ -n "$rec" ]; then
		rf="$REPO/scripts/autoinput/${rec}_play.txt"
		[ -f "$rf" ] || { echo "no recording $rf"; exit 2; }
		folder=$(sed -n 's/^# folder: //p' "$rf" | tr -d '\r'); cue=$(sed -n 's/^# cue: //p' "$rf" | tr -d '\r')
		c=$(sed -n 's/^# chain line: .*ControllerType=\([0-9]\).*/\1/p' "$rf" | tr -d '\r'); ct=${c:-$ct}
	else
		hit=$(tr -d '\r' < "$REPO/scripts/chains/all.txt" | grep -n '^sd:/wiistation/isos/' | grep -i -F -- "$game")
		[ "$(printf '%s' "$hit" | grep -c .)" = 1 ] || { echo "\"$game\" must match one game in scripts/chains/all.txt"; exit 2; }
		folder=${hit#*:}; cue=$(tr -d '\r' < "$REPO/scripts/chains/all.txt" | sed -n "$(( ${hit%%:*} + 1 ))p")
	fi
	mkdir -p "$RUNS/state_$name"
	sf="$REPO/scripts/autoinput/_state_$name.txt"
	{ echo "state save $name $at"; [ -n "$rec" ] && grep -v '^#' "$rf"; } > "$sf"
	printf 'CHAIN\n%s sd:/wiistation/_state_%s.txt PadAutoAssign=1 ControllerType=%s\n%s\n%s\n' \
		"$((at + 30))" "$name" "$ct" "$folder" "$cue" > "$RUNS/state_$name/chain.txt"
	bash "$0" chain "state_$name" "$RUNS/state_$name/chain.txt" --secs $((at / 30 + 200)) ${rest[@]+"${rest[@]}"} > /dev/null
	rm -f "$sf"
	grep -a "^state:" "$RUNS/state_$name/perf.log"
	card="$LIB/states"   # promoted from each run's card when it ends (dolphin_run.sh)
	[ -f "$card/$name.txt" ] && sed -n '1,8p' "$card/$name.txt"
	;;

abstate)
	# Several settings from one saved moment, in one boot: a chain line per SETS, each loading
	# the state (State=NAME) and running VBL vblanks from it.
	run="${1:-}"; st="${2:-}"; vbl="${3:-}"; shift 3 || { echo "abstate RUN STATE VBL SETS... [--rec REC]"; exit 2; }
	card="$LIB/states"   # promoted from each run's card when it ends (dolphin_run.sh)
	[ -f "$card/$st.txt" ] || { echo "no save state $st on the test card ($card); make it with wsx.sh state"; exit 2; }
	folder=$(sed -n 's/^# folder: //p' "$card/$st.txt" | tr -d '\r'); cue=$(sed -n 's/^# cue: //p' "$card/$st.txt" | tr -d '\r')
	rec=""; sets=(); rest=()
	while [ $# -gt 0 ]; do
		case "$1" in
			--rec) rec="$2"; shift 2 ;;
			--*) rest+=("$1" "$2"); shift 2 ;;
			*) sets+=("$1"); shift ;;
		esac
	done
	[ ${#sets[@]} -gt 0 ] || { echo "abstate: give at least one SETS (\"K=V,K=V\" or -)"; exit 2; }
	# Every group ends with a fingerprint of the machine (statefp), so the groups can be
	# compared: the same settings twice must give the same line.
	sv=$(sed -n 's/^# vblank: //p' "$card/$st.txt" | tr -d '\r')
	ai="$REPO/scripts/autoinput/_abstate_$run.txt"
	{ echo "statefp $((sv + vbl - 1))"; [ -n "$rec" ] && grep -v '^#' "$REPO/scripts/autoinput/${rec}_play.txt"; } > "$ai"
	pad=""
	if [ -n "$rec" ]; then   # the recording's pad type
		c=$(sed -n 's/^# chain line: .*ControllerType=\([0-9]\).*/\1/p' "$REPO/scripts/autoinput/${rec}_play.txt" | tr -d '\r')
		[ -n "$c" ] && pad=" ControllerType=$c"
	fi
	mkdir -p "$RUNS/$run"
	cf="$RUNS/$run.chain.txt"; echo CHAIN > "$cf"
	for s in "${sets[@]}"; do
		kv=""; [ "$s" != "-" ] && kv=" $(echo "$s" | tr ',' ' ')"
		printf '\n%s sd:/wiistation/_abstate_%s.txt PadAutoAssign=1%s State=%s%s\n%s\n%s\n' "$vbl" "$run" "$pad" "$st" "$kv" "$folder" "$cue" >> "$cf"
	done
	rm -rf "$RUNS/$run"
	bash "$0" chain "$run" "$cf" --secs $(( ${#sets[@]} * (vbl / 30 + 90) )) ${rest[@]+"${rest[@]}"}
	rm -f "$ai"
	grep -a "^state:\|^statefp:" "$RUNS/$run/perf.log"
	;;

table)
	n="$1"; shift; "$PYTHON" "$REPO/scripts/chain_table.py" "$RUNS/$n" "$@"
	;;
compare)
	explain=""; a=(); names=()
	for x in "$@"; do
		case "$x" in
			--explain) explain=1 ;;
			--*|gpu|tex|cpu|lc|pmc) a+=("$x") ;;
			*) a+=("$RUNS/$x"); names+=("$RUNS/$x") ;;
		esac
	done
	if [ -z "$explain" ]; then
		"$PYTHON" "$REPO/scripts/chain_compare.py" "${a[@]}"
	else
		# the table, then the reading under it (run_triage.py --explain: both tables and run.info)
		out=$("$PYTHON" "$REPO/scripts/chain_compare.py" "${a[@]}"); st=$?
		printf '%s\n' "$out"
		tf="$(mktemp)"; printf '%s\n' "$out" > "$tf"
		"$PYTHON" "$REPO/scripts/run_triage.py" --explain "$tf" "${names[@]}" || true
		rm -f "$tf"
		exit $st
	fi
	;;
triage)
	n="${1:-}"; shift || true
	[ -n "$n" ] || { echo "triage NAME [question]"; exit 2; }
	d="$RUNS/$n"; [ -d "$d" ] || d="$n"
	[ -d "$d" ] || { echo "no run $n"; exit 2; }
	"$PYTHON" "$REPO/scripts/run_triage.py" "$d" "$@"
	;;
frames)
	a="$1"; b="$2"; shift 2; "$PYTHON" "$REPO/scripts/frame_compare.py" "$RUNS/$a" "$RUNS/$b" "$@"
	;;
audio)
	wa=$(ls "$RUNS/$1"/*dspdump1.wav 2>/dev/null | head -1); wb=$(ls "$RUNS/$2"/*dspdump1.wav 2>/dev/null | head -1)
	[ -n "$wa" ] && [ -n "$wb" ] || { echo "no *dspdump1.wav in both runs (run them without --nodump)"; exit 2; }
	"$PYTHON" "$REPO/scripts/wav_compare.py" "$wa" "$wb" --exact | tail -3
	;;

menu)
	# A guarded menu comparison (scripts/menu_compare.py): one or two DOLs, each photographed
	# (--page N) or walked (--input FILE) with every frame kept. The emulated Wiimotes are off
	# (Source=0) for the whole comparison, because menukeys is ignored while an IR pointer is
	# valid, and their WiimoteNew.ini is put back byte for byte however the script ends.
	name="${1:-}"; shift || true
	[ -n "$name" ] || { echo "menu NAME --dol A.dol [--dol2 B.dol] (--page N | --input FILE) [--secs S] [--set K=V,...] [--pad] [--sd DIR]"; exit 2; }
	dolA=""; dolB=""; page=""; input=""; secs=20; msets=""; mpad=""; msd=""
	while [ $# -gt 0 ]; do
		case "$1" in
			--set) msets="${msets:+$msets,}$2"; shift 2 ;;   # settings for the run
			--sd) msd="$2"; shift 2 ;;   # files for the card: DIR's contents go to sd:/wiistation/
			--pad) mpad=1; shift ;;   # a GameCube pad on port 1 (scripts/dsu_pad.py): pages that need a controller
			--dol) dolA="$2"; shift 2 ;;
			--dol2) dolB="$2"; shift 2 ;;
			--page) page="$2"; shift 2 ;;
			--input) input="$2"; shift 2 ;;
			--secs) secs="$2"; shift 2 ;;
			*) echo "unknown option $1"; exit 2 ;;
		esac
	done
	[ -f "$dolA" ] || { echo "menu: --dol A.dol needed (an existing file)"; exit 2; }
	[ -z "$dolB" ] || [ -f "$dolB" ] || { echo "no such dol: $dolB"; exit 2; }
	if { [ -n "$page" ] && [ -n "$input" ]; } || [ -z "$page$input" ]; then
		echo "menu: give exactly one of --page N or --input FILE"; exit 2
	fi
	if [ -n "$page" ]; then
		case "$page" in *[!0-9]*) echo "menu: --page needs a number"; exit 2 ;; esac
	fi
	[ -z "$input" ] || [ -f "$input" ] || { echo "no such walk file: $input"; exit 2; }
	# The menu gets its own profile, made from the template for this command only. Its
	# WiimoteNew.ini is the one changed; the child runs use it (WSX_PROFILE) and do not take
	# the lock, which this command holds (WSX_LOCK_HELD). Removed at the end (KEEP_PROFILE=1 keeps it).
	mname="menu_$name"
	if dolphin_running; then echo "a Dolphin is running on one of this repo's profiles: the menu run waits for none"; exit 2; fi
	bash "$REPO/scripts/profile.sh" rm "$mname" >/dev/null 2>&1   # an earlier menu run's leftovers
	prof=$(bash "$REPO/scripts/profile.sh" new "$mname") || exit 2
	bash "$REPO/scripts/profile.sh" lock "$mname" "menu:$name" $$ || exit 2
	export WSX_PROFILE="$mname" WSX_LOCK_HELD=1
	if [ -n "$msd" ]; then cp -r "$msd"/. "$prof/Load/WiiSDSync/wiistation/" || exit 2; fi
	MENU_INI="$prof/Config/WiimoteNew.ini"; MENU_BAK="$MENU_INI.menu-backup"
	[ -f "$MENU_INI" ] || { echo "no $MENU_INI in the template"; exit 2; }
	MENU_SHA=$(sha256sum "$MENU_INI" | cut -d' ' -f1)
	cp -f "$MENU_INI" "$MENU_BAK" || { echo "cannot write $MENU_BAK"; exit 2; }
	if [ -n "$mpad" ]; then
		# Wii64's way to give Dolphin a pad with no keyboard or focus: a DSU (cemuhook) server
		# feeding GC pad 1. Configure Buttons was only ever photographed with no controller,
		# so its labels were empty and the ANALOG combo's overflow went unseen (2026-10-22).
		"$PYTHON" "$REPO/scripts/dsu_pad.py" profile "$prof" >/dev/null || exit 2
		"$PYTHON" "$REPO/scripts/dsu_pad.py" serve >/dev/null 2>&1 &
		DSU_PID=$!
		msets="${msets:+$msets,}PadAutoAssign=1"
	fi
	menu_cleanup() {
		[ -n "${DSU_PID:-}" ] && kill "$DSU_PID" 2>/dev/null
		menu_restore_ini
		bash "$REPO/scripts/profile.sh" unlock "$mname" $$ >/dev/null 2>&1
		[ -n "${KEEP_PROFILE:-}" ] || rm -rf "$prof"
	}
	trap menu_cleanup EXIT
	trap 'exit 130' INT TERM
	"$PYTHON" "$REPO/scripts/menu_compare.py" ini-off "$MENU_INI" || exit 1
	out="$RUNS/menu_$name"; mkdir -p "$out"
	rm -f "$out/verdict.json" "$out/verdict.txt"; date +%s > "$out/running"
	if [ -n "$page" ]; then
		printf 'menupage %s\n' "$page" > "$out/input.txt"; flag=(--page)
	else
		cp -f "$input" "$out/input.txt"; flag=(--input "$out/input.txt")
	fi
	inp="$out/input.txt"
	menu_run() {   # menu_run RUN DOL: every frame kept (KEEP), no audio dump
		bash "$0" run "$1" --dol "$2" --autoboot none --input "$inp" --secs "$secs" --nodump --env KEEP=100000 ${msets:+--set "$msets"} > "$out/$1.log" 2>&1
		echo "run $1: exit $? (log $out/$1.log)"
	}
	menu_run "menu_${name}_a" "$dolA"
	cmp_args=("$RUNS/menu_${name}_a")
	if [ -n "$dolB" ]; then
		menu_run "menu_${name}_b" "$dolB"
		cmp_args+=("$RUNS/menu_${name}_b")
	fi
	"$PYTHON" "$REPO/scripts/menu_compare.py" compare "${cmp_args[@]}" --out "$out" "${flag[@]}"
	rc=$?
	# Frames are ~1 GB a run: dropped after the compare (before_after images stay); KEEP_FRAMES=1 keeps them.
	[ -n "${KEEP_FRAMES:-}" ] || rm -rf "${cmp_args[@]/%//frames}"
	verdict "$out" "" --menu-rc "$rc"
	exit $rc
	;;

wait)
	# Block until run NAME (a run, a chain, or menu NAME) has its verdict, then print it. For a
	# run started in the background: no `sleep N; tail` loops. Exit 0 OK, 1 CHECK/DIFFERS,
	# 2 NO-RESULTS or no such run, 3 ended without a verdict, 4 still running at --timeout.
	name="${1:-}"; shift || true
	[ -n "$name" ] || { echo "wait NAME [--timeout SECONDS]   (default 540: under a 10-minute tool limit)"; exit 2; }
	to=540; [ "${1:-}" = --timeout ] && to="${2:-540}"
	d="$RUNS/$name"
	[ -d "$d" ] || [ ! -d "$RUNS/menu_$name" ] || d="$RUNS/menu_$name"
	t0=$(date +%s); settled=""
	while :; do
		now=$(date +%s)
		if [ -f "$d/verdict.json" ] && [ ! -e "$d/running" ]; then
			# a verdict from before this wait may be an earlier run of the same name whose
			# successor has not written its "running" mark yet: look once more, 3 s later
			if [ -n "$settled" ] || [ $(( now - $(stat -c %Y "$d/verdict.json") )) -lt $(( now - t0 + 2 )) ]; then
				cat "$d/verdict.txt"
				case "$(sed -n 's/.*"status": "\([A-Z-]*\)".*/\1/p' "$d/verdict.json")" in
					OK) exit 0 ;; CHECK|DIFFERS) exit 1 ;; *) exit 2 ;;
				esac
			fi
			settled=1; sleep 3; continue
		fi
		if [ -f "$d/dolphin.rc" ] && [ ! -f "$d/verdict.json" ] && [ $(( now - $(stat -c %Y "$d/dolphin.rc") )) -gt 180 ]; then
			echo "VERDICT $name: ENDED WITHOUT A VERDICT (Dolphin exit $(cat "$d/dolphin.rc"); the wsx.sh that ran it stopped early)"
			tail -5 "$d/run.log" 2>/dev/null
			exit 3
		fi
		if [ ! -d "$d" ] && [ $(( now - t0 )) -ge 60 ]; then echo "wait: no run $name in $RUNS"; exit 2; fi
		if [ $(( now - t0 )) -ge "$to" ]; then
			echo "wait: $name still running after ${to} s (call wait again; it picks up where the run is)"
			grep -a "^=== chain" "$d/perf.log" 2>/dev/null | tail -1
			exit 4
		fi
		sleep 5
	done
	;;

summary)
	dirs=(); for n in "$@"; do [ -d "$n" ] && dirs+=("$n") || dirs+=("$RUNS/$n"); done
	"$PYTHON" "$REPO/scripts/run_summary.py" "${dirs[@]}"
	;;

last)
	d=$(ls -td "$RUNS"/*/ 2>/dev/null | head -1)
	[ -n "$d" ] && "$PYTHON" "$REPO/scripts/run_summary.py" "$d" || echo "no runs in $RUNS"
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

# Housekeeping after a run or a chain (user, 2026-10-19: .runs reached 58 GB): past
# WSX_RUNS_MAX_GB (default 15) thin old runs with runs_prune.py (every run keeps its text;
# landmarks in .runs/LANDMARKS.txt and the last 2 days stay whole) and remove orphaned Dolphin
# profiles. Not inside a chain's own runs; WSX_RUNS_MAX_GB=0 turns it off.
case "$cmd" in
run|chain)
	if [ -z "${WSX_TRIAGE_DEFER:-}" ] && [ "${WSX_RUNS_MAX_GB:-15}" != 0 ] && [ -d "$REPO/.runs" ]; then
		mb=$(du -sm "$REPO/.runs" 2>/dev/null | cut -f1)
		if [ "${mb:-0}" -gt $(( ${WSX_RUNS_MAX_GB:-15} * 1024 )) ]; then
			echo "housekeeping: .runs is ${mb} MB (over ${WSX_RUNS_MAX_GB:-15} GB): thinning old runs"
			"$PYTHON" "$REPO/scripts/runs_prune.py" --apply | tail -1
			bash "$REPO/scripts/profile.sh" gc >/dev/null 2>&1
		fi
	fi
	;;
esac
