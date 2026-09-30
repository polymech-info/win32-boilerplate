#!/usr/bin/env bash
# Isolated SwiftUI app: xcodebuild only (no tests; no CMake / no pm-media link).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
export LC_CTYPE="${LC_CTYPE:-en_US.UTF-8}"

# Match host arch unless PM_TEST_APP_ARCH=arm64|x86_64
ARCH="${PM_TEST_APP_ARCH:-}"
if [[ -z "${ARCH}" ]]; then
  case "$(uname -m)" in
    arm64) ARCH=arm64 ;;
    *)     ARCH=x86_64 ;;
  esac
fi

DERIVED="${PM_PM_IMAGE_DERIVED_DATA:-$ROOT/build/DerivedData}"
mkdir -p "$DERIVED"

set -o pipefail
exec xcodebuild \
  -project pm-image.xcodeproj \
  -scheme pm-image \
  -configuration Debug \
  -destination "platform=macOS,arch=${ARCH}" \
  -derivedDataPath "$DERIVED" \
  build
