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
- 当前 PAT 采用 Qt Widgets + Qt Charts。串口分析集成以 Qt 6 为目标版本；现有源代码使用 Qt 6 事件 API，Qt 5 兼容性需单独修复与验证后才能宣称支持。
- 新增串口模块使用可选的 Qt Serial Port；后续 UDP 使用 Qt Network。macOS 应用包采用 Qt 官方部署方式，在无 Qt 开发环境的目标机器验证。具体改造与版本规则见 [技术变更路线](技术变更路线.md)。
- Qt Charts 自 Qt 6.10 起被标为弃用；当前仍沿用已有图表代码，后续评估 Qt Graphs 或其他替代方案。

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
- 后续在 `CMakeLists.txt` 中分别配置 GUI、串口输入和无界面目标所需的 Qt 模块；以实际 target 依赖为准，避免无界面目标链接 Widgets/Charts。
- 包管理锁定：若使用 vcpkg，记录 `vcpkg.json`；若使用 Conan，记录 `conanfile.*` 与 lockfile。
