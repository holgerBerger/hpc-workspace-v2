#!/usr/bin/env bash
# vcpkg-bootstrap.sh — clones and bootstraps vcpkg, then prints available options.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VCPKG_DIR="$REPO_ROOT/vcpkg"
VCPKG_BOOTSTRAP="$VCPKG_DIR/bootstrap-vcpkg.sh"
VCPKG_BIN="$VCPKG_DIR/vcpkg"
VCPKG_TC="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake"

# 1. Clone vcpkg if needed
if [[ ! -d "$VCPKG_DIR" ]]; then
    echo "==> Cloning vcpkg..."
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi

# 2. Bootstrap vcpkg binary if needed
if [[ ! -x "$VCPKG_BIN" ]]; then
    echo "==> Bootstrapping vcpkg..."
    bash "$VCPKG_BOOTSTRAP"
fi

echo ""
echo "vcpkg is ready."
echo ""
echo "Available build presets (vcpkg):"
echo ""
echo  "vcpkg-debug          - Debug (vcpkg)"
echo  "vcpkg-debug-ninja    - Debug (vcpkg, Ninja)"
echo  "vcpkg-release        - Release (vcpkg)"
echo  "vcpkg-release-ninja  - Release (vcpkg, Ninja)"
