#!/bin/sh
# Install the release files into the current user's XDG directories.
set -eu
PREFIX=${PREFIX:-$HOME/.local}
HERE=$(cd "$(dirname "$0")" && pwd)
install -Dm755 "$HERE/wod_c" "$PREFIX/bin/wod_c"
install -Dm644 "$HERE/war-of-dots.png" \
    "$PREFIX/share/icons/hicolor/256x256/apps/war-of-dots.png"
install -Dm644 "$HERE/war-of-dots.desktop" \
    "$PREFIX/share/applications/war-of-dots.desktop"
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$PREFIX/share/applications" || true
echo "Installed War of Dots. Run: wod_c"
