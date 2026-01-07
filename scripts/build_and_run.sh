#!/bin/bash
set -euo pipefail

# 允许在任意目录调用脚本
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${CONFIG:-Release}"
CLEAN="${CLEAN:-0}"

detect_qt_root() {
  if [[ -n "${QT_ROOT:-}" ]]; then
    echo "$QT_ROOT"
    return
  fi
  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    echo "$CMAKE_PREFIX_PATH"
    return
  fi
  if command -v brew >/dev/null 2>&1; then
    local brew_qt
    brew_qt="$(brew --prefix qt 2>/dev/null || true)"
    if [[ -n "$brew_qt" && -d "$brew_qt" ]]; then
      echo "$brew_qt"
      return
    fi
  fi
}

QT_ROOT="$(detect_qt_root || true)"
if [[ -z "$QT_ROOT" ]]; then
  echo "未找到 Qt 安装路径，请设置 QT_ROOT 或 CMAKE_PREFIX_PATH（包含 lib/cmake/Qt6Core）" >&2
  exit 1
fi

export PATH="$QT_ROOT/bin:$PATH"

if [[ "$CLEAN" == "1" ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
  -DPAT_ENABLE_QT_CHARTS=ON \
  -DPAT_STRICT_WARNINGS=ON \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DCMAKE_BUILD_TYPE="$CONFIG"

cmake --build "$BUILD_DIR" --config "$CONFIG"

exe="$BUILD_DIR/pat_app"
if [[ -f "$BUILD_DIR/pat_app.exe" ]]; then
  exe="$BUILD_DIR/pat_app.exe"
fi
"$exe"
