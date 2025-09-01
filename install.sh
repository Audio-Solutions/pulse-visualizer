#!/bin/bash

# Pulse Visualizer Install Script
# This script installs the pulse-visualizer binary and data files to the system

set -e  # Exit on any error

PREFIX=${1:-/usr}

# Default installation paths
BIN_DIR="$PREFIX/bin"
DATA_DIR="$PREFIX/share/pulse-visualizer"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root
if [[ $EUID -ne 0 ]]; then
  echo -e "${RED}ERROR:${NC} This script must be run as root"
  exit 1
fi

echo "Installing..."

# Create installation directories
echo "mkdir -p $DATA_DIR"
mkdir -p "$DATA_DIR"
echo "mkdir -p $DATA_DIR/shaders"
mkdir -p "$DATA_DIR/shaders"
echo "mkdir -p $DATA_DIR/themes"
mkdir -p "$DATA_DIR/themes"
echo "mkdir -p $DATA_DIR/fonts"
mkdir -p "$DATA_DIR/fonts"
echo "mkdir -p $DATA_DIR/icons"
mkdir -p "$DATA_DIR/icons"
echo "mkdir -p $PREFIX/bin"
mkdir -p "$PREFIX/bin"
echo "mkdir -p $PREFIX/share/man/man1"
mkdir -p "$PREFIX/share/man/man1"
echo "mkdir -p $PREFIX/share/applications"
mkdir -p "$PREFIX/share/applications"

# Install license
echo "cp LICENSE $DATA_DIR/"
cp "LICENSE" "$DATA_DIR/"

# Install binary
echo "cp pulse-visualizer $BIN_DIR/"
cp "pulse-visualizer" "$BIN_DIR/"
echo "chmod +x $BIN_DIR/pulse-visualizer"
chmod +x "$BIN_DIR/pulse-visualizer"

# Install shaders
echo "cp -r shaders/* $DATA_DIR/shaders/"
cp -r shaders/* "$DATA_DIR/shaders/"

# Install themes
echo "cp -r themes/* $DATA_DIR/themes/"
cp -r themes/* "$DATA_DIR/themes/"

# Install config template
echo "cp config.yml.template $DATA_DIR/"
cp "config.yml.template" "$DATA_DIR/"

# Install font
echo "cp JetBrainsMonoNerdFont-Medium.ttf $DATA_DIR/fonts/"
cp "JetBrainsMonoNerdFont-Medium.ttf" "$DATA_DIR/fonts/"

# Install desktop file
echo "cp pulse-visualizer.desktop $PREFIX/share/applications/"
cp "pulse-visualizer.desktop" "$PREFIX/share/applications/"

# Install icon
echo "cp pulse-visualizer.png $DATA_DIR/icons/"
cp "pulse-visualizer.png" "$DATA_DIR/icons/"

# Install man page
echo "cp pulse-visualizer.1 $PREFIX/share/man/man1/"
cp "pulse-visualizer.1" "$PREFIX/share/man/man1/"

# Set permissions
echo "chmod -R 644 $DATA_DIR/*"
chmod -R 644 "$DATA_DIR"/*
echo "chmod 755 $DATA_DIR"
chmod 755 "$DATA_DIR"
echo "chmod 755 $DATA_DIR/shaders"
chmod 755 "$DATA_DIR/shaders"
echo "chmod 755 $DATA_DIR/themes"
chmod 755 "$DATA_DIR/themes"
echo "chmod 755 $DATA_DIR/fonts"
chmod 755 "$DATA_DIR/fonts"
echo "chmod 755 $DATA_DIR/icons"
chmod 755 "$DATA_DIR/icons"

echo -e "${GREEN}Installation completed successfully${NC}"
