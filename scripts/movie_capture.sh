#!/bin/bash
# movie_capture.sh GAME [NAME] [--mins M] [--ct 0|1] [--dol debug|release|PATH]
# movie_capture.sh --play NAME [--frames] [--dol ...]
#
# Record how a game is to be played -- once, by a person -- as an input script that a
# chained run (scripts/chains/) plays back exactly.
#
#   GAME    part of the game's folder name in scripts/chains/all.txt ("spyro", "ff7" does
#           not work, "final fantasy" does); run with no GAME to list them
#   NAME    the script's name (default: from the folder); saved as
#           scripts/autoinput/NAME_play.txt, the previous one kept as .prev
#   --mins  how long to record, in emulated minutes (default 4)
#   --ct    PlayStation pad: 0 = digital pad (default), 1 = DualShock (Ape Escape needs it)
#   --play  boot a saved recording in a window and watch it play itself, to check it. It
#           uses the recording's pad type, and says whether the build is the one it was made on
#   --frames  with --play: keep the last 120 frames in .runs/<run>/frames, to compare endings
#
# How it works: the game boots as a one-game chain, the way every measured run boots, with
# the same settings and fresh memory cards. Its input script is the single line "record",
# and WiiStation writes every change of the real pads on ports 1 and 2 to the card, counted in
# emulated vblanks (Gamecube/PadWiiSX.c). That is the clock playback uses, so the recording
# replays exactly, however fast Dolphin ran while it was made. A Dolphin input movie (.dtm)
# counts host frames instead, which drift from the game's whenever the emulation runs
# below full speed; scripts/dtm2autoinput.py stays for old movies only.
#
# A recording replays exactly with the setup that made it: the same pad type, booted alone,
# and a build whose emulated timing matches (a change that moves a load by a frame shifts
# every later press). So the header names the build (commit, DOL date, SHA-1), the settings,
# the pad type and the Dolphin pad setup, and its chain line sets the pad type. If an old
# recording drifts on a new build, build its commit and pass that DOL with --dol.
#
# While recording: click the game window first (Dolphin reads the keyboard only when its
# window has focus). The pad is a digital PlayStation pad: move with the D-pad keys, not the
# arrow keys (those are the GameCube stick, which a digital pad does not have). Do not press
# Start+X, which opens WiiStation's menu and ends the game early. Start+Y (fast forward) is
# safe: the recording counts emulated time. The window closes by itself when the time is up;
# closing it early keeps what was recorded until then. The script draws both pads with their
# keys (scripts/pad_layout.py); a port 2 with nothing bound in Dolphin gets a keyboard layout
# for the run, so a second player can join.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ALL="$REPO/scripts/chains/all.txt"
PAD_USER="/c/tools/Dolphin-x64/User/Config/GCPadNew.ini"   # the user's own controller setup
PAD_TEST="$REPO/.dolphin/Config/GCPadNew.ini"              # the test profile's (dolphin_run.sh)
CARD="$REPO/.dolphin/Load/WiiSD.raw"
SYNC="$REPO/.dolphin/Load/WiiSDSync/wiisxrx"

game=""; name=""; mins=4; dol=debug; play=""; frames=False; ct=0
while [ $# -gt 0 ]; do
	case "$1" in
		--mins) mins="$2"; shift 2 ;;
		--dol)  dol="$2"; shift 2 ;;
		--ct)   ct="$2"; shift 2 ;;
		--play) play="$2"; shift 2 ;;
		--frames) frames=True; shift ;;
		-h|--help) sed -n '2,/^set -u/p' "$0" | sed -e '/^set -u/d' -e 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) if [ -z "$game" ]; then game="$1"; elif [ -z "$name" ]; then name="$1"; else echo "unexpected: $1"; exit 2; fi; shift ;;
	esac
done

games() { tr -d '\r' < "$ALL" | grep -n '^sd:/wiisxrx/isos/'; }

if [ -n "$play" ]; then
	script="$REPO/scripts/autoinput/${play}_play.txt"
	[ -f "$script" ] || { echo "no recording $script"; ls "$REPO/scripts/autoinput/" | sed -n 's/_play\.txt$//p' | sed 's/^/  /'; exit 2; }
	folder=$(sed -n 's/^# folder: //p' "$script" | tr -d '\r'); cue=$(sed -n 's/^# cue: //p' "$script" | tr -d '\r')
	vbl=$(sed -n 's/^# vblanks: //p' "$script" | tr -d '\r')
	[ -n "$folder" ] && [ -n "$cue" ] && [ -n "$vbl" ] || { echo "$script has no folder/cue/vblanks header"; exit 2; }
	name="$play"; input="${play}_play.txt"; src="$script"
	# The recording's pad type, and whether this is the build it was made on
	ct=$(sed -n 's/^# chain line: .*ControllerType=\([0-9]\).*/\1/p' "$script" | tr -d '\r'); ct=${ct:-0}
	built=$(sed -n 's/^# build: //p' "$script" | tr -d '\r')
	case "$dol" in debug) doli="$REPO/Gamecube/WiiSXRX_debug.dol" ;; release) doli="$REPO/Gamecube/WiiSXRX_Release.dol" ;; *) doli="$dol" ;; esac
	want=$(printf '%s' "$built" | sed -n 's/.*sha1 \([0-9a-f]*\).*/\1/p')
	have=$(sha1sum "$doli" 2>/dev/null | cut -c1-40)
	if [ -z "$built" ]; then
		echo "note: the recording names no build; if the game drifts, it was made on another one"
	elif [ "$want" = "$have" ]; then
		echo "same build as the recording: $built"
	else
		echo "note: recorded on $built; this is another build, so presses can drift."
		echo "      To replay it exactly, build that commit and pass the DOL with --dol."
	fi
else
	[ -n "$game" ] || { echo "usage: movie_capture.sh GAME [NAME] [--mins M]   games:"; games | sed 's|^[0-9]*:sd:/wiisxrx/isos/|  |'; exit 2; }
	hit=$(games | grep -i -F -- "$game")
	if [ "$(printf '%s' "$hit" | grep -c .)" != 1 ]; then
		echo "\"$game\" matches $(printf '%s' "$hit" | grep -c .) games in all.txt; one of:"; games | sed 's|^[0-9]*:sd:/wiisxrx/isos/|  |'; exit 2
	fi
	ln=${hit%%:*}; folder=${hit#*:}
	cue=$(tr -d '\r' < "$ALL" | sed -n "$((ln + 1))p")
	[ -n "$name" ] || name=$(basename "$folder" | tr 'A-Z' 'a-z' | sed -E 's/[^a-z0-9]+/_/g; s/^_+|_+$//g')
	vbl=$((mins * 3600))
	input="capture_record.txt"
fi

run="capture_${name}_$(date +%Y%m%d_%H%M%S)"
dir="$REPO/.runs/$run/src"; mkdir -p "$dir"
if [ -z "$play" ]; then
	printf '# scripts/movie_capture.sh: write the real pads on ports 1 and 2 to sd:/wiisxrx/autoinput_rec.txt\nrecord\n' > "$dir/$input"
	src="$dir/$input"
fi
# PadAutoAssign=1: the run's base settings assign no controller (unattended runs have none),
# and a recording needs the keyboard pad on port 1. Playback sets the same, so it boots alike.
printf 'CHAIN\n# scripts/movie_capture.sh %s\n%s sd:/wiisxrx/%s PadAutoAssign=1 ControllerType=%s\n%s\n%s\n' \
	"$name" "$vbl" "$input" "$ct" "$folder" "$cue" > "$dir/chain.txt"
: > "$dir/none.txt"

# The test profile gets the user's controller setup for this run, and its own back after.
# A port 2 with no buttons bound (Dolphin's default) gets a keyboard layout in that copy, so
# two players can share the keyboard (scripts/pad_layout.py); the user's own file is not
# written. Dolphin connects no pad to port 2 unless told: SIDevice1=6 is a GameCube pad.
cp "$PAD_TEST" "$PAD_TEST.capture-bak" 2>/dev/null
python "$REPO/scripts/pad_layout.py" "$PAD_USER" --fill-pad2 "$PAD_TEST" > "$dir/pads.txt"
if [ -z "$play" ]; then
	echo "Recording $(basename "$folder"): $mins emulated minutes ($vbl vblanks), then the window closes."
	echo "Click the game window first. Both pads are recorded; port 2 can stay unused."
	echo
	cat "$dir/pads.txt"
	echo
	[ "$ct" = 1 ] || echo "Move with the D-pad keys, not the arrow keys (those are the GameCube stick; the pad is digital)."
	echo "Avoid Start+X on either pad: it opens WiiStation's menu. Start+Y fast-forwards, which is safe."
else
	echo "Playing $name back ($vbl vblanks). Hands off the keyboard while its window has focus."
fi

rm -f "$SYNC/autoinput_rec.txt"
bash "$REPO/scripts/wsx.sh" run "$run" --dol "$dol" --autoboot "$dir/chain.txt" --input "$dir/none.txt" \
	--nodump --env FRAMES_DUMP=$frames --env "DOLPHIN_ARGS=-C Dolphin.Core.SIDevice1=6" \
	--card "$src" --secs $((vbl / 30 + 180)) | grep -E "refusing|no such|fault|exit"
[ -f "$PAD_TEST.capture-bak" ] && mv "$PAD_TEST.capture-bak" "$PAD_TEST"
[ -n "$play" ] && { echo "done: .runs/$run"; exit 0; }

rec="$REPO/.runs/$run/autoinput_rec.txt"
python "$REPO/scripts/sdimage_read.py" "$CARD" wiisxrx/autoinput_rec.txt "$rec" >/dev/null 2>&1
rm -f "$SYNC/autoinput_rec.txt"
[ -s "$rec" ] || { echo "nothing was recorded (no autoinput_rec.txt on the card): see .runs/$run/run.log"; exit 1; }
grep -q "port 1 has NO controller" "$rec" && echo "WARNING: port 1 had no controller, so the recording holds no presses"
grep -q "port 2 has a controller" "$rec" || echo "note: port 2 had no controller; the recording plays it unplugged"
presses=$(grep -c -E '^(p2 )?[0-9]+ [0-9a-f]{4}' "$rec"); last=$(grep -E '^(p2 )?[0-9]+ ' "$rec" | tail -1 | sed 's/^p2 //' | cut -d' ' -f1)
out="$REPO/scripts/autoinput/${name}_play.txt"
[ -f "$out" ] && mv "$out" "$out.prev" && echo "kept the previous recording as $(basename "$out").prev"
# What it replays exactly on: the build, its settings and the pad setup (run.info, settings.cfg)
info="$REPO/.runs/$run/run.info"
commit=$(sed -n 's/.*commit: //p' "$info" | tr -d '\r')
dolinfo=$(sed -n 's/^dol=\([^ ]*\) (\([^)]*\)).*/\1, \2/p' "$info" | tr -d '\r')
sha=$(sha1sum "$REPO/.runs/$run/boot.dol" | cut -c1-40)
settings=$(grep -v '^#' "$REPO/.runs/$run/settings.cfg" | tr -d '\r' | sed 's/ = /=/' | tr '\n' ' ')
[ "$ct" = 1 ] && padname="DualShock" || padname="digital pad"
{
	echo "# $(basename "$folder"): played by hand, recorded $(date '+%F %T') with scripts/movie_capture.sh"
	echo "# folder: $folder"
	echo "# cue: $cue"
	echo "# vblanks: $vbl"
	echo "# build: commit $commit, $dolinfo, sha1 $sha"
	echo "#   (if it drifts on a later build: build that commit and pass the DOL with --dol)"
	echo "# settings: $settings"
	echo "# pad: ControllerType=$ct ($padname); Dolphin: port 1 keyboard GameCube pad, port 2 GameCube pad (SIDevice1=6)"
	echo "# chain line: $vbl sd:/wiisxrx/${name}_play.txt PadAutoAssign=1 ControllerType=$ct"
	tr -d '\r' < "$rec"
} > "$out"
echo "saved scripts/autoinput/${name}_play.txt: $presses changes, the last at vblank ${last:-none}"
echo "check it plays back:  bash scripts/movie_capture.sh --play $name"
echo "use it in a chain:    $vbl sd:/wiisxrx/${name}_play.txt PadAutoAssign=1 ControllerType=$ct"
