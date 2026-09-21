# PAT 参数分析工具

PAT 是基于 C++20、Qt 6 的桌面应用，支持协议模板、离线数据解析和串口实时采集。

## macOS 自行构建

安装 CMake、C++ 编译器，以及 Qt 6 的 Core、Widgets、Charts、SerialPort 模块。在本目录运行：

```sh
./scripts/build_and_run.sh
```

脚本会编译并启动 `build/local/pat_app.app`。只编译时运行 `RUN=0 ./scripts/build_and_run.sh`；需要附带 Qt 运行库的应用包时运行 `PACKAGE=1 RUN=0 ./scripts/build_and_run.sh`，输出为 `build/local/stage/pat_app.app`。使用 Qt 官方安装包时，可设置 `QT_ROOT=/path/to/Qt/6.x/macos`。脚本可从任意当前目录通过其完整路径调用，构建目录仍位于 PAT 工程内。

打开应用后，选择“文件 → 打开格式”，可加载 `samples/cp01_b3_navigation.json`。串口连接及模板使用详见 [串口功能使用说明](docs/串口功能使用说明.md)；适配其他协议时参照 [协议模板 JSON 编辑指南](docs/协议模板JSON编辑指南.md)。

“帮助 → 版本信息”可查看当前构建版本、发布状态、Qt 运行版本及操作系统；“帮助 → 更新日志”可离线查看随应用打包的 [更新日志](CHANGELOG.md)。当前为 `0.1.0` 开发构建，尚未作为正式版本发布。

加载模板和数据后，右侧“二维轨迹”页可将任意两列数值配成 X/Y 轨迹；CP01-B3 模板默认选中组合经纬度。串口采集时轨迹随有效帧更新，暂停显示时图形冻结。“自动范围”关闭后可框选缩放。当前视图只显示数值平面，不含地图底图；高度三维和离线地图包的扩展条件见 [设计方案第 11 章](docs/DESIGN.md)。

时序曲线支持显示层数据处理：右键信号设置 LSB 比例与纵向平移，组合图可分别调整单条曲线；选择两个同单位信号可创建 `A-B` 差值子图。这些操作兼容离线与串口动态数据，不修改原始解析值和日志。详细规则见 [设计方案第 12 章](docs/DESIGN.md)。
