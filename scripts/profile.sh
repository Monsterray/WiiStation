#!/bin/bash
# profile.sh -- the Dolphin user directories WiiStation runs use (dolphin_run.sh, wsx.sh).
#
#   profile.sh new NAME [--ephemeral] [--game G]...   build .profiles/NAME from profiles/template,
#                                                     then stage the named games; prints its path
#   profile.sh stage DIR|NAME [--game G]...           copy BIOS, fonts, save states and the named
#                                                     games from the library into an existing profile
#   profile.sh path NAME|DIR                          absolute path of a profile (any old name too)
#   profile.sh list                                   every profile: kind, lock owner, age, size
#   profile.sh lock NAME OWNER PID                    take the lock; refused, naming the holder, if held
#   profile.sh unlock NAME PID [--force]              release the lock held by PID
#   profile.sh busy-pids NAME                         Windows pids of Dolphin.exe running on this profile
#   profile.sh dolphin-busy NAME|ANY                  exit 0 if one runs (ANY: on any profile of this repo)
#   profile.sh gc                                     delete unlocked ephemeral profiles older than 2 days;
#                                                     clear locks whose holder is gone
#   profile.sh rm NAME                                promote its save states to the library, then delete it
#   profile.sh keys                                   every game key in the library (WSX_ISOS=all)
#
# A game is named by its path under the library's games/ folder: "Spyro the Dragon",
# "Final Fantasy VII/Final Fantasy VII 1". The library is WSX_LIBRARY (default /c/wiistation-lib);
# only this script's stage/rm and dolphin_run.sh's collect step write to it, never the user's Dolphin
# folder. WSX_EXTRA_ISOS=DIR is a second source: a game folder found there wins over the library.
#
# The lock is a directory, .profiles/locks/NAME.lock, holding "owner=... pid=... since=...". mkdir is
# the atomic step. A lock whose pid is gone is stale and is taken over.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB="${WSX_LIBRARY:-/c/wiistation-lib}"
PROFILES="$REPO/.profiles"
LOCKS="$PROFILES/locks"
TEMPLATE="$REPO/profiles/template"
PYTHON="${PYTHON:-/c/Python312/python.exe}"

die() { echo "profile.sh: $*" >&2; exit 2; }
lower() { tr 'A-Z' 'a-z'; }
is_alive() { [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null; }
win() { (cd "$1" && pwd -W | tr '/' '\\'); }

# A profile name or path -> absolute directory. Old names (.dolphin2) resolve under the repo.
resolve() {
	local d
	for d in "$REPO/$1" "$PROFILES/$1" "$1"; do
		if [ -d "$d" ]; then (cd "$d" && pwd); return 0; fi
	done
	return 1
}

dolphin_cmdlines() {
	powershell.exe -NoProfile -Command 'Get-CimInstance Win32_Process | Where-Object Name -eq Dolphin.exe | ForEach-Object { "$($_.ProcessId)|$($_.CommandLine)" }' 2>/dev/null | tr -d '\r'
}

# Dolphin pids whose -u is exactly this directory. The path must END at a quote, a space or the end
# of the command line, so ".dolphin" does not match ".dolphin2". Case is ignored (Windows paths).
busy_pids() {
	local p; p=$(printf '%s' "$1" | lower)
	dolphin_cmdlines | while IFS='|' read -r pid cmd; do
		[ -n "$pid" ] || continue
		c=$(printf '%s' "$cmd" | lower)
		case "$c" in *"$p"\"*|*"$p "*|*"$p") echo "$pid";; esac
	done
}

lock_pid() { sed -n 's/.*pid=\([0-9]*\).*/\1/p' "$LOCKS/$1.lock/owner" 2>/dev/null; }

take_lock() {   # NAME OWNER PID
	local name="$1" owner="$2" pid="$3" d="$LOCKS/$1.lock"
	mkdir -p "$LOCKS"
	if ! mkdir "$d" 2>/dev/null; then
		if is_alive "$(lock_pid "$name")"; then
			echo "profile $name is locked: $(cat "$d/owner" 2>/dev/null)" >&2; return 3
		fi
		rm -rf "$d"; mkdir "$d" 2>/dev/null || { echo "profile $name: lock race" >&2; return 3; }
	fi
	printf 'owner=%s pid=%s since=%s\n' "$owner" "$pid" "$(date '+%F %T')" > "$d/owner"
}

unlock_lock() {   # NAME PID [--force]
	local name="$1" pid="$2" d="$LOCKS/$1.lock"
	[ -d "$d" ] || return 0
	if [ "${3:-}" != "--force" ] && [ "$(lock_pid "$name")" != "$pid" ]; then
		echo "profile $name: lock is held by $(cat "$d/owner"), not pid $pid; not released" >&2; return 1
	fi
	rm -rf "$d"
}

# Copy what a run needs into a profile. cp -ru: a named profile keeps what it has.
stage_into() {   # DIR GAME...
	local dir="$1" w g src x; shift
	w="$dir/Load/WiiSDSync/wiistation"
	mkdir -p "$w/saves" "$w/isos" "$dir/Logs" "$dir/Load"
	for x in bios fonts; do
		[ -d "$LIB/$x" ] && { mkdir -p "$w/$x"; cp -ru "$LIB/$x/." "$w/$x/"; }
	done
	[ -d "$LIB/states" ] && { mkdir -p "$w/states"; cp -ru "$LIB/states/." "$w/states/"; }
	for g in "$@"; do
		src="${WSX_EXTRA_ISOS:-/nonexistent}/$g"
		[ -d "$src" ] || src="$LIB/games/$g"
		[ -d "$src" ] || die "no game \"$g\" in $LIB/games (or WSX_EXTRA_ISOS)"
		mkdir -p "$w/isos/$g"
		cp -ru "$src/." "$w/isos/$g/"
	done
}

parse_games() {   # sets GAMES=() from --game G options; other args into OTHER=()
	GAMES=(); OTHER=()
	while [ $# -gt 0 ]; do
		case "$1" in
			--game) GAMES+=("$2"); shift 2 ;;
			*) OTHER+=("$1"); shift ;;
		esac
	done
}

cmd_new() {
	local name="${1:-}"; [ -n "$name" ] || die "new NAME [--ephemeral] [--game G]..."; shift
	parse_games "$@"
	local eph=""; for o in "${OTHER[@]+"${OTHER[@]}"}"; do [ "$o" = --ephemeral ] && eph=1; done
	local dir="$PROFILES/$name"
	[ -e "$dir" ] && die "$dir already exists"
	[ -d "$TEMPLATE" ] || die "no template at $TEMPLATE"
	mkdir -p "$dir"
	cp -r "$TEMPLATE/." "$dir/"
	[ -d "$LIB/wii" ] && cp -r "$LIB/wii" "$dir/Wii"   # the NAND skeleton: saves a first-boot build
	stage_into "$dir" "${GAMES[@]+"${GAMES[@]}"}"
	printf '%s\n' "${GAMES[@]+"${GAMES[@]}"}" > "$dir/MANIFEST"
	[ -n "$eph" ] && date +%s > "$dir/EPHEMERAL"
	echo "$dir"
}

cmd_stage() {
	local dir; dir=$(resolve "${1:-}") || die "no profile $1"; shift
	parse_games "$@"
	stage_into "$dir" "${GAMES[@]+"${GAMES[@]}"}"
}

cmd_busy() {   # NAME|ANY: exit 0 when a Dolphin is on it
	if [ "$1" = ANY ]; then
		local d
		for d in "$PROFILES"/*/ "$REPO/.dolphin" "$REPO/.dolphin2"; do
			[ -d "$d" ] && [ -n "$(busy_pids "$(win "$d")")" ] && return 0
		done
		return 1
	fi
	local dir; dir=$(resolve "$1") || return 1
	[ -n "$(busy_pids "$(win "$dir")")" ]
}

cmd_list() {
	local d name kind owner age size
	for d in "$PROFILES"/*/; do
		[ -d "$d" ] || continue; d="${d%/}"; name=$(basename "$d")
		[ "$name" = locks ] && continue
		kind=named; [ -f "$d/EPHEMERAL" ] && kind=run
		owner="-"; [ -f "$LOCKS/$name.lock/owner" ] && owner="$(cat "$LOCKS/$name.lock/owner")"
		age=$(( ( $(date +%s) - $(stat -c %Y "$d") ) / 3600 ))
		size=$(du -sh "$d" 2>/dev/null | cut -f1)
		printf '%-30s %-6s %6sh %8s  %s\n' "$name" "$kind" "$age" "$size" "$owner"
	done
}

cmd_gc() {
	local d name freed
	for d in "$PROFILES"/*/; do
		[ -d "$d" ] || continue; d="${d%/}"; name=$(basename "$d")
		[ "$name" = locks ] && continue
		[ -f "$d/EPHEMERAL" ] || continue
		[ -d "$LOCKS/$name.lock" ] && continue
		if [ -n "$(find "$d" -maxdepth 0 -mmin +2880)" ]; then
			freed=$(du -sh "$d" | cut -f1); rm -rf "$d"; echo "removed $name ($freed)"
		fi
	done
	for d in "$LOCKS"/*.lock; do
		[ -d "$d" ] || continue
		is_alive "$(sed -n 's/.*pid=\([0-9]*\).*/\1/p' "$d/owner" 2>/dev/null)" || { rm -rf "$d"; echo "cleared stale lock $(basename "$d")"; }
	done
}

cmd_rm() {
	local name="${1:-}" dir; [ -n "$name" ] || die "rm NAME"
	dir=$(resolve "$name") || die "no profile $name"
	case "$dir" in "$PROFILES"/*) ;; *) die "rm deletes only profiles under $PROFILES" ;; esac
	[ -d "$LOCKS/$(basename "$dir").lock" ] && is_alive "$(lock_pid "$(basename "$dir")")" && die "$name is locked"
	[ -n "$(busy_pids "$(win "$dir")")" ] && die "a Dolphin still runs on $name"
	if [ -d "$dir/Load/WiiSDSync/wiistation/states" ]; then
		mkdir -p "$LIB/states"; cp -ru "$dir/Load/WiiSDSync/wiistation/states/." "$LIB/states/"
	fi
	rm -rf "$dir" && echo "removed $name"
}

main() {
	local cmd="${1:-}"; shift || true
	case "$cmd" in
		new) cmd_new "$@" ;;
		stage) cmd_stage "$@" ;;
		path) resolve "${1:-}" || die "no profile ${1:-}" ;;
		list) cmd_list ;;
		lock) take_lock "$1" "$2" "$3" ;;
		unlock) unlock_lock "$@" ;;
		busy-pids) local dir; dir=$(resolve "$1") && busy_pids "$(win "$dir")" ;;
		dolphin-busy) cmd_busy "$1" ;;
		gc) cmd_gc ;;
		rm) cmd_rm "$@" ;;
		keys) "$PYTHON" "$REPO/scripts/library_check.py" --keys ;;
		*) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//' ;;
	esac
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then main "$@"; fi
