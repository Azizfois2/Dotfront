#!/bin/sh
# Install the release files into the current user's XDG directories.
set -eu
PREFIX=${PREFIX:-$HOME/.local}
HERE=$(cd "$(dirname "$0")" && pwd)
install -Dm755 "$HERE/dotfront" "$PREFIX/bin/dotfront"
install -Dm644 "$HERE/dotfront.png" \
    "$PREFIX/share/icons/hicolor/256x256/apps/dotfront.png"
install -Dm644 "$HERE/dotfront.desktop" \
    "$PREFIX/share/applications/dotfront.desktop"
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$PREFIX/share/applications" || true
echo "Installed Dotfront. Run: dotfront"
