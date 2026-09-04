#!/usr/bin/env bash
# One-time toolchain setup for building WiiStation on Windows.
#
# This uses the REAL devkitPro pacman package manager for everything it can
# provide: general-tools, gamecube-tools, the zlib portlib, cmake support,
# wiiload, etc. all come from the official, signed, currently-maintained
# devkitPro repositories, tracked in pacman's own package database like any
# other install.
#
# The one deliberate exception: this project ships prebuilt libogc2 (Extrems'
# fork, not the official "libogc" pacman provides) plus Lightrec and GNU
# Lightning binaries, all built against devkitPPC r41-2 specifically. Newer
# devkitPPC releases are not ABI-compatible with those prebuilt binaries, and
# devkitPro's own servers only ever serve the *current* devkitPPC release --
# old versions are simply not retained there, by policy, for anyone's
# project. r41-2 is installed to its own devkitPPC-r41-2 directory (never
# overlapping pacman's own current devkitPPC install) purely for that reason.
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

DEVKITPRO=/c/devkitPro
PACMAN="$DEVKITPRO/msys2/usr/bin/pacman.exe"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIRROR="https://wii.leseratte10.de/devkitPro"

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
log "Installing/updating wii-dev, ppc-zlib and unzip via pacman"
"$PACMAN" -Sy --noconfirm
"$PACMAN" -S --needed --noconfirm wii-dev ppc-zlib unzip

# --- 2. devkitPPC r41-2, pinned for ABI compatibility with this project's --
#        bundled prebuilt libogc2/lightrec/lightning (see header comment)
DEVKITPPC_PIN="$DEVKITPRO/devkitPPC-r41-2"
if [ -x "$DEVKITPPC_PIN/bin/powerpc-eabi-gcc.exe" ]; then
	log "devkitPPC r41-2 already installed, skipping"
else
	log "Downloading devkitPPC r41-2 (~51MB, pinned -- see header comment)"
	tmp=$(mktemp -d)
	curl -L -o "$tmp/devkitPPC.pkg.tar.xz" \
		"$MIRROR/devkitPPC/r41%20(2022-05-31)/devkitPPC-r41-2-windows_x86_64.pkg.tar.xz"
	tar -xf "$tmp/devkitPPC.pkg.tar.xz" -C "$tmp"
	mv "$tmp/opt/devkitpro/devkitPPC" "$DEVKITPPC_PIN"
	rm -rf "$tmp"
fi

# --- 3. libogc2 + SDL + Lightning + Lightrec (bundled in this repo, this --
#        project's own choice of fork/build, not something pacman ships)
if [ -d "$DEVKITPRO/libogc2" ]; then
	log "libogc2 already installed, skipping"
else
	log "Extracting bundled lightrec+Libogc2.zip"
	tmp=$(mktemp -d)
	unzip -oq "$REPO_ROOT/lightrec+Libogc2.zip" -d "$tmp"
	cp -rn "$tmp/lightrec+Libogc2/devkitPPC/"* "$DEVKITPPC_PIN/"
	cp -r "$tmp/lightrec+Libogc2/libogc2" "$DEVKITPRO/libogc2"
	rm -rf "$tmp"
fi

# --- 4. wii_rules / gamecube_rules / base_tools for the pinned compiler ---
#        wii_rules/gamecube_rules must point LIBOGC_INC/LIB at libogc2, not
#        pacman's official libogc -- libogc2's own copies do that, so use
#        those instead of whatever the standard devkitPPC package ships.
#        base_tools we take from pacman's current devkitppc-rules install
#        (same file wii-dev already gave us) rather than fetching a second,
#        separately-versioned copy -- it only sets PATH/PORTLIBS_PATH/the
#        compiler-prefix variables, none of which are version-sensitive.
cp -f "$DEVKITPRO/libogc2/wii_rules" "$DEVKITPPC_PIN/wii_rules"
cp -f "$DEVKITPRO/libogc2/gamecube_rules" "$DEVKITPPC_PIN/gamecube_rules"
cp -f "$DEVKITPRO/devkitPPC/base_tools" "$DEVKITPPC_PIN/base_tools"
# The stock base_tools hardcodes $(DEVKITPRO)/devkitPPC/bin into PATH instead
# of using $(DEVKITPPC) -- harmless with only one devkitPPC install, but it
# silently prepends pacman's *current* compiler ahead of this pinned one
# once both exist side by side. Point it at $(DEVKITPPC) instead.
sed -i 's#\$(DEVKITPATH)/devkitPPC/bin#$(DEVKITPPC)/bin#' "$DEVKITPPC_PIN/base_tools"

log "Toolchain setup complete."

if [[ "$REPO_ROOT" == *" "* ]]; then
	cat <<EOF

WARNING: this repo is checked out at a path containing a space:
  $REPO_ROOT
devkitPro's Makefiles pass \$(CURDIR) to the compiler unquoted, so a space
in the path breaks the build with confusing "file not found" errors.
Move or clone the repo to a path with no spaces (e.g. C:/projects/WiiStation)
before running scripts/build.sh.
EOF
fi

cat <<'EOF'

Done. Build with (from devkitPro's own MSYS2 shell -- Start Menu ->
devkitPro -> MSys2, or C:\devkitPro\msys2\msys2_shell.bat):
  scripts/build.sh
EOF
