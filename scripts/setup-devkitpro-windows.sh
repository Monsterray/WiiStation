#!/usr/bin/env bash
# One-time toolchain setup for building WiiStation on Windows (Git Bash / MSYS2).
#
# Installs, into C:/devkitPro (no admin rights required):
#   - devkitPPC r41-2 (the exact compiler version this project's headers/lib
#     ABI were built against -- newer devkitPPC releases are NOT drop-in
#     compatible with the bundled libogc2/lightrec/lightning binaries below)
#   - libogc2 + SDL + GNU Lightning + Lightrec, from this repo's own
#     lightrec+Libogc2.zip (already checked into the repo root)
#   - the small devkitPro command-line tools that don't ship with the
#     compiler package: make, elf2dol, gxtexconv, bin2s
#   - the ppc-zlib portlib (zlib.h + libz.a), which the main codebase
#     includes directly as <zlib.h>
#
# Safe to re-run: every step is skipped if its target already exists.
set -euo pipefail

DEVKITPRO=/c/devkitPro
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIRROR="https://wii.leseratte10.de/devkitPro"

log() { printf '\n==> %s\n' "$1"; }

mkdir -p "$DEVKITPRO"

# --- 1. devkitPPC r41-2 compiler toolchain ---------------------------------
if [ -x "$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc.exe" ]; then
	log "devkitPPC already installed, skipping"
else
	log "Downloading devkitPPC r41-2 (~51MB)"
	tmp=$(mktemp -d)
	curl -L -o "$tmp/devkitPPC.pkg.tar.xz" \
		"$MIRROR/devkitPPC/r41%20(2022-05-31)/devkitPPC-r41-2-windows_x86_64.pkg.tar.xz"
	tar -xf "$tmp/devkitPPC.pkg.tar.xz" -C "$tmp"
	mv "$tmp/opt/devkitpro/devkitPPC" "$DEVKITPRO/devkitPPC"
	rm -rf "$tmp"
fi

# --- 2. base_tools (defines CC/CXX/AR/PORTLIBS_PATH for the make rules) ----
if [ -f "$DEVKITPRO/devkitPPC/base_tools" ]; then
	log "base_tools already installed, skipping"
else
	log "Downloading devkitppc-rules (base_tools)"
	tmp=$(mktemp -d)
	curl -sL -o "$tmp/rules.tar.gz" \
		"$MIRROR/devkitPPC/devkitppc-rules/devkitppc-rules-1.1.1.tar.gz"
	tar -xf "$tmp/rules.tar.gz" -C "$tmp"
	cp "$tmp"/devkitppc-rules-*/base_tools "$DEVKITPRO/devkitPPC/base_tools"
	rm -rf "$tmp"
fi

# --- 3. libogc2 + SDL + Lightning + Lightrec (bundled in this repo) --------
if [ -d "$DEVKITPRO/libogc2" ]; then
	log "libogc2 already installed, skipping"
else
	log "Extracting bundled lightrec+Libogc2.zip"
	tmp=$(mktemp -d)
	unzip -oq "$REPO_ROOT/lightrec+Libogc2.zip" -d "$tmp"
	cp -rn "$tmp/lightrec+Libogc2/devkitPPC/"* "$DEVKITPRO/devkitPPC/"
	cp -r "$tmp/lightrec+Libogc2/libogc2" "$DEVKITPRO/libogc2"
	rm -rf "$tmp"
fi

# wii_rules / gamecube_rules also need to exist directly under devkitPPC --
# the per-dependency Makefiles (opengx, lightrec, zstd, lzma, zlib, chdr)
# all `include $(DEVKITPPC)/wii_rules` at that standard devkitPro location,
# while Gamecube/Makefile_Wii instead includes libogc2's copy directly.
# Both need the same file.
cp -f "$DEVKITPRO/libogc2/wii_rules" "$DEVKITPRO/devkitPPC/wii_rules"
cp -f "$DEVKITPRO/libogc2/gamecube_rules" "$DEVKITPRO/devkitPPC/gamecube_rules"

# --- 4. devkitPro command-line tools not bundled with the compiler --------
mkdir -p "$DEVKITPRO/tools/bin"

if [ -x "$DEVKITPRO/tools/bin/make.exe" ]; then
	log "make already installed, skipping"
else
	log "Downloading standalone GNU Make 4.4.1 (no admin rights needed)"
	tmp=$(mktemp -d)
	curl -sL -o "$tmp/make.zip" \
		"https://sourceforge.net/projects/ezwinports/files/make-4.4.1-without-guile-w32-bin.zip/download"
	unzip -oq "$tmp/make.zip" -d "$tmp/x"
	cp "$tmp/x/bin/make.exe" "$DEVKITPRO/tools/bin/make.exe"
	rm -rf "$tmp"
fi

if [ -x "$DEVKITPRO/tools/bin/elf2dol.exe" ]; then
	log "elf2dol/gxtexconv already installed, skipping"
else
	log "Downloading gamecube-tools (elf2dol, gxtexconv)"
	tmp=$(mktemp -d)
	curl -sL -o "$tmp/gc-tools.pkg.tar.xz" \
		"$MIRROR/other-stuff/gamecube-tools/gamecube-tools-1.0.4-1-windows_x86_64.pkg.tar.xz"
	tar -xf "$tmp/gc-tools.pkg.tar.xz" -C "$tmp"
	cp "$tmp/opt/devkitpro/tools/bin/"*.exe "$DEVKITPRO/tools/bin/"
	rm -rf "$tmp"
fi

if [ -x "$DEVKITPRO/tools/bin/bin2s.exe" ]; then
	log "bin2s already installed, skipping"
else
	log "Downloading general-tools (bin2s and friends)"
	tmp=$(mktemp -d)
	curl -sL -o "$tmp/gen-tools.pkg.tar.xz" \
		"$MIRROR/other-stuff/general-tools/general-tools-1.2.0-3-windows_x86_64.pkg.tar.xz"
	tar -xf "$tmp/gen-tools.pkg.tar.xz" -C "$tmp"
	cp "$tmp/opt/devkitpro/tools/bin/"*.exe "$DEVKITPRO/tools/bin/"
	rm -rf "$tmp"
fi

# --- 5. ppc-zlib portlib (zlib.h + libz.a, used directly as <zlib.h>) ------
mkdir -p "$DEVKITPRO/portlibs/ppc" "$DEVKITPRO/portlibs/wii"
if [ -f "$DEVKITPRO/portlibs/ppc/include/zlib.h" ]; then
	log "ppc-zlib already installed, skipping"
else
	log "Downloading ppc-zlib portlib"
	tmp=$(mktemp -d)
	curl -sL -o "$tmp/ppc-zlib.pkg.tar.xz" \
		"$MIRROR/portlibs/ppc-zlib-1.2.11-1-any.pkg.tar.xz"
	tar -xf "$tmp/ppc-zlib.pkg.tar.xz" -C "$tmp"
	cp -r "$tmp/opt/devkitpro/portlibs/ppc/"* "$DEVKITPRO/portlibs/ppc/"
	rm -rf "$tmp"
fi

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

Done. Build with:
  scripts/build.sh
EOF
