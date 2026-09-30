#!/usr/bin/env bash
# Remove SwiftLint SwiftPM plugin hooks from Xcode DerivedData checkouts (same idea as polymech_xcode_with_libs.sh).
# Use when Xcode shows "Plugin SwiftLint was disabled because it recently changed" and you prefer not to Trust & Enable.
# After running: Product > Clean Build Folder, then build again. Re-run after File > Packages > Reset Package Caches if needed.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRIP="${SCRIPT_DIR}/strip_spm_swiftlint_plugins.py"
if [[ ! -f "${STRIP}" ]]; then
	echo "strip-swiftlint-spm-checkouts: missing ${STRIP}" >&2
	exit 1
fi
shopt -s nullglob
FOUND=0
for checkout_dir in "${HOME}/Library/Developer/Xcode/DerivedData/"*/SourcePackages/checkouts; do
	if [[ -d "${checkout_dir}" ]]; then
		echo "strip-swiftlint-spm-checkouts: patching ${checkout_dir}"
		chmod -R u+w "${checkout_dir}" 2>/dev/null || true
		python3 "${STRIP}" "${checkout_dir}" 2>&1 || true
		FOUND=1
	fi
done
if [[ "${FOUND}" -eq 0 ]]; then
	echo "strip-swiftlint-spm-checkouts: no ~/Library/Developer/Xcode/DerivedData/*/SourcePackages/checkouts found." >&2
	echo "Open PixelWiz.xcodeproj in Xcode and use File > Packages > Resolve Package Versions, then run this script again." >&2
	exit 1
fi
echo "strip-swiftlint-spm-checkouts: done. In Xcode: Product > Clean Build Folder, then build."
