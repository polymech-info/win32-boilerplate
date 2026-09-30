#!/usr/bin/env bash
# Open PixelWiz in Xcode with DISABLE_SWIFTLINT=1 so SPM SwiftLint build-tool plugins do not run swiftlint.
# If Xcode still asks to "Trust & Enable" SwiftLint after a package update, either click Trust once or run:
#   npm run strip:swiftlint-spm
# then Product > Clean Build Folder.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DISABLE_SWIFTLINT=1
DEV="$(xcode-select -p 2>/dev/null)" || true
if [[ "${DEV:-}" == *"/Contents/Developer" ]]; then
	XCODE_APP="${DEV%/Contents/Developer}"
else
	XCODE_APP="/Applications/Xcode.app"
fi
exec "$XCODE_APP/Contents/MacOS/Xcode" "$HERE/PixelWiz.xcodeproj"
