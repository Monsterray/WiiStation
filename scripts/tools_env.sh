# Source this (bash): it loads ~/wii-tools.env (NAME="path" lines; scripts/tools.py setup writes it)
# without overriding a variable that is already set, then offers `need NAME LEVEL`: stop with
# one line when NAME is unset. DEVKITPRO is made an MSYS path (/c/devkitPro); a stale
# DEVKITPRO of a login shell (a folder that is not there) is dropped first.
_te="${WII_TOOLS_ENV:-$HOME/wii-tools.env}"
if [ -n "${DEVKITPRO:-}" ] && [ ! -d "$DEVKITPRO" ]; then unset DEVKITPRO; fi
if [ -f "$_te" ]; then
	while IFS='=' read -r _k _v; do
		case "$_k" in ''|\#*) continue ;; esac
		_v="${_v%$'\r'}"; _v="${_v#\"}"; _v="${_v%\"}"
		[ -n "${!_k:-}" ] || export "$_k=$_v"
	done < "$_te"
fi
unset _te _k _v
[ -z "${DEVKITPRO:-}" ] || DEVKITPRO="$(cygpath -u "$DEVKITPRO" 2>/dev/null || echo "$DEVKITPRO")"
need() {   # need NAME "level 2"
	[ -n "${!1:-}" ] || { echo "$1 is not set: run python scripts/tools.py setup (SETUP.md, $2)" >&2; exit 2; }
}
dolphin_dirs() {   # sets DOLPHIN_BASE (the folder of $DOLPHIN, with User/Load) and DOLPHIN_DIR (what WiiStation runs)
	need DOLPHIN "level 2"
	if [ -z "${DOLPHIN_BASE:-}" ]; then
		DOLPHIN_BASE="$(dirname "$(cygpath -u "$DOLPHIN" 2>/dev/null || echo "$DOLPHIN")")"
	fi
	# Dolphin 2609: the Dolphin-2609 folder beside the base, whose User folder is a junction to the base's.
	if [ -z "${DOLPHIN_DIR:-}" ]; then
		DOLPHIN_DIR="$(dirname "$DOLPHIN_BASE")/Dolphin-2609"
		[ -d "$DOLPHIN_DIR" ] || DOLPHIN_DIR="$DOLPHIN_BASE"
	fi
}
