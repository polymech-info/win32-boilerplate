#!/usr/bin/env bash
# folder_size.sh -- report the size of the supplied directory (defaults to current directory)
set -euo pipefail

target="${1:-.}"

if [ ! -d "$target" ]; then
  echo "Error: '$target' is not a directory" >&2
  exit 1
fi

du -sh "$target"
