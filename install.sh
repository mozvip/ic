#!/bin/bash
set -e

BUILD_DIR="${BUILD_DIR:-builddir-install}"
PREFIX="/usr/local"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user) PREFIX="$HOME/.local"; shift ;;
        --prefix) [[ $# -ge 2 ]] || { echo "--prefix requires a directory" >&2; exit 2; }; PREFIX="$2"; shift 2 ;;
        --help) echo "Usage: $0 [--user | --prefix DIRECTORY]"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ "$PREFIX" = /* ]] || { echo "Prefix must be an absolute directory" >&2; exit 2; }
if [[ "$EUID" -ne 0 && ( "$PREFIX" = /usr || "$PREFIX" = /usr/local ) ]]; then
    echo "System installation requires root. Use --user for an unprivileged install." >&2
    exit 1
fi

echo "Checking dependencies..."
# Simple check for meson
if ! command -v meson &> /dev/null; then
    echo "Meson is not installed. Please install it first."
    exit 1
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "Configuring build directory..."
    meson setup "$BUILD_DIR" --buildtype=release --prefix="$PREFIX"
else
    echo "Build directory exists. Reconfiguring..."
    meson setup --reconfigure "$BUILD_DIR" --buildtype=release --prefix="$PREFIX"
fi

echo "Building..."
meson compile -C "$BUILD_DIR"

echo "Installing..."
meson install -C "$BUILD_DIR"

echo "Updating desktop database..."
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database "$PREFIX/share/applications"
fi
if command -v gtk-update-icon-cache &> /dev/null; then
    gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor"
fi

echo "Installation complete! You can now launch 'ic' from your application menu."
