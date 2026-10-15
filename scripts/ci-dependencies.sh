#!/usr/bin/env bash
set -euo pipefail
[[ "$EUID" -eq 0 ]] || { echo "Run this dependency bootstrap inside the Ubuntu CI container" >&2; exit 1; }
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl git build-essential cmake meson ninja-build pkg-config \
  libzip-dev libfreeimage-dev libglib2.0-dev libfontconfig1-dev libgtk-3-dev \
  gettext locales imagemagick desktop-file-utils appstream file poppler-utils fonts-dejavu-core xvfb xauth \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libwayland-dev wayland-protocols libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev \
  libjpeg-dev libpng-dev libtiff-dev libwebp-dev libfreetype6-dev libharfbuzz-dev
localedef -i fr_FR -f UTF-8 fr_FR.UTF-8
build_root=$(mktemp -d)
trap 'rm -rf "$build_root"' EXIT
for component in SDL:3.4.2 SDL_image:3.4.0 SDL_ttf:3.2.2; do
  name=${component%%:*}
  version=${component#*:}
  git clone --depth 1 --branch "release-$version" "https://github.com/libsdl-org/$name.git" "$build_root/$name"
  cmake -S "$build_root/$name" -B "$build_root/$name/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
  cmake --build "$build_root/$name/build" --parallel 2
  cmake --install "$build_root/$name/build"
done
ldconfig