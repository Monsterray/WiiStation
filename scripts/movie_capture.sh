#!/bin/bash
# movie_capture.sh GAME [NAME] [--mins M] [--dol debug|release|PATH]
# movie_capture.sh --play NAME [--dol ...]
#
# Record how a game is to be played -- once, by a person -- as an input script that a
# chained run (scripts/chains/) plays back exactly.
#
#   GAME    part of the game's folder name in scripts/chains/all.txt ("spyro", "ff7" does
#           not work, "final fantasy" does); run with no GAME to list them
#   NAME    the script's name (default: from the folder); saved as
#           scripts/autoinput/NAME_play.txt, the previous one kept as .prev
#   --mins  how long to record, in emulated minutes (default 4)
#   --play  boot a saved recording in a window and watch it play itself, to check it
#
# How it works: the game boots as a one-game chain, the way every measured run boots, with
# the same settings and fresh memory cards. Its input script is the single line "record",
# and WiiStation writes every change of the real pad on port 1 to the card, counted in
# emulated vblanks (Gamecube/PadWiiSX.c). That is the clock playback uses, so the recording
# replays exactly, however fast Dolphin ran while it was made. A Dolphin input movie (.dtm)
# counts host frames instead, which drift from the game's whenever the emulation runs
# below full speed; scripts/dtm2autoinput.py stays for old movies only.
#
# While recording: click the game window first (Dolphin reads the keyboard only when its
# window has focus). The pad is a digital PlayStation pad: move with the D-pad keys, not the
# arrow keys (those are the GameCube stick, which a digital pad does not have). Do not press
# Start+X, which opens WiiStation's menu and ends the game early. Start+Y (fast forward) is
# safe: the recording counts emulated time. The window closes by itself when the time is up;
# closing it early keeps what was recorded until then.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ALL="$REPO/scripts/chains/all.txt"
PAD_USER="/c/tools/Dolphin-x64/User/Config/GCPadNew.ini"   # the user's own controller setup
PAD_TEST="$REPO/.dolphin/Config/GCPadNew.ini"              # the test profile's (dolphin_run.sh)
CARD="$REPO/.dolphin/Load/WiiSD.raw"
SYNC="$REPO/.dolphin/Load/WiiSDSync/wiisxrx"

game=""; name=""; mins=4; dol=debug; play=""
while [ $# -gt 0 ]; do
	case "$1" in
		--mins) mins="$2"; shift 2 ;;
		--dol)  dol="$2"; shift 2 ;;
		--play) play="$2"; shift 2 ;;
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
	printf '# scripts/movie_capture.sh: write the real pad on port 1 to sd:/wiisxrx/autoinput_rec.txt\nrecord\n' > "$dir/$input"
	src="$dir/$input"
fi
# PadAutoAssign=1: the run's base settings assign no controller (unattended runs have none),
# and a recording needs the keyboard pad on port 1. Playback sets the same, so it boots alike.
printf 'CHAIN\n# scripts/movie_capture.sh %s\n%s sd:/wiisxrx/%s PadAutoAssign=1\n%s\n%s\n' \
	"$name" "$vbl" "$input" "$folder" "$cue" > "$dir/chain.txt"
: > "$dir/none.txt"

# The test profile gets the user's controller setup for this run, and its own back after.
restore_pad=""
if [ -f "$PAD_USER" ] && ! cmp -s "$PAD_USER" "$PAD_TEST"; then
	cp "$PAD_TEST" "$PAD_TEST.capture-bak" 2>/dev/null && restore_pad=1
	cp "$PAD_USER" "$PAD_TEST"
fi
key() {   # key GCPad1-entry -> the keyboard key or pad button bound to it
	tr -d '\r' < "$PAD_TEST" | awk -v k="$1" -F' = ' '/^\[GCPad1\]/{s=1;next} /^\[/{s=0} s && $1==k {gsub(/`/,"",$2); print $2; exit}'
}
if [ -z "$play" ]; then
	cat <<EOF
Recording $(basename "$folder"): $mins emulated minutes ($vbl vblanks), then the window closes.
Click the game window first. The keys (Dolphin GCPad1 -> WiiStation's GameCube mapping):
  Cross    $(key Buttons/A)          Square   $(key Buttons/B)
  Circle   $(key Buttons/X)          Triangle $(key Buttons/Y)
  Start    $(key Buttons/Start)     Select   $(key Buttons/Z) + $(key Buttons/Start)
  L1 / R1  $(key Triggers/L) / $(key Triggers/R)      L2 / R2  $(key Buttons/Z) + $(key Triggers/L) / $(key Buttons/Z) + $(key Triggers/R)
  D-pad    $(key D-Pad/Up) $(key D-Pad/Left) $(key D-Pad/Down) $(key D-Pad/Right) (up left down right; NOT the arrow keys)
  Avoid Start+X ($(key Buttons/Start)+$(key Buttons/X)): WiiStation's menu. Start+Y fast-forwards, which is safe.
EOF
else
	echo "Playing $name back ($vbl vblanks). Hands off the keyboard while its window has focus."
fi

rm -f "$SYNC/autoinput_rec.txt"
bash "$REPO/scripts/wsx.sh" run "$run" --dol "$dol" --autoboot "$dir/chain.txt" --input "$dir/none.txt" \
	--nodump --env FRAMES_DUMP=False --card "$src" --secs $((vbl / 30 + 180)) | grep -E "refusing|no such|fault|exit"
[ -n "$restore_pad" ] && mv "$PAD_TEST.capture-bak" "$PAD_TEST"
[ -n "$play" ] && { echo "done: .runs/$run"; exit 0; }

rec="$REPO/.runs/$run/autoinput_rec.txt"
python "$REPO/scripts/sdimage_read.py" "$CARD" wiisxrx/autoinput_rec.txt "$rec" >/dev/null 2>&1
rm -f "$SYNC/autoinput_rec.txt"
[ -s "$rec" ] || { echo "nothing was recorded (no autoinput_rec.txt on the card): see .runs/$run/run.log"; exit 1; }
grep -q "has NO controller" "$rec" && echo "WARNING: port 1 had no controller, so the recording holds no presses"
presses=$(grep -c -E '^[0-9]+ [0-9a-f]{4}' "$rec"); last=$(grep -E '^[0-9]+ ' "$rec" | tail -1 | cut -d' ' -f1)
out="$REPO/scripts/autoinput/${name}_play.txt"
[ -f "$out" ] && mv "$out" "$out.prev" && echo "kept the previous recording as $(basename "$out").prev"
{
	echo "# $(basename "$folder"): played by hand, recorded $(date +%F) with scripts/movie_capture.sh"
	echo "# folder: $folder"
	echo "# cue: $cue"
	echo "# vblanks: $vbl"
	echo "# chain line: $vbl sd:/wiisxrx/${name}_play.txt PadAutoAssign=1"
	tr -d '\r' < "$rec"
} > "$out"
echo "saved scripts/autoinput/${name}_play.txt: $presses changes, the last at vblank ${last:-none}"
echo "check it plays back:  bash scripts/movie_capture.sh --play $name"
echo "use it in a chain:    $vbl sd:/wiisxrx/${name}_play.txt PadAutoAssign=1"
