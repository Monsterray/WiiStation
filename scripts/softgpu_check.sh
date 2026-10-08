#!/usr/bin/env bash
# softgpu_check.sh --save NAME | --compare NAME [--plugins "0 1"] [--dol debug|PATH]
#
# The accuracy check for the software GPU plugins (0 = old soft, PeopsSoftGPU; 1 = new
# soft, SoftGPU). They draw into emulated VRAM and emulation is deterministic, so a VRAM
# fingerprint (statefp) at fixed vblanks is the same on every run of the same build -- and
# an optimization that changes no pixel leaves every fingerprint as it was. That is what
# makes these plugins the reference to judge OpenGX against.
#
#   --save NAME     run, then keep the fingerprints as baselines/softgpu/NAME.txt
#   --compare NAME  run, then compare with that baseline: any difference is a changed pixel
#
# The games: the user's recordings (scripts/autoinput/*_play.txt) plus FF7 and Gex, which
# need no input to reach their first scenes. Each line of the chain also prints its speed;
# a debug build with PROBES=deep adds "gpucmd:" (time by GP0 command) to perf.log.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
mode=""; name=""; plugins="0 1"; dol=debug
while [ $# -gt 0 ]; do
	case "$1" in
		--save|--compare) mode="$1"; name="$2"; shift 2 ;;
		--plugins) plugins="$2"; shift 2 ;;
		--dol) dol="$2"; shift 2 ;;
		*) echo "unknown option $1"; exit 2 ;;
	esac
done
[ -n "$mode" ] && [ -n "$name" ] || { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

A="$REPO/scripts/autoinput"
run=softgpu_check
chain="$REPO/.runs/$run.chain.txt"
# game: vblanks, script (a recording or none), pad type, folder, cue
games=(
	"2400|spyro_the_dragon_play|0|sd:/wiistation/isos/Spyro the Dragon|Spyro the Dragon [NTSC-U] [SCUS-94228].cue"
	"3000|crash_bandicoot_3_warped_play|0|sd:/wiistation/isos/Crash Bandicoot 3 - Warped|Crash Bandicoot 3 - Warped [U] [SLUS-94244].cue"
	"2400|crash_bash_play|0|sd:/wiistation/isos/Crash Bash|Crash Bash [U] [SCUS-94570].cue"
	"3600|ape_escape_play|1|sd:/wiistation/isos/Ape Escape|Ape Escape [U] [SCUS-94423].cue"
	"1500||0|sd:/wiistation/isos/Final Fantasy VII/Final Fantasy VII 1|Final Fantasy VII [U] [Disc 1] [SLUS-94163].cue"
	"1500||0|sd:/wiistation/isos/Gex (USA)|Gex (USA).cue"
)
echo CHAIN > "$chain"
made=()
for g in "${games[@]}"; do
	IFS='|' read -r vbl rec ct folder cue <<< "$g"
	s="_softcheck_${rec:-$(basename "$folder" | tr -cd 'A-Za-z0-9')}.txt"
	{
		# three fingerprints per game, the last just before its end
		for v in $((vbl / 4)) $((vbl / 2)) $((vbl - 30)); do echo "statefp $v"; done
		[ -n "$rec" ] && grep -v '^#' "$A/${rec}.txt"
	} > "$A/$s"
	made+=("$A/$s")
	for p in $plugins; do
		printf '%s sd:/wiistation/%s PadAutoAssign=1 ControllerType=%s gpuPlugin=%s\n%s\n%s\n' \
			"$vbl" "$s" "$ct" "$p" "$folder" "$cue" >> "$chain"
	done
done
bash "$REPO/scripts/wsx.sh" chain "$run" "$chain" --dol "$dol" --secs 2400 | tail -n $(( ${#games[@]} * 2 + 2 ))
rm -f "${made[@]}"

# one line per fingerprint: game and plugin (from the chain markers), vblank, the hashes
out="$REPO/.runs/$run/fingerprints.txt"
awk '/^statefp:/ { fp[++n] = $0 }
     /^=== chain / { match($0, /set=[^ ]*/); set = substr($0, RSTART + 4, RLENGTH - 4);
                     match($0, /rom=.*\.cue/); rom = substr($0, RSTART + 4, RLENGTH - 4);
                     for (i = 1; i <= n; i++) { f = fp[i]; sub(/ cycle [0-9]+/, "", f); print rom " | " set " | " f }
                     n = 0 }' "$REPO/.runs/$run/perf.log" > "$out"
grep -a "^gpucmd:" "$REPO/.runs/$run/perf.log" | tail -n 3 || true
base="$REPO/baselines/softgpu/$name.txt"
if [ "$mode" = --save ]; then
	mkdir -p "$(dirname "$base")"
	cp "$out" "$base"
	echo "saved $(wc -l < "$out") fingerprints as baselines/softgpu/$name.txt"
else
	[ -f "$base" ] || { echo "no baseline $base"; exit 2; }
	if diff -q "$base" "$out" > /dev/null; then
		echo "IDENTICAL: all $(wc -l < "$out") fingerprints match baselines/softgpu/$name.txt"
	else
		echo "DIFFERENT from baselines/softgpu/$name.txt:"
		diff "$base" "$out" | head -n 20
		exit 1
	fi
fi
