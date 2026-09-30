#!/usr/bin/env bash
# xcodebuild CodeEdit. Polymech static link flags live in
# dist-osx/polymech-cmake-libs/polymech_codeedit_link.xcconfig (CodeEdit app target only), not on the xcodebuild CLI.
# Args: <xcodebuild> <derivedDataPath> <projectDir> <xcodeproj> <scheme> <config>
set -euo pipefail
XB="${1:-}"
DIRECT="${2:-}"
XWD="${3:-}"
XPROJ="${4:-}"
SCHM="${5:-}"
BCONF="${6:-}"
if [[ -z "$XB" || -z "$DIRECT" || -z "$XWD" || -z "$XPROJ" || -z "$SCHM" || -z "$BCONF" ]]; then
  echo "polymech_xcode_with_libs.sh: missing one of 6 required args" >&2
  exit 2
fi
cd "$XWD"
# Transitive SPM SwiftLintPlugin skips running swiftlint when set (see lukepistrol/SwiftLintPlugin README).
export DISABLE_SWIFTLINT=1
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRIP="${HERE}/strip_spm_swiftlint_plugins.py"

# One arch only: Polymech static .a from CMake match the current machine (x86_64 or arm64), not both.
XARCH1="EXCLUDED_ARCHS=arm64"
XARCH2="ONLY_ACTIVE_ARCH=NO"
if [[ "$(uname -m)" == "arm64" ]]; then
  XARCH1="EXCLUDED_ARCHS=x86_64"
fi

# Resolve packages so SourcePackages/checkouts exist.
"$XB" -project "$XPROJ" -scheme "$SCHM" -configuration "$BCONF" -derivedDataPath "$DIRECT" -destination "platform=macOS" -skipPackagePluginValidation "CODE_SIGNING_ALLOWED=NO" "${XARCH1}" "${XARCH2}" -resolvePackageDependencies

# SwiftLint's build tool runs swiftlint in a sandbox; headless/VM xcodebuild often cannot load
# sourcekitdInProc. Remove SwiftLint plugin hooks from vendored checkouts (see strip script).
if [[ -d "${DIRECT}/SourcePackages/checkouts" && -f "${STRIP}" ]]; then
  chmod -R u+w "${DIRECT}/SourcePackages/checkouts" 2>/dev/null || true
  python3 "${STRIP}" "${DIRECT}/SourcePackages/checkouts" 2>&1 || true
fi

# Non-interactive builds: skip package plugin *validation* (Xcode 15+). Link flags: PolymechCodeEdit*.xcconfig.
exec "$XB" -project "$XPROJ" -scheme "$SCHM" -configuration "$BCONF" -derivedDataPath "$DIRECT" -destination "platform=macOS" -skipPackagePluginValidation "CODE_SIGNING_ALLOWED=NO" "${XARCH1}" "${XARCH2}" build
