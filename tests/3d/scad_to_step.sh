#!/usr/bin/env sh
set -eu

# Wrapper for scad_to_step.py. Pass all arguments through unchanged.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/scad_to_step.py"

if [ ! -f "$SCRIPT" ]; then
  echo "ERROR: Cannot find $SCRIPT" >&2
  exit 1
fi

if command -v python3 >/dev/null 2>&1; then
  exec python3 "$SCRIPT" "$@"
fi

if command -v python >/dev/null 2>&1; then
  exec python "$SCRIPT" "$@"
fi

echo "ERROR: Python was not found. Install Python 3 or add it to PATH." >&2
exit 1
