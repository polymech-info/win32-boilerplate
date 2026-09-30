#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# install-lnx.sh  –  Linux build deps for packages/media/cpp (media-img) and related tools
#
# Tested on: Ubuntu 20.04+ / Debian 11+
# Usage:     sudo bash install-lnx.sh
# ─────────────────────────────────────────────────────────────────────────────
#set -euo pipefail

echo "── media-img (C++) Linux dependency installer ──"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$SCRIPT_DIR/dist"
PATH_MARK="polymech-media-cpp PATH (dist → pm-image)"

append_dist_to_rc() {
    local rc="$1"
    local owner="$2"
    if [[ -z "$rc" || -z "$owner" ]]; then return 0; fi
    if [[ -f "$rc" ]] && grep -qF "$PATH_MARK" "$rc" 2>/dev/null; then
        echo "  PATH already in $rc"
        return 0
    fi
    local block="# $PATH_MARK
export PATH=\"$DIST_DIR:\$PATH\"
"
    if [[ "$(id -un)" == "$owner" ]]; then
        printf '%s' "$block" >>"$rc"
    elif command -v sudo >/dev/null 2>&1; then
        printf '%s' "$block" | sudo -u "$owner" tee -a "$rc" >/dev/null
    else
        echo "  (skip $rc: not running as $owner; add PATH manually — see scripts/env-pm-dist.sh)"
        return 0
    fi
    echo "  Appended dist to PATH in $rc"
}

# ── 1. System packages (apt) ─────────────────────────────────────────────────
echo ""
echo "[1/4] Installing system packages via apt …"
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    g++ \
    git \
    libssl-dev \
    libvips-dev \
    pkg-config \
    snapd

# ── 2. CMake ≥ 3.20 via snap ────────────────────────────────────────────────
#    The project requires cmake_minimum_required(VERSION 3.20).
#    Ubuntu 20.04 ships cmake 3.16, so we use the snap package instead.
echo ""
echo "[2/4] Installing CMake via snap (≥ 3.20 required) …"
if command -v /snap/bin/cmake &>/dev/null; then
    echo "  cmake snap already installed: $(/snap/bin/cmake --version | head -1)"
else
    snap install cmake --classic
    echo "  Installed: $(/snap/bin/cmake --version | head -1)"
fi

# ── 3. Node.js (for npm run build:linux) ──────────────────────────────────────
echo ""
echo "[3/4] Checking for Node.js / npm …"
if command -v node &>/dev/null; then
    echo "  node $(node --version) already installed"
else
    echo "  Node.js not found. Install via nvm or nodesource, e.g.:"
    echo "    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -"
    echo "    sudo apt-get install -y nodejs"
fi

# ── 4. User PATH — dist/ (pm-image, pm-viewer symlinks, etc.) ────────────────
echo ""
echo "[4/4] Shell PATH — $DIST_DIR …"
TARGET_USER="${SUDO_USER:-$USER}"
TARGET_HOME="$(getent passwd "$TARGET_USER" 2>/dev/null | cut -d: -f6)"
if [[ -n "$TARGET_HOME" && -d "$TARGET_HOME" ]]; then
    append_dist_to_rc "$TARGET_HOME/.bashrc" "$TARGET_USER"
    if [[ -f "$TARGET_HOME/.zshrc" ]]; then
        append_dist_to_rc "$TARGET_HOME/.zshrc" "$TARGET_USER"
    fi
else
    echo "  Could not resolve home for $TARGET_USER. Add manually:"
    echo "    export PATH=\"$DIST_DIR:\$PATH\""
    echo "  Or: source \"$SCRIPT_DIR/scripts/env-pm-dist.sh\""
fi
echo "  direnv: copy or symlink .envrc is already in this package (PATH_add dist)."

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "── Done! ──"
echo ""
echo "CMake FetchContent pulls CLI11, Asio, httplib, nlohmann/json, dotenv, and p-ranav/glob."
echo "libvips is required at link time (libvips-dev above)."
echo ""
echo "To build:"
echo "  cd $(dirname "$0")"
echo "  npm install   # devDependencies only; CMake is invoked via node scripts/cmake.mjs (prefers /snap/bin/cmake on Linux)"
echo "  npm run configure:cpp && npm run build:cpp"
echo "  Or: PATH=\"/snap/bin:\$PATH\" cmake --preset release && cmake --build --preset release --config Release"
echo ""
echo "Built CLI: dist/pm-image (on PATH after step [4/4] or: source scripts/env-pm-dist.sh)"
