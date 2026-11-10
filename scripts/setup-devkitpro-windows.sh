#!/usr/bin/env bash
# One-time toolchain setup for building WiiStation on Windows.
#
# This uses the REAL devkitPro pacman package manager for everything it can
# provide: general-tools, gamecube-tools, the zlib portlib, cmake support,
# wiiload, etc. all come from the official, signed, currently-maintained
# devkitPro repositories, tracked in pacman's own package database like any
# other install.
#
# WiiStation builds against pacman's CURRENT devkitPPC and devkitPro's libogc v3
# (with libfat-ogc and wii-sdl from pacman; the remaining libraries are rebuilt
# from deps/ by scripts/build.sh). The old r41-2 / libogc2 build and its bundled
# prebuilt libraries were removed in 6.0.0.
#
# Prerequisites (do this once, manually -- it needs admin rights this script
# cannot obtain on its own):
#   1. Download the official installer:
#      https://github.com/devkitPro/installer/releases/latest
#      (devkitProUpdater-*.exe)
#   2. Right-click it -> Run as administrator.
#   3. In the component picker, check "Wii Development". Leave the install
#      path at its default (C:\devkitPro).
#
# Everything below is safe to re-run.
set -euo pipefail

. "$(dirname "${BASH_SOURCE[0]}")/tools_env.sh"   # ~/wii-tools.env
DEVKITPRO="${DEVKITPRO:-/c/devkitPro}"   # else the installer's default folder
PACMAN="$DEVKITPRO/msys2/usr/bin/pacman.exe"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

log() { printf '\n==> %s\n' "$1"; }

if [ ! -x "$PACMAN" ]; then
	cat <<EOF
devkitPro's pacman was not found at:
  $PACMAN

Run the official installer first (needs admin rights, which this script
cannot obtain on its own):
  1. Download: https://github.com/devkitPro/installer/releases/latest
  2. Right-click the .exe -> Run as administrator
  3. Check "Wii Development" in the component picker; keep the default
     install path (C:\\devkitPro)

Then re-run this script.
EOF
	exit 1
fi

# --- 1. Everything pacman can provide, via the real package manager -------
log "Installing/updating wii-dev, wii-sdl, ppc-zlib and unzip via pacman"
"$PACMAN" -Sy --noconfirm
# wii-dev: devkitPPC, libogc (v3), libfat-ogc; wii-sdl: the SDL 1.2 the default (libogc v3)
# build links for its sound output (dfsound/sdl.c)
"$PACMAN" -S --needed --noconfirm wii-dev wii-sdl ppc-zlib unzip

log "Toolchain setup complete."

if [[ "$REPO_ROOT" == *" "* ]]; then
	cat <<EOF

WARNING: this repo is checked out at a path containing a space:
  $REPO_ROOT
devkitPro's Makefiles pass \$(CURDIR) to the compiler unquoted, so a space
in the path breaks the build with confusing "file not found" errors.
Move or clone the repo to a path with no spaces (e.g. ~/projects/WiiStation)
before running scripts/build.sh.
EOF
fi

cat <<'EOF'

Done. Build with (from devkitPro's own MSYS2 shell -- Start Menu ->
devkitPro -> MSys2, or C:\devkitPro\msys2\msys2_shell.bat):
  scripts/build.sh
EOF
