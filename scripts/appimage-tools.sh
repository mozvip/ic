#!/usr/bin/env bash
set -euo pipefail
TOOLS_DIR="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/ic-appimage-tools"
mkdir -p "$TOOLS_DIR"
for tool in linuxdeploy linuxdeploy-plugin-appimage; do
  curl --fail --location --retry 3 \
    "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage" \
    --output "$TOOLS_DIR/$tool"
  chmod +x "$TOOLS_DIR/$tool"
done
curl --fail --location --retry 3 \
  https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh \
  --output "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"
chmod +x "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"
if [[ -n "${GITHUB_PATH:-}" ]]; then printf '%s\n' "$TOOLS_DIR" >> "$GITHUB_PATH"; fi
printf 'AppImage tools installed in %s\n' "$TOOLS_DIR"