#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/local}"
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
fi
CONFIG="${CONFIG:-Release}"
RUN="${RUN:-1}"
PACKAGE="${PACKAGE:-0}"

detect_qt_root() {
  if [[ -n "${QT_ROOT:-}" ]]; then
    printf '%s\n' "$QT_ROOT"
  elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    printf '%s\n' "$CMAKE_PREFIX_PATH"
  elif command -v brew >/dev/null 2>&1; then
    brew --prefix qt 2>/dev/null || true
  fi
}

QT_PREFIX="$(detect_qt_root)"

if [[ "${CLEAN:-0}" == "1" ]]; then
  if [[ "$BUILD_DIR" == "$REPO_ROOT" || "$BUILD_DIR" == "/" ]]; then
    echo "拒绝清理项目根目录或系统根目录" >&2
    exit 1
  fi
  rm -f -- "$BUILD_DIR/CMakeCache.txt"
  rm -rf -- "$BUILD_DIR/CMakeFiles"
fi

cmake_args=(
  -S "$REPO_ROOT" -B "$BUILD_DIR"
)
if [[ -n "$QT_PREFIX" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi
cmake "${cmake_args[@]}" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DPAT_BUILD_GUI=ON \
  -DPAT_ENABLE_SERIAL="${PAT_ENABLE_SERIAL:-ON}" \
  -DPAT_BUILD_TESTS="${PAT_BUILD_TESTS:-OFF}"

cmake --build "$BUILD_DIR" --config "$CONFIG" --target pat_app --parallel "${JOBS:-4}"

APP_ROOT="$BUILD_DIR"
if [[ "$PACKAGE" == "1" ]]; then
  APP_ROOT="$BUILD_DIR/stage"
  cmake --install "$BUILD_DIR" --prefix "$APP_ROOT"
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  APP_PATH="$APP_ROOT/pat_app.app"
  EXE_PATH="$APP_PATH/Contents/MacOS/pat_app"
else
  if [[ "$PACKAGE" == "1" ]]; then
    APP_ROOT="$APP_ROOT/bin"
  fi
  APP_PATH="$APP_ROOT/pat_app"
  EXE_PATH="$APP_PATH"
fi

if [[ ! -x "$EXE_PATH" ]]; then
  echo "构建完成，但未找到程序：$EXE_PATH" >&2
  exit 1
fi
printf '构建完成：%s\n' "$APP_PATH"

if [[ "$RUN" == "1" ]]; then
  exec "$EXE_PATH"
fi
