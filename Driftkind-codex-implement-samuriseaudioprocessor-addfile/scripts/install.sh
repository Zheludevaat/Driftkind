#!/usr/bin/env bash
set -e

# One-click build and install script for macOS/Linux
if [[ "$1" == "--help" ]]; then
  echo "Usage: $0"
  echo "Builds the plugin using CMake and installs it to the user's plug-in directory."
  exit 0
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_DIR="$ROOT_DIR/deps/juce"

command -v cmake >/dev/null 2>&1 || { echo "cmake is required" >&2; exit 1; }

if [ ! -d "$JUCE_DIR" ]; then
  echo "JUCE not found, cloning to $JUCE_DIR"
  mkdir -p "$ROOT_DIR/deps"
  git clone --depth 1 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

mkdir -p "$ROOT_DIR/build"
# Always build Release when using single‑configuration generators (e.g. Ninja/Unix Makefiles).
cmake -B "$ROOT_DIR/build" -S "$ROOT_DIR" -DJUCE_DIR="$JUCE_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build" --config Release
cpack --config "$ROOT_DIR/build/CPackConfig.cmake" >/dev/null 2>&1 || true

VST3_PLUGIN=$(find "$ROOT_DIR/build" -maxdepth 3 -name '*.vst3' -type d 2>/dev/null | head -n 1)
AU_PLUGIN=$(find "$ROOT_DIR/build" -maxdepth 3 -name '*.component' -type d 2>/dev/null | head -n 1)

if [[ "$OSTYPE" == "darwin"* ]]; then
  VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
  AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
  mkdir -p "$VST3_DIR" "$AU_DIR"
  if [ -n "$VST3_PLUGIN" ]; then
    cp -R "$VST3_PLUGIN" "$VST3_DIR/"
    echo "Installed $(basename "$VST3_PLUGIN") to $VST3_DIR"
  fi
  if [ -n "$AU_PLUGIN" ]; then
    cp -R "$AU_PLUGIN" "$AU_DIR/"
    echo "Installed $(basename "$AU_PLUGIN") to $AU_DIR"
  fi
else
  if [ -n "$VST3_PLUGIN" ]; then
    VST3_DIR="$HOME/.vst3"
    mkdir -p "$VST3_DIR"
    cp -R "$VST3_PLUGIN" "$VST3_DIR/"
    echo "Installed $(basename "$VST3_PLUGIN") to $VST3_DIR"
  fi
fi

if [ -z "$VST3_PLUGIN" ] && [ -z "$AU_PLUGIN" ]; then
  echo "Plugin artifact not found after build"
fi
