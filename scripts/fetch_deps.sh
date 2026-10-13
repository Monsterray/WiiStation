#!/usr/bin/env bash
# Fetch third-party sources that are NOT committed to this repo.
#
# Usage:
#   scripts/fetch_deps.sh          # fetch every external dep that is missing
#   scripts/fetch_deps.sh wupc     # just libwupc
#
# Sources land in deps/ext/<name> (gitignored). Each dep is pinned to an exact
# commit below; re-running is a no-op once the checkout is present. Patches the
# build needs against the current libogc are kept in deps/patches/ and applied
# here (idempotently) after the checkout.
#
# Runs from Git Bash or devkitPro's MSYS2 shell.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXT="$ROOT/deps/ext"
PATCHES="$ROOT/deps/patches"

# ---- libwupc (Wii U Pro Controller library, FIX94, GPL) ----
WUPC_URL="https://github.com/FIX94/libwupc"
WUPC_PIN="24145b35dbc2c1950f6f48079d577acc7efb0c4d"
WUPC_PATCH="$PATCHES/libwupc-libogc3.patch"

# Clone at the pinned commit (shallow: fetch just that object, not history).
fetch_pinned() {
    local url="$1" pin="$2" dir="$3"
    if [ -d "$dir/.git" ]; then
        echo "[fetch_deps] $dir already present"
        return 0
    fi
    echo "[fetch_deps] cloning $url @ $pin -> $dir"
    mkdir -p "$dir"
    git -C "$dir" init -q
    # Keep the checkout byte-identical to the pinned blobs (no CRLF conversion),
    # so the LF patches in deps/patches apply cleanly on Windows.
    git -C "$dir" config core.autocrlf false
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch -q --depth 1 origin "$pin"
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
    local head
    head="$(git -C "$dir" rev-parse HEAD)"
    if [ "$head" != "$pin" ]; then
        echo "[fetch_deps] ERROR: $dir is at $head, expected $pin" >&2
        return 1
    fi
}

# Apply a patch once: skip if it is already applied (reverse-applies cleanly).
apply_patch() {
    local dir="$1" patch="$2"
    [ -f "$patch" ] || return 0
    if git -C "$dir" apply --check --reverse "$patch" >/dev/null 2>&1; then
        echo "[fetch_deps] patch already applied: $(basename "$patch")"
        return 0
    fi
    echo "[fetch_deps] applying $(basename "$patch")"
    git -C "$dir" apply "$patch"
}

fetch_wupc() {
    local dir="$EXT/libwupc"
    fetch_pinned "$WUPC_URL" "$WUPC_PIN" "$dir"
    apply_patch "$dir" "$WUPC_PATCH"
}

what="${1:-all}"
case "$what" in
    all)  fetch_wupc ;;
    wupc) fetch_wupc ;;
    *)    echo "usage: $0 [all|wupc]" >&2; exit 2 ;;
esac
echo "[fetch_deps] done"
