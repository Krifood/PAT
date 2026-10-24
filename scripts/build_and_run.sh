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
TEST="${TEST:-0}"

for flag_name in RUN PACKAGE TEST; do
  flag_value="${!flag_name}"
  if [[ "$flag_value" != "0" && "$flag_value" != "1" ]]; then
    printf '%s 必须为 0 或 1，当前值：%s\n' "$flag_name" "$flag_value" >&2
    exit 1
  fi
done

if [[ "$TEST" == "1" ]]; then
  BUILD_TESTS=ON
else
  BUILD_TESTS=OFF
fi

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
  -DPAT_BUILD_TESTS="$BUILD_TESTS"

if [[ "$TEST" == "1" ]]; then
  printf '已启用测试：编译后执行回归，失败将停止后续启动或打包。\n'
  cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "${JOBS:-4}"
  ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure
else
  printf '已跳过测试：仅编译应用。需要验证时设置 TEST=1。\n'
  cmake --build "$BUILD_DIR" --config "$CONFIG" --target pat_app --parallel "${JOBS:-4}"
fi

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
printf '本次构建产物：%s\n' "$APP_PATH"

if [[ "$(uname -s)" == "Darwin" ]]; then
  LEGACY_EXE="$REPO_ROOT/build/pat_app"
  if [[ -x "$LEGACY_EXE" && "$LEGACY_EXE" != "$EXE_PATH" ]]; then
    printf '提示：%s 是遗留裸程序，请勿用它启动；应使用上面的 .app。\n' "$LEGACY_EXE"
  fi
fi

if [[ "$RUN" == "1" ]]; then
  if [[ "$(uname -s)" == "Darwin" ]]; then
    printf '正在通过 macOS LaunchServices 启动：%s\n' "$APP_PATH"
    exec /usr/bin/open -n -W "$APP_PATH"
  fi
  printf '正在启动：%s\n' "$EXE_PATH"
  exec "$EXE_PATH"
fi
