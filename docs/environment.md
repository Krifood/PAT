# 开发环境与依赖清单

本项目目标：跨平台 C++20/23，核心解析库轻依赖、可迁移至 WebAssembly，GUI 体积尽量小。下列依赖按“最小必需 + 可选”分组，便于在不同机器复现。

## 基础工具
- 编译器：GCC ≥ 11 / Clang ≥ 13 / MSVC ≥ 2019（需完整支持 C++20）。
- 构建：CMake ≥ 3.20。
- 包管理：默认不使用包管理器；若需要第三方库，可选 vcpkg 或 Conan（非必须）。
- 格式/静态分析：clang-format、clang-tidy（版本与编译器匹配）。
- 测试：GoogleTest（源码嵌入或本地已装版本；无包管理亦可）。

## 核心依赖（默认启用）
- Qt Core/Gui/Widgets（含 Qt JSON 支持，用于格式描述解析）。
- Qt Charts（用于折线图绘制，跟随 Qt 安装）。
- fmt / spdlog：可选；当前版本可仅依赖 Qt 的日志/字符串能力，如需要高性能再启用。

## GUI 方案
- 选定方案：Qt Widgets + Qt Charts（功能简单但易发布）。需 Qt 6.x（或 5.15 LTS）开发包；打包可用 CPack/QtIFW。

## 其他可选组件
- JSON Schema 校验：可使用 Qt 自带 JSON 校验或后续引入单文件实现。
- 压缩/存档（如需要读取压缩日志）：zlib（按需）。
- 扩展脚本（可选）：pybind11（嵌入 Python）、sol2（嵌入 Lua）。
- 性能分析：Tracy 客户端（可选，开发阶段启用）。

## 构建/运行配置建议
- 编译选项：`-std=c++20 -Wall -Wextra -Wpedantic`（MSVC 使用 `/std:c++20 /W4`）。
- 调试版：开启 AddressSanitizer/UBSan（Clang/GCC），或 MSVC /RTC。
- 发布版：开启 LTO，`strip` 二进制以控制体积；按需禁用日志 Debug 级别。
- 平台注意：Windows 需安装对应 SDK；macOS 需 Xcode 工具链；Linux 建议安装 `build-essential`/`clang`/`ninja`。

## 目录与依赖记录方式
- 后续将在 `CMakeLists.txt` 中集中列出依赖选项（`ENABLE_QT`, `ENABLE_IMGUI`, `ENABLE_SCHEMA_VALIDATION` 等）。
- 包管理锁定：若使用 vcpkg，记录 `vcpkg.json`；若使用 Conan，记录 `conanfile.*` 与 lockfile。
