#!/bin/sh
# One photograph of every menu page, for the menu forms rebuild (Docs/MENU_FORMS_PLAN.md step 1).
#
#   sh scripts/menu_pages_walk.sh TAG A.dol            # baseline: one still per page, kept in .runs/walk_cache/<sha1 of A>/pN.png
#   sh scripts/menu_pages_walk.sh TAG A.dol B.dol      # A against B, page by page: frames differing per page
#
# One Dolphin boot per DOL: a "menutour" line (Gamecube/PadWiiSX.c) opens the pages in turn, 60 menu
# frames apart, and scripts/menu_tour.py splits the frames into one still per page. A DOL's stills are
# cached by its hash, so a DOL that was walked before is not run again (its missing pages only).
# Pages are the numbers of MenuContext.cpp's "menupage": 1-5 Settings tabs, 6-14 Options pages, 15
# Achievements list, 16 Current ISO, 17 ISO Info, 18 Players, 20 Configure Input, 21 Co-Op, 40 Configure
# Buttons, 70 Load from SD (keep it last). PAGES="1 2" limits it. The Players page uses the account fixture.
# The DOL must be built with menutour; an older one gives "no still" for every page after the first.
# SLOW=1: the old way, one boot per page and DOL (any DOL; frames of each run are dropped after its compare).

tag=${1:?tag}; a=${2:?A.dol}; b=${3:-}
here=$(cd "$(dirname "$0")" && pwd)
runs=${WSX_RUNS:-$here/../.runs}
[ -f "$here/find_python.sh" ] && . "$here/find_python.sh"   # main: $PYTHON through wii_paths.py; this branch: plain python
PYTHON=${PYTHON:-python}
pages=${PAGES:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 20 21 40 70"}

cache_of() { echo "$runs/walk_cache/$(sha1sum "$1" | cut -c1-12)"; }

shoot() {   # shoot NAME DOL: boot once for the pages the DOL's cache lacks
	cache=$(cache_of "$2"); todo=""
	for p in $pages; do [ -f "$cache/p$p.png" ] || todo="$todo $p"; done
	[ -n "$todo" ] || { echo "$1: all pages cached in $cache"; return; }
	printf 'menutour%s\n' "$todo" > "$runs/walk_$tag.txt"
	secs=$(( $(echo $todo | wc -w) * 60 / 40 + 8 ))
	KEEP_FRAMES=1 bash "$here/wsx.sh" menu "walk_${tag}_$1" --dol "$2" --input "$runs/walk_$tag.txt" --secs "$secs" \
		--sd "$here/../tests/fixtures/players_accounts" > "$runs/walk_${tag}_$1.log" 2>&1
	"$PYTHON" "$here/menu_tour.py" extract "$runs/menu_walk_${tag}_${1}_a" "$cache" "$todo" | grep -v ': frame'
	rm -rf "$runs/menu_walk_${tag}_${1}_a/frames"   # a minute of PNGs per run; the stills are kept
}

if [ -n "${SLOW:-}" ]; then
	for p in $pages; do
		out=$(bash "$here/wsx.sh" menu "${tag}_p$p" --dol "$a" ${b:+--dol2 "$b"} --page "$p" --sd "$here/../tests/fixtures/players_accounts" 2>&1)
		echo "page $p: $(echo "$out" | grep -E 'frames differing|page is frame|did not' | tr -s ' ' | tr '\n' ' ')"
	done
	exit
fi

shoot a "$a"
if [ -n "$b" ]; then
	shoot b "$b"
	"$PYTHON" "$here/menu_tour.py" diff "$(cache_of "$a")" "$(cache_of "$b")" "$pages"
else
	echo "stills in $(cache_of "$a")"
fi
