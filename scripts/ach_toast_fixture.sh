#!/bin/bash
# ach_toast_fixture.sh -- the achievement toast's test set, for scripts/chains/ach_toast.txt.
#   usage: scripts/ach_toast_fixture.sh, then
#     WSX_EXTRA_ISOS=.runs/ach_toast.rom WSX_ISOS=Tests/AchToast scripts/wsx.sh chain NAME scripts/chains/ach_toast.txt \
#       --set 'Achievements=2,AchievementFile="sd:/wiistation/isos/Tests/AchToast/set.txt"' --env FRAMES_DUMP=True --env KEEP=100000
# Spyro the Dragon runs its title and attract demo with no input; every achievement is a hit
# count on a condition that is always true, so each fires a fixed number of emulated frames
# after the set loads, whatever the game does. They are 6.5 s apart so the toasts do not queue:
# one of each tier by points, then the Win that beats the game (all Progression earned) and
# the last one, which masters the set.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
dir="$REPO/.runs/ach_toast.rom/Tests/AchToast"
rm -rf "$dir"
mkdir -p "$dir"
cat > "$dir/set.txt" <<'EOF'
Game=SCUS94228
Title=Spyro
1|Sheila's Friend|0xH000000=0xH000000.300.|5|progression|Free Sheila the kangaroo
2|Sparx Saver|0xH000000=0xH000000.690.|10|missable|Keep Sparx through Sunrise Spring
3|Bonus Round|0xH000000=0xH000000.1080.|25|progression|Reach the Super Bonus Round
4|Skate Perfect|0xH000000=0xH000000.1470.|50||Score 30,000 at Sunny Villa
5|Untouchable|0xH000000=0xH000000.1860.|100|missable|Beat the Sorceress without a hit
6|Year of the Dragon|0xH000000=0xH000000.2250.|25|win|Defeat the Sorceress
7|Golden Egg Hunter|0xH000000=0xH000000.2700.|50||Find all 150 dragon eggs
EOF
echo "ach_toast_fixture: $dir/set.txt"
