#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-appimage}"
APPDIR="${APPDIR:-$ROOT_DIR/AppDir}"

cd "$ROOT_DIR"

if ! command -v meson >/dev/null 2>&1; then
  echo "meson is required" >&2
  exit 1
fi

if ! command -v linuxdeploy >/dev/null 2>&1; then
  echo "linuxdeploy is required (install linuxdeploy-appimage from AUR)" >&2
  exit 1
fi

rm -rf "$BUILD_DIR" "$APPDIR"

meson setup "$BUILD_DIR" --buildtype=release -Dimage_backend=sdl_image
meson compile -C "$BUILD_DIR"

install -Dm755 "$BUILD_DIR/ic" "$APPDIR/usr/bin/ic"
install -Dm644 "$ROOT_DIR/ic.desktop" "$APPDIR/usr/share/applications/ic.desktop"
install -Dm644 "$ROOT_DIR/ic.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/ic.png"

export VERSION="${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always 2>/dev/null || echo 1.0.0)}"

linuxdeploy \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/ic" \
  --desktop-file "$APPDIR/usr/share/applications/ic.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/ic.png" \
  --output appimage

echo "AppImage generated in $ROOT_DIR"
