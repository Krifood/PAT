# PAT 参数分析工具

PAT 是基于 C++20、Qt 6 的桌面应用，支持协议模板、离线数据解析和串口实时采集。

## macOS 自行构建

安装 CMake、C++ 编译器，以及 Qt 6 的 Core、Widgets、Charts、SerialPort 模块。在本目录运行：

```sh
./scripts/build_and_run.sh
```

脚本会编译并启动 `build/local/pat_app.app`。只编译时运行 `RUN=0 ./scripts/build_and_run.sh`；需要附带 Qt 运行库的应用包时运行 `PACKAGE=1 RUN=0 ./scripts/build_and_run.sh`，输出为 `build/local/stage/pat_app.app`。使用 Qt 官方安装包时，可设置 `QT_ROOT=/path/to/Qt/6.x/macos`。脚本可从任意当前目录通过其完整路径调用，构建目录仍位于 PAT 工程内。

打开应用后，选择“文件 → 打开格式”，可加载 `samples/cp01_b3_navigation.json`。串口连接及模板使用详见 [串口功能使用说明](docs/串口功能使用说明.md)。
