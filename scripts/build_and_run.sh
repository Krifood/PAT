#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${CONFIG:-Release}"
QT_ROOT="${QT_ROOT:-${CMAKE_PREFIX_PATH:-}}"
CLEAN="${CLEAN:-0}"
if [[ -z "$QT_ROOT" ]]; then echo "Set QT_ROOT or CMAKE_PREFIX_PATH to Qt prefix (with lib/cmake/Qt6Core)"; exit 1; fi
export PATH="$QT_ROOT/bin:$PATH"
if [[ "$CLEAN" == "1" ]]; then rm -rf "$BUILD_DIR"; fi
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DPAT_ENABLE_QT_CHARTS=ON -DPAT_STRICT_WARNINGS=ON -DCMAKE_PREFIX_PATH="$QT_ROOT"
cmake --build "$BUILD_DIR" --config "$CONFIG"
exe="$BUILD_DIR/pat_app"
if [[ -f "$BUILD_DIR/pat_app.exe" ]]; then exe="$BUILD_DIR/pat_app.exe"; fi
"$exe"
