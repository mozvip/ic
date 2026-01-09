#!/bin/bash
set -e

BUILD_DIR="builddir"

echo "Checking dependencies..."
# Simple check for meson
if ! command -v meson &> /dev/null; then
    echo "Meson is not installed. Please install it first."
    exit 1
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "Configuring build directory..."
    meson setup "$BUILD_DIR" --buildtype=release
else
    echo "Build directory exists. Reconfiguring..."
    meson setup --reconfigure "$BUILD_DIR" --buildtype=release
fi

echo "Building..."
meson compile -C "$BUILD_DIR"

echo "Installing..."
# Use sudo if not root
if [ "$EUID" -ne 0 ]; then
    echo "This script attempts to install to system directories. Please enter your password if prompted."
    sudo meson install -C "$BUILD_DIR"
else
    meson install -C "$BUILD_DIR"
fi

echo "Updating desktop database..."
if command -v update-desktop-database &> /dev/null; then
   if [ "$EUID" -ne 0 ]; then
       sudo update-desktop-database
   else
       update-desktop-database
   fi
fi

echo "Installation complete! You can now launch 'ic' from your application menu."
