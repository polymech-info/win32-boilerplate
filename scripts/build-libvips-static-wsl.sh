#!/usr/bin/env bash
# Build libvips from source as static libraries and install to a local prefix.
# Run inside WSL2 (Ubuntu). The result is for building *this* project *on Linux*
# (or WSL) with pkg-config; you cannot use these .a archives with MSVC for Windows.
# For Windows, keep using third_party vips-dev-*.zip (DLL) or a full MSVC dep setup.
#
# Prerequisite: from packages/media/cpp:
#   bash scripts/build-libvips-static-wsl.sh
# Then (example):
#   export VIPS_ROOT=.../third_party/vips-static-prefix
#   export PKG_CONFIG_PATH="${VIPS_ROOT}/lib/x86_64-linux-gnu/pkgconfig:${VIPS_ROOT}/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
#   cmake -B build -DPOLYMECH_VIPS_PREFER_STATIC=ON ...
#
# This installs build dependencies for a *typical* libvips feature set. Trim or
# add packages if meson reports missing optional deps. See:
#   https://www.libvips.org/install.html
set -euo pipefail

VERSION="${1:-8.18.2}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/third_party/libvips-src"
BUILD="${ROOT}/third_party/libvips-meson-build"
PREFIX="${ROOT}/third_party/vips-static-prefix"

if [[ ! -d /proc/sys/fs/binfmt_misc/WSLInterop ]] 2>/dev/null; then
  echo "Tip: this script is intended for WSL; on native Linux it still works."
fi

sudo apt-get update
sudo apt-get install -y \
  build-essential git meson ninja-build pkg-config \
  libglib2.0-dev libexpat1-dev libfftw3-dev \
  libjpeg-dev libexif-dev liblcms2-dev \
  libpng-dev libtiff5-dev liborc-0.4-dev \
  libwebp-dev \
  libopenjp2-7-dev \
  libgsf-1-dev

rm -rf "${BUILD}"
mkdir -p "${PREFIX}"

if [[ ! -d "${SRC}/.git" ]]; then
  git clone --depth 1 --branch "v${VERSION}" https://github.com/libvips/libvips.git "${SRC}"
else
  (cd "${SRC}" && git fetch --depth 1 origin "v${VERSION}" && git checkout "v${VERSION}")
fi

cd "${SRC}"
# Static libvips; introspection off keeps deps smaller (see libvips meson_options).
meson setup "${BUILD}" \
  --prefix "${PREFIX}" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dintrospection=disabled

meson compile -C "${BUILD}"
meson install -C "${BUILD}"

echo ""
echo "Installed to: ${PREFIX}"
echo "Add to your shell before configuring CMake:"
echo "  export VIPS_ROOT=\"${PREFIX}\""
echo "  export PKG_CONFIG_PATH=\"\${VIPS_ROOT}/lib/x86_64-linux-gnu/pkgconfig:\${VIPS_ROOT}/lib/pkgconfig\${PKG_CONFIG_PATH:+:}\${PKG_CONFIG_PATH}\""
