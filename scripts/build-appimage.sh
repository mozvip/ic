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

if command -v magick >/dev/null 2>&1; then
  IMAGE_TOOL=magick
elif command -v convert >/dev/null 2>&1; then
  IMAGE_TOOL=convert
else
  echo "ImageMagick is required to prepare the application icon" >&2
  exit 1
fi

rm -rf "$BUILD_DIR" "$APPDIR"

meson setup "$BUILD_DIR" --buildtype=release --prefix=/usr -Dimage_backend=sdl_image
meson compile -C "$BUILD_DIR"

DESTDIR="$APPDIR" meson install -C "$BUILD_DIR" --no-rebuild
desktop-file-validate "$APPDIR/usr/share/applications/io.github.mozvip.ic.desktop"
appstreamcli validate --no-net "$APPDIR/usr/share/metainfo/io.github.mozvip.ic.metainfo.xml"
mv "$APPDIR/usr/share/metainfo/io.github.mozvip.ic.metainfo.xml" \
  "$APPDIR/usr/share/metainfo/io.github.mozvip.ic.appdata.xml"

export VERSION="${VERSION:-$(git -C "$ROOT_DIR" describe --tags --always 2>/dev/null || echo 1.0.0)}"
export NO_STRIP="${NO_STRIP:-1}"
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"

linuxdeploy \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/ic" \
  --desktop-file "$APPDIR/usr/share/applications/io.github.mozvip.ic.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/io.github.mozvip.ic.png" \
  --plugin gtk \
  --output appimage

echo "AppImage generated in $ROOT_DIR"
