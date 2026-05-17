#!/bin/sh
# POSIX-compliant install script for mt

set -e

PREFIX="${PREFIX:-/usr/local}"
BINDIR="${BINDIR:-$PREFIX/bin}"
DATADIR="${DATADIR:-$PREFIX/share}"
APPDIR="$DATADIR/applications"

cd "$(dirname "$0")"

# Build if binary is missing or sources are newer
if [ ! -f mt ] || [ mt.desktop -nt mt ] 2>/dev/null || \
   [ main.c -nt mt ] || [ wayland.c -nt mt ]; then
    make
fi

# Install binary
mkdir -p "$BINDIR"
install -m 755 mt "$BINDIR/mt"

# Install .desktop entry (for rofi, wofi, dmenu, etc.)
mkdir -p "$APPDIR"
install -m 644 mt.desktop "$APPDIR/mt.desktop"

# Refresh desktop index if available
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPDIR"
fi

printf 'installed mt       -> %s\n' "$BINDIR/mt"
printf 'installed shortcut -> %s\n' "$APPDIR/mt.desktop"
