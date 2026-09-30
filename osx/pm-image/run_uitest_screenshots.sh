#!/usr/bin/env bash
# Run UI test: app opens → screenshot → app quits; PNG on disk + .xcresult (attachments in Xcode).
# Requires: macOS with GUI (WindowServer) — will fail or hang from a headless sshd-only session.
# Env: PM_UI_TEST_SCREENSHOT_DIR (default: build/uitest-screenshots), PM_UI_TEST_XCRESULT, PM_TEST_APP_ARCH
#
# Usage (on the guest, from pm-image/):
#   bash run_uitest_screenshots.sh              # launch + screenshot only
#   bash run_uitest_screenshots.sh all         # + titlebar undock + diagonal window resize screenshot tests
#   PM_UI_TEST_DRAG=1 bash run_uitest_screenshots.sh
#   PM_UI_TEST_RESIZE=1  # include testResizeMainWindowDiagonalScreenshot (also in `all`)
# From host (PM_OSX_CPP_ROOT = packages/media/cpp on the guest; see osx/README.md):
#   cd osx && npm run ssh -- "bash osx/pm-image/run_uitest_screenshots.sh all"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

ARCH="${PM_TEST_APP_ARCH:-}"
if [[ -z "${ARCH}" ]]; then
  case "$(uname -m)" in
    arm64) ARCH=arm64 ;;
    *)     ARCH=x86_64 ;;
  esac
fi

STAMP=$(date +%Y%m%d-%H%M%S)
OUT="${PM_UI_TEST_XCRESULT:-$ROOT/build/UITestResult-$STAMP.xcresult}"
# Same as scheme Test env PM_UI_TEST_SCREENSHOT_DIR: $(SRCROOT)/build/uitest-screenshots
DEFAULT_SHOOT="$ROOT/build/uitest-screenshots"
mkdir -p "$(dirname "$OUT")" "$DEFAULT_SHOOT"
export PM_UI_TEST_SCREENSHOT_DIR="${PM_UI_TEST_SCREENSHOT_DIR:-$DEFAULT_SHOOT}"

TESTS=( "pm-imageUITests/pm_imageUITests/testLaunchSaveScreenshotAndClose" )
if [[ "${1:-}" == "all" || "${PM_UI_TEST_DRAG:-}" == "1" ]]; then
  TESTS+=( "pm-imageUITests/pm_imageUITests/testTitlebarDragUndockScreenshot" )
fi
if [[ "${1:-}" == "all" || "${PM_UI_TEST_RESIZE:-}" == "1" ]]; then
  TESTS+=( "pm-imageUITests/pm_imageUITests/testResizeMainWindowDiagonalScreenshot" )
fi

XCT=()
for t in "${TESTS[@]}"; do
  XCT+=( -only-testing:"$t" )
done

LOG="${PM_UI_TEST_XCODE_LOG:-$ROOT/build/uitest-xcodebuild.log}"
echo "xcodebuild test → $OUT (arch=$ARCH) (log: $LOG)"
echo "PM_UI_TEST_SCREENSHOT_DIR → $PM_UI_TEST_SCREENSHOT_DIR"
echo "Tests: ${TESTS[*]}"
# UITest may only write to TMPDIR (sandbox); it prints PM_UI_TEST_SCREENSHOT_FILE= per PNG — we copy into build/ here.
xcodebuild \
  -project "$ROOT/pm-image.xcodeproj" \
  -scheme pm-image \
  -configuration Debug \
  -destination "platform=macOS,arch=$ARCH" \
  "${XCT[@]}" \
  -resultBundlePath "$OUT" \
  test 2>&1 | tee "$LOG"
TEST_EXIT="${PIPESTATUS[0]}"

if [[ -f "$LOG" ]]; then
  while IFS= read -r line; do
    f="${line#*PM_UI_TEST_SCREENSHOT_FILE=}"
    f="${f//$'\r'/}"
    if [[ -n "$f" && -f "$f" ]]; then
      mkdir -p "$PM_UI_TEST_SCREENSHOT_DIR"
      dest="$PM_UI_TEST_SCREENSHOT_DIR/$(basename "$f")"
      cp -f "$f" "$dest"
      echo "Copied screenshot: $dest"
    fi
  done < <(grep 'PM_UI_TEST_SCREENSHOT_FILE=' "$LOG" 2>/dev/null || true)
fi

if ls "$PM_UI_TEST_SCREENSHOT_DIR"/*.png &>/dev/null; then
  ls -la "$PM_UI_TEST_SCREENSHOT_DIR"/*.png
  (command -v shasum >/dev/null 2>&1 && shasum -a 256 "$PM_UI_TEST_SCREENSHOT_DIR"/*.png) || true
else
  echo "Warning: no PNGs in $PM_UI_TEST_SCREENSHOT_DIR (see $LOG and $OUT)"
fi

echo "Open result bundle: File → Open… in Xcode: $OUT"
echo "Or: xcrun xcresulttool get test-results summary --path \"$OUT\""
exit "$TEST_EXIT"
