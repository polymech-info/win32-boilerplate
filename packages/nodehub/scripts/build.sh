#!/bin/bash
# Build script for blueprints-example project only
# Usage: ./scripts/build.sh [Release|Debug] [console]
#   - First argument: Build type (Release or Debug, default: Debug)
#   - Second argument: 'console' to build console variant instead

export CMAKE_EXPORT_COMPILE_COMMANDS=1

taskkill /F /IM blueprints-example_d.exe 2>/dev/null
taskkill /F /IM blueprints-example-console_d.exe 2>/dev/null

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Default to Debug build if not specified
BUILD_TYPE="${1:-Debug}"

# Check if console variant requested
if [ "$2" == "console" ]; then
    TARGET="nodehub-console"
    VARIANT="Console"
else
    TARGET="nodehub"
    VARIANT="GUI"
fi

echo "=========================================="
echo "Building $TARGET ($VARIANT, $BUILD_TYPE)"
echo "=========================================="

cd "$PROJECT_ROOT"

# Build the specified target
cmake --build build --config "$BUILD_TYPE" --target "$TARGET"

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Build successful!"
    echo ""
    echo "Executable location:"
    if [ "$BUILD_TYPE" == "Release" ]; then
        echo "  build/bin/${TARGET}.exe"
    else
        echo "  build/bin/${TARGET}_d.exe"
    fi
    echo ""
    echo "Run with:"
    if [ "$BUILD_TYPE" == "Release" ]; then
        echo "  ./build/bin/${TARGET}.exe"
    else
        echo "  ./build/bin/${TARGET}_d.exe"
    fi
    echo ""
    if [ "$VARIANT" == "GUI" ]; then
        echo "Tip: Build console variant with: ./scripts/build.sh $BUILD_TYPE console"
    fi
else
    echo ""
    echo "✗ Build failed!"
    exit 1
fi

