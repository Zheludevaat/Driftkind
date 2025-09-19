#!/usr/bin/env bash
set -e

# Entry point; dispatches to platform-specific installer
if [[ "$1" == "--help" ]]; then
  echo "Usage: $0"
  echo "Calls platform-specific install script."
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win"* ]]; then
  powershell -ExecutionPolicy Bypass -File "$SCRIPT_DIR/scripts/install.ps1" "$@"
else
  bash "$SCRIPT_DIR/scripts/install.sh" "$@"
fi
