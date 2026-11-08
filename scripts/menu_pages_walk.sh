#!/bin/sh
# One photograph of every menu page, for the menu forms rebuild (Docs/MENU_FORMS_PLAN.md step 1).
#
#   sh scripts/menu_pages_walk.sh TAG A.dol            # baseline: one still per page, kept in .runs/menu_TAG_pN
#   sh scripts/menu_pages_walk.sh TAG A.dol B.dol      # A against B, page by page: frames differing per page
#
# Pages are the numbers of MenuContext.cpp's "menupage": 1-5 Settings tabs, 6-14 Options pages, 15 Achievements
# list, 16 Current ISO, 17 ISO Info, 18 Players, 20 Configure Input, 21 Co-Op, 40 Configure Buttons, 70 Load from SD.
# PAGES="1 2" limits it. The Players page uses the account fixture.

tag=${1:?tag}; a=${2:?A.dol}; b=${3:-}
here=$(cd "$(dirname "$0")" && pwd)
pages=${PAGES:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 20 21 40 70"}
for p in $pages; do
	if [ -n "$b" ]; then
		out=$(bash "$here/wsx.sh" menu "${tag}_p$p" --dol "$a" --dol2 "$b" --page "$p" --sd "$here/../tests/fixtures/players_accounts" 2>&1)
		echo "page $p: $(echo "$out" | grep -E 'frames differing|did not' | tr -s ' ' | tr '\n' ' ')"
	else
		out=$(bash "$here/wsx.sh" menu "${tag}_p$p" --dol "$a" --page "$p" --sd "$here/../tests/fixtures/players_accounts" 2>&1)
		echo "page $p: $(echo "$out" | grep -E "page is frame|did not|no still" | head -1)"
	fi
done
