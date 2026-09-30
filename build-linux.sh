#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p /tmp/polymech-media-build
export PATH="/snap/bin:$PATH"
cmake -S "$SCRIPT_DIR" -B /tmp/polymech-media-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/polymech-media-build --target pm-image
echo ""
echo "Built binary (CMake RUNTIME_OUTPUT_DIRECTORY): ${SCRIPT_DIR}/dist/pm-image"
if [[ -f "${SCRIPT_DIR}/dist/pm-image" ]]; then
  ls -la "${SCRIPT_DIR}/dist/pm-image"
else
  echo "Warning: ${SCRIPT_DIR}/dist/pm-image not found; see CMake build log above." >&2
fi
