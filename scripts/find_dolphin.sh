# Source this file after find_python.sh: it sets DOLPHIN_BASE and DOLPHIN_DIR.
#
# DOLPHIN_BASE is the folder of the Dolphin that wii_paths.py names ($DOLPHIN or
# ~/.wii-paths.json): its User folder holds the shared SD sync folder (User/Load).
# DOLPHIN_DIR is the Dolphin that WiiStation runs: the environment's, else the
# Dolphin-2609 folder next to DOLPHIN_BASE (its User folder is a junction to
# DOLPHIN_BASE/User), else DOLPHIN_BASE itself. Either stays empty when no Dolphin is found.
if [ -z "${DOLPHIN_BASE:-}" ]; then
	DOLPHIN_BASE="$("$PYTHON" "$(dirname "${BASH_SOURCE[0]}")/wii_paths.py" dolphin 2>/dev/null | tr -d '\r')"
	if [ -n "$DOLPHIN_BASE" ]; then
		DOLPHIN_BASE="$(cygpath -u "$DOLPHIN_BASE" 2>/dev/null || echo "$DOLPHIN_BASE")"
		DOLPHIN_BASE="$(dirname "$DOLPHIN_BASE")"
	fi
fi
if [ -z "${DOLPHIN_DIR:-}" ] && [ -n "$DOLPHIN_BASE" ]; then
	DOLPHIN_DIR="$(dirname "$DOLPHIN_BASE")/Dolphin-2609"
	[ -d "$DOLPHIN_DIR" ] || DOLPHIN_DIR="$DOLPHIN_BASE"
fi
[ -n "${DOLPHIN_BASE:-}" ] || DOLPHIN_BASE="${DOLPHIN_DIR:-}"
[ -n "${DOLPHIN_DIR:-}" ] || echo "no Dolphin found: set DOLPHIN_DIR, or DOLPHIN (see scripts/wii_paths.py --all)" >&2
