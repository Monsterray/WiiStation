#!/bin/bash
# movie_capture.sh GAME [NAME] [--mins M] [--ct 0|1] [--dol debug|release|PATH]
# movie_capture.sh --play NAME [--frames] [--dol debug|release|PATH]
#
# Record a person who plays a game one time. The result is an input script that a
# chained run (scripts/chains/) replays exactly.
#
# Arguments:
#   GAME      Part of the game's folder name in the game library (scripts/profile.sh keys).
#             "spyro" and "final fantasy" are examples. "ff7" does not match. Give no GAME
#             to see the list.
#   NAME      The name of the script (default: made from the folder name). The script is
#             scripts/autoinput/NAME_play.txt. The previous script stays as NAME_play.txt.prev.
#   --mins M  The longest recording, in emulated minutes (default 20). Decimals are permitted (1.5).
#             Close the game window when you are done: the recording ends there. (The old default
#             of 4 cut finish_lvl1 off before the level finished, 2026-11-01.)
#   --ct 0|1  The PlayStation pad: 0 = digital pad (default), 1 = DualShock (analog from the
#             start). With 1 both sticks are recorded too ("sticks" lines, every change), and
#             replay exactly. Ape Escape needs 1.
#   --dol D   The build: debug (default), release, or the path of a DOL.
#   --set K=V[,K=V...]  Settings for the game, as on a chain line (GpuTiming=1,FrameRate=1).
#             Timing settings change what a recording replays, so they are saved with it and
#             --play uses them again. Record with the settings of the Wii you test on.
#   --play NAME  Start a saved recording in a minimized window and let it play itself.
#             Use this to check a recording. It uses the pad type of the recording, and it
#             tells you if the build is not the build that made the recording.
#   --frames  With --play: keep the last 120 frames in .runs/<run>/frames.
#
# Environment:
#   DOLPHIN_MOVIE  The dolphin-movie checkout (default /c/projects/dolphin-movie).
#                  This script uses its dolphin_movie.py for the pads and to read the card.
#   PYTHON         The Python program (default: python, else C:/Python312/python.exe).
#
# Exit status: 0 when the recording (or the playback) is complete, 1 when it failed,
# 2 for a usage error.
#
# How it works:
#   1. The game starts as a one-game chain, with the same settings and fresh memory cards
#      as each measured run.
#   2. Its input script is the single line "record". WiiStation writes each change of the
#      real pads on ports 1 and 2 to the card (sd:/wiistation/autoinput_rec.txt). The
#      clock is the emulated vblank (Gamecube/PadWiiSX.c). Playback uses the same clock,
#      so the recording replays exactly at all Dolphin speeds. (A Dolphin .dtm movie
#      counts host frames. These go out of step when the emulation is slower than full
#      speed. scripts/dtm2autoinput.py is for old movies only.)
#   3. dolphin_movie.py reads the recording from the card. This script adds a header
#      and saves it.
#
# The header records what an exact replay needs: the build (commit, DOL date, SHA-1),
# the settings, the pad type, the Dolphin pad setup, and the chain line. A change that
# moves a load by one frame moves all the presses after it. If an old recording goes
# out of step on a new build, build its commit and give that DOL with --dol.
#
# During the recording:
#   - Click the game window first for the keyboard: Dolphin reads it only when its window has
#     focus. A USB or Bluetooth pad works without focus (BackgroundInput is on while recording).
#   - Use the D-pad keys to move. The arrow keys are the GameCube stick, and the digital
#     pad has no stick.
#   - Do not push Start+X. It opens WiiStation's menu and stops the game.
#     Start+Y (fast forward) is safe, because the recording counts emulated time.
#   - The window closes when the time is complete. If you close it before, the script
#     keeps the presses until that point.
#   - The script shows the keys of both pads (scripts/pad_layout.py). If port 2 has no
#     buttons in Dolphin, it gets a keyboard layout for this run, so a second player can
#     use the numpad.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TSV="${WSX_LIBRARY:-/c/wiistation-lib}/games.tsv"   # every library game and the file it boots (scripts/library_check.py)
MOVIE="${DOLPHIN_MOVIE:-/c/projects/dolphin-movie}/dolphin_movie.py"
REC_CARD="wiistation/autoinput_rec.txt"            # where WiiStation writes the recording (Gamecube/PadWiiSX.c)
REC_PRESS='^(?:p2 )?(\d+) [0-9a-f]{4}'             # a press line: "VBL BTNS" for port 1, "p2 VBL BTNS" for port 2

game=""; name=""; mins=20; dol=debug; play=""; frames=False; ct=0; sets=""
usage() { echo "usage: movie_capture.sh GAME [NAME] [--mins M] [--ct 0|1] [--dol D]   or   --play NAME [--frames]   (--help for more)"; exit 2; }
while [ $# -gt 0 ]; do
	case "$1" in
		--mins|--dol|--ct|--play|--set) [ $# -ge 2 ] || { echo "$1 needs a value"; usage; } ;;
	esac
	case "$1" in
		--mins) mins="$2"; shift 2 ;;
		--dol)  dol="$2"; shift 2 ;;
		--ct)   ct="$2"; shift 2 ;;
		--set)  sets="$2"; shift 2 ;;
		--play) play="$2"; shift 2 ;;
		--frames) frames=True; shift ;;
		-h|--help) sed -n '2,/^set -u/p' "$0" | sed -e '/^set -u/d' -e 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "unknown option $1"; usage ;;
		*) if [ -z "$game" ]; then game="$1"; elif [ -z "$name" ]; then name="$1"; else echo "unexpected: $1"; usage; fi; shift ;;
	esac
done
case "$ct" in 0|1) ;; *) echo "--ct is 0 (digital pad) or 1 (DualShock), not \"$ct\""; exit 2 ;; esac
# --set: " K=V K=V" for the chain line
extra=""
for kv in $(printf '%s' "$sets" | tr ',' ' '); do
	case "$kv" in [A-Za-z]*=*) extra="$extra $kv" ;; *) echo "--set takes K=V,K=V (got \"$kv\")"; exit 2 ;; esac
done

if [ -n "${PYTHON:-}" ]; then :;
elif command -v python >/dev/null 2>&1; then PYTHON=python;
elif [ -x /c/Python312/python.exe ]; then PYTHON=/c/Python312/python.exe;
else PYTHON=python3; fi
[ -f "$MOVIE" ] || { echo "no dolphin-movie at ${MOVIE%/*}: clone it there, or set DOLPHIN_MOVIE to its folder"; exit 2; }
"$PYTHON" "$MOVIE" selftest >/dev/null || { echo "$MOVIE selftest failed: see its output above"; exit 2; }

# Recordings use one named profile, kept between runs (scripts/profile.sh), so the card the
# recording is read from still exists after the run.
MOVIE_PROFILE="$(bash "$REPO/scripts/profile.sh" path movie_capture 2>/dev/null || bash "$REPO/scripts/profile.sh" new movie_capture)" || exit 2
export WSX_PROFILE=movie_capture
SYNC="$MOVIE_PROFILE/Load/WiiSDSync/wiistation"

# Every bootable library game, one per line: its key (the folder under games/).
games() { tr -d '\r' < "$TSV" | awk -F '\t' 'NR > 1 && $4 == "ok" { print $1 }'; }
# The file a chain line boots for game key $1 (a .cue, .ccd, .chd ... as the browser shows it).
disc() { tr -d '\r' < "$TSV" | awk -F '\t' -v k="$1" '$1 == k { print $2; exit }'; }

if [ -n "$play" ]; then
	script="$REPO/scripts/autoinput/${play}_play.txt"
	[ -f "$script" ] || { echo "no recording $script"; ls "$REPO/scripts/autoinput/" | sed -n 's/_play\.txt$//p' | sed 's/^/  /'; exit 2; }
	folder=$(sed -n 's/^# folder: //p' "$script" | tr -d '\r'); cue=$(sed -n 's/^# cue: //p' "$script" | tr -d '\r')
	vbl=$(sed -n 's/^# vblanks: //p' "$script" | tr -d '\r')
	[ -n "$folder" ] && [ -n "$cue" ] && [ -n "$vbl" ] || { echo "$script has no folder/cue/vblanks header"; exit 2; }
	name="$play"; input="${play}_play.txt"; src="$script"
	# The recording's pad type, and whether this is the build it was made on
	ct=$(sed -n 's/^# chain line: .*ControllerType=\([0-9]\).*/\1/p' "$script" | tr -d '\r'); ct=${ct:-0}
	# a recording with port 2 needs the port plugged in on playback, as it was when it was made
	grep -q '^p2 ' "$script" && p2set=" PadType2=1"
	# the settings it was recorded with (--set), unless given again
	[ -n "$sets" ] || extra=$(sed -n 's/^# chain line: .*ControllerType=[0-9]\(.*\)$/\1/p' "$script" | tr -d '\r' | sed 's/ PadType2=1//')
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
	[ -n "$game" ] || { echo "usage: movie_capture.sh GAME [NAME] [--mins M]   games:"; games | sed 's/^/  /'; exit 2; }
	# a whole key wins (playtests.py passes them: "Spyro the Dragon" is also part of four other keys)
	hit=$(games | grep -i -x -F -- "$game")
	[ -n "$hit" ] || hit=$(games | grep -i -F -- "$game")
	if [ "$(printf '%s' "$hit" | grep -c .)" != 1 ]; then
		echo "\"$game\" matches $(printf '%s' "$hit" | grep -c .) games in the library; one of:"; games | sed 's/^/  /'; exit 2
	fi
	folder="sd:/wiistation/isos/$hit"   # dolphin_run.sh stages the games the chain names
	cue=$(disc "$hit")
	[ -n "$name" ] || name=$(basename "$folder" | tr 'A-Z' 'a-z' | sed -E 's/[^a-z0-9]+/_/g; s/^_+|_+$//g')
	# emulated minutes to vblanks (60 a second); decimals allowed
	vbl=$(awk -v m="$mins" 'BEGIN { if (m !~ /^[0-9]*\.?[0-9]+$/ || m <= 0) exit 1; printf "%d", m * 3600 + 0.5 }') ||
		{ echo "--mins takes a number of minutes, like 2 or 1.5 (got \"$mins\")"; exit 2; }
	input="capture_record.txt"
	export WSX_SHOW=1   # a recording opens on screen: the user plays it (dolphin_run.sh); --play stays minimized
fi

run="capture_${name}_$(date +%Y%m%d_%H%M%S)"
dir="$REPO/.runs/$run/src"; mkdir -p "$dir"
if [ -z "$play" ]; then
	printf '# scripts/movie_capture.sh: write the real pads on ports 1 and 2 to sd:/wiistation/autoinput_rec.txt\nrecord\n' > "$dir/$input"
	src="$dir/$input"
fi
# PadAutoAssign=1: the run's base settings assign no controller (unattended runs have none),
# and a recording needs the keyboard pad on port 1. Playback sets the same, so it boots alike.
printf 'CHAIN\n# scripts/movie_capture.sh %s\n%s sd:/wiistation/%s PadAutoAssign=1 ControllerType=%s%s\n%s\n%s\n' \
	"$name" "$vbl" "$input" "$ct" "${extra}${p2set:-}" "$folder" "$cue" > "$dir/chain.txt"
: > "$dir/none.txt"

# The movie profile gets a fresh copy of the user's pad setup for each run (dolphin_movie.py
# pads). The user's own file is only read. That copy:
#   - gives port 2 a keyboard layout when it has no buttons, so two players share the keyboard;
#   - points port 1 at the USB pad connected now (a RumblePad 2 where the setup names a Dual
#     Action), with rumble;
#   - turns the emulated Wii Remotes off.
# The profile is used only by this script, so nothing is put back after the run.
# Dolphin connects no pad to port 2 unless told: SIDevice1=6 (below) is a GameCube pad.
"$PYTHON" "$MOVIE" pads "$MOVIE_PROFILE" --fill-pad2 --detect > "$dir/pads.txt" || { cat "$dir/pads.txt"; exit 1; }
# The GameCube pad Dolphin emulates has no stick buttons: the real pad's stick clicks press Z+A
# and Z+B, which WiiStation reads as L3 and R3 (controller-GC.c)
"$PYTHON" "$REPO/scripts/pad_stick_clicks.py" "$MOVIE_PROFILE/Config/GCPadNew.ini" >> "$dir/pads.txt"
"$PYTHON" "$REPO/scripts/pad_layout.py" "$MOVIE_PROFILE/Config/GCPadNew.ini" >> "$dir/pads.txt"
if [ -z "$play" ]; then
	echo "Recording $(basename "$folder"): at most $mins emulated minutes ($vbl vblanks). Close the game window when you are done."
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
# The DOL, copied now: an agent rebuilding Gamecube/WiiSXRX_debug.dol (it is deleted while the
# link runs) left a recording with no DOL at all (2026-10-21). Wait for one being built.
case "$dol" in debug) dolp="$REPO/Gamecube/WiiSXRX_debug.dol" ;; release) dolp="$REPO/Gamecube/WiiSXRX_Release.dol" ;; *) dolp="$dol" ;; esac
for _ in $(seq 60); do [ -s "$dolp" ] && break; [ "$_" = 1 ] && echo "waiting for $dolp (a build is running?) ..."; sleep 5; done
[ -s "$dolp" ] || { echo "no DOL at $dolp after 5 minutes: build it (scripts/wsx.sh build debug) and run this again"; exit 1; }
sleep 2; cp "$dolp" "$dir/boot.dol" || exit 1
# A recording reads the pads even when the game window has no focus (2026-11-01: a run recorded
# no press at all from a USB pad); a playback never does, so nobody's hands can get in.
bg=; [ -z "$play" ] && bg=" -C Dolphin.Input.BackgroundInput=True"
bash "$REPO/scripts/wsx.sh" run "$run" --dol "$dir/boot.dol" --autoboot "$dir/chain.txt" --input "$dir/none.txt" \
	--nodump --env FRAMES_DUMP=$frames --env "DOLPHIN_ARGS=-C Dolphin.Core.SIDevice1=6$bg" \
	--card "$src" --secs $((vbl / 30 + 180)) | grep -E "refusing|no such|fault|exit"
[ -n "$play" ] && { echo "done: .runs/$run"; exit 0; }

# No run, no recording: the card image still holds the last session's autoinput_rec.txt, and a
# run that never started saved that one under the new name (2026-10-21)
[ -f "$REPO/.runs/$run/run.info" ] || { echo "the game did not start (see the lines above): nothing was recorded or saved"; exit 1; }
rec="$REPO/.runs/$run/autoinput_rec.txt"
summary=$("$PYTHON" "$MOVIE" collect "$MOVIE_PROFILE" --card "$REC_CARD" --out "$rec" --press "$REC_PRESS")
rm -f "$SYNC/autoinput_rec.txt"
printf '%s\n' "$summary" > "$REPO/.runs/$run/movie_summary.json"
# found presses last_press reached error, from dolphin_movie.py's JSON summary
read -r found presses last reached err <<< "$("$PYTHON" -c 'import json, sys
d = json.loads(sys.argv[1] or "{}")
print(int(d.get("found", False)), d.get("presses", 0), "none" if d.get("last_press") is None else d["last_press"], d.get("reached", 0), d.get("error", "-").replace(" ", "_"))' "$summary")"
[ "${err:--}" = - ] || { echo "the card could not be read: ${err//_/ }"; exit 1; }
[ "${found:-0}" = 1 ] && [ -s "$rec" ] || { echo "nothing was recorded (no $REC_CARD on the card): see .runs/$run/run.log"; exit 1; }
grep -q "port 1 has NO controller" "$rec" && echo "WARNING: port 1 had no controller, so the recording holds no presses"
grep -q "port 2 has a controller" "$rec" || echo "note: port 2 had no controller; the recording plays it unplugged"
# Closed early: the recording ends where it stopped, not at the planned length. WiiStation
# writes "# alive <vblank>" every 5 seconds. "reached" is the last one, or the last press if
# later. One second more keeps the last press's release.
if [ -n "$reached" ] && [ "$reached" -gt 0 ] && [ $((reached + 60)) -lt "$vbl" ]; then
	echo "the window was closed at about vblank $reached of $vbl: the recording ends there"
	vbl=$((reached + 60))
fi
out="$REPO/scripts/autoinput/${name}_play.txt"
[ -f "$out" ] && mv "$out" "$out.prev" && echo "kept the previous recording as $(basename "$out").prev"
# What it replays exactly on: the build, its settings and the pad setup (run.info, settings.cfg)
info="$REPO/.runs/$run/run.info"
commit=$(sed -n 's/.*commit: //p' "$info" | tr -d '\r')
dolinfo=$(sed -n 's/^dol=\([^ ]*\) (\([^)]*\)).*/\1, \2/p' "$info" | tr -d '\r')
sha=$(sha1sum "$REPO/.runs/$run/boot.dol" | cut -c1-40)
settings=$(grep -v '^#' "$REPO/.runs/$run/settings.cfg" | tr -d '\r' | sed 's/ = /=/' | tr '\n' ' ')
[ "$ct" = 1 ] && padname="DualShock" || padname="digital pad"
# Port 2 is plugged in during a recording only by the user's real pad 2 (PadAutoAssign sees it);
# on playback nothing is there, and PadType2 defaults to none, so the game would talk to one pad
# where it talked to two -- different pad traffic every frame, and the replay drifts (2026-09-30,
# Spyro and Crash 3 ten-minute sessions). A recording with p2 lines plugs port 2 in.
grep -q '^p2 ' "$rec" && p2set=" PadType2=1"
{
	echo "# $(basename "$folder"): played by hand, recorded $(date '+%F %T') with scripts/movie_capture.sh"
	echo "# folder: $folder"
	echo "# cue: $cue"
	echo "# vblanks: $vbl"
	echo "# build: commit $commit, $dolinfo, sha1 $sha"
	echo "#   (if it drifts on a later build: build that commit and pass the DOL with --dol)"
	echo "# settings: $settings"
	echo "# pad: ControllerType=$ct ($padname); Dolphin: port 1 keyboard GameCube pad, port 2 GameCube pad (SIDevice1=6)"
	echo "# chain line: $vbl sd:/wiistation/${name}_play.txt PadAutoAssign=1 ControllerType=$ct${extra}${p2set:-}"
	# Marks: each in-game screenshot (the Shot button combo) is a moment the player pointed at;
	# scripts/marks.py makes a page to draw a box on each, then traces that box.
	sed -n 's/^screenshot: saved \([^ ]*\) \([0-9]*x[0-9]*\) .* vbl=\([0-9]*\).*/# mark: \3 \2 \1/p' "$REPO/.runs/$run/perf.log" 2>/dev/null | tr -d '\r'
	tr -d '\r' < "$rec"
} > "$out"
nmarks=$(grep -c '^# mark: ' "$out")
echo "saved scripts/autoinput/${name}_play.txt: $presses changes, the last at vblank ${last:-none}, $nmarks marks (screenshots)"
echo "check it plays back:  bash scripts/movie_capture.sh --play $name"
echo "use it in a chain:    $vbl sd:/wiistation/${name}_play.txt PadAutoAssign=1 ControllerType=$ct${extra}${p2set:-}"
