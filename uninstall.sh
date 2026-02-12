#!/bin/bash

# Pulse Visualizer Uninstall Script
# This script removes the pulse-visualizer binary and data files from the system

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
if [[ $EUID -ne 0 && $2 != "skip-root" ]]; then
  echo -e "${RED}ERROR:${NC} This script must be run as root"
  exit 1
fi

echo "Uninstalling..."

# Remove binary
if [ -f "$BIN_DIR/pulse-visualizer" ]; then
  echo "rm $BIN_DIR/pulse-visualizer"
  rm "$BIN_DIR/pulse-visualizer"
else
  echo -e "${YELLOW}WARNING:${NC} Binary not found at $BIN_DIR/pulse-visualizer"
fi

# Remove desktop file
if [ -f "$PREFIX/share/applications/pulse-visualizer.desktop" ]; then
  echo "rm $PREFIX/share/applications/pulse-visualizer.desktop"
  rm "$PREFIX/share/applications/pulse-visualizer.desktop"
else
  echo -e "${YELLOW}WARNING:${NC} Desktop file not found at $PREFIX/share/applications/pulse-visualizer.desktop"
fi

# Remove man page
if [ -f "$PREFIX/share/man/man1/pulse-visualizer.1" ]; then
  echo "rm $PREFIX/share/man/man1/pulse-visualizer.1"
  rm "$PREFIX/share/man/man1/pulse-visualizer.1"
else
  echo -e "${YELLOW}WARNING:${NC} Man page not found at $PREFIX/share/man/man1/pulse-visualizer.1"
fi

# Remove data directory
if [ -d "$DATA_DIR" ] && [[ "$DATA_DIR" == *"pulse-visualizer"* ]]; then
  echo "Removing data files from $DATA_DIR..."
  rm -f "$DATA_DIR/LICENSE"
  rm -f "$DATA_DIR/config.yml.template"
  rm -f "$DATA_DIR/JetBrainsMonoNerdFont-Medium.ttf"
  rm -f "$DATA_DIR"/README.md "$DATA_DIR"/CONFIGURATION.md "$DATA_DIR"/CONTRIBUTORS.md
  rm -f "$DATA_DIR"/shaders/*
  rm -f "$DATA_DIR"/themes/*
  rm -f "$DATA_DIR"/fonts/*
  rm -f "$DATA_DIR"/icons/*
  rmdir "$DATA_DIR"/shaders 2>/dev/null || true
  rmdir "$DATA_DIR"/themes 2>/dev/null || true
  rmdir "$DATA_DIR"/fonts 2>/dev/null || true
  rmdir "$DATA_DIR"/icons 2>/dev/null || true
  rmdir "$DATA_DIR" 2>/dev/null || true
else
  echo -e "${YELLOW}WARNING:${NC} Data directory not found at $DATA_DIR"
fi

echo -e "${GREEN}Uninstallation completed successfully${NC}"
